/*
 * qwrt-rt — standalone child-process binary for the multi-process model.
 *
 * 两种形态（由 argv 选择）：
 *   1) worker 进程（M-P1）：--qwrt-worker --parent-fd N --worker-id K [--script PATH]
 *      worker 自己的 JSRuntime + worker-boot 垫片，服务一个 worker JS。
 *   2) 主RT 进程（M-P2）：--qwrt-rt-server --parent-fd N [--script PATH]
 *                            [--worker-backend 0|1]
 *      父运行时语义（无 worker_self）：跑宿主 initial_script，与宿主一条 IPC
 *      通道（信封 payload = JSON 文本；宿主 post_message 的 json 原样透传）；
 *      自身可经 JS 层 spawn worker 进程（树形拓扑的主RT 层，§1.1）。
 *
 * Lifecycle:
 *   1. Parse argv
 *   2. Handshake on raw parent-fd (child sends first, waits for ack, 5s)
 *   3. Init qwrt_t (loop + wake + runtime + polyfill)
 *   4. uv_pipe_open(parent-fd) + uv_read_start (frame accumulator → msgq)
 *   5. worker: eval worker boot bytecode + script / server: eval initial script,
 *      然后回 CONTROL{ready}（宿主 qwrt_create 据此判定成功）
 *   6. Main loop (uv_run(ONCE) + flush microtasks; server 形态含 idle 检测)
 *   7. EOF on parent-fd → shutting_down → teardown → exit(0)
 *
 * Design: docs/plans/2026-09-04-multi-process-model.md §5, §6.2, §9.2, M-P1/M-P2.
 */

#include "qwrt_internal.h"
#include "ipc_process.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <poll.h>
#include <errno.h>
#include <sys/socket.h>   /* recv：M-P4 同步 storage RPC 的 poll/recv 等待 */

/* ── Pipe read state (one pipe per child process — static is fine) ── */

static uv_pipe_t g_parent_pipe;

static struct {
    uint8_t *buf;       /* accumulation buffer */
    size_t   cap;
    size_t   len;       /* bytes accumulated so far */
    uint32_t frame_len; /* expected frame body length (0 = need 4-byte hdr) */
} g_rx;

/* Hand-rolled substring match — replaces glibc memmem, a GNU extension that
 * would force _GNU_SOURCE (violates the project's C99 discipline). */
static int has_substring(const uint8_t *hay, size_t hlen, const char *needle)
{
    size_t nlen = strlen(needle);
    if (nlen == 0 || nlen > hlen) return 0;
    for (size_t i = 0; i + nlen <= hlen; i++) {
        if (memcmp(hay + i, needle, nlen) == 0) return 1;
    }
    return 0;
}


/* ── Wake callback: drain msgq and dispatch ──
 * worker 形态走 qwrt_worker_dispatch（worker-boot 垫片语义）；主RT 形态（M-P2）
 * 走 qwrt_dispatch_message + 逐条微任务冲刷——与 thread.c 的 qwrt_wake_cb 完全
 * 同构（宿主消息即主 runtime 的 onmessage）。 */

static int g_server_mode;   /* 1 = --qwrt-rt-server（主RT 进程，M-P2） */
static int32_t g_local_id;  /* CTL-1：本节点槽位 id（主RT=1 / worker=--worker-id） */

static void process_rx(qwrt_t *rt);   /* 帧累加器解码；child_storage_sync 在其前定义 */

static void child_wake_cb(uv_async_t *a)
{
    qwrt_t *rt = (qwrt_t *)a->data;
    if (__atomic_load_n(&rt->shutting_down, __ATOMIC_ACQUIRE)) return;
    qwrt_msg_t *m;
    while ((m = qwrt_msg_pop(rt)) != NULL) {
        /* CONTROL（flags==1）→ control dispatch；其余（含 PORT_TRANSFER 的
         * flags==2）走运行时的应用消息派发，kind 作为第三参交给 JS。 */
        if (m->flags == QWRT_MSG_FLAG_CONTROL) {
            qwrt_control_dispatch(rt, m);
        } else if (g_server_mode) {
            qwrt_dispatch_message(rt, m);
            qwrt_flush_microtasks(rt);
        } else {
            qwrt_worker_dispatch(rt, m);
        }
    }
}

/* ── 主RT 形态：宿主边界出站（§6.2）──
 * bridge.c 的宿主 postMessage 路径调 config.message_cb（JSON 文本），此处把它
 * 装成 MESSAGE 信封上行给宿主。跑在 JS 线程 = loop 线程，tx 写路径单线程独占。 */
static void server_emit_cb(qwrt_t *rt, const char *json, size_t len, void *data)
{
    QWRT_UNUSED(rt); QWRT_UNUSED(data);
    if (!json || len == 0) return;
    qwrt_ipc_child_emit(QWRT_IPC_MAIN_ID, QWRT_IPC_HOST_ID,
                        IPC_ENV_KIND_MESSAGE,
                        (const uint8_t *)json, (uint32_t)len);
}

/* 主RT 形态的 CONTROL 分流（§6.1）：M-P2 协议消息在管道读路径就地消化；
 * 其余 CONTROL（控制面消息等）照 worker 形态推入 msgq flags=1。 */
static void server_handle_control(qwrt_t *rt, const ipc_envelope_view_t *view)
{
    int val = 0;
    qwrt_ipc_ctl_kind_t kind =
        qwrt_ipc_ctl_classify(view->payload, view->payload_len, &val);
    if (kind == QWRT_IPC_CTL_IDLE && val == 0) {
        /* 宿主请求 idle：置标志，主循环在排空后回 ack 并自身退出。 */
        __atomic_store_n(&rt->wait_idle, 1, __ATOMIC_RELEASE);
        uv_async_send(&rt->wake);
    }
    /* READY / IDLE ack / SHUTDOWN 由宿主→主RT 方向不出现；shutdown 的两种
     * payload 形态（{"qwrt":1,"shutdown":1} 与 M-P1 的 {"cmd":"shutdown"}）统一
     * 在下方 substring 判定里处理。 */
}

/* ── M-P4 同步 storage RPC（§10.2 单所有者代理的传输半边）──
 * worker 进程的 localStorage 代理调用 pal.storageSync → 本函数：发一条
 * kind=STORAGE 信封上行（target=父，主RT 所有者），然后在**不跑 uv_run**
 * 的前提下 poll/recv 驱动管道读。帧照常进 g_rx 累加器（process_rx 统一
 * 处理：STORAGE 回复捕获；其它帧 push msgq——wake 的 uv_async 挂起标志
 * 仍在，主循环下一个 uv_run 会派发它们，不丢帧）。等待期间无 uv_run →
 * 无 timer/async 回调 → 无 JS 重入，同步 API 语义不被破坏。
 * 父进程死亡（fd EOF/POLLHUP）→ 置 shutting_down 走 §9.4 孤儿自杀路径，
 * 返回 -1（JS 抛错；worker 随之退出，连锁死亡是预期行为）。
 * 无 request id：单飞行（JS 同步调用期间无并发），等待中收到的第一个
 * STORAGE 帧必是本次回复。 */
static int g_sync_waiting = 0;
static int g_sync_done = 0;
static int g_sync_err = 0;
static uint8_t *g_sync_reply = NULL;
static uint32_t g_sync_reply_len = 0;

static int child_storage_sync(qwrt_t *rt, const uint8_t *payload,
                              uint32_t payload_len,
                              uint8_t **out_reply, uint32_t *out_reply_len)
{
    qwrt_worker_t *w = (qwrt_worker_t *)rt->worker_self;
    int fd = qwrt_ipc_child_channel();
    *out_reply = NULL;
    *out_reply_len = 0;
    if (!w || fd < 0) return -1;

    /* 阻塞发送整帧（含 FIFO 排空 spill buffer）：大 payload（quota 内可达
     * ~5MB）远超 socket 缓冲，poll(POLLOUT) 等待父侧排空；父死 → -1。 */
    if (qwrt_ipc_child_emit_sync((int32_t)w->id, 0, IPC_ENV_KIND_STORAGE,
                                 payload, payload_len) != 0)
        return -1;

    g_sync_waiting = 1;
    g_sync_done = 0;
    g_sync_err = 0;
    g_sync_reply = NULL;
    g_sync_reply_len = 0;

    for (;;) {
        if (__atomic_load_n(&rt->shutting_down, __ATOMIC_ACQUIRE)) break;
        struct pollfd pfd = { .fd = fd, .events = POLLIN };
        int pr = poll(&pfd, 1, 200);   /* 200ms 节拍复查 shutting_down */
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (pr == 0) continue;
        if (pfd.revents & (POLLIN | POLLHUP | POLLERR)) {
            uint8_t tmp[QWRT_IPC_READ_BUF_SIZE];
            ssize_t n = recv(fd, tmp, sizeof tmp, 0);
            if (n == 0) {
                /* EOF：父进程死亡 → §9.4 孤儿自杀 */
                __atomic_store_n(&rt->shutting_down, 1, __ATOMIC_RELEASE);
                uv_async_send(&rt->wake);
                break;
            }
            if (n < 0) {
                if (errno == EINTR || errno == EAGAIN) continue;
                if (errno == ECONNRESET) {
                    __atomic_store_n(&rt->shutting_down, 1, __ATOMIC_RELEASE);
                    uv_async_send(&rt->wake);
                }
                break;
            }
            /* 喂累加器 → process_rx 统一帧处理（回复捕获 / msgq push） */
            size_t need = g_rx.len + (size_t)n;
            if (need > g_rx.cap) {
                size_t ncap = g_rx.cap ? g_rx.cap : 4096;
                while (ncap < need) ncap *= 2;
                uint8_t *nb = (uint8_t *)realloc(g_rx.buf, ncap);
                if (!nb) break;   /* OOM：按失败返回 */
                g_rx.buf = nb;
                g_rx.cap = ncap;
            }
            memcpy(g_rx.buf + g_rx.len, tmp, (size_t)n);
            g_rx.len += (size_t)n;
            process_rx(rt);
            if (g_sync_done) break;
        }
    }

    g_sync_waiting = 0;
    if (g_sync_done && !g_sync_err && g_sync_reply) {
        *out_reply = g_sync_reply;
        *out_reply_len = g_sync_reply_len;
        g_sync_reply = NULL;
        return 0;
    }
    free(g_sync_reply);
    g_sync_reply = NULL;
    return -1;
}

/* ── Pipe read: frame accumulator → envelope decode → msgq push ── */

static void pipe_alloc_cb(uv_handle_t *h, size_t suggested, uv_buf_t *buf)
{
    (void)h; (void)suggested;
    /* Allocate a fixed chunk; libuv reuses it per read */
    buf->base = (char *)malloc(QWRT_IPC_READ_BUF_SIZE);
    buf->len = buf->base ? QWRT_IPC_READ_BUF_SIZE : 0;
}

/* Process accumulated bytes: extract complete frames, decode, push to msgq */
static void process_rx(qwrt_t *rt)
{
    for (;;) {
        if (g_rx.frame_len == 0) {
            /* Need 4-byte header */
            if (g_rx.len < 4) return;
            g_rx.frame_len = ((uint32_t)g_rx.buf[0]) |
                             ((uint32_t)g_rx.buf[1] << 8) |
                             ((uint32_t)g_rx.buf[2] << 16) |
                             ((uint32_t)g_rx.buf[3] << 24);
            /* Consume header */
            g_rx.len -= 4;
            if (g_rx.len > 0)
                memmove(g_rx.buf, g_rx.buf + 4, g_rx.len);
            if (g_rx.frame_len > 16u * 1024 * 1024) {
                /* Oversized frame — protocol error, self-terminate. Wake the
                 * loop: uv_run(ONCE) may be blocked in pipe poll (I2). */
                __atomic_store_n(&rt->shutting_down, 1, __ATOMIC_RELEASE);
                uv_async_send(&rt->wake);
                return;
            }

        }
        /* Need frame body */
        if (g_rx.len < g_rx.frame_len) return;

        /* Complete frame available */
        if (g_rx.frame_len > 0) {
            ipc_envelope_view_t view;
            if (ipc_envelope_decode(g_rx.buf, g_rx.frame_len, &view) == 0) {
                int is_ctl = (view.kind == IPC_ENV_KIND_CONTROL);
                int ctl_val = 0;    /* CTL-1：CONTROL 命令类判定 */
                if (is_ctl && g_server_mode)
                    server_handle_control(rt, &view);
                /* CONTROL{shutdown} → graceful exit (§9.2 tier 1)。两种 payload
                 * 形态都认：M-P2 的 {"qwrt":1,"shutdown":1} 与 M-P1 三级终止
                 * tier-1 的 {"cmd":"shutdown"}（qwrt_proc_terminate 发出）。 */
                if (is_ctl && view.payload_len > 0 &&
                    has_substring(view.payload, view.payload_len, "shutdown")) {
                    __atomic_store_n(&rt->shutting_down, 1, __ATOMIC_RELEASE);
                    uv_async_send(&rt->wake);
                } else if (view.kind == IPC_ENV_KIND_STORAGE) {
                    /* M-P4 §10.2：worker 进程侧 = 同步 RPC 的回复（单飞行，
                     * 无 request id）→ 捕获给等待方；非等待状态收到 STORAGE =
                     * 协议外（所有者从不主动发起），丢弃。所有者（主RT）侧的
                     * 请求帧不经过本管道——worker 通道是 proc 句柄读泵，JS 层
                     * processOnMessage 按 kind=4 分流（worker.js）。 */
                    if (g_sync_waiting) {
                        g_sync_reply = (uint8_t *)malloc(view.payload_len);
                        if (g_sync_reply || view.payload_len == 0) {
                            if (view.payload_len > 0)
                                memcpy(g_sync_reply, view.payload,
                                       view.payload_len);
                            g_sync_reply_len = view.payload_len;
                        } else {
                            g_sync_err = 1;   /* OOM：等待方按失败返回 */
                        }
                        g_sync_done = 1;
                    }
                } else if (is_ctl &&
                           qwrt_ipc_ctl_classify(view.payload, view.payload_len,
                                                 &ctl_val) ==
                               QWRT_IPC_CTL_NONE) {
                    /* CTL-1（§2.2）：命令类 CONTROL 信封在本节点树路由——命中
                     * 本地则入 msgq（flags=CONTROL）交 dispatch，否则逐跳向上/
                     * 向下转发。系统级 CONTROL 已由上方分支消化，不受影响。 */
                    qwrt_control_route(rt, g_local_id, view.source, view.target,
                                       view.payload, view.payload_len);
                } else {
                    /* kind → msgq flags：CONTROL 交控制面；PORT_TRANSFER 走
                     * 应用派发但 JS 拿到 kind=1，据此走 port 端点路由（M-P3）。 */
                    int flags = view.kind == IPC_ENV_KIND_CONTROL
                                    ? QWRT_MSG_FLAG_CONTROL
                                    : (view.kind == IPC_ENV_KIND_PORT_TRANSFER
                                           ? QWRT_MSG_FLAG_PORT_TRANSFER : 0);
                    qwrt_msg_push(rt, (const char *)view.payload,
                                  view.payload_len, view.source, flags);
                    uv_async_send(&rt->wake);
                }
            }
        }

        /* Consume frame body */
        g_rx.len -= g_rx.frame_len;
        if (g_rx.len > 0)
            memmove(g_rx.buf, g_rx.buf + g_rx.frame_len, g_rx.len);
        g_rx.frame_len = 0;
    }
}

static void pipe_read_cb(uv_stream_t *s, ssize_t nread, const uv_buf_t *buf)
{
    qwrt_t *rt = (qwrt_t *)s->data;

    if (nread < 0) {
        /* EOF or error → parent gone → self-terminate (§6.4) */
        free(buf->base);
        __atomic_store_n(&rt->shutting_down, 1, __ATOMIC_RELEASE);
        uv_async_send(&rt->wake);
        return;
    }

    if (nread > 0) {
        /* Grow buffer if needed */
        size_t need = g_rx.len + (size_t)nread;
        if (need > g_rx.cap) {
            size_t ncap = g_rx.cap ? g_rx.cap : 4096;
            while (ncap < need) ncap *= 2;
            uint8_t *nb = (uint8_t *)realloc(g_rx.buf, ncap);
            if (nb) {
                g_rx.buf = nb;
                g_rx.cap = ncap;
            } else {
                free(buf->base);
                /* OOM — self-terminate; wake the loop so uv_run(ONCE) does
                 * not stay blocked in pipe poll (I2). */
                __atomic_store_n(&rt->shutting_down, 1, __ATOMIC_RELEASE);
                uv_async_send(&rt->wake);
                return;
            }
        }
        memcpy(g_rx.buf + g_rx.len, buf->base, (size_t)nread);
        g_rx.len += (size_t)nread;
        process_rx(rt);
    }
    free(buf->base);
}

/* ── Main ── */

int main(int argc, char **argv)
{
    int parent_fd = -1;
    int worker_id = 0;
    const char *script_path = NULL;
    int is_server = 0;          /* M-P2：主RT serve 形态 */
    int is_worker = 0;
    int worker_backend = -1;    /* --worker-backend；-1 = 编译缺省 */
    int control_plane = -1;     /* --control-plane；-1 = 缺省（OFF） */
    const char *control_pipe = NULL;   /* --control-pipe 路径（NULL = 缺省） */
    const char *path_arg = NULL;       /* §8.2 path 链 "k1,k2,..."（父经 argv 传） */

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--qwrt-worker") == 0) {
            is_worker = 1;
        } else if (strcmp(argv[i], "--qwrt-rt-server") == 0) {
            is_server = 1;
        } else if (strcmp(argv[i], "--parent-fd") == 0 && i + 1 < argc) {
            parent_fd = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--worker-id") == 0 && i + 1 < argc) {
            worker_id = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--worker-backend") == 0 && i + 1 < argc) {
            /* 传符号名（非数值）：宿主与主RT 必须对枚举取值理解一致，符号名
             * 免去「同构建内数值排列相同」这一隐含前提。 */
            const char *wb = argv[++i];
            if (strcmp(wb, "process") == 0)
                worker_backend = QWRT_WORKER_BACKEND_PROCESS;
            else if (strcmp(wb, "thread") == 0)
                worker_backend = QWRT_WORKER_BACKEND_THREAD;
            else {
                fprintf(stderr, "qwrt-rt: bad --worker-backend: %s\n", wb);
                return 1;
            }
        } else if (strcmp(argv[i], "--script") == 0 && i + 1 < argc) {
            script_path = argv[++i];
        } else if (strcmp(argv[i], "--path") == 0 && i + 1 < argc) {
            /* §8.2：完整 path 链（逗号分隔），由直接父在 spawn 时拼好传入——
             * 父知自身 path 与本节点本地槽位 id。 */
            path_arg = argv[++i];
        } else if (strcmp(argv[i], "--control-plane") == 0 && i + 1 < argc) {
            const char *cp = argv[++i];
            if (strcmp(cp, "off") == 0) control_plane = QWRT_CONTROL_OFF;
            else if (strcmp(cp, "in-proc") == 0) control_plane = QWRT_CONTROL_IN_PROC;
            else if (strcmp(cp, "local") == 0) control_plane = QWRT_CONTROL_LOCAL;
            else {
                fprintf(stderr, "qwrt-rt: bad --control-plane: %s\n", cp);
                return 1;
            }
        } else if (strcmp(argv[i], "--control-pipe") == 0 && i + 1 < argc) {
            control_pipe = argv[++i];
        }
    }

    g_server_mode = is_server;
    /* CTL-1 本地标签：主RT 在宿主通道上恒为 1（QWRT_IPC_MAIN_ID）；worker 用
     * --worker-id（与父侧 spawn 时登记的子槽位 id 同值）。 */
    g_local_id = is_server ? QWRT_IPC_MAIN_ID : (int32_t)worker_id;

    if ((!is_worker && !is_server) || (is_worker && is_server) || parent_fd < 0) {
        fprintf(stderr,
                "qwrt-rt: usage:\n"
                "  qwrt-rt --qwrt-worker    --parent-fd N --worker-id K [--script PATH]\n"
                "  qwrt-rt --qwrt-rt-server --parent-fd N [--script PATH]"
                " [--worker-backend process|thread]\n");
        return 1;
    }

    /* ── Read script file ── */
    char *script = NULL;
    if (script_path) {
        FILE *f = fopen(script_path, "r");
        if (!f) {
            fprintf(stderr, "qwrt-rt: cannot open script: %s\n", script_path);
            return 1;
        }
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz < 0) { fclose(f); return 1; }
        script = (char *)malloc((size_t)sz + 1);
        if (!script) { fclose(f); return 1; }
        size_t rd = fread(script, 1, (size_t)sz, f);
        script[rd] = '\0';
        fclose(f);
        /* C1: script fully read — unlink the temp file now. The parent keeps
         * no reference to the file (mkstemp fd was closed before exec), so
         * removing the directory entry is race-free, and this runs before the
         * handshake so the parent's success path never leaks the file. */
        unlink(script_path);
    }

    /* ── Handshake (child sends first, §3.3) ── */
    {
        char hs_json[40];
        /* 握手身份：worker 形态 = 本地槽位 id（相对直接父）；主RT 形态 =
         * 固定本地标签 1（宿主为 0，§4.3 主RT 通道上的相对寻址）。 */
        int hs_role = is_server ? QWRT_IPC_ROLE_MAIN : QWRT_IPC_ROLE_WORKER;
        int hs_id = is_server ? QWRT_IPC_MAIN_ID : worker_id;
        size_t hs_len = qwrt_ipc_build_handshake(hs_json, sizeof hs_json,
                                                  hs_role, hs_id);
        if (hs_len == 0) {
            fprintf(stderr, "qwrt-rt: handshake build failed\n");
            free(script);
            return 1;
        }
        size_t env_cap = IPC_ENVELOPE_ENCODED_SIZE(hs_len);
        uint8_t *env_buf = (uint8_t *)malloc(env_cap);
        if (!env_buf) { free(script); return 1; }
        size_t env_len = ipc_envelope_encode(env_buf, env_cap,
                                            (int32_t)hs_id,
                                            is_server ? QWRT_IPC_HOST_ID : 1,
                                            IPC_ENV_KIND_CONTROL,
                                            (const uint8_t *)hs_json,
                                            (uint32_t)hs_len);
        if (env_len == 0) {
            fprintf(stderr, "qwrt-rt: envelope encode failed\n");
            free(env_buf); free(script);
            return 1;
        }
        if (qwrt_ipc_write_frame(parent_fd, env_buf, env_len) < 0) {
            fprintf(stderr, "qwrt-rt: handshake write failed\n");
            free(env_buf); free(script);
            return 1;
        }
        free(env_buf);

        /* Read ack (5s deadline) */
        int64_t deadline = qwrt_now_ms() + QWRT_IPC_HANDSHAKE_TIMEOUT_MS;
        uint8_t *ack_frame = NULL;
        size_t ack_flen = 0;
        if (qwrt_ipc_read_frame(parent_fd, &ack_frame, &ack_flen, deadline) < 0) {
            fprintf(stderr, "qwrt-rt: handshake ack timeout/EOF\n");
            free(script);
            return 1;
        }
        ipc_envelope_view_t view;
        if (ipc_envelope_decode(ack_frame, ack_flen, &view) < 0) {
            fprintf(stderr, "qwrt-rt: ack decode failed\n");
            free(ack_frame); free(script);
            return 1;
        }
        char ack_json[40];
        size_t cp = view.payload_len < sizeof(ack_json) - 1
                      ? view.payload_len : sizeof(ack_json) - 1;
        memcpy(ack_json, view.payload, cp);
        ack_json[cp] = '\0';
        free(ack_frame);

        int ok = 0, ver = 0;
        if (qwrt_ipc_parse_ack(ack_json, &ok, &ver) < 0 || !ok ||
            ver != QWRT_IPC_PROTO_VERSION) {
            fprintf(stderr, "qwrt-rt: handshake rejected (ok=%d v=%d)\n", ok, ver);
            free(script);
            return 1;
        }
    }

    /* Register the emit channel for bridge.c (child → parent envelopes). */
    qwrt_ipc_child_set_channel(parent_fd);

    /* ── Init qwrt_t ── */
    qwrt_t *rt = (qwrt_t *)calloc(1, sizeof(qwrt_t));
    if (!rt) { free(script); return 1; }
    rt->magic = QWRT_MAGIC;
    rt->config.initial_script = NULL;
    rt->config.debug = 0;
    rt->config.control_plane = 0;
    rt->msg_head = &rt->msg_stub;
    rt->msg_tail = &rt->msg_stub;

    qwrt_worker_t *w = NULL;

    /* 运行时角色：worker 形态标记 worker_self（bridge.c 的 pal 按 worker 绑定）；
     * 主RT 形态保持 worker_self == NULL = 父运行时语义（可自行 spawn worker
     * 进程 = §1.1 树形拓扑的主RT 层），并把宿主边界出站接到信封上行。 */
    if (is_server) {
        if (worker_backend >= 0) rt->config.worker_backend = worker_backend;
        rt->config.message_cb = server_emit_cb;
    } else {
        /* qwrt-rt --qwrt-worker 进程按构造即 PROCESS 后端 worker（THREAD
         * worker 是同进程线程，不 exec 本二进制）。强制置位让 worker 侧
         * pal.workerBackend() 返回 'process'——local-storage.js 据此挂
         * §10.2 单所有者代理（M-P4）。 */
        rt->config.worker_backend = QWRT_WORKER_BACKEND_PROCESS;
        w = (qwrt_worker_t *)calloc(1, sizeof(qwrt_worker_t));
        if (!w) { free(rt); free(script); return 1; }
        w->self = rt;
        w->id = worker_id;
        w->parent = NULL;  /* no in-process parent — IPC pipe is the channel */
        rt->worker_self = w;
    }

    /* CTL-2：控制面档位与端点路径由宿主经 argv 传入（ISOLATED 下 runtime
     * 在本进程，宿主进程只有通道桩）。只有主RT 监听端点——worker 经父路由
     * （§2.2 树形拓扑）。 */
    if (control_plane >= 0) {
        rt->config.control_plane = control_plane;
    } else if (is_worker) {
        /* worker 形态缺省 IN_PROC：worker 没有任何外部面（唯一入站是父通道，
         * 且父受自身档位门控——父 OFF 时命令在父侧就丢了，§4.1），而 CTL-1
         * 要求「target 指向 worker 槽位 → 沿树下发 → worker 自己 safepoint
         * 执行」（§2.2）。若父未显式传档位，OFF 会让 worker 静默丢弃所有
         * 命令、回执退化为 TIMEOUT——故 worker 缺省取可执行档。 */
        rt->config.control_plane = QWRT_CONTROL_IN_PROC;
    }
    rt->config.control_pipe_path = control_pipe;

    /* §8.2：本节点 path 链。父经 --path 传完整链；缺省（无 --path 的 worker）
     * 退化为单元素 [worker_id]（深度 1 的旧扁平语义）。主RT/宿主形态为空 path。 */
    if (path_arg) {
        const char *p = path_arg;
        while (*p && rt->self_path_len < QWRT_SELF_PATH_MAX) {
            char *end = NULL;
            long v = strtol(p, &end, 10);
            if (end == p) break;
            if (v > 0 && v <= 0xFFFF) rt->self_path[rt->self_path_len++] = (uint16_t)v;
            p = (*end == ',') ? end + 1 : end;
        }
    } else if (is_worker && worker_id > 0 && worker_id <= 0xFFFF) {
        rt->self_path[rt->self_path_len++] = (uint16_t)worker_id;
    }

    int loop_inited = 0;
    if (uv_loop_init(&rt->loop) != 0) {
        fprintf(stderr, "qwrt-rt: loop init failed\n");
        free(w); free(rt); free(script);
        return 1;
    }
    loop_inited = 1;

    rt->wake.data = rt;
    if (uv_async_init(&rt->loop, &rt->wake, child_wake_cb) != 0) {
        fprintf(stderr, "qwrt-rt: async init failed\n");
        goto fail;
    }

    /* CTL-2 §2.3：LOCAL 档在主RT 打开本地端点（qwrt-ctl 连入）。必须在
     * qwrt_runtime_init 之前——runtime_init 会 attach DAP 并阻塞在
     * configuration（debug 模式），端点若排在其后则调试会话期间完全不可用。
     * 端点与 DAP stdio 是两个分离通道（§2.3「与 DAP 并存规则」），互不抢占。
     * bind/listen 失败即显式失败，不静默降级为 in-proc。 */
    if (is_server && rt->config.control_plane == QWRT_CONTROL_LOCAL &&
        qwrt_ctl_endpoint_init(rt) != 0) {
        fprintf(stderr, "qwrt-rt: control endpoint init failed\n");
        goto fail;
    }

    if (qwrt_runtime_init(rt) != 0) {
        fprintf(stderr, "qwrt-rt: runtime init failed\n");
        goto fail;
    }

    /* ── Open pipe on loop for async reads ── */
    if (uv_pipe_init(&rt->loop, &g_parent_pipe, 0) != 0) {
        fprintf(stderr, "qwrt-rt: pipe init failed\n");
        goto fail;
    }
    if (uv_pipe_open(&g_parent_pipe, parent_fd) != 0) {
        fprintf(stderr, "qwrt-rt: pipe open failed\n");
        goto fail;
    }
    g_parent_pipe.data = rt;
    if (uv_read_start((uv_stream_t *)&g_parent_pipe,
                      pipe_alloc_cb, (uv_read_cb)pipe_read_cb) != 0) {
        fprintf(stderr, "qwrt-rt: read_start failed\n");
        goto fail;
    }
    /* emit 写路径走 spill buffer（非阻塞 send + 1ms flush timer，背压不丢帧） */
    qwrt_ipc_child_tx_init(&rt->loop, parent_fd);
    /* M-P4：worker 进程注册同步 storage RPC 实现（pal.storageSync 的传输
     * 半边；主RT/宿主进程不注册，调用即 -1 = 不可达）。 */
    qwrt_ipc_child_set_storage_sync(child_storage_sync);

    /* 主RT 形态：登记宿主通道管道 —— 读管道恒活动（duplex 读泵），wait_idle 的
     * idle 判定须豁免它，否则主RT 永不判 idle（qwrt_proc_handle_is_pipe）。 */
    if (is_server) rt->ipc_channel_pipe = &g_parent_pipe;

    if (is_server) {
        /* ── 主RT 形态：eval 初始脚本（宿主 config.initial_script，经临时文件
         * 传入）→ 回 CONTROL{ready}。旧 thread 后端的 ready_err 语义搬到这里：
         * 初始脚本抛异常 = 运行时起不来，宿主 qwrt_create 必须返回失败（不静默
         * 降级，§5.3）。 */
        int ready_ok = 1;
        if (script) {
            char *err = NULL;
            if (qwrt_eval_internal(rt, script, &err) != 0) {
                fprintf(stderr, "qwrt-rt: initial script error: %s\n",
                        err ? err : "?");
                free(err);
                ready_ok = 0;
            }
            free(script);
            script = NULL;
        }
        if (qwrt_ipc_child_emit_ctl(ready_ok ? QWRT_IPC_CTL_READY_OK
                                             : QWRT_IPC_CTL_READY_ERR) < 0) {
            fprintf(stderr, "qwrt-rt: ready emit failed\n");
            goto fail;
        }
        if (!ready_ok) {
            qwrt_thread_teardown(rt);
            free(g_rx.buf);
            g_rx.buf = NULL;
            free(rt);
            return 1;
        }
    } else {
        /* ── Eval worker boot bytecode ── */
        {
            char *err = NULL;
            if (qwrt_eval_bytecode_internal(rt, qwrt_default_worker_boot,
                                             qwrt_default_worker_boot_len,
                                             &err) != 0) {
                fprintf(stderr, "qwrt-rt: boot failed: %s\n", err ? err : "?");
                free(err);
                goto fail;
            }
        }

        /* ── Eval worker script ── */
        if (script) {
            char *err = NULL;
            if (qwrt_eval_internal(rt, script, &err) != 0) {
                fprintf(stderr, "qwrt-rt: script error: %s\n", err ? err : "?");
                free(err);
                /* Worker continues (per spec: error event, not crash) */
            }
            free(script);
            script = NULL;
        }
    }

    /* ── Main loop ──
     * worker 形态镜像 qwrt_worker_thread_main；主RT 形态镜像 qwrt_thread_main
     * （多一层 idle 检测：排空且自身 idle → 回 CONTROL{idle} ack → 自身退出，
     * 与 thread 后端的「idle 即退」语义一致，§6.1）。 */
    while (!__atomic_load_n(&rt->shutting_down, __ATOMIC_ACQUIRE) &&
           !(w && __atomic_load_n(&w->shutting_down, __ATOMIC_ACQUIRE))) {
        uv_run(&rt->loop, UV_RUN_ONCE);
        if (is_server) qwrt_ctl_reap_timeouts(rt);
        if (__atomic_load_n(&rt->shutting_down, __ATOMIC_ACQUIRE) ||
            (w && __atomic_load_n(&w->shutting_down, __ATOMIC_ACQUIRE))) break;
        qwrt_flush_microtasks(rt);
        if (is_server &&
            __atomic_load_n(&rt->wait_idle, __ATOMIC_ACQUIRE) &&
            qwrt_loop_idle(rt)) {
            qwrt_ipc_child_emit_ctl(QWRT_IPC_CTL_IDLE_ACK);
            __atomic_store_n(&rt->shutting_down, 1, __ATOMIC_RELEASE);
            break;
        }
    }

    /* ── Teardown ── */
    qwrt_thread_teardown(rt);
    free(g_rx.buf);
    g_rx.buf = NULL;
    free(w);
    free(rt);
    return 0;

fail:
    free(script);
    if (g_rx.buf) { free(g_rx.buf); g_rx.buf = NULL; }
    free(w);
    if (loop_inited) {
        uv_loop_close(&rt->loop);
    }
    free(rt);
    return 1;
}

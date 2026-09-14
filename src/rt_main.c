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

static void child_wake_cb(uv_async_t *a)
{
    qwrt_t *rt = (qwrt_t *)a->data;
    if (__atomic_load_n(&rt->shutting_down, __ATOMIC_ACQUIRE)) return;
    qwrt_msg_t *m;
    while ((m = qwrt_msg_pop(rt)) != NULL) {
        /* flags=1 → control dispatch; flags=0 → 运行时的应用消息派发 */
        if (m->flags) {
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
                if (is_ctl && g_server_mode)
                    server_handle_control(rt, &view);
                /* CONTROL{shutdown} → graceful exit (§9.2 tier 1)。两种 payload
                 * 形态都认：M-P2 的 {"qwrt":1,"shutdown":1} 与 M-P1 三级终止
                 * tier-1 的 {"cmd":"shutdown"}（qwrt_proc_terminate 发出）。 */
                if (is_ctl && view.payload_len > 0 &&
                    has_substring(view.payload, view.payload_len, "shutdown")) {
                    __atomic_store_n(&rt->shutting_down, 1, __ATOMIC_RELEASE);
                    uv_async_send(&rt->wake);
                } else {
                    int flags = is_ctl ? 1 : 0;
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
    int is_worker = 0;
    int is_server = 0;          /* M-P2：主RT serve 形态 */
    int worker_backend = -1;    /* --worker-backend；-1 = 编译缺省 */

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
        }
    }

    g_server_mode = is_server;

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

    /* 运行时角色：worker 形态标记 worker_self（bridge.c 的 pal 按 worker 绑定）；
     * 主RT 形态保持 worker_self == NULL = 父运行时语义（可自行 spawn worker
     * 进程 = §1.1 树形拓扑的主RT 层），并把宿主边界出站接到信封上行。 */
    qwrt_worker_t *w = NULL;
    if (is_server) {
        if (worker_backend >= 0) rt->config.worker_backend = worker_backend;
        rt->config.message_cb = server_emit_cb;
    } else {
        w = (qwrt_worker_t *)calloc(1, sizeof(qwrt_worker_t));
        if (!w) { free(rt); free(script); return 1; }
        w->self = rt;
        w->id = worker_id;
        w->parent = NULL;  /* no in-process parent — IPC pipe is the channel */
        rt->worker_self = w;
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

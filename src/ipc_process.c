/*
 * amoib IPC Process — parent-side spawn / handshake / terminate (M-P1)
 *
 * Design: docs/plans/2026-09-04-multi-process-model.md §3, §5, §9.2, M-P1.
 * Linux-only (AF_UNIX socketpair, fork+exec, SIGKILL+waitpid).
 *
 * Frame format: [4-byte LE length][envelope bytes]
 * Handshake (§3.3): child sends Envelope{kind=CONTROL,
 *   payload=JSON{"v","role","id"}}; parent validates, replies
 *   Envelope{kind=CONTROL, payload=JSON{"ok","v"}}. Parent blocks 5s.
 */

#include "ipc_process.h"
#include "am_internal.h"
#include <cJSON.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <poll.h>
#include <time.h>
#include <sched.h>   /* sched_yield: am_proc_ping 的自旋等待 */
#include <stdio.h>

/* ── Helpers ── */


/* All fds written here are socketpair(AF_UNIX) sockets. Use send() with
 * MSG_NOSIGNAL so a write to a dead peer returns EPIPE instead of raising
 * SIGPIPE and killing the whole host process (C2). This is precise — it does
 * not touch the process-wide SIGPIPE disposition the way signal(SIGPIPE,
 * SIG_IGN) would, so embedding hosts keep their own signal handling. */
static int write_all(int fd, const void *buf, size_t len)
{
    const char *p = (const char *)buf;
    size_t off = 0;
    while (off < len) {
        ssize_t n = send(fd, p + off, len - off, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        off += (size_t)n;
    }
    return 0;
}

static int read_all_deadline(int fd, void *buf, size_t len, int64_t deadline_ms)
{
    char *p = (char *)buf;
    size_t off = 0;
    while (off < len) {
        int64_t remaining = deadline_ms - am_now_ms();
        if (remaining <= 0) return -1;
        struct pollfd pfd = { .fd = fd, .events = POLLIN };
        int pr = poll(&pfd, 1, (int)remaining);
        if (pr <= 0) return -1;
        if (pfd.revents & (POLLERR | POLLNVAL)) return -1;
        if (!(pfd.revents & POLLIN)) return -1;
        ssize_t n = read(fd, p + off, len - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;
        off += (size_t)n;
    }
    return 0;
}

/* ── Framing ── */

int am_ipc_write_frame(int fd, const uint8_t *data, size_t len)
{
    if (len > 0xFFFFFFFFu) return -1;
    uint8_t hdr[4];
    am_wr32(hdr, (uint32_t)len);
    if (write_all(fd, hdr, 4) < 0) return -1;
    if (len > 0 && write_all(fd, data, len) < 0) return -1;
    return 0;
}

int am_ipc_read_frame(int fd, uint8_t **out_frame, size_t *out_len,
                        int64_t deadline_ms)
{
    uint8_t hdrbuf[4];
    if (read_all_deadline(fd, hdrbuf, 4, deadline_ms) < 0) return -1;
    uint32_t flen = am_rd32(hdrbuf);
    if (flen > 16u * 1024 * 1024) return -1;
    uint8_t *frame = (uint8_t *)malloc(flen ? flen : 1);
    if (!frame) return -1;
    if (flen > 0 && read_all_deadline(fd, frame, flen, deadline_ms) < 0) {
        free(frame);
        return -1;
    }
    *out_frame = frame;
    *out_len = flen;
    return 0;
}

/* ── Handshake JSON ── */

size_t am_ipc_build_handshake(char *out, size_t cap, int role, int id)
{
    int n = snprintf(out, cap, "{\"v\":%d,\"role\":%d,\"id\":%d}",
                     AM_IPC_PROTO_VERSION, role, id);
    return (n < 0 || (size_t)n >= cap) ? 0 : (size_t)n;
}

size_t am_ipc_build_ack(char *out, size_t cap, int ok)
{
    int n = snprintf(out, cap, "{\"ok\":%d,\"v\":%d}",
                     ok ? 1 : 0, AM_IPC_PROTO_VERSION);
    return (n < 0 || (size_t)n >= cap) ? 0 : (size_t)n;
}

/* ── Handshake / ack parsing ──
 *
 * 为何必须在 C 层：两个调用点都在 JS 不可重入窗口——父进程 am_proc_spawn
 * 的同步握手窗口、子进程 rt_main 的启动顺序 handshake/ack（生命周期步骤 2）
 * 先于 am_t init（步骤 3），JS context 尚不存在，JS_ParseJSON 不可用
 * （裁决：docs/architecture/c-js-layering.md §6.6）。字段提取用 vendored
 * cJSON（用户指令：不手写）。消息由本文件 build_handshake/build_ack 生成，
 * 格式自产自销。 */

int am_ipc_parse_handshake(const char *json, int *out_v,
                             int *out_role, int *out_id)
{
    cJSON *j = cJSON_Parse(json);
    if (!j) return -1;
    int rc = -1;
    const cJSON *vv = cJSON_GetObjectItemCaseSensitive(j, "v");
    const cJSON *role = cJSON_GetObjectItemCaseSensitive(j, "role");
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(j, "id");
    if (cJSON_IsNumber(vv) && cJSON_IsNumber(role) && cJSON_IsNumber(id)) {
        *out_v = vv->valueint;
        *out_role = role->valueint;
        *out_id = id->valueint;
        rc = 0;
    }
    cJSON_Delete(j);
    return rc;
}

int am_ipc_parse_ack(const char *json, int *out_ok, int *out_v)
{
    cJSON *j = cJSON_Parse(json);
    if (!j) return -1;
    int rc = -1;
    const cJSON *ok = cJSON_GetObjectItemCaseSensitive(j, "ok");
    const cJSON *vv = cJSON_GetObjectItemCaseSensitive(j, "v");
    if (cJSON_IsNumber(ok) && cJSON_IsNumber(vv)) {
        *out_ok = ok->valueint;
        *out_v = vv->valueint;
        rc = 0;
    }
    cJSON_Delete(j);
    return rc;
}

/* ── M-P2 主RT 通道 CONTROL 协议分类（§6.1，两侧共用） ── */

am_ipc_ctl_kind_t am_ipc_ctl_classify(const uint8_t *payload,
                                          uint32_t len, int *out_val)
{
    if (!payload || len == 0) return AM_IPC_CTL_NONE;

    /* cJSON 按 NUL 结尾扫描，payload 是零拷贝片（rbuf 内），补一份带 NUL 的
     * 副本再解析。CONTROL 消息都很小（<64B），一次性栈缓冲足够。 */
    char buf[256];
    if (len >= sizeof(buf)) return AM_IPC_CTL_NONE;
    memcpy(buf, payload, len);
    buf[len] = '\0';

    cJSON *j = cJSON_Parse(buf);
    if (!j) return AM_IPC_CTL_NONE;
    am_ipc_ctl_kind_t kind = AM_IPC_CTL_NONE;
    if (cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(j, "amoib"))) {
        const cJSON *ready = cJSON_GetObjectItemCaseSensitive(j, "ready");
        const cJSON *idle  = cJSON_GetObjectItemCaseSensitive(j, "idle");
        const cJSON *sd    = cJSON_GetObjectItemCaseSensitive(j, "shutdown");
        if (cJSON_IsNumber(ready)) {
            kind = AM_IPC_CTL_READY;
            *out_val = ready->valueint;
        } else if (cJSON_IsNumber(idle)) {
            kind = AM_IPC_CTL_IDLE;
            *out_val = idle->valueint;
        } else if (cJSON_IsNumber(sd)) {
            kind = AM_IPC_CTL_SHUTDOWN;
            *out_val = sd->valueint;
        } else if (cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(j, "ping"))) {
            /* liveness 探测：宿主→对端。对端 C 层读泵识别后直回 PONG。 */
            kind = AM_IPC_CTL_PING;
            *out_val = 1;
        } else if (cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(j, "pong"))) {
            /* liveness 应答：对端读泵回显 corr（= ping seq）。 */
            kind = AM_IPC_CTL_PONG;
            *out_val = 1;
        } else if (cJSON_IsNumber(
                       cJSON_GetObjectItemCaseSensitive(j, "pfail"))) {
            /* 跨层 ping 转发失败：中间节点回 corr=seq（宿主快速 -1）。 */
            kind = AM_IPC_CTL_PFAIL;
            *out_val = 1;
        } else {
            /* 带 "amoib" 标记但非 ready/idle/shutdown（M-P4 closing 等）：
             * 通道级系统消息，交通道层消费，不被当作控制面命令路由。 */
            kind = AM_IPC_CTL_SYSTEM;
        }
    }
    cJSON_Delete(j);
    return kind;
}

/* 跨层 ping 的 "tp" 数组提取（{"amoib":1,"ping":N,"tp":[...]} / PONG 回显同
 * 字段）。返回元素数（0 = 无 tp = 单跳形态）。栈缓冲 + cJSON（与 classify
 * 同裁决：不手写解析）；读泵/主RT g_rx 两侧共用。 */
int am_ipc_ping_tp(const uint8_t *payload, uint32_t len,
                     int32_t *out, int cap)
{
    if (!payload || len == 0 || len > 256) return 0;
    char buf[257];
    memcpy(buf, payload, len);
    buf[len] = '\0';
    cJSON *j = cJSON_Parse(buf);
    if (!j) return 0;
    int n = 0;
    const cJSON *tp = cJSON_GetObjectItemCaseSensitive(j, "tp");
    if (cJSON_IsArray(tp)) {
        const cJSON *e = NULL;
        cJSON_ArrayForEach(e, tp) {
            if (n >= cap) break;
            if (cJSON_IsNumber(e)) out[n++] = (int32_t)e->valuedouble;
        }
    }
    cJSON_Delete(j);
    return n;
}

/* ── Binary path detection ── */

/* Resolve the amoib-rt binary path. On success returns a malloc'd string;
 * on failure returns NULL and sets *oom to 1 iff an allocation failed (so
 * the caller can distinguish NO_MEMORY from NOT_FOUND — a plain strdup OOM
 * used to be misreported as "binary not found"). */
static char *resolve_binary(const char *binary_path, int *oom)
{
    if (binary_path && binary_path[0]) {
        char *r = strdup(binary_path);
        if (!r) *oom = 1;
        return r;
    }

    const char *env = getenv("AM_RT_SERVER");
    if (env && env[0]) {
        char *r = strdup(env);
        if (!r) *oom = 1;
        return r;
    }

    /* Try /proc/self/exe directory + "/amoib-rt" */
    char self[4096];
    ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (n > 0) {
        self[n] = '\0';
        char *slash = strrchr(self, '/');
        if (slash) {
            size_t dlen = (size_t)(slash - self) + 1;
            char *path = (char *)malloc(dlen + sizeof("amoib-rt") + 1);
            if (!path) {
                *oom = 1;
            } else {
                memcpy(path, self, dlen);
                memcpy(path + dlen, "amoib-rt", sizeof("amoib-rt") - 1);
                path[dlen + sizeof("amoib-rt") - 1] = '\0';
                if (access(path, X_OK) == 0)
                    return path;
                free(path);
            }
        }
    }

#ifdef AM_RT_PATH
    if (access(AM_RT_PATH, X_OK) == 0) {
        char *r = strdup(AM_RT_PATH);
        if (!r) *oom = 1;
        return r;
    }
#endif

    return NULL;
}

/* Blocking reap with EINTR retry. Used on spawn-failure and terminate/destroy
 * paths where the child is known to be exiting (channel EOF or SIGKILL sent),
 * so waitpid returns promptly and blocking is safe. */
static void proc_reap_blocking(pid_t pid)
{
    int status;
    pid_t r;
    do { r = waitpid(pid, &status, 0); } while (r < 0 && errno == EINTR);
}

/* ── Parent side: spawn ── */

int am_proc_spawn(am_t *parent, am_proc_t *proc,
                    const char *exe,
                    char *const argv[],
                    int role, int id,
                    int require_handshake)
{
    if (!proc || !argv) return AM_ERR_INVALID_ARG;
    memset(proc, 0, sizeof(*proc));
    proc->pid = -1;
    proc->id = id;
    proc->role = role;
    proc->state = AM_PROC_BUILD;
    proc->parent_rt = parent;
    int kill_err = AM_ERR_GENERIC;   /* refined per failure cause below */

    int oom = 0;
    char *path = resolve_binary(exe, &oom);
    if (!path) return oom ? AM_ERR_NO_MEMORY : AM_ERR_NOT_FOUND;

    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        free(path);
        return AM_ERR_IO;
    }

    /* Clear CLOEXEC on child end so it survives execv */
    int fd_flags = fcntl(sv[1], F_GETFD);
    if (fd_flags >= 0)
        fcntl(sv[1], F_SETFD, fd_flags & ~FD_CLOEXEC);

    pid_t pid = fork();
    if (pid < 0) {
        close(sv[0]); close(sv[1]);
        free(path);
        return AM_ERR_IO;
    }

    if (pid == 0) {
        /* ── Child ── */
        close(sv[0]);
        /* 固定子端通道 fd：调用方拼的 argv 里 --parent-fd 写的是
         * AM_IPC_CHANNEL_FD（3），故 exec 前必须把 socketpair 子端搬到该
         * fd。dup2 无条件覆盖（child 拥有自己的 fd 表）并自动清除 CLOEXEC；
         * sv[1] 恰为固定 fd 时已在上方清过 CLOEXEC，直接保留。 */
        if (sv[1] != AM_IPC_CHANNEL_FD) {
            dup2(sv[1], AM_IPC_CHANNEL_FD);
            close(sv[1]);
        }
        execv(path, argv);
        _exit(127);
    }

    /* ── Parent ── */
    free(path);
    close(sv[1]);
    proc->pid = pid;

    /* uv_pipe_open the parent end */
    uv_loop_t *loop = parent ? &parent->loop : uv_default_loop();
    if (uv_pipe_init(loop, &proc->pipe, 0) != 0) {
        close(sv[0]);
        goto kill_fail;
    }
    proc->pipe_inited = 1;   /* init 过即标记：free 时须 uv_close 该 handle */
    /* Outbound spill-buffer flush timer (lossless backpressure). 紧随 pipe
     * init：am_proc_free 以 pipe_inited 为"pipe 与 tx timer 均已 init"的
     * 不变量（两者都要 uv_close，proc 内存在最后一个 close 回调里释放）。
     * 若排在 pipe_open 之后，pipe_open 失败走 kill_fail → free 会对未 init
     * 的 timer 调 uv_close → UB。 */
    proc->tx.fd = -1;
    uv_timer_init(loop, &proc->tx.timer);
    if (uv_pipe_open(&proc->pipe, sv[0]) != 0) {
        close(sv[0]);
        goto kill_fail;
    }

    /* ── Handshake (§3.3): child sends first, parent validates, replies ack.
     * 仅当 require_handshake 且 role>=0（调用方要求 amoib 信封协议握手）；
     * 任意可执行文件（不 speak 信封协议）以 require_handshake=0 直接 RUN。 */
    if (require_handshake && role >= 0) {
        uv_os_fd_t osfd;
        int pfd;
        if (uv_fileno((uv_handle_t *)&proc->pipe, &osfd) == 0)
            pfd = (int)(intptr_t)osfd;
        else {
            pfd = sv[0];
        }

        /* Read child's handshake (5s deadline) */
        int64_t deadline = am_now_ms() + AM_IPC_HANDSHAKE_TIMEOUT_MS;
        uint8_t *hs_frame = NULL;
        size_t hs_flen = 0;
        if (am_ipc_read_frame(pfd, &hs_frame, &hs_flen, deadline) < 0) {
            kill_err = AM_ERR_TIMEOUT;   /* handshake never arrived in 5s */
            goto kill_fail;
        }

        /* Decode envelope */
        ipc_envelope_view_t view;
        if (ipc_envelope_decode(hs_frame, hs_flen, &view) < 0) {
            free(hs_frame);
            goto kill_fail;
        }

        /* Parse handshake JSON */
        char hs_json[40];
        size_t cp = view.payload_len < sizeof(hs_json) - 1
                      ? view.payload_len : sizeof(hs_json) - 1;
        memcpy(hs_json, view.payload, cp);
        hs_json[cp] = '\0';
        free(hs_frame);

        int hsv = 0, hsrole = 0, hsid = 0;
        if (am_ipc_parse_handshake(hs_json, &hsv, &hsrole, &hsid) < 0 ||
            hsv != AM_IPC_PROTO_VERSION ||
            hsrole != role || hsid != id)
            goto kill_fail;

        /* Build + send ack */
        char ack_json[24];
        size_t ack_jlen = am_ipc_build_ack(ack_json, sizeof ack_json, 1);
        if (ack_jlen == 0) goto kill_fail;

        size_t env_cap = IPC_ENVELOPE_ENCODED_SIZE(ack_jlen);
        uint8_t *env_buf = (uint8_t *)malloc(env_cap);
        if (!env_buf) goto kill_fail;
        size_t env_len = ipc_envelope_encode(env_buf, env_cap,
                                            1, (int32_t)hsid,
                                            IPC_ENV_KIND_CONTROL,
                                            0,
                                            (const uint8_t *)ack_json,
                                            (uint32_t)ack_jlen);
        if (env_len == 0) { free(env_buf); goto kill_fail; }

        if (am_ipc_write_frame(pfd, env_buf, env_len) < 0) {
            free(env_buf);
            goto kill_fail;
        }
        free(env_buf);
    }

    proc->state = AM_PROC_RUN;
    return 0;

kill_fail:
    if (proc->pid > 0) {
        int kr = kill(proc->pid, SIGKILL);
        /* Only reap when the child is ours to reap: kill succeeded, or it is
         * already gone (ESRCH → waitpid returns ECHILD immediately). On any
         * other error (EPERM) a blocking waitpid could hang the parent. */
        if (kr == 0 || errno == ESRCH) proc_reap_blocking(proc->pid);
        proc->pid = -1;
    }
    /* 不在此 uv_close：pipe 已在 loop 上，调用方随后 am_proc_free 会统一
     * uv_close(proc_on_closed) 异步回收 proc 内存。二次 close 会 assert。 */
    return kill_err;
}

/* ── 3-tier terminate ──
 *
 * KNOWN LIMITATION (I5, design §9.2 deviation, documented): tiers 2 and 3
 * block the CALLING thread — the parent loop thread when invoked from
 * workerTerminate, or the teardown thread at am_thread_teardown — for up to
 * timeout_ms (default 2s) per hung worker. A worker that ignores
 * CONTROL{shutdown} (deadloop / stuck syscall) therefore freezes the parent
 * loop for up to 2s instead of the design's "one hung worker must not block
 * the whole tree shutdown". Accepted for M-P1 because:
 *   - the graceful path (worker exits on shutdown) completes in ~1ms — the
 *     freeze only materializes for already-broken workers;
 *   - an async tier-2 (uv_timer-driven WNOHANG polling + escalation) reworks
 *     the synchronous terminate contract that am_worker_terminate and the
 *     teardown loop both rely on, with real lifecycle risk (teardown ordering,
 *     handle close, pid ownership).
 * Revisit in M-P4 if a 2s worst-case freeze is unacceptable for a host.
 * M-P4 (2026-09-14): revisited → DEFERRED. The graceful path completes in
 * ~1ms; only already-broken workers freeze, and the tree-close bound is
 * 2s × hung-workers. Making tier-2 async (uv_timer WNOHANG + escalation)
 * would move pid ownership and proc lifetime across loop ticks while
 * am_worker_terminate, the teardown chain and am_proc_free (proc memory
 * freed in the last uv_close callback) all assume "pid reaped when terminate
 * returns" — re-entrancy/double-free risk outweighs the saving. See plan §9.2. */

int am_proc_terminate(am_proc_t *proc, int timeout_ms)
{
    if (!proc || proc->pid <= 0) return -1;
    if (timeout_ms <= 0) timeout_ms = AM_IPC_TERMINATE_TIMEOUT_MS;

    /* Tier 1: CONTROL{shutdown} */
    const char *shutdown_json = "{\"cmd\":\"shutdown\"}";
    size_t slen = strlen(shutdown_json);
    size_t env_cap = IPC_ENVELOPE_ENCODED_SIZE(slen);
    uint8_t *env_buf = (uint8_t *)malloc(env_cap);
    if (env_buf) {
        size_t env_len = ipc_envelope_encode(env_buf, env_cap,
                                            0, (int32_t)proc->id,
                                            IPC_ENV_KIND_CONTROL,
                                            0,
                                            (const uint8_t *)shutdown_json,
                                            (uint32_t)slen);
        if (env_len > 0) {
            uv_os_fd_t osfd;
            if (uv_fileno((uv_handle_t *)&proc->pipe, &osfd) == 0)
                am_ipc_write_frame((int)(intptr_t)osfd, env_buf, env_len);
        }
        free(env_buf);
    }

    /* Tier 2: poll for exit */
    int64_t deadline = am_now_ms() + timeout_ms;
    for (;;) {
        int status;
        pid_t r;
        do { r = waitpid(proc->pid, &status, WNOHANG); }
        while (r < 0 && errno == EINTR);
        if (r == proc->pid) {
            proc->pid = -1;
            proc->state = AM_PROC_DEAD;
            return 0;
        }
        if (r < 0) break;
        if (am_now_ms() >= deadline) break;
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 10 * 1000000 };
        nanosleep(&ts, NULL);
    }

    /* Tier 3: SIGKILL + reap */
    {
        int kr = kill(proc->pid, SIGKILL);
        if (kr == 0 || errno == ESRCH) proc_reap_blocking(proc->pid);
    }
    proc->pid = -1;
    proc->state = AM_PROC_DEAD;
    return 0;
}

/* ── Async outbound writes: spill buffer + timer flush ──
 *
 * Backpressure handling for the socketpair channel. The fds are O_NONBLOCK
 * (uv_pipe_open), so a synchronous send() either blocks the single-threaded
 * loop (deadlock when the peer blocks writing the other direction) or returns
 * EAGAIN — and treating EAGAIN as a hard failure SILENTLY DROPS the frame
 * (parent→child and child→parent messages vanish, echo counts never reconcile,
 * and JS awaiting the missing replies hangs forever — the R3/R4 PROCESS hang).
 * Instead, on EAGAIN the frame is appended to an unbounded spill buffer flushed
 * by a 1ms uv_timer while non-empty: lossless, non-blocking, FIFO. The buffer
 * outlives every send(), so there is no libuv write-request lifetime to get
 * wrong (uv_write's request/buffer must outlive the completion callback; a
 * per-message malloc'd buffer freed in write_cb corrupts under load).
 */
static void am_tx_flush(am_tx_t *tx)
{
    if (tx->fd < 0 || tx->len == 0) return;
    size_t off = 0;
    while (off < tx->len) {
        ssize_t n = send(tx->fd, tx->buf + off, tx->len - off, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            /* ENOBUFS 是 socket 缓冲耗尽（同 EAGAIN），不是永久错误——libuv
             * uv__try_write 也把 ENOBUFS 当背压。硬当错误会把整个 spill buffer
             * 丢弃：洪水下间歇性丢帧 → echo 数不清 → 挂死。 */
            if (errno == EAGAIN || errno == EWOULDBLOCK ||
                errno == ENOBUFS) break;
            /* EPIPE/ECONNRESET: peer gone — drop the backlog; the read side
             * reports the death via EOF. */
            tx->len = 0;
            break;
        }
        off += (size_t)n;
    }
    if (off > 0) {
        tx->len -= off;
        if (tx->len > 0)
            memmove(tx->buf, tx->buf + off, tx->len);
    }
    if (tx->len == 0 && tx->timer_active) {
        uv_timer_stop(&tx->timer);
        tx->timer_active = 0;
    }
}

static void am_tx_timer_cb(uv_timer_t *t)
{
    am_tx_t *tx = (am_tx_t *)t->data;
    am_tx_flush(tx);
}

static int am_tx_send(am_tx_t *tx, const uint8_t *frame, size_t flen)
{
    if (tx->len + flen > tx->cap) {
        size_t ncap = tx->cap ? tx->cap : 4096;
        while (ncap < tx->len + flen) ncap *= 2;
        uint8_t *nb = (uint8_t *)realloc(tx->buf, ncap);
        if (!nb) return -1;
        tx->buf = nb;
        tx->cap = ncap;
    }
    memcpy(tx->buf + tx->len, frame, flen);
    tx->len += flen;
    am_tx_flush(tx);
    if (tx->len > 0 && !tx->timer_active) {
        tx->timer.data = tx;
        if (uv_timer_start(&tx->timer, am_tx_timer_cb, 1, 1) == 0)
            tx->timer_active = 1;
    }
    return 0;
}
/* Encode [4-byte LE len][envelope] and hand to the spill-buffer queue. */
static int proc_frame_send(am_tx_t *tx,
                           int32_t source, int32_t target, int8_t kind,
                           int32_t corr,
                           const uint8_t *payload, uint32_t payload_len)
{
    size_t env_cap = IPC_ENVELOPE_ENCODED_SIZE(payload_len);
    uint8_t *env_buf = (uint8_t *)malloc(env_cap);
    if (!env_buf) return -1;
    size_t env_len = ipc_envelope_encode(env_buf, env_cap,
                                        source, target, kind,
                                        corr,
                                        payload, payload_len);
    if (env_len == 0 || env_len > 0xFFFFFFFFu - 4u) {
        free(env_buf);
        return -1;
    }
    uint32_t flen = (uint32_t)env_len + 4u;
    uint8_t *frame = (uint8_t *)malloc(flen);
    if (!frame) { free(env_buf); return -1; }
    am_wr32(frame, (uint32_t)env_len);
    memcpy(frame + 4, env_buf, env_len);
    free(env_buf);
    int rc = am_tx_send(tx, frame, flen);
    free(frame);
    return rc;
}

int am_proc_post(am_proc_t *proc,
                   int32_t source, int32_t target, int8_t kind,
                   int32_t corr,
                   const uint8_t *payload, uint32_t payload_len)
{
    if (!proc || proc->state != AM_PROC_RUN) return -1;
    if (proc->tx.fd < 0) {
        uv_os_fd_t osfd;
        if (uv_fileno((uv_handle_t *)&proc->pipe, &osfd) < 0) return -1;
        proc->tx.fd = (int)(intptr_t)osfd;
    }
    return proc_frame_send(&proc->tx, source, target, kind,
                           corr,
                           payload, payload_len);
}

/* 宿主侧：M-P2 CONTROL 协议消息（source=宿主 0, target=主RT 1）。 */
int am_proc_post_ctl(am_proc_t *proc, const char *json)
{
    if (!json) return -1;
    return am_proc_post(proc, AM_IPC_HOST_ID, AM_IPC_MAIN_ID,
                          IPC_ENV_KIND_CONTROL,
                          0,
                          (const uint8_t *)json, (uint32_t)strlen(json));
}

/* ── Destroy & Free (libuv-idiomatic self-reclaim) ── */

/* proc 内嵌两个 handle（pipe + tx flush timer），am_proc_free 对两者都
 * uv_close；close_pending 归零（最后一个 close 回调）才释放 proc 内存。
 * 若只关 pipe 就 free，tx timer 的 handle_queue 节点残留成悬垂 → uv_walk
 * （wait_idle 的 idle 检测）遍历到已释放内存 SIGSEGV —— PROCESS 后端高负载
 * 崩溃根因。 */
static void proc_reclaim(am_proc_t *proc)
{
    if (proc && --proc->close_pending == 0) {
        free(proc->rbuf);
        free(proc);
    }
}

static void proc_on_closed(uv_handle_t *h)   /* pipe 的 close cb */
{
    proc_reclaim((am_proc_t *)h->data);
}

static void proc_tx_timer_on_closed(uv_handle_t *h)  /* tx flush timer 的 close cb */
{
    /* timer.data 归 am_tx_timer_cb 用（am_tx_t*），不能复用——用
     * container_of 从内嵌地址回推 proc。 */
    proc_reclaim((am_proc_t *)((char *)h - offsetof(am_proc_t, tx.timer)));
}

void am_proc_destroy(am_proc_t *proc)
{
    if (!proc) return;
    if (proc->pid > 0) {
        int kr = kill(proc->pid, SIGKILL);
        if (kr == 0 || errno == ESRCH) proc_reap_blocking(proc->pid);
        proc->pid = -1;
    }
    proc->state = AM_PROC_DEAD;
}

void am_proc_free(am_proc_t *proc)
{
    if (!proc || proc->freed) return;   /* 幂等：防重复 free / 二次 uv_close */
    proc->freed = 1;
    am_proc_destroy(proc);
    if (proc->tx.timer_active)
        uv_timer_stop(&proc->tx.timer);
    free(proc->tx.buf);
    proc->tx.buf = NULL;
    if (proc->pipe_inited) {
        uv_read_stop((uv_stream_t *)&proc->pipe);
        proc->pipe.data = proc;
        proc->pipe_inited = 0;
        /* libuv pattern: proc memory（内含两个 handle）在最后一个 close 回调
         * 里释放，两个 handle 都经 uv__finish_close 从 loop 摘除后才行。 */
        proc->close_pending = 2;
        uv_close((uv_handle_t *)&proc->pipe, proc_on_closed);
        uv_close((uv_handle_t *)&proc->tx.timer, proc_tx_timer_on_closed);
    } else {
        free(proc->rbuf);
        free(proc);
    }
}

/* ── Child-side emit channel (single per process) ── */

static int g_child_fd = -1;
static am_tx_t g_child_tx;

void am_ipc_child_set_channel(int fd)
{
    g_child_fd = fd;
}

int am_ipc_child_channel(void)
{
    return g_child_fd;
}

void am_ipc_child_tx_init(uv_loop_t *loop, int fd)
{
    g_child_fd = fd;
    memset(&g_child_tx, 0, sizeof(g_child_tx));
    g_child_tx.fd = fd;
    if (uv_timer_init(loop, &g_child_tx.timer) != 0)
        g_child_tx.fd = -1;
}

int am_ipc_child_emit(int32_t source, int32_t target, int8_t kind,
                        int32_t corr,
                        const uint8_t *payload, uint32_t payload_len)
{
    if (g_child_tx.fd < 0) return -1;
    return proc_frame_send(&g_child_tx, source, target, kind,
                           corr,
                           payload, payload_len);
}

/* ── M-P4 同步 storage RPC（§10.2）：实现驻留 rt_main.c（独占子进程管道
 * 读状态），libamoib 侧经函数指针调度——CLI/宿主进程不注册，调用即 -1
 * （worker 进程之外不可达，与 set_channel 同构）。 */
static am_ipc_storage_sync_fn g_storage_sync = NULL;

void am_ipc_child_set_storage_sync(am_ipc_storage_sync_fn fn)
{
    g_storage_sync = fn;
}

int am_ipc_child_storage_sync(am_t *rt, const uint8_t *payload,
                                uint32_t payload_len,
                                uint8_t **out_reply, uint32_t *out_reply_len)
{
    if (!g_storage_sync) return -1;
    return g_storage_sync(rt, payload, payload_len, out_reply, out_reply_len);
}

/* 主RT 进程侧：M-P2 CONTROL 协议消息（source=主RT 1, target=宿主 0）。 */
int am_ipc_child_emit_ctl(const char *json)
{
    if (!json) return -1;
    return am_ipc_child_emit(AM_IPC_MAIN_ID, AM_IPC_HOST_ID,
                               IPC_ENV_KIND_CONTROL,
                               0,
                               (const uint8_t *)json, (uint32_t)strlen(json));
}

/* ── M-P4 同步 storage RPC 用：出站缓冲的同步排空/查询 ──
 * child_storage_sync 在等待期间不跑 uv_run（flush timer 回调只在 loop 里
 * 触发），请求帧若落进 spill buffer 就必须当场排空；仍背压则快速失败
 * （等 timer 会死锁）。 */
void am_ipc_child_tx_flush(void)
{
    am_tx_flush(&g_child_tx);
}

int am_ipc_child_tx_pending(void)
{
    return g_child_tx.len > 0;
}
/* ── M-P4 同步 storage RPC 用：阻塞整帧发送 ──
 * 异步 emit（am_tx_send）把 EAGAIN 帧塞进 spill buffer，由 1ms timer 在
 * uv_run 里排空——但同步 RPC 的等待循环不跑 uv_run（否则 JS timer 重入），
 * 大 payload（quota 内可达 ~5MB，远超 socket 缓冲）会永久滞留在 buffer 里。
 * 本函数：先 poll(POLLOUT) 排空 spill buffer（保持 FIFO 顺序），再直接
 * poll+send 循环发完整帧，父进程死亡（POLLHUP/EOF）→ 返回 -1（§9.4 孤儿
 * 自杀路径由调用方触发）。 */
int am_ipc_child_emit_sync(int32_t source, int32_t target, int8_t kind,
                             int32_t corr,
                             const uint8_t *payload, uint32_t payload_len)
{
    if (g_child_tx.fd < 0) return -1;
    int fd = g_child_tx.fd;

    /* 排空既有 spill buffer（先发先序） */
    for (;;) {
        struct pollfd pfd = { .fd = fd, .events = POLLOUT };
        int pr = poll(&pfd, 1, 200);
        if (pr < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (pr == 0) continue;
        if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) return -1;
        am_tx_flush(&g_child_tx);
        if (g_child_tx.len == 0) break;
    }

    size_t env_cap = IPC_ENVELOPE_ENCODED_SIZE(payload_len);
    uint8_t *env_buf = (uint8_t *)malloc(env_cap);
    if (!env_buf) return -1;
    size_t env_len = ipc_envelope_encode(env_buf, env_cap,
                                         source, target, kind,
                                         corr,
                                         payload, payload_len);
    if (env_len == 0 || env_len > 0xFFFFFFFFu - 4u) {
        free(env_buf);
        return -1;
    }
    uint32_t flen = (uint32_t)env_len + 4u;
    uint8_t *frame = (uint8_t *)malloc(flen);
    if (!frame) { free(env_buf); return -1; }
    am_wr32(frame, (uint32_t)env_len);
    memcpy(frame + 4, env_buf, env_len);
    free(env_buf);

    size_t off = 0;
    int rc = 0;
    while (off < flen) {
        struct pollfd pfd = { .fd = fd, .events = POLLOUT };
        int pr = poll(&pfd, 1, 200);
        if (pr < 0) {
            if (errno == EINTR) continue;
            rc = -1;
            break;
        }
        if (pr == 0) continue;
        if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) { rc = -1; break; }
        ssize_t n = send(fd, frame + off, flen - off, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == ENOBUFS)
                continue;
            rc = -1;
            break;
        }
        off += (size_t)n;
    }
    free(frame);
    return rc;
}

am_proc_t *am_proc_new(void)
{
    return (am_proc_t *)calloc(1, sizeof(am_proc_t));
}

/* ── Async read pump: inbound envelopes → parent msgq ── */

static void proc_alloc_cb(uv_handle_t *h, size_t suggested, uv_buf_t *buf)
{
    (void)h; (void)suggested;
    buf->base = (char *)malloc(AM_IPC_READ_BUF_SIZE);
    buf->len = buf->base ? AM_IPC_READ_BUF_SIZE : 0;
}

static void proc_peer_dead(am_proc_t *proc)
{
    proc->state = AM_PROC_DEAD;
    if (proc->pid > 0) {
        int status;
        pid_t r;
        do { r = waitpid(proc->pid, &status, WNOHANG); }
        while (r < 0 && errno == EINTR);
        if (r == proc->pid) {
            proc->pid = -1;
        } else if (r == 0) {
            int kr = kill(proc->pid, SIGKILL);
            if (kr == 0 || errno == ESRCH) proc_reap_blocking(proc->pid);
            proc->pid = -1;   /* ECHILD — 已被别处收割 */
        }
    }
    if (proc->msg_cb)
        proc->msg_cb(proc->msg_user, 0, 0, 0, NULL, 0);  /* EOF 通知（JS-managed） */
}

static void proc_process_rx(am_proc_t *proc)
{
    am_t *parent = (am_t *)proc->parent_rt;
    for (;;) {
        if (proc->frame_len == 0) {
            if (proc->rbuf_len < 4) return;
            proc->frame_len = am_rd32(proc->rbuf);
            proc->rbuf_len -= 4;
            if (proc->rbuf_len > 0)
                memmove(proc->rbuf, proc->rbuf + 4, proc->rbuf_len);
            if (proc->frame_len > 16u * 1024 * 1024) {
                /* protocol error → treat as peer death (reap + release slot) */
                proc_peer_dead(proc);
                return;
            }
        }
        if (proc->rbuf_len < proc->frame_len) return;

        if (proc->frame_len > 0) {
            ipc_envelope_view_t view;
            if (ipc_envelope_decode(proc->rbuf, proc->frame_len, &view) == 0) {
                /* CTL-1（§2.2）：命令类 CONTROL 信封在本节点树路由（worker 的
                 * 回执上行 / 命令下行），不进 JS 层；系统级 CONTROL（握手/
                 * idle/shutdown）与非 CONTROL 帧照旧走 msg_cb / msgq。 */
                int ctl_routed = 0;
                if (view.kind == IPC_ENV_KIND_CONTROL &&
                    view.payload_len > 0) {
                    int cval = 0;
                    am_ipc_ctl_kind_t ck =
                        am_ipc_ctl_classify(view.payload, view.payload_len,
                                              &cval);
                    if (ck == AM_IPC_CTL_PONG || ck == AM_IPC_CTL_PFAIL) {
                        /* 跨层帧（am_ping_path 家族）：PONG 带 tp = 过境
                         * 标记 → 沿本节点父通道上行转发（corr/payload 保持，
                         * 信封 source = 本节点槽位）；pfail（中间节点转发
                         * 失败回执）同理直接上行。无 tp 的 PONG = 单跳
                         * am_proc_ping 的直回应答 → 拦截回填 proc->pong_seq
                         * （c734194d：宿主主RT 通道 ctl_route_id==0 的 PONG
                         * 必须走 msg_cb，否则劫走宿主 PONG 致其永远超时）。 */
                        int32_t tp[AM_SELF_PATH_MAX];
                        int tpn = (ck == AM_IPC_CTL_PONG)
                                      ? am_ipc_ping_tp(view.payload,
                                                         view.payload_len,
                                                         tp,
                                                         AM_SELF_PATH_MAX)
                                      : 1;
                        if (proc->ctl_route_id > 0 &&
                            (ck == AM_IPC_CTL_PFAIL || tpn > 0)) {
                            am_ipc_child_emit(proc->ctl_route_id, 0,
                                                IPC_ENV_KIND_CONTROL,
                                                view.corr,
                                                view.payload,
                                                view.payload_len);
                            ctl_routed = 1;
                        } else if (proc->ctl_route_id > 0 &&
                                   ck == AM_IPC_CTL_PONG) {
                            __atomic_store_n(&proc->pong_seq,
                                             (int32_t)view.corr,
                                             __ATOMIC_RELEASE);
                            ctl_routed = 1;
                        }
                        /* ctl_route_id==0（宿主主RT 通道）：PONG/pfail 一律
                         * 不拦截 → 交 msg_cb（host_proc_msg_cb 回填
                         * rt->pong_seq/ping_fail）——守卫回归 3e733e12。 */
                    } else if (proc->ctl_route_id > 0 &&
                               ck == AM_IPC_CTL_NONE) {
                        am_control_route((am_t *)proc->parent_rt,
                                           proc->ctl_route_id, view.source,
                                           view.target, view.payload,
                                           view.payload_len);
                        ctl_routed = 1;
                    }
                }
                if (!ctl_routed && proc->msg_cb) {
                    /* JS-managed 模式：信封解码 → 直接回调（bridge.c 的
                     * pal.processOnMessage 消费者）。payload 指向 rbuf 内
                     * 部，回调返回后即失效，JS_Call 需同步复制成 ArrayBuffer。 */
                    proc->msg_cb(proc->msg_user, view.kind, view.source,
                                 view.corr,
                                 view.payload, view.payload_len);
                } else if (!ctl_routed) {
                    int flags =
                        view.kind == IPC_ENV_KIND_CONTROL
                            ? AM_MSG_FLAG_CONTROL
                            : (view.kind == IPC_ENV_KIND_PORT_TRANSFER
                                   ? AM_MSG_FLAG_PORT_TRANSFER : 0);
                    if (parent && parent->magic == AM_MAGIC) {
                        am_msg_push(parent, (const char *)view.payload,
                                      view.payload_len, proc->id, flags);
                        uv_async_send(&parent->wake);
                    }
                }
            }
        }
        proc->rbuf_len -= proc->frame_len;
        if (proc->rbuf_len > 0)
            memmove(proc->rbuf, proc->rbuf + proc->frame_len, proc->rbuf_len);
        proc->frame_len = 0;
    }
}

static void proc_read_cb(uv_stream_t *s, ssize_t nread, const uv_buf_t *buf)
{
    am_proc_t *proc = (am_proc_t *)s->data;
    if (!proc) return;

    if (nread < 0) {
        /* EOF → peer dead (§9.3/§9.4): reap + release slot. JS error event is
         * M-P4. */
        free(buf->base);
        proc_peer_dead(proc);
        return;
    }
    if (nread == 0) { free(buf->base); return; }   /* EAGAIN */

    size_t need = proc->rbuf_len + (size_t)nread;
    if (need > proc->rbuf_cap) {
        size_t ncap = proc->rbuf_cap ? proc->rbuf_cap : 4096;
        while (ncap < need) ncap *= 2;
        uint8_t *nb = (uint8_t *)realloc(proc->rbuf, ncap);
        if (!nb) {
            free(buf->base);
            proc_peer_dead(proc);
            return;
        }
        proc->rbuf = nb;
        proc->rbuf_cap = ncap;
    }
    memcpy(proc->rbuf + proc->rbuf_len, buf->base, (size_t)nread);
    proc->rbuf_len += (size_t)nread;
    free(buf->base);
    proc_process_rx(proc);
}

void am_proc_start_read(am_proc_t *proc)
{
    if (!proc || proc->state != AM_PROC_RUN) return;
    proc->pipe.data = proc;
    uv_read_start((uv_stream_t *)&proc->pipe, proc_alloc_cb, proc_read_cb);
}

void am_proc_start_read_cb(am_proc_t *proc, am_proc_msg_cb_t cb,
                             void *user_data)
{
    if (!proc || proc->state != AM_PROC_RUN) return;
    proc->msg_cb = cb;
    proc->msg_user = user_data;
    proc->pipe.data = proc;
    uv_read_start((uv_stream_t *)&proc->pipe, proc_alloc_cb, proc_read_cb);
}

/* 恒活动 IPC pipe 的 idle 豁免检查（spawn 分层化 Phase C + M-P2）：
 *   - JS-managed 进程 worker 句柄（pal.processSpawn）的 pipe 随 child 生命周期
 *     保持打开 —— 宿主脚本在进程 worker 存活时仍须能判 idle 退出；
 *   - M-P2 主RT 进程的宿主通道读管道（parent-fd）同样恒活动 —— 不豁免则主RT
 *     在自己事件循环里永不判 idle，CONTROL{idle} 永远不会到达。
 * 替代已随 C 层进程 worker 分流移除的 am_worker_is_proc_handle。 */
int am_proc_handle_is_pipe(am_t *rt, uv_handle_t *h)
{
    if (!rt || !h) return 0;
    if (rt->ipc_channel_pipe && (uv_handle_t *)rt->ipc_channel_pipe == h)
        return 1;
    for (int i = 0; i < AM_MAX_PROC_HANDLES; i++) {
        am_proc_handle_t *ph = &rt->proc_handles[i];
        if (ph->live && ph->proc && (uv_handle_t *)&ph->proc->pipe == h)
            return 1;
    }
    return 0;
}

/* ── Liveness ping（worker 进程→sub worker，镜像 rt_host.c 的 am_ping）──
 * 发 CONTROL{"amoib":1,"ping":seq}（corr = seq，schema 零破坏）→ 阻塞等待
 * sub worker C 层读泵直回的 PONG（不经 JS/msgq——pong 延迟反映对端 uv loop
 * 健康度）。等待期间 uv 读泵不跑（JS 同步调用栈内），本函数自 poll+recv
 * 驱动：收到的字节进 rbuf 累加器逐帧解析，PONG 按 corr 配对（pong_seq 回
 * 填），非 PONG 帧留在 rbuf 原样待读泵下次活动正常消费（无 JS 重入、零丢
 * 帧）。POLLHUP/read==0 = 对端死 → -1（EOF 路径）。单飞行：同一 proc 同
 * 时至多一个 ping（JS 同步调用无并发，无需加锁）。 */
int am_proc_ping(am_proc_t *proc, int32_t timeout_ms)
{
    if (!proc || proc->state != AM_PROC_RUN) return -1;
    uv_os_fd_t osfd;
    if (uv_fileno((uv_handle_t *)&proc->pipe, &osfd) < 0) return -1;
    int fd = (int)(intptr_t)osfd;

    int32_t seq = __atomic_add_fetch(&proc->ping_seq, 1, __ATOMIC_ACQ_REL);
    if (am_proc_post(proc, 0, (int32_t)proc->id, IPC_ENV_KIND_CONTROL,
                       seq,
                       (const uint8_t *)AM_IPC_CTL_PING_MSG,
                       (uint32_t)(sizeof AM_IPC_CTL_PING_MSG - 1)) < 0)
        return -1;

    int64_t deadline = am_now_ms() + timeout_ms;
    uint8_t chunk[4096];
    for (;;) {
        if (__atomic_load_n(&proc->pong_seq, __ATOMIC_ACQUIRE) >= seq)
            return 0;   /* deadline 内 PONG 命中 = 对端 loop 通畅 */
        int64_t remain = deadline - am_now_ms();
        if (remain <= 0) return 1;   /* 超时 = 对端 loop 阻塞 */
        struct pollfd pfd = { .fd = fd, .events = POLLIN };
        int pr = poll(&pfd, 1, (int)(remain > 100 ? 100 : remain));
        if (pr < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) {
            /* 可能在 HUP 前还有残余数据：照常尝试读一轮，读不到再判死。 */
            if (!(pfd.revents & POLLIN)) return -1;
        }
        if (pfd.revents & POLLIN) {
            ssize_t n = read(fd, chunk, sizeof chunk);
            if (n < 0) {
                if (errno == EINTR || errno == EAGAIN) continue;
                return -1;
            }
            if (n == 0) return -1;   /* EOF = 对端死 */
            /* 喂 rbuf 累加器并逐帧解析（与 proc_process_rx 同逻辑，仅在此
             * 等待窗口内联——PONG 回填 pong_seq，非 PONG 帧留在 rbuf）。 */
            size_t need = proc->rbuf_len + (size_t)n;
            if (need > proc->rbuf_cap) {
                size_t ncap = proc->rbuf_cap ? proc->rbuf_cap : 4096;
                while (ncap < need) ncap *= 2;
                uint8_t *nb = (uint8_t *)realloc(proc->rbuf, ncap);
                if (!nb) return -1;
                proc->rbuf = nb;
                proc->rbuf_cap = ncap;
            }
            memcpy(proc->rbuf + proc->rbuf_len, chunk, (size_t)n);
            proc->rbuf_len += (size_t)n;
            for (;;) {
                if (proc->frame_len == 0) {
                    if (proc->rbuf_len < 4) break;
                    proc->frame_len = am_rd32(proc->rbuf);
                    proc->rbuf_len -= 4;
                    if (proc->rbuf_len > 0)
                        memmove(proc->rbuf, proc->rbuf + 4, proc->rbuf_len);
                    if (proc->frame_len > 16u * 1024 * 1024) {
                        /* 与 proc_process_rx 一致：协议错误按对端死亡处理
                         * （收尸 + 释放槽位），不留垃圾 frame_len 卡死通道。 */
                        proc_peer_dead(proc);
                        return -1;
                    }
                }
                if (proc->rbuf_len < proc->frame_len) break;
                if (proc->frame_len > 0) {
                    ipc_envelope_view_t v;
                    int is_pong = 0;
                    if (ipc_envelope_decode(proc->rbuf, proc->frame_len,
                                            &v) == 0 &&
                        v.kind == IPC_ENV_KIND_CONTROL && v.payload_len > 0) {
                        int pv = 0;
                        if (am_ipc_ctl_classify(v.payload, v.payload_len,
                                                  &pv) == AM_IPC_CTL_PONG)
                            is_pong = 1;
                    }
                    if (!is_pong) {
                        /* 非 PONG 帧保留在 rbuf（frame_len 不归零）：读泵
                         * 下次活动按完整帧正常消费——ping 等待窗口不吞应用
                         * 帧（MESSAGE/STORAGE/CONTROL 命令），零丢失。 */
                        break;
                    }
                    __atomic_store_n(&proc->pong_seq, (int32_t)v.corr,
                                     __ATOMIC_RELEASE);
                }
                proc->rbuf_len -= proc->frame_len;
                if (proc->rbuf_len > 0)
                    memmove(proc->rbuf,
                            proc->rbuf + proc->frame_len, proc->rbuf_len);
                proc->frame_len = 0;
                }
            continue;
        }
        sched_yield();
    }
}

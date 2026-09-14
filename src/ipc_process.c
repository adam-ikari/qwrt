/*
 * qwrt IPC Process — parent-side spawn / handshake / terminate (M-P1)
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
#include "qwrt_internal.h"
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
        int64_t remaining = deadline_ms - qwrt_now_ms();
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

int qwrt_ipc_write_frame(int fd, const uint8_t *data, size_t len)
{
    if (len > 0xFFFFFFFFu) return -1;
    uint8_t hdr[4];
    qwrt_wr32(hdr, (uint32_t)len);
    if (write_all(fd, hdr, 4) < 0) return -1;
    if (len > 0 && write_all(fd, data, len) < 0) return -1;
    return 0;
}

int qwrt_ipc_read_frame(int fd, uint8_t **out_frame, size_t *out_len,
                        int64_t deadline_ms)
{
    uint8_t hdrbuf[4];
    if (read_all_deadline(fd, hdrbuf, 4, deadline_ms) < 0) return -1;
    uint32_t flen = qwrt_rd32(hdrbuf);
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

size_t qwrt_ipc_build_handshake(char *out, size_t cap, int role, int id)
{
    int n = snprintf(out, cap, "{\"v\":%d,\"role\":%d,\"id\":%d}",
                     QWRT_IPC_PROTO_VERSION, role, id);
    return (n < 0 || (size_t)n >= cap) ? 0 : (size_t)n;
}

size_t qwrt_ipc_build_ack(char *out, size_t cap, int ok)
{
    int n = snprintf(out, cap, "{\"ok\":%d,\"v\":%d}",
                     ok ? 1 : 0, QWRT_IPC_PROTO_VERSION);
    return (n < 0 || (size_t)n >= cap) ? 0 : (size_t)n;
}

/* ── Handshake / ack parsing ──
 *
 * 为何必须在 C 层：两个调用点都在 JS 不可重入窗口——父进程 qwrt_proc_spawn
 * 的同步握手窗口、子进程 rt_main 的启动顺序 handshake/ack（生命周期步骤 2）
 * 先于 qwrt_t init（步骤 3），JS context 尚不存在，JS_ParseJSON 不可用
 * （裁决：docs/architecture/c-js-layering.md §6.6）。字段提取用 vendored
 * cJSON（用户指令：不手写）。消息由本文件 build_handshake/build_ack 生成，
 * 格式自产自销。 */

int qwrt_ipc_parse_handshake(const char *json, int *out_v,
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

int qwrt_ipc_parse_ack(const char *json, int *out_ok, int *out_v)
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

qwrt_ipc_ctl_kind_t qwrt_ipc_ctl_classify(const uint8_t *payload,
                                          uint32_t len, int *out_val)
{
    if (!payload || len == 0) return QWRT_IPC_CTL_NONE;

    /* cJSON 按 NUL 结尾扫描，payload 是零拷贝片（rbuf 内），补一份带 NUL 的
     * 副本再解析。CONTROL 消息都很小（<64B），一次性栈缓冲足够。 */
    char buf[256];
    if (len >= sizeof(buf)) return QWRT_IPC_CTL_NONE;
    memcpy(buf, payload, len);
    buf[len] = '\0';

    cJSON *j = cJSON_Parse(buf);
    if (!j) return QWRT_IPC_CTL_NONE;
    qwrt_ipc_ctl_kind_t kind = QWRT_IPC_CTL_NONE;
    if (cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(j, "qwrt"))) {
        const cJSON *ready = cJSON_GetObjectItemCaseSensitive(j, "ready");
        const cJSON *idle  = cJSON_GetObjectItemCaseSensitive(j, "idle");
        const cJSON *sd    = cJSON_GetObjectItemCaseSensitive(j, "shutdown");
        if (cJSON_IsNumber(ready)) {
            kind = QWRT_IPC_CTL_READY;
            *out_val = ready->valueint;
        } else if (cJSON_IsNumber(idle)) {
            kind = QWRT_IPC_CTL_IDLE;
            *out_val = idle->valueint;
        } else if (cJSON_IsNumber(sd)) {
            kind = QWRT_IPC_CTL_SHUTDOWN;
            *out_val = sd->valueint;
        }
    }
    cJSON_Delete(j);
    return kind;
}

/* ── Binary path detection ── */

/* Resolve the qwrt-rt binary path. On success returns a malloc'd string;
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

    const char *env = getenv("QWRT_RT_SERVER");
    if (env && env[0]) {
        char *r = strdup(env);
        if (!r) *oom = 1;
        return r;
    }

    /* Try /proc/self/exe directory + "/qwrt-rt" */
    char self[4096];
    ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (n > 0) {
        self[n] = '\0';
        char *slash = strrchr(self, '/');
        if (slash) {
            size_t dlen = (size_t)(slash - self) + 1;
            char *path = (char *)malloc(dlen + 8);
            if (!path) {
                *oom = 1;
            } else {
                memcpy(path, self, dlen);
                memcpy(path + dlen, "qwrt-rt", 7);
                path[dlen + 7] = '\0';
                if (access(path, X_OK) == 0)
                    return path;
                free(path);
            }
        }
    }

#ifdef QWRT_RT_PATH
    if (access(QWRT_RT_PATH, X_OK) == 0) {
        char *r = strdup(QWRT_RT_PATH);
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

int qwrt_proc_spawn(qwrt_t *parent, qwrt_proc_t *proc,
                    const char *exe,
                    char *const argv[],
                    int role, int id,
                    int require_handshake)
{
    if (!proc || !argv) return QWRT_ERR_INVALID_ARG;
    memset(proc, 0, sizeof(*proc));
    proc->pid = -1;
    proc->id = id;
    proc->role = role;
    proc->state = QWRT_PROC_BUILD;
    proc->parent_rt = parent;
    int kill_err = QWRT_ERR_GENERIC;   /* refined per failure cause below */

    int oom = 0;
    char *path = resolve_binary(exe, &oom);
    if (!path) return oom ? QWRT_ERR_NO_MEMORY : QWRT_ERR_NOT_FOUND;

    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        free(path);
        return QWRT_ERR_IO;
    }

    /* Clear CLOEXEC on child end so it survives execv */
    int fd_flags = fcntl(sv[1], F_GETFD);
    if (fd_flags >= 0)
        fcntl(sv[1], F_SETFD, fd_flags & ~FD_CLOEXEC);

    pid_t pid = fork();
    if (pid < 0) {
        close(sv[0]); close(sv[1]);
        free(path);
        return QWRT_ERR_IO;
    }

    if (pid == 0) {
        /* ── Child ── */
        close(sv[0]);
        /* 固定子端通道 fd：调用方拼的 argv 里 --parent-fd 写的是
         * QWRT_IPC_CHANNEL_FD（3），故 exec 前必须把 socketpair 子端搬到该
         * fd。dup2 无条件覆盖（child 拥有自己的 fd 表）并自动清除 CLOEXEC；
         * sv[1] 恰为固定 fd 时已在上方清过 CLOEXEC，直接保留。 */
        if (sv[1] != QWRT_IPC_CHANNEL_FD) {
            dup2(sv[1], QWRT_IPC_CHANNEL_FD);
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
     * init：qwrt_proc_free 以 pipe_inited 为"pipe 与 tx timer 均已 init"的
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
     * 仅当 require_handshake 且 role>=0（调用方要求 qwrt 信封协议握手）；
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
        int64_t deadline = qwrt_now_ms() + QWRT_IPC_HANDSHAKE_TIMEOUT_MS;
        uint8_t *hs_frame = NULL;
        size_t hs_flen = 0;
        if (qwrt_ipc_read_frame(pfd, &hs_frame, &hs_flen, deadline) < 0) {
            kill_err = QWRT_ERR_TIMEOUT;   /* handshake never arrived in 5s */
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
        if (qwrt_ipc_parse_handshake(hs_json, &hsv, &hsrole, &hsid) < 0 ||
            hsv != QWRT_IPC_PROTO_VERSION ||
            hsrole != role || hsid != id)
            goto kill_fail;

        /* Build + send ack */
        char ack_json[24];
        size_t ack_jlen = qwrt_ipc_build_ack(ack_json, sizeof ack_json, 1);
        if (ack_jlen == 0) goto kill_fail;

        size_t env_cap = IPC_ENVELOPE_ENCODED_SIZE(ack_jlen);
        uint8_t *env_buf = (uint8_t *)malloc(env_cap);
        if (!env_buf) goto kill_fail;
        size_t env_len = ipc_envelope_encode(env_buf, env_cap,
                                            1, (int32_t)hsid,
                                            IPC_ENV_KIND_CONTROL,
                                            (const uint8_t *)ack_json,
                                            (uint32_t)ack_jlen);
        if (env_len == 0) { free(env_buf); goto kill_fail; }

        if (qwrt_ipc_write_frame(pfd, env_buf, env_len) < 0) {
            free(env_buf);
            goto kill_fail;
        }
        free(env_buf);
    }

    proc->state = QWRT_PROC_RUN;
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
    /* 不在此 uv_close：pipe 已在 loop 上，调用方随后 qwrt_proc_free 会统一
     * uv_close(proc_on_closed) 异步回收 proc 内存。二次 close 会 assert。 */
    return kill_err;
}

/* ── 3-tier terminate ──
 *
 * KNOWN LIMITATION (I5, design §9.2 deviation, documented): tiers 2 and 3
 * block the CALLING thread — the parent loop thread when invoked from
 * workerTerminate, or the teardown thread at qwrt_thread_teardown — for up to
 * timeout_ms (default 2s) per hung worker. A worker that ignores
 * CONTROL{shutdown} (deadloop / stuck syscall) therefore freezes the parent
 * loop for up to 2s instead of the design's "one hung worker must not block
 * the whole tree shutdown". Accepted for M-P1 because:
 *   - the graceful path (worker exits on shutdown) completes in ~1ms — the
 *     freeze only materializes for already-broken workers;
 *   - an async tier-2 (uv_timer-driven WNOHANG polling + escalation) reworks
 *     the synchronous terminate contract that qwrt_worker_terminate and the
 *     teardown loop both rely on, with real lifecycle risk (teardown ordering,
 *     handle close, pid ownership).
 * Revisit in M-P4 if a 2s worst-case freeze is unacceptable for a host.
 * M-P4 (2026-09-14): revisited → DEFERRED. The graceful path completes in
 * ~1ms; only already-broken workers freeze, and the tree-close bound is
 * 2s × hung-workers. Making tier-2 async (uv_timer WNOHANG + escalation)
 * would move pid ownership and proc lifetime across loop ticks while
 * qwrt_worker_terminate, the teardown chain and qwrt_proc_free (proc memory
 * freed in the last uv_close callback) all assume "pid reaped when terminate
 * returns" — re-entrancy/double-free risk outweighs the saving. See plan §9.2. */

int qwrt_proc_terminate(qwrt_proc_t *proc, int timeout_ms)
{
    if (!proc || proc->pid <= 0) return -1;
    if (timeout_ms <= 0) timeout_ms = QWRT_IPC_TERMINATE_TIMEOUT_MS;

    /* Tier 1: CONTROL{shutdown} */
    const char *shutdown_json = "{\"cmd\":\"shutdown\"}";
    size_t slen = strlen(shutdown_json);
    size_t env_cap = IPC_ENVELOPE_ENCODED_SIZE(slen);
    uint8_t *env_buf = (uint8_t *)malloc(env_cap);
    if (env_buf) {
        size_t env_len = ipc_envelope_encode(env_buf, env_cap,
                                            0, (int32_t)proc->id,
                                            IPC_ENV_KIND_CONTROL,
                                            (const uint8_t *)shutdown_json,
                                            (uint32_t)slen);
        if (env_len > 0) {
            uv_os_fd_t osfd;
            if (uv_fileno((uv_handle_t *)&proc->pipe, &osfd) == 0)
                qwrt_ipc_write_frame((int)(intptr_t)osfd, env_buf, env_len);
        }
        free(env_buf);
    }

    /* Tier 2: poll for exit */
    int64_t deadline = qwrt_now_ms() + timeout_ms;
    for (;;) {
        int status;
        pid_t r;
        do { r = waitpid(proc->pid, &status, WNOHANG); }
        while (r < 0 && errno == EINTR);
        if (r == proc->pid) {
            proc->pid = -1;
            proc->state = QWRT_PROC_DEAD;
            return 0;
        }
        if (r < 0) break;
        if (qwrt_now_ms() >= deadline) break;
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 10 * 1000000 };
        nanosleep(&ts, NULL);
    }

    /* Tier 3: SIGKILL + reap */
    {
        int kr = kill(proc->pid, SIGKILL);
        if (kr == 0 || errno == ESRCH) proc_reap_blocking(proc->pid);
    }
    proc->pid = -1;
    proc->state = QWRT_PROC_DEAD;
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
static void qwrt_tx_flush(qwrt_tx_t *tx)
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

static void qwrt_tx_timer_cb(uv_timer_t *t)
{
    qwrt_tx_t *tx = (qwrt_tx_t *)t->data;
    qwrt_tx_flush(tx);
}

static int qwrt_tx_send(qwrt_tx_t *tx, const uint8_t *frame, size_t flen)
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
    qwrt_tx_flush(tx);
    if (tx->len > 0 && !tx->timer_active) {
        tx->timer.data = tx;
        if (uv_timer_start(&tx->timer, qwrt_tx_timer_cb, 1, 1) == 0)
            tx->timer_active = 1;
    }
    return 0;
}
/* Encode [4-byte LE len][envelope] and hand to the spill-buffer queue. */
static int proc_frame_send(qwrt_tx_t *tx,
                           int32_t source, int32_t target, int8_t kind,
                           const uint8_t *payload, uint32_t payload_len)
{
    size_t env_cap = IPC_ENVELOPE_ENCODED_SIZE(payload_len);
    uint8_t *env_buf = (uint8_t *)malloc(env_cap);
    if (!env_buf) return -1;
    size_t env_len = ipc_envelope_encode(env_buf, env_cap,
                                        source, target, kind,
                                        payload, payload_len);
    if (env_len == 0 || env_len > 0xFFFFFFFFu - 4u) {
        free(env_buf);
        return -1;
    }
    uint32_t flen = (uint32_t)env_len + 4u;
    uint8_t *frame = (uint8_t *)malloc(flen);
    if (!frame) { free(env_buf); return -1; }
    qwrt_wr32(frame, (uint32_t)env_len);
    memcpy(frame + 4, env_buf, env_len);
    free(env_buf);
    int rc = qwrt_tx_send(tx, frame, flen);
    free(frame);
    return rc;
}

int qwrt_proc_post(qwrt_proc_t *proc,
                   int32_t source, int32_t target, int8_t kind,
                   const uint8_t *payload, uint32_t payload_len)
{
    if (!proc || proc->state != QWRT_PROC_RUN) return -1;
    if (proc->tx.fd < 0) {
        uv_os_fd_t osfd;
        if (uv_fileno((uv_handle_t *)&proc->pipe, &osfd) < 0) return -1;
        proc->tx.fd = (int)(intptr_t)osfd;
    }
    return proc_frame_send(&proc->tx, source, target, kind,
                           payload, payload_len);
}

/* 宿主侧：M-P2 CONTROL 协议消息（source=宿主 0, target=主RT 1）。 */
int qwrt_proc_post_ctl(qwrt_proc_t *proc, const char *json)
{
    if (!json) return -1;
    return qwrt_proc_post(proc, QWRT_IPC_HOST_ID, QWRT_IPC_MAIN_ID,
                          IPC_ENV_KIND_CONTROL,
                          (const uint8_t *)json, (uint32_t)strlen(json));
}

/* ── Destroy & Free (libuv-idiomatic self-reclaim) ── */

/* proc 内嵌两个 handle（pipe + tx flush timer），qwrt_proc_free 对两者都
 * uv_close；close_pending 归零（最后一个 close 回调）才释放 proc 内存。
 * 若只关 pipe 就 free，tx timer 的 handle_queue 节点残留成悬垂 → uv_walk
 * （wait_idle 的 idle 检测）遍历到已释放内存 SIGSEGV —— PROCESS 后端高负载
 * 崩溃根因。 */
static void proc_reclaim(qwrt_proc_t *proc)
{
    if (proc && --proc->close_pending == 0) {
        free(proc->rbuf);
        free(proc);
    }
}

static void proc_on_closed(uv_handle_t *h)   /* pipe 的 close cb */
{
    proc_reclaim((qwrt_proc_t *)h->data);
}

static void proc_tx_timer_on_closed(uv_handle_t *h)  /* tx flush timer 的 close cb */
{
    /* timer.data 归 qwrt_tx_timer_cb 用（qwrt_tx_t*），不能复用——用
     * container_of 从内嵌地址回推 proc。 */
    proc_reclaim((qwrt_proc_t *)((char *)h - offsetof(qwrt_proc_t, tx.timer)));
}

void qwrt_proc_destroy(qwrt_proc_t *proc)
{
    if (!proc) return;
    if (proc->pid > 0) {
        int kr = kill(proc->pid, SIGKILL);
        if (kr == 0 || errno == ESRCH) proc_reap_blocking(proc->pid);
        proc->pid = -1;
    }
    proc->state = QWRT_PROC_DEAD;
}

void qwrt_proc_free(qwrt_proc_t *proc)
{
    if (!proc || proc->freed) return;   /* 幂等：防重复 free / 二次 uv_close */
    proc->freed = 1;
    qwrt_proc_destroy(proc);
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
static qwrt_tx_t g_child_tx;

void qwrt_ipc_child_set_channel(int fd)
{
    g_child_fd = fd;
}

int qwrt_ipc_child_channel(void)
{
    return g_child_fd;
}

void qwrt_ipc_child_tx_init(uv_loop_t *loop, int fd)
{
    g_child_fd = fd;
    memset(&g_child_tx, 0, sizeof(g_child_tx));
    g_child_tx.fd = fd;
    if (uv_timer_init(loop, &g_child_tx.timer) != 0)
        g_child_tx.fd = -1;
}

int qwrt_ipc_child_emit(int32_t source, int32_t target, int8_t kind,
                        const uint8_t *payload, uint32_t payload_len)
{
    if (g_child_tx.fd < 0) return -1;
    return proc_frame_send(&g_child_tx, source, target, kind,
                           payload, payload_len);
}

/* ── M-P4 同步 storage RPC（§10.2）：实现驻留 rt_main.c（独占子进程管道
 * 读状态），libqwrt 侧经函数指针调度——CLI/宿主进程不注册，调用即 -1
 * （worker 进程之外不可达，与 set_channel 同构）。 */
static qwrt_ipc_storage_sync_fn g_storage_sync = NULL;

void qwrt_ipc_child_set_storage_sync(qwrt_ipc_storage_sync_fn fn)
{
    g_storage_sync = fn;
}

int qwrt_ipc_child_storage_sync(qwrt_t *rt, const uint8_t *payload,
                                uint32_t payload_len,
                                uint8_t **out_reply, uint32_t *out_reply_len)
{
    if (!g_storage_sync) return -1;
    return g_storage_sync(rt, payload, payload_len, out_reply, out_reply_len);
}

/* 主RT 进程侧：M-P2 CONTROL 协议消息（source=主RT 1, target=宿主 0）。 */
int qwrt_ipc_child_emit_ctl(const char *json)
{
    if (!json) return -1;
    return qwrt_ipc_child_emit(QWRT_IPC_MAIN_ID, QWRT_IPC_HOST_ID,
                               IPC_ENV_KIND_CONTROL,
                               (const uint8_t *)json, (uint32_t)strlen(json));
}

/* ── M-P4 同步 storage RPC 用：出站缓冲的同步排空/查询 ──
 * child_storage_sync 在等待期间不跑 uv_run（flush timer 回调只在 loop 里
 * 触发），请求帧若落进 spill buffer 就必须当场排空；仍背压则快速失败
 * （等 timer 会死锁）。 */
void qwrt_ipc_child_tx_flush(void)
{
    qwrt_tx_flush(&g_child_tx);
}

int qwrt_ipc_child_tx_pending(void)
{
    return g_child_tx.len > 0;
}
/* ── M-P4 同步 storage RPC 用：阻塞整帧发送 ──
 * 异步 emit（qwrt_tx_send）把 EAGAIN 帧塞进 spill buffer，由 1ms timer 在
 * uv_run 里排空——但同步 RPC 的等待循环不跑 uv_run（否则 JS timer 重入），
 * 大 payload（quota 内可达 ~5MB，远超 socket 缓冲）会永久滞留在 buffer 里。
 * 本函数：先 poll(POLLOUT) 排空 spill buffer（保持 FIFO 顺序），再直接
 * poll+send 循环发完整帧，父进程死亡（POLLHUP/EOF）→ 返回 -1（§9.4 孤儿
 * 自杀路径由调用方触发）。 */
int qwrt_ipc_child_emit_sync(int32_t source, int32_t target, int8_t kind,
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
        qwrt_tx_flush(&g_child_tx);
        if (g_child_tx.len == 0) break;
    }

    size_t env_cap = IPC_ENVELOPE_ENCODED_SIZE(payload_len);
    uint8_t *env_buf = (uint8_t *)malloc(env_cap);
    if (!env_buf) return -1;
    size_t env_len = ipc_envelope_encode(env_buf, env_cap,
                                         source, target, kind,
                                         payload, payload_len);
    if (env_len == 0 || env_len > 0xFFFFFFFFu - 4u) {
        free(env_buf);
        return -1;
    }
    uint32_t flen = (uint32_t)env_len + 4u;
    uint8_t *frame = (uint8_t *)malloc(flen);
    if (!frame) { free(env_buf); return -1; }
    qwrt_wr32(frame, (uint32_t)env_len);
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

qwrt_proc_t *qwrt_proc_new(void)
{
    return (qwrt_proc_t *)calloc(1, sizeof(qwrt_proc_t));
}

/* ── Async read pump: inbound envelopes → parent msgq ── */

static void proc_alloc_cb(uv_handle_t *h, size_t suggested, uv_buf_t *buf)
{
    (void)h; (void)suggested;
    buf->base = (char *)malloc(QWRT_IPC_READ_BUF_SIZE);
    buf->len = buf->base ? QWRT_IPC_READ_BUF_SIZE : 0;
}

static void proc_peer_dead(qwrt_proc_t *proc)
{
    proc->state = QWRT_PROC_DEAD;
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
            proc->pid = -1;
        } else {
            proc->pid = -1;   /* ECHILD — 已被别处收割 */
        }
    }
    if (proc->msg_cb)
        proc->msg_cb(proc->msg_user, 0, 0, NULL, 0);   /* EOF 通知（JS-managed） */
}

static void proc_process_rx(qwrt_proc_t *proc)
{
    qwrt_t *parent = (qwrt_t *)proc->parent_rt;
    for (;;) {
        if (proc->frame_len == 0) {
            if (proc->rbuf_len < 4) return;
            proc->frame_len = qwrt_rd32(proc->rbuf);
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
                if (proc->msg_cb) {
                    /* JS-managed 模式：信封解码 → 直接回调（bridge.c 的
                     * pal.processOnMessage 消费者）。payload 指向 rbuf 内
                     * 部，回调返回后即失效，JS_Call 需同步复制成 ArrayBuffer。 */
                    proc->msg_cb(proc->msg_user, view.kind, view.source,
                                 view.payload, view.payload_len);
                } else {
                    int flags =
                        view.kind == IPC_ENV_KIND_CONTROL
                            ? QWRT_MSG_FLAG_CONTROL
                            : (view.kind == IPC_ENV_KIND_PORT_TRANSFER
                                   ? QWRT_MSG_FLAG_PORT_TRANSFER : 0);
                    if (parent && parent->magic == QWRT_MAGIC) {
                        qwrt_msg_push(parent, (const char *)view.payload,
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
    qwrt_proc_t *proc = (qwrt_proc_t *)s->data;
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

void qwrt_proc_start_read(qwrt_proc_t *proc)
{
    if (!proc || proc->state != QWRT_PROC_RUN) return;
    proc->pipe.data = proc;
    uv_read_start((uv_stream_t *)&proc->pipe, proc_alloc_cb, proc_read_cb);
}

void qwrt_proc_start_read_cb(qwrt_proc_t *proc, qwrt_proc_msg_cb_t cb,
                             void *user_data)
{
    if (!proc || proc->state != QWRT_PROC_RUN) return;
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
 * 替代已随 C 层进程 worker 分流移除的 qwrt_worker_is_proc_handle。 */
int qwrt_proc_handle_is_pipe(qwrt_t *rt, uv_handle_t *h)
{
    if (!rt || !h) return 0;
    if (rt->ipc_channel_pipe && (uv_handle_t *)rt->ipc_channel_pipe == h)
        return 1;
    for (int i = 0; i < QWRT_MAX_PROC_HANDLES; i++) {
        qwrt_proc_handle_t *ph = &rt->proc_handles[i];
        if (ph->live && ph->proc && (uv_handle_t *)&ph->proc->pipe == h)
            return 1;
    }
    return 0;
}

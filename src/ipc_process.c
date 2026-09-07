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

static int64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

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
        int64_t remaining = deadline_ms - now_ms();
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
    uint8_t hdr[4] = {
        (uint8_t)(len & 0xFF),
        (uint8_t)((len >> 8) & 0xFF),
        (uint8_t)((len >> 16) & 0xFF),
        (uint8_t)((len >> 24) & 0xFF),
    };
    if (write_all(fd, hdr, 4) < 0) return -1;
    if (len > 0 && write_all(fd, data, len) < 0) return -1;
    return 0;
}

int qwrt_ipc_read_frame(int fd, uint8_t **out_frame, size_t *out_len,
                        int64_t deadline_ms)
{
    uint8_t hdrbuf[4];
    if (read_all_deadline(fd, hdrbuf, 4, deadline_ms) < 0) return -1;
    uint32_t flen = ((uint32_t)hdrbuf[0]) |
                    ((uint32_t)hdrbuf[1] << 8) |
                    ((uint32_t)hdrbuf[2] << 16) |
                    ((uint32_t)hdrbuf[3] << 24);
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

static int json_find_int(const char *json, const char *key, int *out)
{
    char pattern[32];
    snprintf(pattern, sizeof pattern, "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return -1;
    p += strlen(pattern);
    while (*p && *p != ':') p++;
    if (*p != ':') return -1;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\n') p++;
    int neg = 0;
    if (*p == '-') { neg = 1; p++; }
    if (*p < '0' || *p > '9') return -1;
    int val = 0;
    while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; }
    *out = neg ? -val : val;
    return 0;
}

int qwrt_ipc_parse_handshake(const char *json, int *out_v,
                             int *out_role, int *out_id)
{
    if (json_find_int(json, "v", out_v) < 0) return -1;
    if (json_find_int(json, "role", out_role) < 0) return -1;
    if (json_find_int(json, "id", out_id) < 0) return -1;
    return 0;
}

int qwrt_ipc_parse_ack(const char *json, int *out_ok, int *out_v)
{
    if (json_find_int(json, "ok", out_ok) < 0) return -1;
    if (json_find_int(json, "v", out_v) < 0) return -1;
    return 0;
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
                    const char *binary_path,
                    int role, int id,
                    const char *script_path)
{
    if (!proc) return QWRT_ERR_INVALID_ARG;
    memset(proc, 0, sizeof(*proc));
    proc->pid = -1;
    proc->id = id;
    proc->role = role;
    proc->state = QWRT_PROC_BUILD;
    proc->parent_rt = parent;
    int kill_err = QWRT_ERR_GENERIC;   /* refined per failure cause below */


    int oom = 0;
    char *exe = resolve_binary(binary_path, &oom);
    if (!exe) return oom ? QWRT_ERR_NO_MEMORY : QWRT_ERR_NOT_FOUND;


    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        free(exe);
        return QWRT_ERR_IO;
    }

    /* Clear CLOEXEC on child end so it survives execv */
    int fd_flags = fcntl(sv[1], F_GETFD);
    if (fd_flags >= 0)
        fcntl(sv[1], F_SETFD, fd_flags & ~FD_CLOEXEC);

    pid_t pid = fork();
    if (pid < 0) {
        close(sv[0]); close(sv[1]);
        free(exe);
        return QWRT_ERR_IO;
    }

    if (pid == 0) {
        /* ── Child ── */
        close(sv[0]);
        char fd_str[16], id_str[16];
        snprintf(fd_str, sizeof fd_str, "%d", sv[1]);
        snprintf(id_str, sizeof id_str, "%d", id);

        char *argv[10];
        int ai = 0;
        argv[ai++] = exe;
        argv[ai++] = (char *)"--qwrt-worker";
        argv[ai++] = (char *)"--parent-fd";
        argv[ai++] = fd_str;
        argv[ai++] = (char *)"--worker-id";
        argv[ai++] = id_str;
        if (script_path) {
            argv[ai++] = (char *)"--script";
            argv[ai++] = (char *)script_path;
        }
        argv[ai] = NULL;

        execv(exe, argv);
        _exit(127);
    }

    /* ── Parent ── */
    free(exe);
    close(sv[1]);
    proc->pid = pid;

    /* uv_pipe_open the parent end */
    uv_loop_t *loop = parent ? &parent->loop : uv_default_loop();
    if (uv_pipe_init(loop, &proc->pipe, 0) != 0) {
        close(sv[0]);
        goto kill_fail;
    }
    proc->pipe_inited = 1;   /* init 过即标记：free 时须 uv_close 该 handle */
    if (uv_pipe_open(&proc->pipe, sv[0]) != 0) {
        close(sv[0]);
        goto kill_fail;
    }

    /* ── Handshake (§3.3): child sends first, parent validates, replies ack ── */
    {
        uv_os_fd_t osfd;
        int pfd;
        if (uv_fileno((uv_handle_t *)&proc->pipe, &osfd) == 0)
            pfd = (int)(intptr_t)osfd;
        else {
            pfd = sv[0];
        }

        /* Read child's handshake (5s deadline) */
        int64_t deadline = now_ms() + QWRT_IPC_HANDSHAKE_TIMEOUT_MS;
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
 * Revisit in M-P4 if a 2s worst-case freeze is unacceptable for a host. */

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
    int64_t deadline = now_ms() + timeout_ms;
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
        if (now_ms() >= deadline) break;
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

/* ── Post ── */

int qwrt_proc_post(qwrt_proc_t *proc,
                   int32_t source, int32_t target, int8_t kind,
                   const uint8_t *payload, uint32_t payload_len)
{
    if (!proc || proc->state != QWRT_PROC_RUN) return -1;
    uv_os_fd_t osfd;
    if (uv_fileno((uv_handle_t *)&proc->pipe, &osfd) < 0) return -1;
    int pfd = (int)(intptr_t)osfd;

    size_t env_cap = IPC_ENVELOPE_ENCODED_SIZE(payload_len);
    uint8_t *env_buf = (uint8_t *)malloc(env_cap);
    if (!env_buf) return -1;
    size_t env_len = ipc_envelope_encode(env_buf, env_cap,
                                        source, target, kind,
                                        payload, payload_len);
    if (env_len == 0) { free(env_buf); return -1; }
    int rc = qwrt_ipc_write_frame(pfd, env_buf, env_len);
    free(env_buf);
    return rc;
}

/* ── Destroy & Free (libuv-idiomatic self-reclaim) ── */

static void proc_on_closed(uv_handle_t *h)
{
    qwrt_proc_t *proc = (qwrt_proc_t *)h->data;
    if (proc) {
        free(proc->rbuf);
        free(proc);
    }
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
    if (!proc) return;
    qwrt_proc_destroy(proc);
    if (proc->pipe_inited) {
        uv_read_stop((uv_stream_t *)&proc->pipe);
        proc->pipe.data = proc;
        proc->pipe_inited = 0;
        /* libuv pattern: handle memory (proc) is freed in close_cb AFTER
         * uv__finish_close has unlinked the handle from the loop entirely. */
        uv_close((uv_handle_t *)&proc->pipe, proc_on_closed);
    } else {
        free(proc->rbuf);
        free(proc);
    }
}

/* ── Child-side emit channel (single per process) ── */

static int g_child_fd = -1;

void qwrt_ipc_child_set_channel(int fd)
{
    g_child_fd = fd;
}

int qwrt_ipc_child_channel(void)
{
    return g_child_fd;
}

int qwrt_ipc_child_emit(int32_t source, int32_t target, int8_t kind,
                        const uint8_t *payload, uint32_t payload_len)
{
    if (g_child_fd < 0) return -1;
    size_t env_cap = IPC_ENVELOPE_ENCODED_SIZE(payload_len);
    uint8_t *env_buf = (uint8_t *)malloc(env_cap);
    if (!env_buf) return -1;
    size_t env_len = ipc_envelope_encode(env_buf, env_cap,
                                        source, target, kind,
                                        payload, payload_len);
    if (env_len == 0) { free(env_buf); return -1; }
    int rc = qwrt_ipc_write_frame(g_child_fd, env_buf, env_len);
    free(env_buf);
    return rc;
}

/* ── Opaque lifecycle ── */

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

/* Extract complete frames from the accumulator; decode; push into the parent
 * runtime's msgq (source = child slot id, flags = CONTROL?1:0) and wake the
 * parent loop. Zero-copy inside frames: payload slice decoded, then one copy
 * into the msgq node (same copy count as the thread backend's push). */
/* Peer death (channel EOF / oversized-frame protocol error / rx realloc OOM):
 * reap the exited child so it never becomes a zombie, and release the parent's
 * worker slot so the id is reusable — without the slot release, 16 crashed
 * workers permanently exhaust QWRT_MAX_WORKERS (I1). The error-event dispatch
 * into main-runtime JS is deferred to M-P4.
 *
 * Safe to call from inside the pipe read callback: the proc struct is freed
 * asynchronously via uv_close(proc_on_closed), so the caller's `proc` pointer
 * stays valid until it returns. */
static void proc_peer_dead(qwrt_proc_t *proc)
{
    proc->state = QWRT_PROC_DEAD;
    if (proc->pid > 0) {
        int status;
        pid_t r;
        do { r = waitpid(proc->pid, &status, WNOHANG); }
        while (r < 0 && errno == EINTR);
        if (r == proc->pid) proc->pid = -1;
        /* r == 0: child closed the channel but has not become a zombie yet.
         * Leave pid set — the slot release below runs qwrt_proc_destroy, which
         * SIGKILL+reaps as a backstop, so no zombie is ever orphaned. */
    }
    qwrt_t *parent = (qwrt_t *)proc->parent_rt;
    if (parent && parent->magic == QWRT_MAGIC && proc->id >= 1)
        qwrt_worker_reap(parent, proc->id);
}

static void proc_process_rx(qwrt_proc_t *proc)
{
    qwrt_t *parent = (qwrt_t *)proc->parent_rt;
    for (;;) {
        if (proc->frame_len == 0) {
            if (proc->rbuf_len < 4) return;
            proc->frame_len = ((uint32_t)proc->rbuf[0]) |
                              ((uint32_t)proc->rbuf[1] << 8) |
                              ((uint32_t)proc->rbuf[2] << 16) |
                              ((uint32_t)proc->rbuf[3] << 24);
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
                int flags = (view.kind == IPC_ENV_KIND_CONTROL) ? 1 : 0;
                if (parent && parent->magic == QWRT_MAGIC) {
                    qwrt_msg_push(parent, (const char *)view.payload,
                                  view.payload_len, proc->id, flags);
                    uv_async_send(&parent->wake);
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
    free(buf->base);
    if (!proc) return;

    if (nread < 0) {
        /* EOF → peer dead (§9.3/§9.4): reap + release slot. JS error event is
         * M-P4. */
        proc_peer_dead(proc);
        return;
    }
    if (nread == 0) return;   /* EAGAIN */

    size_t need = proc->rbuf_len + (size_t)nread;
    if (need > proc->rbuf_cap) {
        size_t ncap = proc->rbuf_cap ? proc->rbuf_cap : 4096;
        while (ncap < need) ncap *= 2;
        uint8_t *nb = (uint8_t *)realloc(proc->rbuf, ncap);
        if (!nb) {
            proc_peer_dead(proc);
            return;
        }
        proc->rbuf = nb;
        proc->rbuf_cap = ncap;
    }
    memcpy(proc->rbuf + proc->rbuf_len, buf->base, (size_t)nread);
    proc->rbuf_len += (size_t)nread;
    proc_process_rx(proc);
}

void qwrt_proc_start_read(qwrt_proc_t *proc)
{
    if (!proc || proc->state != QWRT_PROC_RUN) return;
    proc->pipe.data = proc;
    uv_read_start((uv_stream_t *)&proc->pipe, proc_alloc_cb, proc_read_cb);
}

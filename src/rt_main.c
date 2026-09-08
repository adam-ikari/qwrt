/*
 * qwrt-rt — standalone worker process binary (M-P1)
 *
 * Entry point for a child process spawned by qwrt_proc_spawn.
 * argv: --qwrt-worker --parent-fd N --worker-id K [--script PATH]
 *
 * Lifecycle:
 *   1. Parse argv
 *   2. Handshake on raw parent-fd (child sends first, waits for ack, 5s)
 *   3. Init qwrt_t (loop + wake + runtime + polyfill)
 *   4. uv_pipe_open(parent-fd) + uv_read_start (frame accumulator → msgq)
 *   5. Eval worker boot bytecode + worker script
 *   6. Main loop (uv_run(ONCE) + flush microtasks, same as
 *      qwrt_worker_thread_main)
 *   7. EOF on parent-fd → shutting_down → teardown → exit(0)

 *
 * Design: docs/plans/2026-09-04-multi-process-model.md §5, §6.2, §9.4, M-P1.
 */

#include "qwrt_internal.h"
#include "ipc_process.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>
#include <poll.h>

/* ── Pipe read state (one pipe per child process — static is fine) ── */

static uv_pipe_t g_parent_pipe;

static struct {
    uint8_t *buf;       /* accumulation buffer */
    size_t   cap;
    size_t   len;       /* bytes accumulated so far */
    uint32_t frame_len; /* expected frame body length (0 = need 4-byte hdr) */
} g_rx;

static int64_t now_ms_local(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
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


/* ── Wake callback: drain msgq and dispatch (same as qwrt_worker_wake_cb) ── */

static void child_wake_cb(uv_async_t *a)
{
    qwrt_t *rt = (qwrt_t *)a->data;
    if (__atomic_load_n(&rt->shutting_down, __ATOMIC_ACQUIRE)) return;
    qwrt_msg_t *m;
    while ((m = qwrt_msg_pop(rt)) != NULL) {
        /* flags=1 → control dispatch; flags=0 → worker message dispatch */
        if (m->flags)
            qwrt_control_dispatch(rt, m);
        else
            qwrt_worker_dispatch(rt, m);
    }
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
                /* CONTROL{shutdown} → graceful exit (§9.2 tier 1). */
                if (view.kind == IPC_ENV_KIND_CONTROL &&
                    view.payload_len > 0 &&
                    has_substring(view.payload, view.payload_len,
                                  "shutdown")) {

                    __atomic_store_n(&rt->shutting_down, 1, __ATOMIC_RELEASE);
                    uv_async_send(&rt->wake);
                } else {
                    int flags = (view.kind == IPC_ENV_KIND_CONTROL) ? 1 : 0;
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

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--qwrt-worker") == 0) {
            is_worker = 1;
        } else if (strcmp(argv[i], "--parent-fd") == 0 && i + 1 < argc) {
            parent_fd = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--worker-id") == 0 && i + 1 < argc) {
            worker_id = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--script") == 0 && i + 1 < argc) {
            script_path = argv[++i];
        }
    }

    if (!is_worker || parent_fd < 0) {
        fprintf(stderr, "qwrt-rt: usage: --qwrt-worker --parent-fd N --worker-id K [--script PATH]\n");
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
        size_t hs_len = qwrt_ipc_build_handshake(hs_json, sizeof hs_json,
                                                  QWRT_IPC_ROLE_WORKER,
                                                  worker_id);
        if (hs_len == 0) {
            fprintf(stderr, "qwrt-rt: handshake build failed\n");
            free(script);
            return 1;
        }
        size_t env_cap = IPC_ENVELOPE_ENCODED_SIZE(hs_len);
        uint8_t *env_buf = (uint8_t *)malloc(env_cap);
        if (!env_buf) { free(script); return 1; }
        size_t env_len = ipc_envelope_encode(env_buf, env_cap,
                                            (int32_t)worker_id, 1,
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
        int64_t deadline = now_ms_local() + QWRT_IPC_HANDSHAKE_TIMEOUT_MS;
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

    /* Mark as worker runtime (bridge.c pal binding) */
    qwrt_worker_t *w = (qwrt_worker_t *)calloc(1, sizeof(qwrt_worker_t));
    if (!w) { free(rt); free(script); return 1; }
    w->self = rt;
    w->id = worker_id;
    w->parent = NULL;  /* no in-process parent — IPC pipe is the channel */
    rt->worker_self = w;

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

    /* ── Main loop (mirrors qwrt_worker_thread_main) ── */
    while (!__atomic_load_n(&rt->shutting_down, __ATOMIC_ACQUIRE) &&
           !__atomic_load_n(&w->shutting_down, __ATOMIC_ACQUIRE)) {
        uv_run(&rt->loop, UV_RUN_ONCE);
        if (__atomic_load_n(&rt->shutting_down, __ATOMIC_ACQUIRE) ||
            __atomic_load_n(&w->shutting_down, __ATOMIC_ACQUIRE)) break;
        qwrt_flush_microtasks(rt);
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

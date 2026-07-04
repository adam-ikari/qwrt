#ifndef QWRT_H
#define QWRT_H

#include <stdint.h>
#include <stddef.h>

/* ================================================================
 * PAL callback type
 * ================================================================ */

typedef void (*qwrt_pal_cb_t)(void *user_data, int status, const char *data, size_t data_len);

/* ================================================================
 * Streaming PAL callback types
 * ================================================================ */

typedef struct qwrt_pal_stream_ops {
    void (*on_headers)(void *user_data, int status, const char *headers_json);
    void (*on_data)(void *user_data, const char *data, size_t len);
    void (*on_end)(void *user_data, int error_status);
    void *user_data;
} qwrt_pal_stream_ops_t;

/* ================================================================
 * PAL interface (qwrt_pal_t)
 * ================================================================ */

typedef struct qwrt_pal_t qwrt_pal_t;

struct qwrt_pal_t {
    void *user_data;

    /* async operations */
    void (*http_request)(qwrt_pal_t *pal,
                         const char *url, const char *method,
                         const char *headers, const char *body,
                         size_t body_len,
                         qwrt_pal_cb_t cb, void *cb_data);

    void (*http_request_stream)(qwrt_pal_t *pal,
                                const char *url, const char *method,
                                const char *headers, const char *body,
                                size_t body_len,
                                qwrt_pal_stream_ops_t *ops);

    /*
     * Abort the currently-active streaming HTTP request (if any).
     *
     * Closes the in-flight TCP connection and any associated timers,
     * delivering an error/abort to the stream's on_end callback so the
     * caller's fetch Promise rejects promptly. Safe to call when no
     * stream is active (no-op). Used to implement ace_cancel() aborting
     * an in-flight LLM stream.
     *
     * OPTIONAL: platforms without streaming HTTP may leave this NULL;
     * ace_cancel() then only sets its cancellation flag (the next poll
     * iteration observes it) without forcing the underlying request down.
     */
    void (*http_abort)(qwrt_pal_t *pal);

    void (*fs_read)(qwrt_pal_t *pal, const char *path,
                    qwrt_pal_cb_t cb, void *cb_data);
    void (*fs_write)(qwrt_pal_t *pal, const char *path,
                     const char *data, size_t data_len,
                     qwrt_pal_cb_t cb, void *cb_data);
    void (*fs_exists)(qwrt_pal_t *pal, const char *path,
                      qwrt_pal_cb_t cb, void *cb_data);
    void (*fs_remove)(qwrt_pal_t *pal, const char *path,
                      qwrt_pal_cb_t cb, void *cb_data);
    void (*fs_list)(qwrt_pal_t *pal, const char *path,
                    qwrt_pal_cb_t cb, void *cb_data);

    void (*storage_get)(qwrt_pal_t *pal, const char *key,
                       qwrt_pal_cb_t cb, void *cb_data);
    void (*storage_set)(qwrt_pal_t *pal, const char *key,
                       const char *value, size_t value_len,
                       qwrt_pal_cb_t cb, void *cb_data);
    void (*storage_del)(qwrt_pal_t *pal, const char *key,
                       qwrt_pal_cb_t cb, void *cb_data);

    void *(*timer_start)(qwrt_pal_t *pal, uint64_t delay_ms, int repeat,
                          qwrt_pal_cb_t cb, void *cb_data);
    void (*timer_stop)(qwrt_pal_t *pal, void *handle);

    uint64_t (*time_now)(qwrt_pal_t *pal);
    uint64_t (*hrtime)(qwrt_pal_t *pal); /* nanoseconds since arbitrary epoch */
    void (*log)(qwrt_pal_t *pal, int level, const char *msg);
    void *(*mem_alloc)(qwrt_pal_t *pal, size_t size);
    void (*mem_free)(qwrt_pal_t *pal, void *ptr);
    void (*random_bytes)(qwrt_pal_t *pal, uint8_t *buf, size_t len);

    /*
     * Drive one iteration of the PAL's event loop, blocking up to timeout_ms:
     *   timeout_ms < 0  block until an event arrives
     *   timeout_ms == 0 non-blocking (process only already-ready work)
     *   timeout_ms > 0  block up to timeout_ms for the next event
     * Processes pending I/O completions, timers, and deferred callbacks.
     * Returns the number of events processed, 0 if none/timeout elapsed,
     * or -1 if the loop was asked to stop (host should exit its poll loop).
     *
     * OPTIONAL: NULL means the PAL exposes no event loop to drive (the
     * embedder is responsible for pumping qwrt_tick another way). ace_poll
     * treats NULL as "no PAL work" and just sleeps between ticks.
     *
     * This decouples hosts (e.g. ace-core) from a specific loop library
     * (libuv / FreeRTOS): pal_uv wraps uv_run, pal_freertos wraps its
     * event-group wait + deferred queue drain.
     */
    int (*run_cycle)(qwrt_pal_t *pal, int timeout_ms);

    /* ── Process management ────────────────────────────────────── */
    /*
     * Platform-agnostic process primitives for multi-agent isolation.
     *
     *   spawn(cmd, args, env)  → channel  (create child process)
     *   channel_send(ch, data)           (write to child's stdin)
     *   channel_recv(ch, cb)             (read from child's stdout)
     *   channel_close(ch)                (close one direction)
     *   join(proc, timeout_ms) → status  (wait for child to exit)
     *   terminate(proc)                  (force-kill child)
     *
     * POSIX:   spawn = fork+exec,  channel = pipe
     * Windows: spawn = CreateProcess, channel = NamedPipe
     * WASM:    spawn = Worker,     channel = postMessage
     *
     * These are OPTIONAL — set to NULL if the platform doesn't support
     * multi-process isolation (e.g. embedded MCU).
     */

    /* Spawn a child process. Returns opaque handle, NULL on failure.
     * cmd:  executable path
     * args: NULL-terminated argument array (args[0] = cmd by convention)
     * env:  NULL-terminated environment array ("KEY=VALUE"), or NULL
     * Returns an opaque process handle. */
    void *(*spawn)(qwrt_pal_t *pal,
                   const char *cmd,
                   const char *const *args,
                   const char *const *env);

    /* ── Channel (IPC pipe to child) ───────────────────────────── */

    /* Get the send channel to the child's stdin.
     * Returns an opaque channel handle, or NULL if not available. */
    void *(*spawn_get_stdin)(qwrt_pal_t *pal, void *proc);

    /* Get the receive channel from the child's stdout.
     * Returns an opaque channel handle, or NULL if not available. */
    void *(*spawn_get_stdout)(qwrt_pal_t *pal, void *proc);

    /* Send data to child. Returns bytes written, -1 on error. */
    int (*channel_send)(qwrt_pal_t *pal, void *ch,
                        const char *data, size_t len);

    /* Receive data from child (async). cb fires with data or empty on EOF. */
    void (*channel_recv)(qwrt_pal_t *pal, void *ch,
                         qwrt_pal_cb_t cb, void *cb_data);

    /* Close channel. */
    void (*channel_close)(qwrt_pal_t *pal, void *ch);

    /* ── Process lifecycle ─────────────────────────────────────── */

    /* Wait for child to exit. timeout_ms: -1 = block forever, 0 = poll.
     * Returns exit code, or -1 if still running. */
    int (*join)(qwrt_pal_t *pal, void *proc, int timeout_ms);

    /* Force-terminate child process. */
    void (*terminate)(qwrt_pal_t *pal, void *proc);
};

/* ================================================================
 * Extension interface
 * ================================================================ */

/* Forward declarations — qwrt_t must be declared before qwrt_ext_t
 * because the extension hooks reference qwrt_t in their signatures. */
typedef struct qwrt_t qwrt_t;
typedef struct qwrt_ext_t qwrt_ext_t;

struct qwrt_ext_t {
    const char *name;
    int version;
    int (*init)(qwrt_ext_t *ext, qwrt_t *rt);
    void (*destroy)(qwrt_ext_t *ext, qwrt_t *rt);
    int (*suspend)(qwrt_ext_t *ext, qwrt_t *rt);
    int (*resume)(qwrt_ext_t *ext, qwrt_t *rt);
    void *user_data;
};

/* ================================================================
 * qwrt configuration and runtime types
 * ================================================================ */

typedef struct qwrt_config_t {
    const qwrt_pal_t *pal;
    int debug;
    const qwrt_ext_t **extensions;  /* NULL-terminated array, or NULL */
} qwrt_config_t;

/* qwrt_t forward declaration already provided above */

/* Forward declaration for JSContext */
struct JSContext;

/* ================================================================
 * Core API
 * ================================================================ */

qwrt_t *qwrt_create(const qwrt_config_t *config);
void qwrt_destroy(qwrt_t *rt);
int qwrt_tick(qwrt_t *rt);
int qwrt_eval(qwrt_t *rt, const char *code, char **result);
int qwrt_eval_bytecode(qwrt_t *rt, const uint8_t *bytecode, size_t len,
                       char **result);
int qwrt_call(qwrt_t *rt, const char *func,
              const char *args_json, char **result);
void qwrt_free(void *ptr);

/* ================================================================
 * Multi-context API
 * ================================================================ */

int qwrt_reset(qwrt_t *rt, const qwrt_config_t *config);
int qwrt_spawn(qwrt_t *rt, const qwrt_config_t *config);
int qwrt_suspend(qwrt_t *rt);
int qwrt_resume(qwrt_t *rt, int context_id);
int qwrt_destroy_ctx(qwrt_t *rt, int context_id);
int qwrt_get_active_ctx_id(qwrt_t *rt);
struct JSContext *qwrt_get_jsctx(qwrt_t *rt);

/* Register an extension on the active context at runtime.
 * Calls ext->init immediately. Returns 0 on success, -1 on failure. */
int qwrt_register_ext(qwrt_t *rt, const qwrt_ext_t *ext);

/* ================================================================
 * Bytecode compilation API
 * ================================================================ */

/* Compile JS source code to QuickJS bytecode.
 * Returns allocated buffer (free with qwrt_free), writes length to *out_len.
 * Returns NULL on error. */
uint8_t *qwrt_compile(qwrt_t *rt, const char *code, size_t code_len,
                      size_t *out_len);

/* Compile JS source as an ES module to bytecode.
 * Same as qwrt_compile but treats the source as an ES module. */
uint8_t *qwrt_compile_module(qwrt_t *rt, const char *code, size_t code_len,
                             size_t *out_len);

#endif /* QWRT_H */

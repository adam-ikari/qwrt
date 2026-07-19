#ifndef QWRT_H
#define QWRT_H

#include <stdint.h>
#include <stddef.h>

/* ================================================================
 * PAL error codes (qwrt_pal_err_t)
 *
 * Standardized return values for all PAL callbacks and synchronous
 * methods.  Every PAL implementation MUST return these values so
 * callers can branch on named constants instead of magic numbers.
 *
 * The integer values are negative so they are distinguishable from
 * byte counts / event counts (which are >=0 on success).
 * ================================================================ */

typedef enum {
    QWRT_OK                 =  0,   /* success */
    QWRT_ERR_GENERIC        = -1,   /* unknown / unspecified error */
    QWRT_ERR_NOT_FOUND      = -2,   /* file, key, or resource not found */
    QWRT_ERR_IO             = -3,   /* read / write / close failure */
    QWRT_ERR_PERMISSION     = -4,   /* access denied (EACCES, etc.) */
    QWRT_ERR_NETWORK        = -5,   /* DNS, connect, or TLS failure */
    QWRT_ERR_INVALID_ARG    = -6,   /* bad parameter (NULL where required, etc.) */
    QWRT_ERR_CANCELLED      = -7,   /* operation cancelled by caller */
    QWRT_ERR_BUSY            = -8,   /* resource temporarily unavailable */
    QWRT_ERR_NOT_SUPPORTED   = -9,   /* operation not implemented by this PAL */
    QWRT_ERR_TIMEOUT         = -10,  /* operation timed out */
    QWRT_ERR_NO_MEMORY       = -11,  /* allocation failed */
} qwrt_pal_err_t;

/* ================================================================
 * PAL callback type
 *
 * @param user_data  opaque pointer passed through from the call site
 * @param status     a qwrt_pal_err_t value (0 = success, <0 = error)
 * @param data       result payload (JSON string, file content, …)
 * @param data_len   length of data in bytes (0 if data is NULL)
 * ================================================================ */

typedef void (*qwrt_pal_cb_t)(void *user_data, int status,
                               const char *data, size_t data_len);

/* ================================================================
 * Streaming PAL callback types
 *
 * Streaming HTTP responses are delivered through three callbacks
 * instead of a single final callback.  The sequence is:
 *   1. on_headers — HTTP status + headers (once, may be the only call
 *      on error)
 *   2. on_data    — body chunk (zero or more times)
 *   3. on_end     — stream finished or aborted (exactly once)
 *
 * @param on_headers  called when status line + headers are parsed;
 *                    status is the HTTP status code (200, 404, …)
 * @param on_data     called for each body chunk; len may be 0
 * @param on_end      called when the stream ends; error_status is
 *                    QWRT_OK on clean close or QWRT_ERR_* on abort/error
 * @param user_data   opaque pointer for the embedder
 * ================================================================ */

typedef struct qwrt_pal_stream_ops {
    void (*on_headers)(void *user_data, int status, const char *headers_json);
    void (*on_data)(void *user_data, const char *data, size_t len);
    void (*on_end)(void *user_data, int error_status);
    void *user_data;
} qwrt_pal_stream_ops_t;

/* ================================================================
 * PAL interface (qwrt_pal_t)
 *
 * A vtable of function pointers that every PAL backend must fill in.
 * PAL implementations embed this struct as their first or near-first
 * member and recover their private state from the `pal` pointer.
 *
 * Lifetime:
 *   The embedder creates a PAL, fills in the function pointers, and
 *   passes it to qwrt_create().  The PAL must outlive the qwrt_t.
 *   When init/destroy hooks are provided, qwrt_create calls init()
 *   and qwrt_destroy calls destroy().
 *
 * Fields are grouped by category; optional methods may be NULL.
 * ================================================================ */

typedef struct qwrt_pal_t qwrt_pal_t;

struct qwrt_pal_t {
    /* ── Identity & lifecycle ──────────────────────────────────── */

    /**
     * Opaque pointer for the PAL implementation's private state.
     * Set by the PAL constructor, never touched by qwrt core.
     */
    void *user_data;

    /**
     * PAL interface version.  MUST be 1 for the current ABI.
     * Future additions to qwrt_pal_t will bump this so embedders
     * can check compatibility at runtime.  Old PALs that leave this
     * at 0 are treated as "pre-versioning" and still work.
     */
    uint32_t version;

    /**
     * Human-readable PAL name for diagnostics (e.g. "libuv", "mock",
     * "freertos").  Must be a static string literal or otherwise
     * outlive the PAL struct.  May be NULL.
     */
    const char *name;

    /* ── Core I/O ──────────────────────────────────────────────── */

    /**
     * Perform an HTTP request and deliver the full response body.
     *
     * @param pal       this PAL
     * @param url       full URL (http:// or https://)
     * @param method    HTTP method ("GET", "POST", …)
     * @param headers   JSON object string of headers, or NULL
     * @param body      request body, or NULL
     * @param body_len  request body length in bytes
     * @param cb        callback receiving QWRT_OK + JSON response on
     *                  success, or QWRT_ERR_* on failure
     * @param cb_data   opaque user data forwarded to cb
     *
     * The JSON delivered to cb has the form:
     *   {"status":NNN,"headers":{...},"body":"..."}
     */
    void (*http_request)(qwrt_pal_t *pal,
                         const char *url, const char *method,
                         const char *headers, const char *body,
                         size_t body_len,
                         qwrt_pal_cb_t cb, void *cb_data);

    /**
     * Perform a streaming HTTP request.
     *
     * Same parameters as http_request, but the response is delivered
     * through qwrt_pal_stream_ops callbacks instead of a single final
     * callback.  Use this for large / long-lived responses (SSE, LLM
     * streaming).
     *
     * OPTIONAL: set to NULL if the PAL does not support streaming.
     */
    void (*http_request_stream)(qwrt_pal_t *pal,
                                const char *url, const char *method,
                                const char *headers, const char *body,
                                size_t body_len,
                                qwrt_pal_stream_ops_t *ops);

    /**
     * Abort the currently-active streaming HTTP request (if any).
     *
     * Closes the in-flight TCP connection and any associated timers,
     * delivering QWRT_ERR_CANCELLED to the stream's on_end callback.
     * Safe to call when no stream is active (no-op).
     *
     * OPTIONAL: platforms without streaming HTTP may leave this NULL;
     * the caller then only sets its cancellation flag without forcing
     * the underlying request down.
     */
    void (*http_abort)(qwrt_pal_t *pal);

    /* ── Filesystem ────────────────────────────────────────────── */

    /**
     * Read the full contents of a file.
     * cb receives QWRT_OK + file content, or QWRT_ERR_* on failure.
     */
    void (*fs_read)(qwrt_pal_t *pal, const char *path,
                    qwrt_pal_cb_t cb, void *cb_data);

    /**
     * Write data to a file (creates or overwrites).
     * cb receives QWRT_OK or QWRT_ERR_*.
     */
    void (*fs_write)(qwrt_pal_t *pal, const char *path,
                     const char *data, size_t data_len,
                     qwrt_pal_cb_t cb, void *cb_data);

    /**
     * Check whether a file exists.
     * cb receives QWRT_OK (exists) or QWRT_ERR_NOT_FOUND (doesn't).
     */
    void (*fs_exists)(qwrt_pal_t *pal, const char *path,
                      qwrt_pal_cb_t cb, void *cb_data);

    /**
     * Delete a file.
     * cb receives QWRT_OK or QWRT_ERR_*.
     */
    void (*fs_remove)(qwrt_pal_t *pal, const char *path,
                      qwrt_pal_cb_t cb, void *cb_data);

    /**
     * List entries in a directory.
     * cb receives QWRT_OK + JSON array of entry names, or QWRT_ERR_*.
     */
    void (*fs_list)(qwrt_pal_t *pal, const char *path,
                    qwrt_pal_cb_t cb, void *cb_data);

    /* ── Key-value storage ─────────────────────────────────────── */

    /**
     * Get a value by key.
     * cb receives QWRT_OK + value, or QWRT_ERR_NOT_FOUND.
     */
    void (*storage_get)(qwrt_pal_t *pal, const char *key,
                       qwrt_pal_cb_t cb, void *cb_data);

    /**
     * Set a key-value pair (creates or overwrites).
     * cb receives QWRT_OK or QWRT_ERR_*.
     */
    void (*storage_set)(qwrt_pal_t *pal, const char *key,
                       const char *value, size_t value_len,
                       qwrt_pal_cb_t cb, void *cb_data);

    /**
     * Delete a key.
     * cb receives QWRT_OK or QWRT_ERR_* (QWRT_ERR_NOT_FOUND if absent).
     */
    void (*storage_del)(qwrt_pal_t *pal, const char *key,
                       qwrt_pal_cb_t cb, void *cb_data);

    /* ── Timers ────────────────────────────────────────────────── */

    /**
     * Start a timer.
     *
     * @param delay_ms  delay before first (or only) fire
     * @param repeat    if non-zero, the timer fires repeatedly
     * @param cb        called with QWRT_OK on each fire
     * @param cb_data   opaque user data
     * @return          opaque timer handle for timer_stop(), or NULL
     *                  on QWRT_ERR_NO_MEMORY
     */
    void *(*timer_start)(qwrt_pal_t *pal, uint64_t delay_ms, int repeat,
                          qwrt_pal_cb_t cb, void *cb_data);

    /**
     * Stop and free a timer.  Safe to call with NULL (no-op).
     */
    void (*timer_stop)(qwrt_pal_t *pal, void *handle);

    /* ── Time & entropy ────────────────────────────────────────── */

    /**
     * Current wall-clock time in milliseconds since Unix epoch.
     */
    uint64_t (*time_now)(qwrt_pal_t *pal);

    /**
     * High-resolution monotonic time in nanoseconds since an
     * arbitrary (but fixed) epoch.  Used for performance timing.
     */
    uint64_t (*hrtime)(qwrt_pal_t *pal);

    /* ── Logging & memory ──────────────────────────────────────── */

    /**
     * Emit a log message at the given level.
     * Level semantics follow syslog: 0=EMERG … 7=DEBUG.
     * OPTIONAL: may be NULL (logs are silently dropped).
     */
    void (*log)(qwrt_pal_t *pal, int level, const char *msg);

    /**
     * Allocate memory.  If NULL, malloc() is used by the core.
     */
    void *(*mem_alloc)(qwrt_pal_t *pal, size_t size);

    /**
     * Free memory allocated by mem_alloc.  If NULL, free() is used.
     */
    void (*mem_free)(qwrt_pal_t *pal, void *ptr);

    /**
     * Fill buf[0..len-1] with cryptographically-secure random bytes.
     *
     * Synchronous by design — hardware RNGs and /dev/urandom are
     * fast enough that an async API adds complexity without benefit.
     * If a future platform truly needs async entropy, this signature
     * can be changed to accept a callback.
     */
    void (*random_bytes)(qwrt_pal_t *pal, uint8_t *buf, size_t len);

    /* ── Event loop ────────────────────────────────────────────── */

    /**
     * Drive one iteration of the PAL's event loop, blocking up to
     * timeout_ms:
     *   timeout_ms < 0   block until an event arrives
     *   timeout_ms == 0  non-blocking (process ready work only)
     *   timeout_ms > 0   block up to timeout_ms milliseconds
     *
     * Returns the number of events processed, 0 if the timeout
     * elapsed with no events, or QWRT_ERR_GENERIC if the loop was
     * asked to stop (host should exit its poll loop).
     *
     * OPTIONAL: NULL means the PAL exposes no event loop to drive
     * (the embedder pumps qwrt_tick directly).  pal_uv wraps uv_run,
     * pal_freertos wraps its event-group wait + deferred queue drain.
     */
    int (*run_cycle)(qwrt_pal_t *pal, int timeout_ms);

    /* ── Process management ────────────────────────────────────── */

    /**
     * Spawn a child process.
     *
     * @param cmd   executable path
     * @param args  NULL-terminated argument array (args[0] = cmd by
     *              convention)
     * @param env   NULL-terminated environment array ("KEY=VALUE"),
     *              or NULL to inherit
     * @return      opaque process handle, or NULL on failure
     *
     * OPTIONAL: set to NULL if the platform doesn't support
     * multi-process isolation (e.g. embedded MCU).
     */
    void *(*spawn)(qwrt_pal_t *pal,
                   const char *cmd,
                   const char *const *args,
                   const char *const *env);

    /* ── Channel (IPC pipe to child) ───────────────────────────── */

    /**
     * Get the send channel to the child's stdin.
     * Returns an opaque channel handle, or NULL if not available.
     */
    void *(*spawn_get_stdin)(qwrt_pal_t *pal, void *proc);

    /**
     * Get the receive channel from the child's stdout.
     * Returns an opaque channel handle, or NULL if not available.
     */
    void *(*spawn_get_stdout)(qwrt_pal_t *pal, void *proc);

    /**
     * Send data to the child process.
     * Returns the number of bytes written, or QWRT_ERR_* on error.
     */
    int (*channel_send)(qwrt_pal_t *pal, void *ch,
                        const char *data, size_t len);

    /**
     * Receive data from the child (async).
     * cb fires with data, or with data_len==0 on EOF.
     */
    void (*channel_recv)(qwrt_pal_t *pal, void *ch,
                         qwrt_pal_cb_t cb, void *cb_data);

    /**
     * Close the channel.  Safe to call with NULL (no-op).
     */
    void (*channel_close)(qwrt_pal_t *pal, void *ch);

    /* ── Process lifecycle ─────────────────────────────────────── */

    /**
     * Wait for the child process to exit.
     *
     * @param timeout_ms  -1 = block forever, 0 = poll, >0 = wait up
     *                    to timeout_ms
     * @return            exit code (>=0), QWRT_ERR_TIMEOUT if the
     *                    timeout expired, or QWRT_ERR_GENERIC on error
     */
    int (*join)(qwrt_pal_t *pal, void *proc, int timeout_ms);

    /**
     * Force-terminate the child process.
     */
    void (*terminate)(qwrt_pal_t *pal, void *proc);

    /* ── Lifecycle hooks ───────────────────────────────────────── */

    /**
     * Initialize the PAL.  Called once by qwrt_create() before any
     * other method.  The PAL should allocate resources, open
     * connections, etc.
     *
     * Returns QWRT_OK on success, QWRT_ERR_* on failure.  If this
     * fails, qwrt_create() returns NULL.
     *
     * OPTIONAL: NULL means no initialization is needed (the PAL was
     * fully set up by its constructor).
     */
    int (*init)(qwrt_pal_t *pal);

    /**
     * Tear down the PAL.  Called once by qwrt_destroy() after all
     * contexts have been freed.  The PAL should release all
     * resources and cancel any pending async operations.
     *
     * After destroy() returns, no further PAL methods will be called.
     *
     * OPTIONAL: NULL means no cleanup is needed (the embedder will
     * free the PAL manually after qwrt_destroy returns).
     */
    void (*destroy)(qwrt_pal_t *pal);

    /* ── Reserved for future expansion ─────────────────────────── */

    /**
     * Reserved slots for future PAL interface additions.
     * Must be initialized to NULL.  Future versions of qwrt will
     * assign meaning to these slots; PAL implementations that
     * zero-initialize the struct are forward-compatible.
     */
    void *reserved[4];
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
    void *host_data;  /* per-runtime opaque pointer copied to qwrt_t; readable
                       * by extension init hooks via qwrt_get_runtime_data()
                       * during qwrt_create (before the host receives the rt). */
} qwrt_config_t;

/* qwrt_t forward declaration already provided above */

/* Forward declaration for JSContext */
struct JSContext;

/* ================================================================
 * Core API
 * ================================================================ */

/* Create a qwrt runtime. The PAL in `config` is borrowed (not owned) and
 * must outlive the runtime. The registered extension set is fixed at build
 * time via the QWRT_EXTENSIONS macro (see qwrt_ext_registry.h); there is no
 * runtime extension list. Returns NULL on failure.
 * Thread-bound: all subsequent qwrt_* calls must come from this thread. */
qwrt_t *qwrt_create(const qwrt_config_t *config);

/* Destroy the runtime and free all resources (contexts, handles, polyfill).
 * The PAL is NOT freed — caller owns it. Safe to call with NULL. */
void qwrt_destroy(qwrt_t *rt);

/* Process one batch: collects I/O events (if PAL has run_cycle), then
 * drains deferred callbacks + pending JS microtasks. Returns immediately
 * — does NOT loop internally. Use in a while loop:
 *
 *   while (running) {
 *       qwrt_tick(rt, 100);  // collect events + process one batch
 *       my_other_work();      // never starved
 *   }
 *
 * Returns 1 if any work was done, 0 if idle, -1 on error. */
int qwrt_tick(qwrt_t *rt, int timeout_ms);

/* Evaluate JS `code` on the active context. If `result` is non-NULL, receives
 * a malloc'd stringified result (free with qwrt_free). Returns 0 on success,
 * <0 on JS exception. The WinterCG polyfill (fetch, console, timers, …) is
 * auto-injected. */
int qwrt_eval(qwrt_t *rt, const char *code, char **result);

/* Evaluate precompiled QuickJS `bytecode` (len bytes). Same result/return
 * semantics as qwrt_eval. See qwrt_compile to produce bytecode. */
int qwrt_eval_bytecode(qwrt_t *rt, const uint8_t *bytecode, size_t len,
                       char **result);

/* Call global JS function `func` with arguments described by `args_json`
 * (a JSON array string, or NULL for no args). Result semantics as qwrt_eval. */
int qwrt_call(qwrt_t *rt, const char *func,
              const char *args_json, char **result);

/* Free memory returned by qwrt_eval / qwrt_call / qwrt_compile. NULL-safe. */
void qwrt_free(void *ptr);

/* ================================================================
 * Multi-context API
 *
 * One qwrt_t owns one JSRuntime; multiple contexts share it. Each context
 * has its own globals, polyfill, PAL (so different permissions), and
 * extension state. Only one context is "active" at a time — qwrt_eval /
 * qwrt_tick operate on the active context. QuickJS classes are
 * runtime-scoped, so class IDs are shared across contexts within one qwrt_t.
 * ================================================================ */

/* Reset the runtime: destroy all contexts, then create a fresh initial
 * context from `config`. The PAL is reused. Returns 0 on success, -1 on error. */
int qwrt_reset(qwrt_t *rt, const qwrt_config_t *config);

/* Spawn a new context from `config` in suspended state. Returns the new
 * context_id (>=0), or -1 on failure. The current active context is
 * unchanged. */
int qwrt_spawn(qwrt_t *rt, const qwrt_config_t *config);

/* Suspend the active context (calls ext->suspend hooks). No context is
 * active afterwards. Returns 0 on success, -1 on error. */
int qwrt_suspend(qwrt_t *rt);

/* Resume context `context_id`: auto-suspends the currently active context
 * and activates the target (calls ext->resume hooks). Returns 0 on success,
 * -1 if the context_id is invalid. */
int qwrt_resume(qwrt_t *rt, int context_id);

/* Destroy a specific context. Fails (-1) if it is the only remaining
 * context. Returns 0 on success. */
int qwrt_destroy_ctx(qwrt_t *rt, int context_id);

/* Return the active context_id, or -1 if none. */
int qwrt_get_active_ctx_id(qwrt_t *rt);

/* Return the active context's JSContext* (for direct QuickJS API use).
 * NULL if no active context. The pointer is valid until the context is
 * destroyed or reset. */
struct JSContext *qwrt_get_jsctx(qwrt_t *rt);

/* Per-runtime host data. qwrt_create copies config->host_data onto the
 * runtime, so an extension's init hook can read it via qwrt_get_runtime_data
 * during qwrt_create — before the host has the rt pointer (resolving the
 * init-time ordering deadlock). Set to NULL by default. */
void *qwrt_get_runtime_data(qwrt_t *rt);
void qwrt_set_runtime_data(qwrt_t *rt, void *data);

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

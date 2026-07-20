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

/*
 * PAL interface (qwrt_pal_t)
 *
 * THE universal platform abstraction layer. This struct is the single
 * contract between qwrt and the outside world. Every resource qwrt
 * needs — memory, network, files, timers, entropy, execution units — comes
 * through this interface. qwrt makes NO direct system calls.
 *
 * PAL Consumer: every qwrt component that uses this interface.
 * PAL Provider: the implementation that fills in the function pointers.
 *
 * Contract levels:
 *   [REQUIRED]  Must be non-NULL. qwrt_create rejects NULL.
 *   [OPTIONAL]  May be NULL. qwrt returns QWRT_ERR_NOT_SUPPORTED.
 *
 * Execution primitives (universal across all platforms):
 *   spawn — create a new execution unit
 *   exit  — terminate the current execution unit
 *   yield — relinquish CPU to other execution units
 *   sleep — suspend the current execution unit for a duration
 *   wait  — wait for a child execution unit to complete
 *
 * Versioning: the `version` field identifies the ABI. Bump when
 * adding new required fields. PAL implementations set it to the
 * version they implement. qwrt checks compatibility at create time.
 *
 * Thread model: PAL methods are called from the qwrt thread only.
 * PAL callbacks (qwrt_pal_cb_t) may fire from any thread — they must
 * enqueue via qwrt_defer_callback, never call JS directly.
 *
 * Lifetime: embedder creates PAL, passes to qwrt_create(). PAL must
 * outlive qwrt_t. qwrt_destroy() calls pal->destroy() if provided.
 * ================================================================ */

typedef struct qwrt_pal_t qwrt_pal_t;

struct qwrt_pal_t {
    /* ── Identity ───────────────────────────────────────────────── */

    /** Opaque pointer for PAL implementation's private state. */
    void *user_data;

    /** PAL interface version. Must be 1 for the current ABI. */
    uint32_t version;

    /** Human-readable name for diagnostics ("libuv", "mock", etc.).
     *  OPTIONAL — may be NULL. */
    const char *name;

    /* ── Execution primitives (five universals) ─────────────────── */

    /**
     * [REQUIRED] Create a new execution unit.
     *
     * @param entry_fn  function to execute in the new unit
     * @param arg       argument passed to entry_fn
     * @return          opaque handle to the new unit, NULL on failure
     *
     * The execution unit runs independently. Its lifecycle is managed
     * by the caller via wait() and exit().
     */
    void *(*spawn)(qwrt_pal_t *pal, void (*entry_fn)(void *arg), void *arg);

    /**
     * [REQUIRED] Terminate the current execution unit.
     *
     * @param code  exit code (0 = success, non-zero = error)
     *
     * After exit(), the execution unit stops and no further PAL
     * methods are called on it. Resources are reclaimed by the
     * parent via wait().
     */
    void (*exit)(qwrt_pal_t *pal, int code);

    /**
     * [REQUIRED] Relinquish the CPU to other execution units.
     *
     * Gives other units a chance to run. In single-threaded
     * environments (mock PAL), this is a no-op. In multi-tasking
     * environments (FreeRTOS), this triggers a context switch.
     */
    void (*yield)(qwrt_pal_t *pal);

    /**
     * [REQUIRED] Suspend the current execution unit.
     *
     * @param ms  duration in milliseconds
     *
     * The unit does NOT consume CPU during sleep. Other execution
     * units may run. Replaces timer_start/timer_stop for scheduling.
     */
    void (*sleep)(qwrt_pal_t *pal, uint64_t ms);

    /**
     * [REQUIRED] Wait for a child execution unit to complete.
     *
     * @param unit        handle returned by spawn()
     * @param timeout_ms  -1 = block forever, 0 = poll, >0 = timeout
     * @return           exit code (>=0), QWRT_ERR_TIMEOUT, or error
     */
    int (*wait)(qwrt_pal_t *pal, void *unit, int timeout_ms);

    /* ── Memory ─────────────────────────────────────────────────── */

    /** [OPTIONAL] Allocate memory. If NULL, malloc() is used. */
    void *(*mem_alloc)(qwrt_pal_t *pal, size_t size);

    /** [OPTIONAL] Free memory allocated by mem_alloc. If NULL, free() is used. */
    void (*mem_free)(qwrt_pal_t *pal, void *ptr);

    /* ── I/O ────────────────────────────────────────────────────── */

    /**
     * [REQUIRED] Fill buf[0..len-1] with cryptographically-secure
     * random bytes. Synchronous — hardware RNGs are fast enough.
     */
    void (*random_bytes)(qwrt_pal_t *pal, uint8_t *buf, size_t len);

    /** [REQUIRED] Current wall-clock time in milliseconds since Unix epoch. */
    uint64_t (*time_now)(qwrt_pal_t *pal);

    /** [REQUIRED] High-resolution monotonic time in nanoseconds. */
    uint64_t (*hrtime)(qwrt_pal_t *pal);

    /**
     * [OPTIONAL] Emit a log message. Level: 0=EMERG ... 7=DEBUG.
     * If NULL, logs are silently dropped.
     */
    void (*log)(qwrt_pal_t *pal, int level, const char *msg);

    /**
     * [REQUIRED] Perform an HTTP request. Delivers full response
     * body to callback.
     *
     * JSON delivered to cb: {"status":NNN,"headers":{...},"body":"..."}
     */
    void (*http_request)(qwrt_pal_t *pal,
                         const char *url, const char *method,
                         const char *headers, const char *body,
                         size_t body_len,
                         qwrt_pal_cb_t cb, void *cb_data);

    /**
     * [OPTIONAL] Streaming HTTP request. Same as http_request but
     * delivers response via qwrt_pal_stream_ops callbacks.
     */
    void (*http_request_stream)(qwrt_pal_t *pal,
                                const char *url, const char *method,
                                const char *headers, const char *body,
                                size_t body_len,
                                qwrt_pal_stream_ops_t *ops);

    /** [OPTIONAL] Abort in-flight streaming HTTP request. */
    void (*http_abort)(qwrt_pal_t *pal);

    /* ── Filesystem ─────────────────────────────────────────────── */

    /** [OPTIONAL] Read file contents. */
    void (*fs_read)(qwrt_pal_t *pal, const char *path,
                    qwrt_pal_cb_t cb, void *cb_data);
    /** [OPTIONAL] Write data to file. */
    void (*fs_write)(qwrt_pal_t *pal, const char *path,
                     const char *data, size_t data_len,
                     qwrt_pal_cb_t cb, void *cb_data);
    /** [OPTIONAL] Check file existence. */
    void (*fs_exists)(qwrt_pal_t *pal, const char *path,
                      qwrt_pal_cb_t cb, void *cb_data);
    /** [OPTIONAL] Delete a file. */
    void (*fs_remove)(qwrt_pal_t *pal, const char *path,
                      qwrt_pal_cb_t cb, void *cb_data);
    /** [OPTIONAL] List directory entries (JSON array). */
    void (*fs_list)(qwrt_pal_t *pal, const char *path,
                    qwrt_pal_cb_t cb, void *cb_data);

    /* ── Key-value storage ──────────────────────────────────────── */

    /** [OPTIONAL] Get value by key. */
    void (*storage_get)(qwrt_pal_t *pal, const char *key,
                       qwrt_pal_cb_t cb, void *cb_data);
    /** [OPTIONAL] Set key-value pair. */
    void (*storage_set)(qwrt_pal_t *pal, const char *key,
                       const char *value, size_t value_len,
                       qwrt_pal_cb_t cb, void *cb_data);
    /** [OPTIONAL] Delete a key. */
    void (*storage_del)(qwrt_pal_t *pal, const char *key,
                       qwrt_pal_cb_t cb, void *cb_data);

    /* ── Inter-unit communication ───────────────────────────────── */

    /**
     * [OPTIONAL] Send data to another execution unit.
     * @param ch    channel handle (from spawn or created separately)
     */
    int (*channel_send)(qwrt_pal_t *pal, void *ch,
                        const char *data, size_t len);

    /**
     * [OPTIONAL] Receive data from another execution unit (async).
     * cb fires with data, or data_len==0 on EOF.
     */
    void (*channel_recv)(qwrt_pal_t *pal, void *ch,
                         qwrt_pal_cb_t cb, void *cb_data);

    /** [OPTIONAL] Close a communication channel. */
    void (*channel_close)(qwrt_pal_t *pal, void *ch);

    /* ── Lifecycle hooks ────────────────────────────────────────── */

    /** [OPTIONAL] Initialize PAL. Called once by qwrt_create(). */
    int (*init)(qwrt_pal_t *pal);

    /** [OPTIONAL] Tear down PAL. Called once by qwrt_destroy(). */
    void (*destroy)(qwrt_pal_t *pal);

    /* ── Reserved ───────────────────────────────────────────────── */

    /** Must be initialized to NULL. */
    /* ── Timer (legacy, built on sleep + spawn) ────────────────── */

    /** [OPTIONAL] Start a timer. Built on sleep() internally.
     *  Kept for backward compatibility with setTimeout/setInterval.
     *  If NULL, timers are not available. */
    void *(*timer_start)(qwrt_pal_t *pal, uint64_t delay_ms, int repeat,
                          qwrt_pal_cb_t cb, void *cb_data);

    /** [OPTIONAL] Stop a timer. Safe to call with NULL (no-op). */
    void (*timer_stop)(qwrt_pal_t *pal, void *handle);

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
 * <0 on JS exception. The WinterTC polyfill (fetch, console, timers, …) is
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

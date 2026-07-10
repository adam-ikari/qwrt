#ifndef QWRT_INTERNAL_H
#define QWRT_INTERNAL_H

#include "qwrt/qwrt.h"
#include <quickjs.h>

/* Maximum concurrent timer/PAL-async handles. 256 slots balances memory
 * (qwrt_t grows by ~8 KB per 128 slots) against the rare case of
 * hundreds of overlapping timers or I/O operations.  When the table is
 * full, timer_start returns a RangeError. */
#define QWRT_MAX_HANDLES 256

/* Maximum number of concurrent contexts per runtime. */
#define QWRT_MAX_CONTEXTS 64

/* Magic sentinel for qwrt_t validation — "QWRT" in ASCII */
#define QWRT_MAGIC 0x51575254U

/* Silence -Wunused-parameter for fixed-signature callbacks (e.g. QuickJS
 * JSCFunction prototypes require this_val/argc/argv even when unused). */
#define QWRT_UNUSED(x) ((void)(x))

/* Forward declaration */
struct qwrt_ext_t;

/* Default polyfill bytecode (compiled in from polyfill_default.c) */
extern const uint8_t qwrt_default_polyfill[];
extern const size_t qwrt_default_polyfill_len;

/* Callback data shared between bridge.c and qwrt.c for async operations.
 * Allocated with js_malloc, freed with js_free (or qwrt_free_cb_data). */
typedef struct pal_cb_data_t {
    JSContext *ctx;
    JSValue resolve;
    JSValue reject;
    qwrt_t *rt;
    int is_timer;        /* 1 if this is a timer callback */
    int repeat;          /* 1 if this is a repeating timer */
    int handle_idx;      /* timer handle index if is_timer */
} pal_cb_data_t;

/* Per-context state — holds JSContext*, PAL, handle tables, timer data,
 * extensions, and polyfill config for reset re-injection. */
typedef struct qwrt_ctx_t {
    JSContext *jsctx;
    int context_id;
    int active;          /* 1 if this is the active context */
    int suspended;       /* 1 if context is suspended */
    const qwrt_pal_t *pal; /* per-context PAL for different permissions */

    void *handles[QWRT_MAX_HANDLES];
    JSValue timer_resolves[QWRT_MAX_HANDLES];
    void *timer_cbds[QWRT_MAX_HANDLES];  /* pal_cb_data_t* for cleanup on timerStop */
    int handle_count;

    const qwrt_ext_t **extensions;  /* NULL-terminated array, or NULL */
    int extensions_dynamic;           /* 1 if extensions was malloc'd by qwrt_ext_register */

    /* Polyfill config saved for reset re-injection */
    const uint8_t *polyfill;
    size_t polyfill_len;
} qwrt_ctx_t;

struct qwrt_t {
    uint32_t magic;      /* QWRT_MAGIC — set in qwrt_create, validates opaque ptr */
    JSRuntime *jsrt;
    qwrt_ctx_t *contexts[QWRT_MAX_CONTEXTS];  /* array of context pointers */
    int context_count;
    int active_ctx_id;   /* -1 if no active context */
    int has_pending_jobs;
    int debug;

    /* Per-runtime extension state. QuickJS registers classes per-JSRuntime,
     * and one qwrt_t owns one JSRuntime, so these live here (not per-context).
     * void* for engine types (e.g. wasm3 IM3Environment) to keep this header
     * free of third-party includes; ext_*.c cast as needed. */
#ifdef QWRT_HAS_WASM3
    JSClassID wasm3_module_class_id;
    JSClassID wasm3_instance_class_id;
    JSClassID wasm3_func_closure_class_id;
    JSClassID wasm3_import_closure_class_id;
    JSClassID wasm3_memory_class_id;
    JSClassID wasm3_table_class_id;
    JSClassID wasm3_global_class_id;
    void *wasm3_env;   /* IM3Environment */
#endif
#ifdef QWRT_HAS_WAMR
    JSClassID wamr_module_class_id;
    JSClassID wamr_instance_class_id;
#endif

#ifdef QWRT_DEBUG_SUPPORT
    /* DAP debugger session (NULL when no debugger attached). Opaque here to
     * keep this header free of qwrt_debug.h; src/debugger.c casts. Named
     * dbg_session to avoid clashing with the legacy `int debug` log flag. */
    void *dbg_session;
#endif

    /* Deferred PAL callback queue — libuv callbacks enqueue here,
     * qwrt_tick drains them so JS_Call happens in a valid JS context. */
    struct pal_deferred_cb {
        void (*fn)(void *data);
        void *data;
        struct pal_deferred_cb *next;
    } *deferred_cb_head;
    struct pal_deferred_cb *deferred_cb_tail;
};

/* ================================================================
 * Internal helper functions
 * ================================================================ */

/* context.c — context lifecycle helpers */
qwrt_ctx_t *qwrt_get_active_ctx(qwrt_t *rt);
JSContext *qwrt_get_active_jsctx(qwrt_t *rt);
qwrt_ctx_t *qwrt_get_ctx_by_id(qwrt_t *rt, int context_id);
qwrt_ctx_t *qwrt_ctx_create(qwrt_t *rt, const qwrt_config_t *config);
void qwrt_ctx_destroy(qwrt_t *rt, qwrt_ctx_t *ctx);

/* bridge.c — recover qwrt_t* from a JSRuntime* (finalizers get JSRuntime*).
 * Returns NULL if the runtime was not created by qwrt (magic check). */
qwrt_t *qwrt_get_rt_from_jsrt(JSRuntime *jsrt);

/* bridge.c — recover qwrt_t* from a JSContext* (non-static so extensions
 * with a JSContext* can use it). Equivalent to qwrt_get_rt_from_jsrt. */
qwrt_t *qwrt_get_rt_from_ctx(JSContext *ctx);
void qwrt_ctx_cleanup_resources(qwrt_t *rt, qwrt_ctx_t *ctx);

/* extension.c — extension lifecycle hooks */
int qwrt_ext_init_all(qwrt_t *rt, qwrt_ctx_t *ctx);
void qwrt_ext_destroy_all(qwrt_t *rt, qwrt_ctx_t *ctx);
int qwrt_ext_suspend_all(qwrt_t *rt, qwrt_ctx_t *ctx);
int qwrt_ext_resume_all(qwrt_t *rt, qwrt_ctx_t *ctx);

/* extension.c — register a single extension on an active context at runtime */
int qwrt_ext_register(qwrt_t *rt, qwrt_ctx_t *ctx, const qwrt_ext_t *ext);

/* bridge.c — creates the internal pal JS object (per-context version) */
JSValue qwrt_create_pal_object_ctx(qwrt_t *rt, qwrt_ctx_t *ctx);

/* bridge.c — defer callback to qwrt_tick context */
void qwrt_defer_callback(qwrt_t *rt, void (*fn)(void *data), void *data);

/* bridge.c — inject polyfill via __pal_inject__ temp global (per-context version) */
int qwrt_inject_polyfill_ctx(qwrt_t *rt, qwrt_ctx_t *ctx, const uint8_t *code, size_t code_len);

/* bridge.c — free a pal_cb_data_t: releases resolve/reject JSValues and
 * calls js_free on the allocation.  Safe to call with NULL. */
void qwrt_free_cb_data(JSContext *ctx, void *cbd);

#endif /* QWRT_INTERNAL_H */

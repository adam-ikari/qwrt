#ifndef QWRT_INTERNAL_H
#define QWRT_INTERNAL_H

#include "qwrt/qwrt.h"
#include <quickjs.h>

/* libuv include switch: qwrt embeds uv types (uv_loop_t etc.) BY VALUE in
 * qwrt_t, so the compiled struct layout must match the uv implementation
 * that the host links against. Test builds compile against mock_libuv.h
 * (deterministic offline scheduler); production builds use real libuv's
 * uv.h. The public qwrt.h stays uv-free — this switch is internal only. */
#ifdef QWRT_USE_MOCK_LIBUV
#include "mock_libuv.h"
#else
#include <uv.h>
#endif

/* Maximum concurrent timer/PAL-async handles. 256 slots balances memory
 * (qwrt_t grows by ~8 KB per 128 slots) against the rare case of
 * hundreds of overlapping timers or I/O operations.  When the table is
 * full, timer_start returns a RangeError. */
#define QWRT_MAX_HANDLES 256

/* Maximum number of concurrent contexts per runtime. */
#define QWRT_MAX_CONTEXTS 64

/* Maximum concurrent Web Workers per runtime (Task 4). A worker's id is its
 * slot index + 1 (id 0 is reserved for the host source), and tags inbound
 * messages (source = worker id, so source > 0 always means "from a worker"). */
#define QWRT_MAX_WORKERS 16

/* Magic sentinel for qwrt_t validation — "QWRT" in ASCII */
#define QWRT_MAGIC 0x51575254U

/* Silence -Wunused-parameter for fixed-signature callbacks (e.g. QuickJS
 * JSCFunction prototypes require this_val/argc/argv even when unused). */
#define QWRT_UNUSED(x) ((void)(x))

/* ── I/O error codes (used by uv_io.c / bridge) ── */

typedef enum {
    QWRT_OK                 =  0,
    QWRT_ERR_GENERIC        = -1,
    QWRT_ERR_NOT_FOUND      = -2,
    QWRT_ERR_IO             = -3,
    QWRT_ERR_PERMISSION     = -4,
    QWRT_ERR_NETWORK        = -5,
    QWRT_ERR_INVALID_ARG    = -6,
    QWRT_ERR_CANCELLED      = -7,
    QWRT_ERR_BUSY           = -8,
    QWRT_ERR_NOT_SUPPORTED  = -9,
    QWRT_ERR_TIMEOUT        = -10,
    QWRT_ERR_NO_MEMORY      = -11,
} qwrt_err_t;

/* Async I/O completion callback: status is 0 (OK) or a qwrt_err_t,
 * result/len hold an optional JSON/C-string payload. */
typedef void (*qwrt_io_done_t)(void *opaque, int status,
                               const char *result, size_t len);

/* Streaming HTTP response callbacks (uv_io_http_request_stream). */
typedef struct qwrt_pal_stream_ops_s {
    void (*on_headers)(void *user_data, int status, const char *headers_json);
    void (*on_data)(void *user_data, const char *data, size_t len);
    void (*on_end)(void *user_data, int error_status);
    void *user_data;
} qwrt_pal_stream_ops_t;

/* Forward declarations */
struct qwrt_ext_t;

/* Web Worker (Task 4): worker = 独立 qwrt_t（自己的线程 + JSRuntime + loop）。
 * id = 槽位索引 + 1（0 保留给宿主 source，source>0 恒为 worker），所以
 * 入站消息用 source 标签即可区分宿主 / worker，无需额外字段。定义放这里：
 * bridge.c / qwrt.c（teardown）都要解引用 w->parent / w->id / w->thread。 */
typedef struct qwrt_worker_s {
    qwrt_t *parent;            /* 父 runtime（worker 的 JS 线程就是父线程） */
    int id;                    /* 槽位索引 + 1 = 消息 source 标签 */
    uv_thread_t thread;        /* worker 线程句柄（父 teardown 时 join） */
    qwrt_t *self;              /* worker 自己的 runtime */
    char *script;              /* worker 脚本源码 */
    int shutting_down;
} qwrt_worker_t;

/* Default polyfill bytecode (compiled in from polyfill_default.c) */
extern const uint8_t qwrt_default_polyfill[];
extern const size_t qwrt_default_polyfill_len;

/* Inbound message source: 0 = host; >0 = worker id (Task 4). */
typedef enum { QWRT_MSG_SRC_HOST = 0 } qwrt_msg_src_t;

/* Inbound message FIFO node. data points into the same allocation (char array
 * after the struct header); freed with qwrt_msg_free. */
typedef struct qwrt_msg_s {
    char *data;
    size_t len;
    int source;
    struct qwrt_msg_s *next;
} qwrt_msg_t;

/* Per-context state — holds JSContext*, handle tables, timer data,
 * extensions, and polyfill config for reset re-injection. */
typedef struct qwrt_ctx_s {
    JSContext *jsctx;
    int context_id;
    int active;          /* 1 if this is the active context */
    int suspended;       /* 1 if context is suspended */

    void *handles[QWRT_MAX_HANDLES];
    JSValue timer_resolves[QWRT_MAX_HANDLES];
    void *timer_cbds[QWRT_MAX_HANDLES];  /* qwrt_cb_data_t* for cleanup on timerStop */
    int handle_count;

    const qwrt_ext_t * const *extensions;  /* compile-time table (QWRT_EXTENSIONS), read-only */
    int extensions_count;                    /* table length; iterate by count, skip NULL slots */

    /* Polyfill config saved for reset re-injection */
    const uint8_t *polyfill;
    size_t polyfill_len;
} qwrt_ctx_t;

/* Callback data shared between bridge.c and qwrt.c for async operations.
 * Allocated with js_malloc, freed with js_free (or qwrt_free_cb_data). */
typedef struct qwrt_cb_data_s {
    struct qwrt_ctx_s *ctx;
    JSValue resolve;
    JSValue reject;
    qwrt_t *rt;
    int is_timer;        /* 1 if this is a timer callback */
    int repeat;          /* 1 if this is a repeating timer */
    int handle_idx;      /* timer handle index if is_timer */
} qwrt_cb_data_t;

/* uv_io.c in-memory storage entry (per-runtime key-value store). */
typedef struct uv_io_store_entry_t {
    char *key;
    char *value;
    size_t value_len;
} uv_io_store_entry_t;

/* Forward decl: uv_io_http_op_t is defined in uv_io.c; qwrt_t only holds a
 * pointer to the active streaming op (see active_stream below), so only the
 * struct tag is needed here. */
struct uv_io_http_op_t;

struct qwrt_t {
    uint32_t magic;      /* QWRT_MAGIC — set in qwrt_create, validates opaque ptr */
    JSRuntime *jsrt;

    /* 线程 + loop（执行模型 A：qwrt 自持线程跑 libuv loop） */
    uv_loop_t loop;
    uv_thread_t thread;
    uv_async_t wake;         /* 宿主 post_message 唤醒；data = rt */
    uv_mutex_t msg_mutex;    /* 保护入站 FIFO */
    uv_cond_t  ready_cond;   /* ready 握手 */
    uv_mutex_t ready_mutex;

    /* 入站 FIFO（qwrt_post_message / worker 入站） */
    qwrt_msg_t *msg_head;
    qwrt_msg_t *msg_tail;
    int shutting_down;   /* destroy 置位 → 线程退出主循环 */
    int thread_ready;    /* ready 握手：线程初始化完成 */
    int ready_err;       /* init 失败码（0 成功；非 0 → qwrt_create 返回 NULL） */

    /* 配置副本（initial_script 由 qwrt_create strdup，destroy 释放） */
    qwrt_config_t config;
    void *host_data;     /* per-runtime opaque ptr；qwrt_get_runtime_data 读取 */
    int debug;

    /* uv_io.c in-memory storage（storage_get/set/del 的键值区，destroy 回收） */
    uv_io_store_entry_t *store;
    int storage_max;     /* 存储条目上限（uv_io 用 PAL_UV_STORAGE_DEFAULT） */
    int store_count;

    /* uv_io.c 当前活动的流式 HTTP op（http_abort 借它触达 in-flight 句柄） */
    struct uv_io_http_op_t *active_stream;

    qwrt_ctx_t *contexts[QWRT_MAX_CONTEXTS];  /* array of context pointers */
    int context_count;
    int active_ctx_id;   /* -1 if no active context */

    /* Web Worker (Task 4): worker_self is set on a worker's own qwrt_t (points
     * back to its qwrt_worker_t, non-NULL → this runtime is a worker); the
     * parent runtime keeps its workers table (worker id = slot index). Both
     * are only touched by the owning qwrt thread. */
    void *worker_self;
    qwrt_worker_t *workers[QWRT_MAX_WORKERS];


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
    JSClassID wamr_global_class_id;
#endif

#ifdef QWRT_DEBUG_SUPPORT
    /* DAP debugger session (NULL when no debugger attached). Opaque here to
     * keep this header free of qwrt_debug.h; src/debugger.c casts. Named
     * dbg_session to avoid clashing with the legacy `int debug` log flag. */
    void *dbg_session;
    /* DAP protocol layer (NULL when no DAP attached). Opaque here; owned by
     * src/debugger_dap.c. Per-runtime, so multiple runtimes (e.g. a worker)
     * each get their own DAP state. */
    void *dap;
#endif
};

/* ================================================================
 * Internal helper functions
 * ================================================================ */

/* msgq.c — thread-safe inbound FIFO */
int qwrt_msg_push(qwrt_t *rt, const char *data, size_t len, int source);
qwrt_msg_t *qwrt_msg_pop(qwrt_t *rt);
void qwrt_msg_free(qwrt_msg_t *m);

/* thread.c — the qwrt thread: uv loop + wake dispatch + microtask flush */
void qwrt_thread_main(void *arg);

/* qwrt.c — runtime init / eval / teardown (called from thread.c) */
int  qwrt_runtime_init(qwrt_t *rt);
int  qwrt_eval_internal(qwrt_t *rt, const char *script, char **err);
void qwrt_thread_teardown(qwrt_t *rt);

/* thread.c — flush pending JS microtasks (worker.c calls this on its loop) */
int qwrt_flush_microtasks(qwrt_t *rt);

/* worker.c — real-thread Web Workers. Parent-thread-only API (the parent qwrt
 * thread is the only one touching the workers table). qwrt_worker_create blocks
 * until the worker thread is ready; on failure sets *out_err (qwrt_err_t) and
 * returns NULL. */
qwrt_worker_t *qwrt_worker_create(qwrt_t *parent, const char *script, int *out_err);
void qwrt_worker_post(qwrt_t *parent, qwrt_worker_t *w,
                      const uint8_t *bytes, size_t len);
void qwrt_worker_terminate(qwrt_t *parent, qwrt_worker_t *w);
qwrt_worker_t *qwrt_worker_get(qwrt_t *parent, int id);
void qwrt_worker_free(qwrt_worker_t *w);

/* context.c — context lifecycle helpers */
qwrt_ctx_t *qwrt_get_active_ctx(qwrt_t *rt);
JSContext *qwrt_get_active_jsctx(qwrt_t *rt);
qwrt_ctx_t *qwrt_get_ctx_by_id(qwrt_t *rt, int context_id);
qwrt_ctx_t *qwrt_ctx_create(qwrt_t *rt, const qwrt_config_t *config);
void qwrt_ctx_destroy(qwrt_t *rt, qwrt_ctx_t *ctx);

/* context.c — multi-context + soft suspend/resume (Task 5). 全部由父（主
 * context）线程调用；宿主只见主 context，spawn/suspend/resume/destroy 由
 * polyfill 的 qwrtContext 经 bridge 驱动。目标 ctx_id 若 == active（正在执行
 * JS 的 ctx）返回 QWRT_ERR_BUSY——不能挂起/销毁/重建自己正在运行的 context。 */
int qwrt_ctx_spawn(qwrt_t *rt, const char *init_script);   /* 返回 ctx id 或 <0 */
int qwrt_ctx_serialize(qwrt_t *rt, int ctx_id, const char *state_path);
int qwrt_ctx_rebuild(qwrt_t *rt, int ctx_id, const char *script_ref, const char *state_path);
int qwrt_ctx_destroy_id(qwrt_t *rt, int ctx_id);

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

/* bridge.c — creates the internal pal JS object (per-context version) */
JSValue qwrt_create_pal_object_ctx(qwrt_t *rt, qwrt_ctx_t *ctx);

/* bridge.c — inject polyfill via __pal_inject__ temp global (per-context version) */
int qwrt_inject_polyfill_ctx(qwrt_t *rt, qwrt_ctx_t *ctx, const uint8_t *code, size_t code_len);

/* bridge.c — dispatch an inbound message to the main context's
 * __qwrt_dispatch__ (source 0 = host JSON, parsed; >0 = worker bytes). */
void qwrt_dispatch_message(qwrt_t *rt, qwrt_msg_t *m);

/* bridge.c — free a qwrt_cb_data_t: releases resolve/reject JSValues and
 * calls js_free on the allocation.  Safe to call with NULL. */
void qwrt_free_cb_data(JSContext *ctx, void *cbd);

/* bridge.c — cancel a live timer slot: uv_stop + uv_close (struct freed by
 * the close callback) + free resolve/cbd.  Used by js_pal_timer_stop and by
 * qwrt_ctx_cleanup_resources (context.c).  Safe when the slot is NULL. */
void qwrt_timer_cancel(qwrt_ctx_t *cctx, int idx);

/* uv_io.c — async I/O entry points.  Done callbacks fire on the qwrt
 * thread's loop (执行模型 A), so the bridge JS_Calls resolve/reject directly.
 * rt->loop / rt->store are owned here; qwrt.c frees rt->store at teardown. */
void uv_io_storage_get(qwrt_t *rt, const char *key,
                       qwrt_io_done_t cb, void *cb_data);
void uv_io_storage_set(qwrt_t *rt, const char *key,
                       const char *value, size_t value_len,
                       qwrt_io_done_t cb, void *cb_data);
void uv_io_storage_del(qwrt_t *rt, const char *key,
                       qwrt_io_done_t cb, void *cb_data);
void uv_io_fs_read(qwrt_t *rt, const char *path,
                   qwrt_io_done_t cb, void *cb_data);
void uv_io_fs_write(qwrt_t *rt, const char *path,
                    const char *data, size_t data_len,
                    qwrt_io_done_t cb, void *cb_data);
void uv_io_fs_exists(qwrt_t *rt, const char *path,
                     qwrt_io_done_t cb, void *cb_data);
void uv_io_fs_remove(qwrt_t *rt, const char *path,
                     qwrt_io_done_t cb, void *cb_data);
void uv_io_fs_list(qwrt_t *rt, const char *path,
                   qwrt_io_done_t cb, void *cb_data);
void uv_io_http_abort(qwrt_t *rt);
void uv_io_http_request(qwrt_t *rt, const char *url, const char *method,
                        const char *headers, const char *body, size_t body_len,
                        qwrt_io_done_t cb, void *cb_data);
void uv_io_http_request_stream(qwrt_t *rt, const char *url, const char *method,
                               const char *headers, const char *body,
                               size_t body_len, qwrt_pal_stream_ops_t *ops);

/* uv_io.c — synchronous helpers the bridge inlines (time_now uses uv_now on
 * rt->loop; hrtime/log/random_bytes are standalone). */
uint64_t uv_io_hrtime(void);
void uv_io_log(int level, const char *msg);
void uv_io_random_bytes(uint8_t *buf, size_t len);

#endif /* QWRT_INTERNAL_H */

/*
 * qwrt C Bridge Layer (执行模型 A)
 *
 * Creates the internal 'pal' JS object whose primitives map directly onto the
 * qwrt thread's libuv loop and the host message boundary. All libuv callbacks
 * run on the qwrt thread, so JS_Call happens directly — the PAL-era deferred
 * callback queue is gone.
 *
 *   timeNow / hrtime / log / randomBytes — sync, inlined to uv_now / uv_hrtime
 *       / stderr / /dev/urandom (no PAL backend in this model).
 *   timerStart / timerStop — malloc'd uv_timer_t on rt->loop; the uv timer
 *       callback resolves the polyfill's one-shot promise (polyfill only ever
 *       passes repeat=0; setInterval re-schedules setTimeout per tick).
 *   http / fs / storage — direct uv_io_* calls. Each builds a promise
 *       capability, calls the uv_io entry (whose done callback JS_Calls
 *       resolve/reject on the qwrt thread), and hands ownership of the
 *       resolving funcs to a qwrt_cb_data_t. The streaming HTTP path
 *       (uv_io_http_request_stream) JS_Calls on_headers/on_data/on_end.
 *   postMessage — host boundary: JSON out (rt->config.message_cb on qwrt
 *       thread); __qwrt_dispatch__ handles inbound host JSON (source 0).
 */

#include "qwrt_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* ================================================================
 * Forward declarations
 * ================================================================ */

static JSValue js_pal_time_now(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_pal_hrtime(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_pal_log(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_pal_timer_stop(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_pal_timer_start(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_pal_http_request(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_pal_http_request_stream(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_pal_fs_read(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_pal_fs_write(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_pal_fs_exists(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_pal_fs_remove(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_pal_fs_list(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_pal_storage_get(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_pal_storage_set(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_pal_storage_del(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_pal_random_bytes(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_pal_post_message(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_pal_fs_read_sync(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_pal_port_create(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_pal_spawn_worker(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_pal_worker_post(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_pal_worker_terminate(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_pal_worker_emit(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_pal_worker_close(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_pal_context_spawn(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_pal_context_suspend(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_pal_context_resume(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_pal_context_destroy(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);

/* ================================================================
 * Helper: get qwrt_t from JSContext / JSRuntime.
 * qwrt_get_rt_from_ctx is also used by extensions (declared in qwrt_internal.h).
 * ================================================================ */

qwrt_t *qwrt_get_rt_from_ctx(JSContext *ctx)
{
    if (!ctx) {
        return NULL;
    }
    return qwrt_get_rt_from_jsrt(JS_GetRuntime(ctx));
}

qwrt_t *qwrt_get_rt_from_jsrt(JSRuntime *jsrt)
{
    qwrt_t *rt = (qwrt_t *)JS_GetRuntimeOpaque(jsrt);
    if (!rt || rt->magic != QWRT_MAGIC) {
        return NULL;
    }
    return rt;
}

/* ================================================================
 * Helper: get qwrt_ctx_t from JSContext — iterate rt->contexts
 * to find the one matching jsctx
 * ================================================================ */

static qwrt_ctx_t *get_ctx_from_jsctx(qwrt_t *rt, JSContext *jsctx)
{
    if (!rt || !jsctx) {
        return NULL;
    }
    for (int i = 0; i < QWRT_MAX_CONTEXTS; i++) {
        if (rt->contexts[i] && rt->contexts[i]->jsctx == jsctx) {
            return rt->contexts[i];
        }
    }
    return NULL;
}

/* ================================================================
 * Helper: allocate and init callback data
 * ================================================================ */

static qwrt_cb_data_t *alloc_cb_data(qwrt_ctx_t *cctx, JSValue resolve, JSValue reject, qwrt_t *rt)
{
    qwrt_cb_data_t *cbd = (qwrt_cb_data_t *)js_malloc(cctx->jsctx, sizeof(qwrt_cb_data_t));
    if (!cbd) {
        return NULL;
    }
    cbd->ctx = cctx;
    cbd->resolve = resolve;  /* takes ownership */
    cbd->reject = reject;    /* takes ownership */
    cbd->rt = rt;
    cbd->repeat = 0;
    cbd->handle_idx = -1;
    return cbd;
}

/* ================================================================
 * Free callback data — shared with context.c for cleanup
 * ================================================================ */

void qwrt_free_cb_data(JSContext *ctx, void *cbd_)
{
    if (!cbd_) {
        return;
    }
    qwrt_cb_data_t *cbd = (qwrt_cb_data_t *)cbd_;
    JS_FreeValue(ctx, cbd->resolve);
    JS_FreeValue(ctx, cbd->reject);
    js_free(ctx, cbd);
}

/* ================================================================
 * Timer machinery — malloc'd uv_timer_t on rt->loop
 *
 * t->data = cbd (qwrt_cb_data_t holding resolve/reject). The uv close
 * callback frees the timer struct, so the memory stays valid until libuv
 * finishes closing (close callbacks run on the qwrt thread).
 * ================================================================ */

static void qwrt_timer_close_cb(uv_handle_t *h)
{
    free(h);
}

static void qwrt_timer_cb(uv_timer_t *t)
{
    qwrt_cb_data_t *cbd = (qwrt_cb_data_t *)t->data;
    JSContext *jsctx = cbd->ctx->jsctx;

    JSValue arg = JS_UNDEFINED;
    JSValue ret = JS_Call(jsctx, cbd->resolve, JS_UNDEFINED, 1, &arg);
    JS_FreeValue(jsctx, ret);

    if (cbd->repeat) {
        /* repeating: stays armed via the uv repeat interval; resolve is a
         * settled-promise no-op on subsequent fires. */
        return;
    }

    /* one-shot: settle and release the slot + cbd + handle struct */
    qwrt_ctx_t *cctx = cbd->ctx;
    if (cbd->handle_idx >= 0 && cbd->handle_idx < QWRT_MAX_HANDLES) {
        if (!JS_IsUndefined(cctx->timer_resolves[cbd->handle_idx])) {
            JS_FreeValue(jsctx, cctx->timer_resolves[cbd->handle_idx]);
            cctx->timer_resolves[cbd->handle_idx] = JS_UNDEFINED;
        }
        cctx->handles[cbd->handle_idx] = NULL;
        cctx->timer_cbds[cbd->handle_idx] = NULL;   /* cbd freed below */
    }
    JS_FreeValue(jsctx, cbd->resolve);
    JS_FreeValue(jsctx, cbd->reject);
    js_free(jsctx, cbd);
    uv_close((uv_handle_t *)t, qwrt_timer_close_cb);
}

/* Cancel a live timer slot: stop + uv_close (struct freed by the close
 * callback) + free resolve/cbd. Used by js_pal_timer_stop and by context.c
 * cleanup. Safe when the handle slot is NULL. */
void qwrt_timer_cancel(qwrt_ctx_t *cctx, int idx)
{
    JSContext *jsctx = cctx->jsctx;
    if (cctx->handles[idx]) {
        uv_timer_stop((uv_timer_t *)cctx->handles[idx]);
        uv_close((uv_handle_t *)cctx->handles[idx], qwrt_timer_close_cb);
        cctx->handles[idx] = NULL;
    }
    if (!JS_IsUndefined(cctx->timer_resolves[idx])) {
        JS_FreeValue(jsctx, cctx->timer_resolves[idx]);
        cctx->timer_resolves[idx] = JS_UNDEFINED;
    }
    if (cctx->timer_cbds[idx]) {
        qwrt_free_cb_data(jsctx, cctx->timer_cbds[idx]);
        cctx->timer_cbds[idx] = NULL;
    }
}

/* ================================================================
 * Sync primitives
 * ================================================================ */

static JSValue js_pal_time_now(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    QWRT_UNUSED(this_val); QWRT_UNUSED(argc); QWRT_UNUSED(argv);
    qwrt_t *rt = qwrt_get_rt_from_ctx(ctx);
    if (!rt) {
        return JS_ThrowTypeError(ctx, "pal.time_now not available");
    }
    return JS_NewFloat64(ctx, (double)uv_now(&rt->loop));
}

static JSValue js_pal_hrtime(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    QWRT_UNUSED(this_val); QWRT_UNUSED(argc); QWRT_UNUSED(argv);
    qwrt_t *rt = qwrt_get_rt_from_ctx(ctx);
    if (!rt) {
        return JS_ThrowTypeError(ctx, "pal.hrtime not available");
    }
    return JS_NewFloat64(ctx, (double)uv_hrtime());
}

static JSValue js_pal_log(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    QWRT_UNUSED(this_val);
    int32_t level = 0; /* default: info */
    const char *msg = "";
    int msg_needs_free = 0;

    if (argc >= 1) {
        if (JS_ToInt32(ctx, &level, argv[0]) < 0) {
            return JS_EXCEPTION;
        }
    }
    if (argc >= 2) {
        msg = JS_ToCString(ctx, argv[1]);
        if (!msg) {
            return JS_EXCEPTION;
        }
        msg_needs_free = 1;
    }

    /* Web 运行时 console 行为：log/info/debug → stdout；warn/error → stderr。
     * 去掉 [qwrt:N] 前缀，对齐 node/deno 的 console 输出形态。
     * fflush 保证输出即使在全缓冲的管道/重定向场景下也即时可见——否则 server
     * 常驻进程（listener 活跃、loop 不空）的输出会滞留缓冲直到退出才 flush。 */
    FILE *out = (level >= 2) ? stderr : stdout;
    fprintf(out, "%s\n", msg);
    fflush(out);

    if (msg_needs_free) {
        JS_FreeCString(ctx, msg);
    }

    return JS_UNDEFINED;
}

/* ================================================================
 * Timer start / stop
 * ================================================================ */

static JSValue js_pal_timer_stop(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    QWRT_UNUSED(this_val);
    qwrt_t *rt = qwrt_get_rt_from_ctx(ctx);
    if (!rt) {
        return JS_ThrowTypeError(ctx, "pal.timer_stop not available");
    }
    qwrt_ctx_t *cctx = get_ctx_from_jsctx(rt, ctx);
    if (!cctx) {
        return JS_ThrowTypeError(ctx, "pal.timer_stop not available");
    }

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "timer_stop requires handle argument");
    }

    int32_t handle_idx;
    if (JS_ToInt32(ctx, &handle_idx, argv[0]) < 0) {
        return JS_EXCEPTION;
    }

    /* Validate handle index */
    if (handle_idx < 0 || handle_idx >= cctx->handle_count) {
        return JS_ThrowRangeError(ctx, "invalid timer handle");
    }

    if (cctx->handles[handle_idx]) {
        qwrt_timer_cancel(cctx, handle_idx);
    }

    return JS_UNDEFINED;
}

static JSValue js_pal_timer_start(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    QWRT_UNUSED(this_val);
    qwrt_t *rt = qwrt_get_rt_from_ctx(ctx);
    if (!rt) {
        return JS_ThrowTypeError(ctx, "pal.timer_start not available");
    }
    qwrt_ctx_t *cctx = get_ctx_from_jsctx(rt, ctx);
    if (!cctx) {
        return JS_ThrowTypeError(ctx, "pal.timer_start not available");
    }

    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "timer_start requires delay_ms and repeat arguments");
    }

    double delay_ms;
    int32_t repeat;

    if (JS_ToFloat64(ctx, &delay_ms, argv[0]) < 0) {
        return JS_EXCEPTION;
    }
    if (JS_ToInt32(ctx, &repeat, argv[1]) < 0) {
        return JS_EXCEPTION;
    }

    /* Create promise capability */
    JSValue resolving_funcs[2];
    JSValue promise = JS_NewPromiseCapability(ctx, resolving_funcs);
    if (JS_IsException(promise)) {
        return JS_EXCEPTION;
    }

    /* Dup resolve for timer_resolves; alloc_cb_data takes ownership of originals */
    JSValue resolve_dup = JS_DupValue(ctx, resolving_funcs[0]);

    /* Allocate callback data — takes ownership of resolving_funcs */
    qwrt_cb_data_t *cbd = alloc_cb_data(cctx, resolving_funcs[0], resolving_funcs[1], rt);
    if (!cbd) {
        JS_FreeValue(ctx, resolve_dup);
        JS_FreeValue(ctx, resolving_funcs[0]);
        JS_FreeValue(ctx, resolving_funcs[1]);
        return JS_ThrowOutOfMemory(ctx);
    }
    cbd->repeat = repeat;

    /* Allocate + arm the uv timer on the qwrt thread's loop */
    uv_timer_t *t = (uv_timer_t *)malloc(sizeof *t);
    if (!t) {
        JS_FreeValue(ctx, resolve_dup);
        qwrt_free_cb_data(ctx, cbd);
        return JS_ThrowOutOfMemory(ctx);
    }
    memset(t, 0, sizeof *t);
    t->data = cbd;
    if (uv_timer_init(&rt->loop, t) != 0) {
        free(t);
        JS_FreeValue(ctx, resolve_dup);
        qwrt_free_cb_data(ctx, cbd);
        return JS_ThrowOutOfMemory(ctx);
    }
    uint64_t timeout = (uint64_t)delay_ms;
    uint64_t interval = repeat ? timeout : 0;
    if (uv_timer_start(t, qwrt_timer_cb, timeout, interval) != 0) {
        uv_close((uv_handle_t *)t, qwrt_timer_close_cb);
        JS_FreeValue(ctx, resolve_dup);
        qwrt_free_cb_data(ctx, cbd);
        return JS_ThrowTypeError(ctx, "failed to start timer");
    }

    /* Find a free handle slot (reuse NULL slots) */
    int idx = -1;
    for (int i = 0; i < cctx->handle_count; i++) {
        if (cctx->handles[i] == NULL) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        if (cctx->handle_count >= QWRT_MAX_HANDLES) {
            uv_close((uv_handle_t *)t, qwrt_timer_close_cb);
            JS_FreeValue(ctx, resolve_dup);
            qwrt_free_cb_data(ctx, cbd);
            return JS_ThrowRangeError(ctx, "too many timers");
        }
        idx = cctx->handle_count;
        cctx->handle_count++;
    }

    cctx->handles[idx] = t;
    cctx->timer_resolves[idx] = resolve_dup;
    cctx->timer_cbds[idx] = cbd;
    cbd->handle_idx = idx;

    /* Return {handle: number, promise: Promise} so polyfill can both
     * call timerStop(handle) and await the promise. */
    JSValue obj = JS_NewObject(ctx);
    if (JS_IsException(obj)) {
        qwrt_timer_cancel(cctx, idx);
        JS_FreeValue(ctx, promise);
        return JS_EXCEPTION;
    }
    JS_SetPropertyStr(ctx, obj, "handle", JS_NewInt32(ctx, idx));
    JS_SetPropertyStr(ctx, obj, "promise", promise);

    return obj;
}

/* ================================================================
 * Path traversal guard
 * ================================================================ */

/*
 * Validate that a file path does not contain path traversal sequences.
 * Returns true if the path is safe, false if it contains traversal.
 *
 * Blocks:
 *   - ".." path components (parent directory escape)
 *   - Null bytes (path truncation attacks)
 *   - Leading "/" is allowed (absolute paths)
 */
static bool bridge_validate_path(const char *path)
{
    if (!path || !*path) return false;

    /*
     * Note: a literal null-byte scan (strchr(path, '\0')) is pointless here —
     * it always finds the C-string terminator and would reject every path.
     * An embedded NUL is impossible to receive through JS_ToCString (it stops
     * at the first NUL), so there is nothing to reject on that axis; the
     * meaningful protection is the ".." component check below.
     */

    /* Reject "../" and "..\" and trailing ".." */
    const char *p = path;
    while (*p) {
        /* Check for ".." as a path component */
        if (p[0] == '.' && p[1] == '.') {
            char next = p[2];
            /* End of string, or followed by path separator */
            if (next == '\0' || next == '/' || next == '\\') {
                return false;
            }
        }
        p++;
    }

    return true;
}
/* ================================================================
 * Async primitives — direct uv_io_* calls (执行模型 A)
 *
 * Each wrapper: JS_ToCString args → JS_NewPromiseCapability → alloc_cb_data
 * (takes ownership of resolve/reject) → call the uv_io entry, then free the
 * C strings. The uv_io done callback fires on the qwrt thread's loop and
 * JS_Calls resolve/reject directly — no deferred-queue relay. Defaults,
 * validation and level mapping stay in the polyfill JS or the uv_io
 * implementation (thin-bridge rule).
 * ================================================================ */

/* Shared done callback for the non-streaming ops. status==0 resolves with the
 * payload string (or an empty string when there is no payload); status<0
 * rejects with the payload, or "unknown error" when there is none. Frees the
 * qwrt_cb_data_t and releases resolve/reject. */
static void bridge_io_done(void *opaque, int status, const char *data, size_t len)
{
    qwrt_cb_data_t *cd = (qwrt_cb_data_t *)opaque;
    JSContext *ctx = cd->ctx->jsctx;
    JSValue fn = (status == 0) ? cd->resolve : cd->reject;
    JSValue result;

    if (status == 0) {
        result = JS_NewStringLen(ctx, data ? data : "", data ? len : 0);
    } else if (data) {
        result = JS_NewStringLen(ctx, data, len);
    } else {
        result = JS_NewString(ctx, "unknown error");
    }

    if (!JS_IsException(result)) {
        JSValue ret = JS_Call(ctx, fn, JS_UNDEFINED, 1, &result);
        JS_FreeValue(ctx, ret);
    }
    JS_FreeValue(ctx, result);

    qwrt_free_cb_data(ctx, cd);
}

/* storage_get done callback: found → resolve the stored string; not-found →
 * resolve null (storage.js contract: a miss is a normal result, not a
 * rejection); other errors → reject. */
static void storage_get_done(void *opaque, int status, const char *data, size_t len)
{
    qwrt_cb_data_t *cd = (qwrt_cb_data_t *)opaque;
    JSContext *ctx = cd->ctx->jsctx;
    JSValue fn = cd->resolve;
    JSValue result;

    if (status == QWRT_ERR_NOT_FOUND) {
        result = JS_NULL;
    } else if (status == 0) {
        result = JS_NewStringLen(ctx, data ? data : "", data ? len : 0);
    } else {
        fn = cd->reject;
        result = JS_NewStringLen(ctx, data ? data : "storage error",
                                 data ? len : 13);
    }

    if (!JS_IsException(result)) {
        JSValue ret = JS_Call(ctx, fn, JS_UNDEFINED, 1, &result);
        JS_FreeValue(ctx, ret);
    }
    JS_FreeValue(ctx, result);

    qwrt_free_cb_data(ctx, cd);
}

/* Streaming HTTP — the ops->user_data passed to uv_io_http_request_stream.
 * bridge_stream_on_end frees bs; uv_io guarantees on_end fires exactly once
 * per op (every completion / error / abort path terminates in on_end). */
typedef struct bridge_stream_ctx_s {
    JSContext *ctx;
    JSValue on_headers;
    JSValue on_data;
    JSValue on_end;
} bridge_stream_ctx_t;

static void bridge_stream_on_headers(void *ud, int status, const char *headers_json)
{
    bridge_stream_ctx_t *bs = (bridge_stream_ctx_t *)ud;
    if (!JS_IsFunction(bs->ctx, bs->on_headers)) {
        return;
    }
    JSValue args[2];
    args[0] = JS_NewInt32(bs->ctx, status);
    args[1] = JS_NewString(bs->ctx, headers_json ? headers_json : "{}");
    if (JS_IsException(args[1])) {
        JS_FreeValue(bs->ctx, args[0]);
        JS_FreeValue(bs->ctx, args[1]);
        return;
    }
    JSValue ret = JS_Call(bs->ctx, bs->on_headers, JS_UNDEFINED, 2, args);
    JS_FreeValue(bs->ctx, ret);
    JS_FreeValue(bs->ctx, args[0]);
    JS_FreeValue(bs->ctx, args[1]);
}

static void bridge_stream_on_data(void *ud, const char *data, size_t len)
{
    bridge_stream_ctx_t *bs = (bridge_stream_ctx_t *)ud;
    if (!JS_IsFunction(bs->ctx, bs->on_data)) {
        return;
    }
    JSValue buf = JS_NewArrayBufferCopy(bs->ctx, (const uint8_t *)data, len);
    if (JS_IsException(buf)) {
        JS_FreeValue(bs->ctx, buf);
        return;
    }
    JSValue ret = JS_Call(bs->ctx, bs->on_data, JS_UNDEFINED, 1, &buf);
    JS_FreeValue(bs->ctx, ret);
    JS_FreeValue(bs->ctx, buf);
}

static void bridge_stream_on_end(void *ud, int error_status)
{
    bridge_stream_ctx_t *bs = (bridge_stream_ctx_t *)ud;
    if (JS_IsFunction(bs->ctx, bs->on_end)) {
        JSValue arg = JS_NewInt32(bs->ctx, error_status);
        JSValue ret = JS_Call(bs->ctx, bs->on_end, JS_UNDEFINED, 1, &arg);
        JS_FreeValue(bs->ctx, ret);
        JS_FreeValue(bs->ctx, arg);
    }
    JS_FreeValue(bs->ctx, bs->on_headers);
    JS_FreeValue(bs->ctx, bs->on_data);
    JS_FreeValue(bs->ctx, bs->on_end);
    js_free(bs->ctx, bs);
}

static JSValue js_pal_http_request(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    QWRT_UNUSED(this_val);
    qwrt_t *rt = qwrt_get_rt_from_ctx(ctx);
    if (!rt) {
        return JS_ThrowTypeError(ctx, "pal.http_request not available");
    }
    qwrt_ctx_t *cctx = get_ctx_from_jsctx(rt, ctx);
    if (!cctx) {
        return JS_ThrowTypeError(ctx, "pal.http_request not available");
    }
    if (argc < 4) {
        return JS_ThrowTypeError(ctx, "http_request requires url, method, headers, body arguments");
    }

    const char *url = JS_ToCString(ctx, argv[0]);
    const char *method = JS_ToCString(ctx, argv[1]);
    const char *headers = JS_ToCString(ctx, argv[2]);
    if (!url || !method || !headers) {
        if (url) JS_FreeCString(ctx, url);
        if (method) JS_FreeCString(ctx, method);
        if (headers) JS_FreeCString(ctx, headers);
        return JS_EXCEPTION;
    }

    const char *body = NULL;
    size_t body_len = 0;
    if (!JS_IsUndefined(argv[3]) && !JS_IsNull(argv[3])) {
        body = JS_ToCStringLen(ctx, &body_len, argv[3]);
        if (!body) {
            JS_FreeCString(ctx, url);
            JS_FreeCString(ctx, method);
            JS_FreeCString(ctx, headers);
            return JS_EXCEPTION;
        }
    }

    JSValue resolving_funcs[2];
    JSValue promise = JS_NewPromiseCapability(ctx, resolving_funcs);
    if (JS_IsException(promise)) {
        JS_FreeCString(ctx, url);
        JS_FreeCString(ctx, method);
        JS_FreeCString(ctx, headers);
        if (body) JS_FreeCString(ctx, body);
        return JS_EXCEPTION;
    }

    qwrt_cb_data_t *cbd = alloc_cb_data(cctx, resolving_funcs[0], resolving_funcs[1], rt);
    if (!cbd) {
        JS_FreeValue(ctx, resolving_funcs[0]);
        JS_FreeValue(ctx, resolving_funcs[1]);
        JS_FreeCString(ctx, url);
        JS_FreeCString(ctx, method);
        JS_FreeCString(ctx, headers);
        if (body) JS_FreeCString(ctx, body);
        return JS_ThrowOutOfMemory(ctx);
    }

    uv_io_http_request(rt, url, method, headers, body, body_len, bridge_io_done, cbd);

    JS_FreeCString(ctx, url);
    JS_FreeCString(ctx, method);
    JS_FreeCString(ctx, headers);
    if (body) JS_FreeCString(ctx, body);

    return promise;
}

static JSValue js_pal_http_request_stream(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    QWRT_UNUSED(this_val);
    qwrt_t *rt = qwrt_get_rt_from_ctx(ctx);
    if (!rt) {
        return JS_ThrowTypeError(ctx, "pal.http_request_stream not available");
    }
    if (argc < 7) {
        return JS_ThrowTypeError(ctx, "http_request_stream requires url, method, headers, body, onHeaders, onData, onEnd arguments");
    }

    const char *url = JS_ToCString(ctx, argv[0]);
    const char *method = JS_ToCString(ctx, argv[1]);
    const char *headers = JS_ToCString(ctx, argv[2]);
    if (!url || !method || !headers) {
        if (url) JS_FreeCString(ctx, url);
        if (method) JS_FreeCString(ctx, method);
        if (headers) JS_FreeCString(ctx, headers);
        return JS_EXCEPTION;
    }

    const char *body = NULL;
    size_t body_len = 0;
    if (!JS_IsUndefined(argv[3]) && !JS_IsNull(argv[3])) {
        body = JS_ToCStringLen(ctx, &body_len, argv[3]);
        if (!body) {
            JS_FreeCString(ctx, url);
            JS_FreeCString(ctx, method);
            JS_FreeCString(ctx, headers);
            return JS_EXCEPTION;
        }
    }

    bridge_stream_ctx_t *bs = (bridge_stream_ctx_t *)js_malloc(ctx, sizeof *bs);
    if (!bs) {
        JS_FreeCString(ctx, url);
        JS_FreeCString(ctx, method);
        JS_FreeCString(ctx, headers);
        if (body) JS_FreeCString(ctx, body);
        return JS_ThrowOutOfMemory(ctx);
    }
    bs->ctx = ctx;
    bs->on_headers = JS_DupValue(ctx, argv[4]);
    bs->on_data = JS_DupValue(ctx, argv[5]);
    bs->on_end = JS_DupValue(ctx, argv[6]);

    qwrt_io_stream_ops_t ops;
    memset(&ops, 0, sizeof ops);
    ops.on_headers = bridge_stream_on_headers;
    ops.on_data = bridge_stream_on_data;
    ops.on_end = bridge_stream_on_end;
    ops.user_data = bs;

    uv_io_http_request_stream(rt, url, method, headers, body, body_len, &ops);

    JS_FreeCString(ctx, url);
    JS_FreeCString(ctx, method);
    JS_FreeCString(ctx, headers);
    if (body) JS_FreeCString(ctx, body);

    return JS_UNDEFINED;
}

static JSValue js_pal_fs_read(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    QWRT_UNUSED(this_val);
    qwrt_t *rt = qwrt_get_rt_from_ctx(ctx);
    if (!rt) {
        return JS_ThrowTypeError(ctx, "pal.fs_read not available");
    }
    qwrt_ctx_t *cctx = get_ctx_from_jsctx(rt, ctx);
    if (!cctx) {
        return JS_ThrowTypeError(ctx, "pal.fs_read not available");
    }
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "fs_read requires path argument");
    }

    const char *path = JS_ToCString(ctx, argv[0]);
    if (!path) {
        return JS_EXCEPTION;
    }
    if (!bridge_validate_path(path)) {
        JS_FreeCString(ctx, path);
        return JS_ThrowTypeError(ctx, "Path traversal detected");
    }

    JSValue resolving_funcs[2];
    JSValue promise = JS_NewPromiseCapability(ctx, resolving_funcs);
    if (JS_IsException(promise)) {
        JS_FreeCString(ctx, path);
        return JS_EXCEPTION;
    }

    qwrt_cb_data_t *cbd = alloc_cb_data(cctx, resolving_funcs[0], resolving_funcs[1], rt);
    if (!cbd) {
        JS_FreeValue(ctx, resolving_funcs[0]);
        JS_FreeValue(ctx, resolving_funcs[1]);
        JS_FreeCString(ctx, path);
        return JS_ThrowOutOfMemory(ctx);
    }

    uv_io_fs_read(rt, path, bridge_io_done, cbd);

    JS_FreeCString(ctx, path);
    return promise;
}

static JSValue js_pal_fs_write(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    QWRT_UNUSED(this_val);
    qwrt_t *rt = qwrt_get_rt_from_ctx(ctx);
    if (!rt) {
        return JS_ThrowTypeError(ctx, "pal.fs_write not available");
    }
    qwrt_ctx_t *cctx = get_ctx_from_jsctx(rt, ctx);
    if (!cctx) {
        return JS_ThrowTypeError(ctx, "pal.fs_write not available");
    }
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "fs_write requires path and data arguments");
    }

    const char *path = JS_ToCString(ctx, argv[0]);
    if (!path) {
        return JS_EXCEPTION;
    }
    if (!bridge_validate_path(path)) {
        JS_FreeCString(ctx, path);
        return JS_ThrowTypeError(ctx, "Path traversal detected");
    }

    const char *data = NULL;
    size_t data_len = 0;
    if (!JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1])) {
        data = JS_ToCStringLen(ctx, &data_len, argv[1]);
        if (!data) {
            JS_FreeCString(ctx, path);
            return JS_EXCEPTION;
        }
    }

    JSValue resolving_funcs[2];
    JSValue promise = JS_NewPromiseCapability(ctx, resolving_funcs);
    if (JS_IsException(promise)) {
        JS_FreeCString(ctx, path);
        if (data) JS_FreeCString(ctx, data);
        return JS_EXCEPTION;
    }

    qwrt_cb_data_t *cbd = alloc_cb_data(cctx, resolving_funcs[0], resolving_funcs[1], rt);
    if (!cbd) {
        JS_FreeValue(ctx, resolving_funcs[0]);
        JS_FreeValue(ctx, resolving_funcs[1]);
        JS_FreeCString(ctx, path);
        if (data) JS_FreeCString(ctx, data);
        return JS_ThrowOutOfMemory(ctx);
    }

    uv_io_fs_write(rt, path, data ? data : "", data ? data_len : 0,
                   bridge_io_done, cbd);

    JS_FreeCString(ctx, path);
    if (data) JS_FreeCString(ctx, data);

    return promise;
}

static JSValue js_pal_fs_exists(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    QWRT_UNUSED(this_val);
    qwrt_t *rt = qwrt_get_rt_from_ctx(ctx);
    if (!rt) {
        return JS_ThrowTypeError(ctx, "pal.fs_exists not available");
    }
    qwrt_ctx_t *cctx = get_ctx_from_jsctx(rt, ctx);
    if (!cctx) {
        return JS_ThrowTypeError(ctx, "pal.fs_exists not available");
    }
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "fs_exists requires path argument");
    }

    const char *path = JS_ToCString(ctx, argv[0]);
    if (!path) {
        return JS_EXCEPTION;
    }
    if (!bridge_validate_path(path)) {
        JS_FreeCString(ctx, path);
        return JS_ThrowTypeError(ctx, "Path traversal detected");
    }

    JSValue resolving_funcs[2];
    JSValue promise = JS_NewPromiseCapability(ctx, resolving_funcs);
    if (JS_IsException(promise)) {
        JS_FreeCString(ctx, path);
        return JS_EXCEPTION;
    }

    qwrt_cb_data_t *cbd = alloc_cb_data(cctx, resolving_funcs[0], resolving_funcs[1], rt);
    if (!cbd) {
        JS_FreeValue(ctx, resolving_funcs[0]);
        JS_FreeValue(ctx, resolving_funcs[1]);
        JS_FreeCString(ctx, path);
        return JS_ThrowOutOfMemory(ctx);
    }

    uv_io_fs_exists(rt, path, bridge_io_done, cbd);

    JS_FreeCString(ctx, path);
    return promise;
}

static JSValue js_pal_fs_remove(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    QWRT_UNUSED(this_val);
    qwrt_t *rt = qwrt_get_rt_from_ctx(ctx);
    if (!rt) {
        return JS_ThrowTypeError(ctx, "pal.fs_remove not available");
    }
    qwrt_ctx_t *cctx = get_ctx_from_jsctx(rt, ctx);
    if (!cctx) {
        return JS_ThrowTypeError(ctx, "pal.fs_remove not available");
    }
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "fs_remove requires path argument");
    }

    const char *path = JS_ToCString(ctx, argv[0]);
    if (!path) {
        return JS_EXCEPTION;
    }
    if (!bridge_validate_path(path)) {
        JS_FreeCString(ctx, path);
        return JS_ThrowTypeError(ctx, "Path traversal detected");
    }

    JSValue resolving_funcs[2];
    JSValue promise = JS_NewPromiseCapability(ctx, resolving_funcs);
    if (JS_IsException(promise)) {
        JS_FreeCString(ctx, path);
        return JS_EXCEPTION;
    }

    qwrt_cb_data_t *cbd = alloc_cb_data(cctx, resolving_funcs[0], resolving_funcs[1], rt);
    if (!cbd) {
        JS_FreeValue(ctx, resolving_funcs[0]);
        JS_FreeValue(ctx, resolving_funcs[1]);
        JS_FreeCString(ctx, path);
        return JS_ThrowOutOfMemory(ctx);
    }

    uv_io_fs_remove(rt, path, bridge_io_done, cbd);

    JS_FreeCString(ctx, path);
    return promise;
}

static JSValue js_pal_fs_list(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    QWRT_UNUSED(this_val);
    qwrt_t *rt = qwrt_get_rt_from_ctx(ctx);
    if (!rt) {
        return JS_ThrowTypeError(ctx, "pal.fs_list not available");
    }
    qwrt_ctx_t *cctx = get_ctx_from_jsctx(rt, ctx);
    if (!cctx) {
        return JS_ThrowTypeError(ctx, "pal.fs_list not available");
    }
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "fs_list requires path argument");
    }

    const char *path = JS_ToCString(ctx, argv[0]);
    if (!path) {
        return JS_EXCEPTION;
    }
    if (!bridge_validate_path(path)) {
        JS_FreeCString(ctx, path);
        return JS_ThrowTypeError(ctx, "Path traversal detected");
    }

    JSValue resolving_funcs[2];
    JSValue promise = JS_NewPromiseCapability(ctx, resolving_funcs);
    if (JS_IsException(promise)) {
        JS_FreeCString(ctx, path);
        return JS_EXCEPTION;
    }

    qwrt_cb_data_t *cbd = alloc_cb_data(cctx, resolving_funcs[0], resolving_funcs[1], rt);
    if (!cbd) {
        JS_FreeValue(ctx, resolving_funcs[0]);
        JS_FreeValue(ctx, resolving_funcs[1]);
        JS_FreeCString(ctx, path);
        return JS_ThrowOutOfMemory(ctx);
    }

    uv_io_fs_list(rt, path, bridge_io_done, cbd);

    JS_FreeCString(ctx, path);
    return promise;
}

static JSValue js_pal_storage_get(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    QWRT_UNUSED(this_val);
    qwrt_t *rt = qwrt_get_rt_from_ctx(ctx);
    if (!rt) {
        return JS_ThrowTypeError(ctx, "pal.storage_get not available");
    }
    qwrt_ctx_t *cctx = get_ctx_from_jsctx(rt, ctx);
    if (!cctx) {
        return JS_ThrowTypeError(ctx, "pal.storage_get not available");
    }
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "storage_get requires key argument");
    }

    const char *key = JS_ToCString(ctx, argv[0]);
    if (!key) {
        return JS_EXCEPTION;
    }

    JSValue resolving_funcs[2];
    JSValue promise = JS_NewPromiseCapability(ctx, resolving_funcs);
    if (JS_IsException(promise)) {
        JS_FreeCString(ctx, key);
        return JS_EXCEPTION;
    }

    qwrt_cb_data_t *cbd = alloc_cb_data(cctx, resolving_funcs[0], resolving_funcs[1], rt);
    if (!cbd) {
        JS_FreeValue(ctx, resolving_funcs[0]);
        JS_FreeValue(ctx, resolving_funcs[1]);
        JS_FreeCString(ctx, key);
        return JS_ThrowOutOfMemory(ctx);
    }

    uv_io_storage_get(rt, key, storage_get_done, cbd);

    JS_FreeCString(ctx, key);
    return promise;
}

static JSValue js_pal_storage_set(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    QWRT_UNUSED(this_val);
    qwrt_t *rt = qwrt_get_rt_from_ctx(ctx);
    if (!rt) {
        return JS_ThrowTypeError(ctx, "pal.storage_set not available");
    }
    qwrt_ctx_t *cctx = get_ctx_from_jsctx(rt, ctx);
    if (!cctx) {
        return JS_ThrowTypeError(ctx, "pal.storage_set not available");
    }
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "storage_set requires key and value arguments");
    }

    const char *key = JS_ToCString(ctx, argv[0]);
    if (!key) {
        return JS_EXCEPTION;
    }

    const char *value = NULL;
    size_t value_len = 0;
    if (!JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1])) {
        value = JS_ToCStringLen(ctx, &value_len, argv[1]);
        if (!value) {
            JS_FreeCString(ctx, key);
            return JS_EXCEPTION;
        }
    }

    JSValue resolving_funcs[2];
    JSValue promise = JS_NewPromiseCapability(ctx, resolving_funcs);
    if (JS_IsException(promise)) {
        JS_FreeCString(ctx, key);
        if (value) JS_FreeCString(ctx, value);
        return JS_EXCEPTION;
    }

    qwrt_cb_data_t *cbd = alloc_cb_data(cctx, resolving_funcs[0], resolving_funcs[1], rt);
    if (!cbd) {
        JS_FreeValue(ctx, resolving_funcs[0]);
        JS_FreeValue(ctx, resolving_funcs[1]);
        JS_FreeCString(ctx, key);
        if (value) JS_FreeCString(ctx, value);
        return JS_ThrowOutOfMemory(ctx);
    }

    uv_io_storage_set(rt, key, value ? value : "", value ? value_len : 0,
                      bridge_io_done, cbd);

    JS_FreeCString(ctx, key);
    if (value) JS_FreeCString(ctx, value);

    return promise;
}

static JSValue js_pal_storage_del(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    QWRT_UNUSED(this_val);
    qwrt_t *rt = qwrt_get_rt_from_ctx(ctx);
    if (!rt) {
        return JS_ThrowTypeError(ctx, "pal.storage_del not available");
    }
    qwrt_ctx_t *cctx = get_ctx_from_jsctx(rt, ctx);
    if (!cctx) {
        return JS_ThrowTypeError(ctx, "pal.storage_del not available");
    }
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "storage_del requires key argument");
    }

    const char *key = JS_ToCString(ctx, argv[0]);
    if (!key) {
        return JS_EXCEPTION;
    }

    JSValue resolving_funcs[2];
    JSValue promise = JS_NewPromiseCapability(ctx, resolving_funcs);
    if (JS_IsException(promise)) {
        JS_FreeCString(ctx, key);
        return JS_EXCEPTION;
    }

    qwrt_cb_data_t *cbd = alloc_cb_data(cctx, resolving_funcs[0], resolving_funcs[1], rt);
    if (!cbd) {
        JS_FreeValue(ctx, resolving_funcs[0]);
        JS_FreeValue(ctx, resolving_funcs[1]);
        JS_FreeCString(ctx, key);
        return JS_ThrowOutOfMemory(ctx);
    }

    uv_io_storage_del(rt, key, bridge_io_done, cbd);

    JS_FreeCString(ctx, key);
    return promise;
}

/* ================================================================
 * randomBytes(len) — synchronous CSPRNG
 * Returns an ArrayBuffer filled with len random bytes.
 * ================================================================ */

static JSValue js_pal_random_bytes(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    QWRT_UNUSED(this_val);

    int64_t len = 0;
    if (argc >= 1 && JS_ToInt64(ctx, &len, argv[0])) {
        return JS_EXCEPTION;
    }

    /* Bridge does only JSValue↔C conversion + the byte fill. Input
     * validation (positive length, the 65536-byte cap from the Web Crypto
     * spec) lives in the JS polyfill, not here. */
    if (len <= 0) {
        return JS_ThrowRangeError(ctx, "randomBytes: length must be positive");
    }

    size_t ulen = (size_t)len;
    uint8_t *buf = js_malloc(ctx, ulen);
    if (!buf) {
        return JS_ThrowOutOfMemory(ctx);
    }

    /* Inline: fill from /dev/urandom (no PAL backend in this model) */
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f) {
        js_free(ctx, buf);
        return JS_ThrowTypeError(ctx, "randomBytes: /dev/urandom unavailable");
    }
    size_t got = fread(buf, 1, ulen, f);
    fclose(f);
    if (got != ulen) {
        js_free(ctx, buf);
        return JS_ThrowTypeError(ctx, "randomBytes: short read");
    }

    /* Copy into ArrayBuffer (QuickJS owns the copy) */
    JSValue ab = JS_NewArrayBufferCopy(ctx, buf, ulen);
    js_free(ctx, buf);
    return ab;
}

/* ================================================================
 * MessagePort transfer — global port id allocator (Task: transferable)
 * ================================================================ */

/* 全局递增 port id 池（跨线程原子分配；0 保留给无效 id）。每个
 * MessageChannel 分配一对连续 id（id1=port1, id2=port2，纠缠对）。
 * id 只用于跨线程路由标记，具体路由在 JS 层 polyfill 完成。 */
static _Atomic uint32_t g_qwrt_next_port_id = 1;

/* portCreate() -> {id1, id2}：分配一对全局唯一纠缠 port id。 */
static JSValue js_pal_port_create(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv)
{
    QWRT_UNUSED(this_val); QWRT_UNUSED(argc); QWRT_UNUSED(argv);
    uint32_t id1 = __atomic_fetch_add(&g_qwrt_next_port_id, 1, __ATOMIC_RELAXED) + 1;
    uint32_t id2 = __atomic_fetch_add(&g_qwrt_next_port_id, 1, __ATOMIC_RELAXED) + 1;
    JSValue obj = JS_NewObject(ctx);
    if (JS_IsException(obj)) return JS_EXCEPTION;
    JS_SetPropertyStr(ctx, obj, "id1", JS_NewUint32(ctx, id1));
    JS_SetPropertyStr(ctx, obj, "id2", JS_NewUint32(ctx, id2));
    return obj;
}

/* ================================================================
 * Host message boundary
 * ================================================================ */

static JSValue js_pal_post_message(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv)
{
    QWRT_UNUSED(this_val);
    qwrt_t *rt = qwrt_get_rt_from_ctx(ctx);
    if (!rt) return JS_EXCEPTION;
    if (argc < 1) return JS_UNDEFINED;
    if (!rt->config.message_cb) return JS_UNDEFINED;

    /* JSON 序列化 JS 值 → C 字符串，再交给宿主（plan §7：JS_JSONStringify
     * → message_cb；data 需 JSON 可序列化）。 */
    JSValue str = JS_JSONStringify(ctx, argv[0], JS_UNDEFINED, JS_UNDEFINED);
    if (JS_IsException(str)) return JS_EXCEPTION;
    size_t len = 0;
    const char *json = JS_ToCStringLen(ctx, &len, str);
    if (!json) {
        JS_FreeValue(ctx, str);
        return JS_EXCEPTION;
    }

    rt->config.message_cb(rt, json, len, rt->host_data);
    JS_FreeCString(ctx, json);
    JS_FreeValue(ctx, str);
    return JS_UNDEFINED;
}

/* ================================================================
 * Web Worker (Task 4) — parent/worker pal primitives
 * ================================================================ */

/* 同步整文件读取（worker.js 的 new Worker(file://) 需要同步加载脚本；
 * polyfill 的 fs.js readFileSync 不可用，故补这一个同步原语）。 */
static JSValue js_pal_fs_read_sync(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv)
{
    QWRT_UNUSED(this_val);
    if (argc < 1) return JS_EXCEPTION;
    const char *path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_EXCEPTION;
    /* Same ".." traversal guard as the async fs ops — sync reads (worker
     * script loader) must not bypass it. */
    if (!bridge_validate_path(path)) {
        JS_FreeCString(ctx, path);
        return JS_ThrowTypeError(ctx, "Path traversal detected");
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        JSValue e = JS_ThrowTypeError(ctx, "fsReadSync: cannot open %s", path);
        JS_FreeCString(ctx, path);
        return e;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        JS_FreeCString(ctx, path);
        return JS_ThrowTypeError(ctx, "fsReadSync: seek failed");
    }
    long sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        JS_FreeCString(ctx, path);
        return JS_ThrowTypeError(ctx, "fsReadSync: ftell failed");
    }
    rewind(f);
    char *buf = (char *)js_malloc(ctx, (size_t)sz + 1);
    if (!buf) {
        fclose(f);
        JS_FreeCString(ctx, path);
        return JS_EXCEPTION;
    }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[got] = '\0';
    JS_FreeCString(ctx, path);
    JSValue ret = JS_NewStringLen(ctx, buf, got);
    js_free(ctx, buf);
    return ret;
}

/* 父侧 pal.spawnWorker：脚本字符串 → 阻塞创建 worker 线程，返回 worker id */
static JSValue js_pal_spawn_worker(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv)
{
    QWRT_UNUSED(this_val);
    qwrt_t *rt = qwrt_get_rt_from_ctx(ctx);
    if (!rt) return JS_EXCEPTION;
    if (argc < 1) return JS_EXCEPTION;
    const char *script = JS_ToCString(ctx, argv[0]);
    if (!script) return JS_EXCEPTION;

    int err = 0;
    qwrt_worker_t *w = qwrt_worker_create(rt, script, &err);
    JS_FreeCString(ctx, script);
    if (!w) {
        return JS_ThrowTypeError(ctx, "spawnWorker failed (err %d)", err);
    }
    return JS_NewInt32(ctx, w->id);
}

/* 父侧 pal.workerPost：结构化克隆字节 → worker 入站队列 */
static JSValue js_pal_worker_post(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv)
{
    QWRT_UNUSED(this_val);
    qwrt_t *rt = qwrt_get_rt_from_ctx(ctx);
    if (!rt) return JS_EXCEPTION;
    if (argc < 2) return JS_EXCEPTION;
    int32_t id;
    if (JS_ToInt32(ctx, &id, argv[0]) != 0) return JS_EXCEPTION;
    size_t len = 0;
    const uint8_t *bytes = JS_GetUint8Array(ctx, &len, argv[1]);
    if (!bytes) bytes = JS_GetArrayBuffer(ctx, &len, argv[1]);
    if (!bytes) return JS_ThrowTypeError(ctx, "workerPost: expected bytes");

    qwrt_worker_t *w = qwrt_worker_get(rt, id);
    if (!w) return JS_ThrowTypeError(ctx, "workerPost: no worker %d", id);
    qwrt_worker_post(rt, w, bytes, len);
    return JS_UNDEFINED;
}

/* 父侧 pal.workerTerminate：请求 worker 退出（异步，父 teardown 时 join） */
static JSValue js_pal_worker_terminate(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv)
{
    QWRT_UNUSED(this_val);
    qwrt_t *rt = qwrt_get_rt_from_ctx(ctx);
    if (!rt) return JS_EXCEPTION;
    if (argc < 1) return JS_EXCEPTION;
    int32_t id;
    if (JS_ToInt32(ctx, &id, argv[0]) != 0) return JS_EXCEPTION;
    qwrt_worker_t *w = qwrt_worker_get(rt, id);
    if (!w) return JS_UNDEFINED;   /* 已终止/不存在：幂等 */
    qwrt_worker_terminate(rt, w);
    return JS_UNDEFINED;
}

/* Worker 侧 pal.postMessage：克隆字节 → 父入站队列（source=worker id） */
static JSValue js_pal_worker_emit(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv)
{
    QWRT_UNUSED(this_val);
    qwrt_t *rt = qwrt_get_rt_from_ctx(ctx);
    if (!rt) return JS_EXCEPTION;
    if (argc < 1) return JS_EXCEPTION;
    size_t len = 0;
    const uint8_t *bytes = JS_GetUint8Array(ctx, &len, argv[0]);
    if (!bytes) bytes = JS_GetArrayBuffer(ctx, &len, argv[0]);
    if (!bytes) return JS_ThrowTypeError(ctx, "worker postMessage: expected bytes");

    qwrt_worker_t *w = (qwrt_worker_t *)rt->worker_self;
    if (!w || !w->parent) return JS_UNDEFINED;
    qwrt_msg_push(w->parent, (const char *)bytes, len, w->id);
    return JS_UNDEFINED;
}

/* Worker 侧 pal.workerClose：请求终止自身（不 join；父 teardown 时 join） */
static JSValue js_pal_worker_close(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv)
{
    QWRT_UNUSED(this_val); QWRT_UNUSED(argc); QWRT_UNUSED(argv);
    qwrt_t *rt = qwrt_get_rt_from_ctx(ctx);
    if (!rt) return JS_EXCEPTION;
    qwrt_worker_t *w = (qwrt_worker_t *)rt->worker_self;
    if (w && w->parent) qwrt_worker_terminate(w->parent, w);
    return JS_UNDEFINED;
}

/* Worker 侧 pal.workerId：返回自身 worker id（>0）。worker 把 MessagePort
 * transfer 给父线程时，父侧需要真实 workerId 才能经 pal.workerPost 把消息
 * 路由回留在 worker 的对端 port——故此处必须暴露，不能用 'parent' 占位。 */
static JSValue js_pal_worker_id(JSContext *ctx, JSValueConst this_val,
                                int argc, JSValueConst *argv)
{
    QWRT_UNUSED(this_val); QWRT_UNUSED(argc); QWRT_UNUSED(argv);
    qwrt_t *rt = qwrt_get_rt_from_ctx(ctx);
    if (!rt) return JS_EXCEPTION;
    qwrt_worker_t *w = (qwrt_worker_t *)rt->worker_self;
    if (!w) return JS_NewInt32(ctx, 0);
    return JS_NewInt32(ctx, w->id);
}

/* ================================================================
 * Multi-context JS API (Task 5) — 父 runtime 上运行，宿主只见主 context。
 * spawn/suspend/resume/destroy 全部由 qwrtContext（context.js）经这里驱动；
 * C 只做边界转换 + 调用 context.c 的辅助函数，序列化逻辑在 JS 侧。
 * ================================================================ */

/* pal.contextSpawn(initScript)：新子 context，返回 ctx id */
static JSValue js_pal_context_spawn(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv)
{
    QWRT_UNUSED(this_val);
    qwrt_t *rt = qwrt_get_rt_from_ctx(ctx);
    if (!rt) return JS_EXCEPTION;
    if (argc < 1) return JS_EXCEPTION;
    const char *script = JS_ToCString(ctx, argv[0]);
    if (!script) return JS_EXCEPTION;
    int id = qwrt_ctx_spawn(rt, script);
    JS_FreeCString(ctx, script);
    if (id < 0) return JS_ThrowTypeError(ctx, "contextSpawn failed (err %d)", id);
    return JS_NewInt32(ctx, id);
}

/* pal.contextSuspend(ctxId, statePath)：挂起 = 序列化全局克隆字节写盘 + 销毁 ctx */
static JSValue js_pal_context_suspend(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv)
{
    QWRT_UNUSED(this_val);
    qwrt_t *rt = qwrt_get_rt_from_ctx(ctx);
    if (!rt) return JS_EXCEPTION;
    if (argc < 2) return JS_EXCEPTION;
    int32_t id;
    if (JS_ToInt32(ctx, &id, argv[0]) != 0) return JS_EXCEPTION;
    const char *path = JS_ToCString(ctx, argv[1]);
    if (!path) return JS_EXCEPTION;
    int rc = qwrt_ctx_serialize(rt, id, path);
    JS_FreeCString(ctx, path);
    if (rc != QWRT_OK) return JS_ThrowTypeError(ctx, "contextSuspend failed (err %d)", rc);
    return JS_UNDEFINED;
}

/* pal.contextResume(ctxId, scriptRef, statePath)：在原槽位重建 ctx + 恢复状态 */
static JSValue js_pal_context_resume(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv)
{
    QWRT_UNUSED(this_val);
    qwrt_t *rt = qwrt_get_rt_from_ctx(ctx);
    if (!rt) return JS_EXCEPTION;
    if (argc < 3) return JS_EXCEPTION;
    int32_t id;
    if (JS_ToInt32(ctx, &id, argv[0]) != 0) return JS_EXCEPTION;
    const char *script = JS_ToCString(ctx, argv[1]);
    if (!script) return JS_EXCEPTION;
    const char *path = JS_ToCString(ctx, argv[2]);
    if (!path) { JS_FreeCString(ctx, script); return JS_EXCEPTION; }
    int rc = qwrt_ctx_rebuild(rt, id, script, path);
    JS_FreeCString(ctx, script);
    JS_FreeCString(ctx, path);
    if (rc < 0) return JS_ThrowTypeError(ctx, "contextResume failed (err %d)", rc);
    return JS_NewInt32(ctx, rc);
}

/* pal.contextDestroy(ctxId)：销毁子 context（active 的返回 BUSY） */
static JSValue js_pal_context_destroy(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv)
{
    QWRT_UNUSED(this_val);
    qwrt_t *rt = qwrt_get_rt_from_ctx(ctx);
    if (!rt) return JS_EXCEPTION;
    if (argc < 1) return JS_EXCEPTION;
    int32_t id;
    if (JS_ToInt32(ctx, &id, argv[0]) != 0) return JS_EXCEPTION;
    int rc = qwrt_ctx_destroy_id(rt, id);
    if (rc != QWRT_OK) return JS_ThrowTypeError(ctx, "contextDestroy failed (err %d)", rc);
    return JS_UNDEFINED;
}

/* 主 context 入站派发：宿主 JSON 已解析成值；source0=host。 */
void qwrt_dispatch_message(qwrt_t *rt, qwrt_msg_t *m)
{
    /* 主 context（第一个 context）上找 __qwrt_dispatch__ 并调用 */
    qwrt_ctx_t *cctx = rt->contexts[0];
    if (!cctx || !cctx->jsctx) return;
    JSContext *ctx = cctx->jsctx;
    JSValue g = JS_GetGlobalObject(ctx);
    JSValue fn = JS_GetPropertyStr(ctx, g, "__qwrt_dispatch__");
    JS_FreeValue(ctx, g);
    if (JS_IsFunction(ctx, fn)) {
        JSValue src = JS_NewInt32(ctx, m->source);
        JSValue data;
        if (m->source == QWRT_MSG_SRC_HOST) {
            /* msgq 保证 m->data 以 '\0' 结尾（data[len]=='\0'），可直接喂
             * JS_ParseJSON（quickjs-ng 无 JS_JSONParse）。 */
            data = JS_ParseJSON(ctx, m->data, m->len, "<qwrt-msg>");
            if (JS_IsException(data)) {
                /* spec §5: bad JSON → error envelope */
                JS_FreeValue(ctx, data);
                JS_FreeValue(ctx, fn);
                if (rt->config.message_cb) {
                    static const char *bad = "{\"type\":\"error\",\"error\":\"bad-json\"}";
                    rt->config.message_cb(rt, bad, strlen(bad), rt->host_data);
                }
                return;
            }
        } else {
            data = JS_NewArrayBufferCopy(ctx, (const uint8_t *)m->data, m->len);
        }
        JSValue args[2] = { data, src };
        JSValue r = JS_Call(ctx, fn, JS_UNDEFINED, 2, args);
        JS_FreeValue(ctx, r);
        JS_FreeValue(ctx, data);
        JS_FreeValue(ctx, src);
    }
    JS_FreeValue(ctx, fn);
}

/* ================================================================
 * Create the internal pal JS object (per-context version)
 * ================================================================ */

JSValue qwrt_create_pal_object_ctx(qwrt_t *rt, qwrt_ctx_t *ctx)
{
    JSContext *jsctx = ctx->jsctx;
    JSValue pal = JS_NewObject(jsctx);
    if (JS_IsException(pal)) {
        return JS_EXCEPTION;
    }

    /* Sync functions */
    JS_SetPropertyStr(jsctx, pal, "timeNow", JS_NewCFunction(jsctx, js_pal_time_now, "timeNow", 0));
    JS_SetPropertyStr(jsctx, pal, "hrtime", JS_NewCFunction(jsctx, js_pal_hrtime, "hrtime", 0));
    JS_SetPropertyStr(jsctx, pal, "log", JS_NewCFunction(jsctx, js_pal_log, "log", 2));
    JS_SetPropertyStr(jsctx, pal, "timerStop", JS_NewCFunction(jsctx, js_pal_timer_stop, "timerStop", 1));

    /* Timer start (returns {handle, promise}) */
    JS_SetPropertyStr(jsctx, pal, "timerStart", JS_NewCFunction(jsctx, js_pal_timer_start, "timerStart", 2));

    /* Async functions (return Promises) — stubs until Task 3 */
    JS_SetPropertyStr(jsctx, pal, "httpRequest", JS_NewCFunction(jsctx, js_pal_http_request, "httpRequest", 4));
    JS_SetPropertyStr(jsctx, pal, "httpRequestStream", JS_NewCFunction(jsctx, js_pal_http_request_stream, "httpRequestStream", 7));
    JS_SetPropertyStr(jsctx, pal, "fsRead", JS_NewCFunction(jsctx, js_pal_fs_read, "fsRead", 1));
    JS_SetPropertyStr(jsctx, pal, "fsReadSync", JS_NewCFunction(jsctx, js_pal_fs_read_sync, "fsReadSync", 1));
    JS_SetPropertyStr(jsctx, pal, "fsWrite", JS_NewCFunction(jsctx, js_pal_fs_write, "fsWrite", 2));
    JS_SetPropertyStr(jsctx, pal, "fsExists", JS_NewCFunction(jsctx, js_pal_fs_exists, "fsExists", 1));
    JS_SetPropertyStr(jsctx, pal, "fsRemove", JS_NewCFunction(jsctx, js_pal_fs_remove, "fsRemove", 1));
    JS_SetPropertyStr(jsctx, pal, "fsList", JS_NewCFunction(jsctx, js_pal_fs_list, "fsList", 1));
    JS_SetPropertyStr(jsctx, pal, "storageGet", JS_NewCFunction(jsctx, js_pal_storage_get, "storageGet", 1));
    JS_SetPropertyStr(jsctx, pal, "storageSet", JS_NewCFunction(jsctx, js_pal_storage_set, "storageSet", 2));
    JS_SetPropertyStr(jsctx, pal, "storageDel", JS_NewCFunction(jsctx, js_pal_storage_del, "storageDel", 1));

    /* Sync CSPRNG */
    JS_SetPropertyStr(jsctx, pal, "randomBytes", JS_NewCFunction(jsctx, js_pal_random_bytes, "randomBytes", 1));

    /* MessagePort transfer: 全局唯一 port id 对分配（worker/父都可用） */
    JS_SetPropertyStr(jsctx, pal, "portCreate", JS_NewCFunction(jsctx, js_pal_port_create, "portCreate", 0));

    /* Host message boundary / Web Worker (Task 4).
     * worker runtime（rt->worker_self 非 NULL）：postMessage → 父入站（克隆
     * 字节），另有 workerClose；无宿主 message_cb。父 runtime：postMessage →
     * 宿主 JSON，另有 spawnWorker / workerPost / workerTerminate。 */
    if (rt->worker_self) {
        JS_SetPropertyStr(jsctx, pal, "postMessage", JS_NewCFunction(jsctx, js_pal_worker_emit, "postMessage", 1));
        JS_SetPropertyStr(jsctx, pal, "workerClose", JS_NewCFunction(jsctx, js_pal_worker_close, "workerClose", 0));
        JS_SetPropertyStr(jsctx, pal, "workerId", JS_NewCFunction(jsctx, js_pal_worker_id, "workerId", 0));
    } else {
        JS_SetPropertyStr(jsctx, pal, "postMessage", JS_NewCFunction(jsctx, js_pal_post_message, "postMessage", 1));
        JS_SetPropertyStr(jsctx, pal, "spawnWorker", JS_NewCFunction(jsctx, js_pal_spawn_worker, "spawnWorker", 1));
        JS_SetPropertyStr(jsctx, pal, "workerPost", JS_NewCFunction(jsctx, js_pal_worker_post, "workerPost", 2));
        JS_SetPropertyStr(jsctx, pal, "workerTerminate", JS_NewCFunction(jsctx, js_pal_worker_terminate, "workerTerminate", 1));
        /* Multi-context (Task 5) — 父 runtime 专属 */
        JS_SetPropertyStr(jsctx, pal, "contextSpawn", JS_NewCFunction(jsctx, js_pal_context_spawn, "contextSpawn", 1));
        JS_SetPropertyStr(jsctx, pal, "contextSuspend", JS_NewCFunction(jsctx, js_pal_context_suspend, "contextSuspend", 2));
        JS_SetPropertyStr(jsctx, pal, "contextResume", JS_NewCFunction(jsctx, js_pal_context_resume, "contextResume", 3));
        JS_SetPropertyStr(jsctx, pal, "contextDestroy", JS_NewCFunction(jsctx, js_pal_context_destroy, "contextDestroy", 1));
    }

    return pal;
}

/* ================================================================
 * Inject polyfill via __native_inject__ temp global (per-context version)
 * ================================================================ */

int qwrt_inject_polyfill_ctx(qwrt_t *rt, qwrt_ctx_t *ctx, const uint8_t *code, size_t code_len)
{
    JSContext *jsctx = ctx->jsctx;
    JSValue global = JS_GetGlobalObject(jsctx);

    /* Create the pal object */
    JSValue pal = qwrt_create_pal_object_ctx(rt, ctx);
    if (JS_IsException(pal)) {
        JS_FreeValue(jsctx, global);
        return -1;
    }

    /* Set __native_inject__ as a global temp var */
    JS_SetPropertyStr(jsctx, global, "__native_inject__", pal);

    JS_FreeValue(jsctx, global);

    /* Evaluate the polyfill code.
     * Expected format: (function(pal){ ... })(__native_inject__);
     */
    JSValue result = JS_Eval(jsctx, (const char *)code, code_len, "<polyfill>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result)) {
        JSValue exc = JS_GetException(jsctx);
        if (rt->debug) {
            const char *err_str = JS_ToCString(jsctx, exc);
            if (err_str) {
                fprintf(stderr, "[qwrt] polyfill eval error: %s\n", err_str);
                JS_FreeCString(jsctx, err_str);
            }
        }
        JS_FreeValue(jsctx, exc);
        return -1;
    }
    JS_FreeValue(jsctx, result);

    /* Post-polyfill fixup: set ReadableStream[Symbol.asyncIterator]
     * if not already set. The polyfill may not set it if Symbol.asyncIterator
     * was not available during evaluation (QuickJS IIFE loading order). */
    {
        JSValue global2 = JS_GetGlobalObject(jsctx);
        JSValue rs_ctor = JS_GetPropertyStr(jsctx, global2, "ReadableStream");
        JS_FreeValue(jsctx, global2);
        if (JS_IsFunction(jsctx, rs_ctor)) {
            /* Set asyncIterator on ReadableStream.prototype if not already set.
             * Using JS_Eval since Symbol.asyncIterator is awkward to construct from C.
             * The `if` guard prevents overwriting if the polyfill already set it. */
            const char *iter_code =
                "if(!ReadableStream.prototype[Symbol.asyncIterator])"
                "ReadableStream.prototype[Symbol.asyncIterator]=function(){"
                "var r=this.getReader();"
                "return{next:function(){return r.read();},"
                "return:function(v){r.releaseLock();return{done:true,value:v};}};"
                "}";
            JSValue iter_result = JS_Eval(jsctx, iter_code, strlen(iter_code),
                                            "<asyncIterator>", JS_EVAL_TYPE_GLOBAL);
            JS_FreeValue(jsctx, iter_result);
        }
        JS_FreeValue(jsctx, rs_ctor);
    }

    /* After polyfill runs, move __native_inject__ to __native__ so extension init
     * hooks can register functions on the same pal object that the polyfill's
     * closures reference. */
    global = JS_GetGlobalObject(jsctx);
    JSAtom inject_atom = JS_NewAtom(jsctx, "__native_inject__");
    JSValue pal_ref = JS_GetProperty(jsctx, global, inject_atom);
    JS_DeleteProperty(jsctx, global, inject_atom, 0);
    JS_FreeAtom(jsctx, inject_atom);
    if (!JS_IsUndefined(pal_ref) && !JS_IsException(pal_ref)) {
        JS_SetPropertyStr(jsctx, global, "__native__", pal_ref);
    } else {
        JS_FreeValue(jsctx, pal_ref);
    }
    JS_FreeValue(jsctx, global);

    return 0;
}

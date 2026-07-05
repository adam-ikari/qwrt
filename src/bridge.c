/*
 * qwrt C Bridge Layer
 *
 * Creates internal 'pal' JS object with PAL primitives.
 * Converts PAL async callbacks to JS Promises.
 * All PAL bridge functions use per-context data (qwrt_ctx_t).
 */

#include "qwrt_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* ================================================================
 * Forward declarations
 * ================================================================ */

static void pal_async_cb(void *user_data, int status, const char *data, size_t data_len);
static void pal_timer_cb(void *user_data, int status, const char *data, size_t data_len);
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

/* ================================================================
 * Helper: get qwrt_t from JSContext
 * ================================================================ */

static qwrt_t *get_rt_from_ctx(JSContext *ctx)
{
    JSRuntime *jsrt = JS_GetRuntime(ctx);
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

static pal_cb_data_t *alloc_cb_data(JSContext *ctx, JSValue resolve, JSValue reject, qwrt_t *rt)
{
    pal_cb_data_t *cbd = (pal_cb_data_t *)js_malloc(ctx, sizeof(pal_cb_data_t));
    if (!cbd) {
        return NULL;
    }
    cbd->ctx = ctx;
    cbd->resolve = resolve;  /* takes ownership */
    cbd->reject = reject;    /* takes ownership */
    cbd->rt = rt;
    cbd->is_timer = 0;
    cbd->handle_idx = -1;
    return cbd;
}

/* ================================================================
 * Free callback data — shared with qwrt.c for cleanup
 * ================================================================ */

void qwrt_free_cb_data(JSContext *ctx, void *cbd_)
{
    if (!cbd_) {
        return;
    }
    pal_cb_data_t *cbd = (pal_cb_data_t *)cbd_;
    JS_FreeValue(ctx, cbd->resolve);
    JS_FreeValue(ctx, cbd->reject);
    js_free(ctx, cbd);
}

/* ================================================================
 * Streaming HTTP bridge context and callbacks
 * ================================================================ */

typedef struct {
    JSContext *ctx;
    qwrt_t *rt;
    JSValue on_headers;
    JSValue on_data;
    JSValue on_end;
} stream_bridge_ctx_t;

/* Deferred callback wrappers — executed from qwrt_tick's stack frame
 * so JS_Call happens in a valid QuickJS context. */

typedef struct {
    JSContext *ctx;
    JSValue fn;
    int status;
    char *headers_json;
} deferred_headers_t;

static void deferred_on_headers(void *data)
{
    deferred_headers_t *dh = (deferred_headers_t *)data;
    if (JS_IsFunction(dh->ctx, dh->fn)) {
        JSValue args[2] = {
            JS_NewInt32(dh->ctx, dh->status),
            JS_NewString(dh->ctx, dh->headers_json ? dh->headers_json : "{}")
        };
        JSValue ret = JS_Call(dh->ctx, dh->fn, JS_UNDEFINED, 2, args);
        JS_FreeValue(dh->ctx, ret);
        JS_FreeValue(dh->ctx, args[0]);
        JS_FreeValue(dh->ctx, args[1]);
    }
    free(dh->headers_json);
    free(dh);
}

static void stream_on_headers(void *ud, int status, const char *headers_json)
{
    stream_bridge_ctx_t *sbctx = (stream_bridge_ctx_t *)ud;
    deferred_headers_t *dh = malloc(sizeof(*dh));
    if (!dh) return;
    dh->ctx = sbctx->ctx;
    dh->fn = sbctx->on_headers;  /* JSValue copied — caller keeps ownership */
    dh->status = status;
    dh->headers_json = headers_json ? strdup(headers_json) : NULL;
    qwrt_defer_callback(sbctx->rt, deferred_on_headers, dh);
}

typedef struct {
    JSContext *ctx;
    JSValue fn;
    uint8_t *data;
    size_t len;
} deferred_data_t;

static void deferred_on_data(void *data)
{
    deferred_data_t *dd = (deferred_data_t *)data;
    if (JS_IsFunction(dd->ctx, dd->fn)) {
        JSValue buf = JS_NewArrayBufferCopy(dd->ctx, dd->data, dd->len);
        JSValue ret = JS_Call(dd->ctx, dd->fn, JS_UNDEFINED, 1, &buf);
        JS_FreeValue(dd->ctx, ret);
        JS_FreeValue(dd->ctx, buf);
    }
    free(dd->data);
    free(dd);
}

static void stream_on_data(void *ud, const char *data, size_t len)
{
    stream_bridge_ctx_t *sbctx = (stream_bridge_ctx_t *)ud;
    deferred_data_t *dd = malloc(sizeof(*dd));
    if (!dd) return;
    dd->ctx = sbctx->ctx;
    dd->fn = sbctx->on_data;
    dd->data = malloc(len);
    if (!dd->data) { free(dd); return; }
    memcpy(dd->data, data, len);
    dd->len = len;
    qwrt_defer_callback(sbctx->rt, deferred_on_data, dd);
}

typedef struct {
    JSContext *ctx;
    JSValue fn;
    JSValue on_headers;  /* to free */
    JSValue on_data;     /* to free */
    stream_bridge_ctx_t *sbctx;  /* to free */
    int error_status;
} deferred_end_t;

static void deferred_on_end(void *data)
{
    deferred_end_t *de = (deferred_end_t *)data;
    if (JS_IsFunction(de->ctx, de->fn)) {
        JSValue arg = JS_NewInt32(de->ctx, de->error_status);
        JSValue ret = JS_Call(de->ctx, de->fn, JS_UNDEFINED, 1, &arg);
        JS_FreeValue(de->ctx, ret);
        JS_FreeValue(de->ctx, arg);
    }
    JS_FreeValue(de->ctx, de->on_headers);
    JS_FreeValue(de->ctx, de->on_data);
    JS_FreeValue(de->ctx, de->fn);  /* on_end */
    free(de->sbctx);
    free(de);
}

static void stream_on_end(void *ud, int error_status)
{
    stream_bridge_ctx_t *sbctx = (stream_bridge_ctx_t *)ud;
    deferred_end_t *de = malloc(sizeof(*de));
    if (!de) {
        /* Can't defer — free directly (no JS calls needed for cleanup) */
        JS_FreeValue(sbctx->ctx, sbctx->on_headers);
        JS_FreeValue(sbctx->ctx, sbctx->on_data);
        JS_FreeValue(sbctx->ctx, sbctx->on_end);
        free(sbctx);
        return;
    }
    de->ctx = sbctx->ctx;
    de->fn = sbctx->on_end;
    de->on_headers = sbctx->on_headers;
    de->on_data = sbctx->on_data;
    de->sbctx = sbctx;
    de->error_status = error_status;
    qwrt_defer_callback(sbctx->rt, deferred_on_end, de);
}

/* ================================================================
 * Unified async callback for all PAL async operations
 * ================================================================ */

/* Deferred callback for non-streaming PAL operations.
 * Executed from qwrt_tick's stack frame so JS_Call on Promise
 * resolve/reject works correctly. */

typedef struct {
    JSContext *ctx;
    qwrt_t *rt;
    JSValue resolve;
    JSValue reject;
    int status;
    char *data;
    size_t data_len;
} deferred_pal_cb_t;

static void deferred_pal_cb(void *udata)
{
    deferred_pal_cb_t *dcb = (deferred_pal_cb_t *)udata;
    JSValue result;

    if (dcb->status == 0) {
        if (dcb->data && dcb->data_len > 0) {
            result = JS_NewStringLen(dcb->ctx, dcb->data, dcb->data_len);
        } else if (dcb->data) {
            result = JS_NewStringLen(dcb->ctx, dcb->data, 0);
        } else {
            result = JS_UNDEFINED;
        }
        JS_Call(dcb->ctx, dcb->resolve, JS_UNDEFINED, 1, &result);
        JS_FreeValue(dcb->ctx, result);
    } else {
        if (dcb->data && dcb->data_len > 0) {
            result = JS_NewStringLen(dcb->ctx, dcb->data, dcb->data_len);
        } else {
            result = JS_NewString(dcb->ctx, "unknown error");
        }
        JS_Call(dcb->ctx, dcb->reject, JS_UNDEFINED, 1, &result);
        JS_FreeValue(dcb->ctx, result);
    }

    JS_FreeValue(dcb->ctx, dcb->resolve);
    JS_FreeValue(dcb->ctx, dcb->reject);
    free(dcb->data);
    free(dcb);
}

static void pal_async_cb(void *user_data, int status, const char *data, size_t data_len)
{
    pal_cb_data_t *cbd = (pal_cb_data_t *)user_data;

    deferred_pal_cb_t *dcb = malloc(sizeof(*dcb));
    if (!dcb) return;
    dcb->ctx = cbd->ctx;
    dcb->rt = cbd->rt;
    dcb->resolve = cbd->resolve;   /* transfer ownership */
    dcb->reject = cbd->reject;     /* transfer ownership */
    dcb->status = status;
    if (data && data_len > 0) {
        dcb->data = malloc(data_len + 1);
        if (dcb->data) {
            memcpy(dcb->data, data, data_len);
            dcb->data[data_len] = '\0';
        }
    } else {
        dcb->data = NULL;
    }
    dcb->data_len = data_len;

    qwrt_defer_callback(cbd->rt, deferred_pal_cb, dcb);

    /* Mark original cbd for cleanup — JSValues now owned by dcb */
    cbd->resolve = JS_UNDEFINED;
    cbd->reject = JS_UNDEFINED;
    js_free(cbd->ctx, cbd);
}

/* ================================================================
 * Sync primitives
 * ================================================================ */

static JSValue js_pal_time_now(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    QWRT_UNUSED(this_val); QWRT_UNUSED(argc); QWRT_UNUSED(argv);
    qwrt_t *rt = get_rt_from_ctx(ctx);
    if (!rt) {
        return JS_ThrowTypeError(ctx, "pal.time_now not available");
    }
    qwrt_ctx_t *cctx = get_ctx_from_jsctx(rt, ctx);
    if (!cctx || !cctx->pal || !cctx->pal->time_now) {
        return JS_ThrowTypeError(ctx, "pal.time_now not available");
    }
    uint64_t now = cctx->pal->time_now((qwrt_pal_t *)cctx->pal);
    return JS_NewFloat64(ctx, (double)now);
}

static JSValue js_pal_hrtime(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    QWRT_UNUSED(this_val); QWRT_UNUSED(argc); QWRT_UNUSED(argv);
    qwrt_t *rt = get_rt_from_ctx(ctx);
    if (!rt) {
        return JS_ThrowTypeError(ctx, "pal.hrtime not available");
    }
    qwrt_ctx_t *cctx = get_ctx_from_jsctx(rt, ctx);
    if (!cctx || !cctx->pal || !cctx->pal->hrtime) {
        return JS_ThrowTypeError(ctx, "pal.hrtime not available");
    }
    uint64_t ns = cctx->pal->hrtime((qwrt_pal_t *)cctx->pal);
    return JS_NewFloat64(ctx, (double)ns);
}

static JSValue js_pal_log(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    QWRT_UNUSED(this_val);
    qwrt_t *rt = get_rt_from_ctx(ctx);
    if (!rt) {
        return JS_ThrowTypeError(ctx, "pal.log not available");
    }
    qwrt_ctx_t *cctx = get_ctx_from_jsctx(rt, ctx);
    if (!cctx || !cctx->pal || !cctx->pal->log) {
        return JS_ThrowTypeError(ctx, "pal.log not available");
    }

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

    cctx->pal->log((qwrt_pal_t *)cctx->pal, level, msg);

    if (msg_needs_free) {
        JS_FreeCString(ctx, msg);
    }

    return JS_UNDEFINED;
}

static JSValue js_pal_timer_stop(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    QWRT_UNUSED(this_val);
    qwrt_t *rt = get_rt_from_ctx(ctx);
    if (!rt) {
        return JS_ThrowTypeError(ctx, "pal.timer_stop not available");
    }
    qwrt_ctx_t *cctx = get_ctx_from_jsctx(rt, ctx);
    if (!cctx || !cctx->pal || !cctx->pal->timer_stop) {
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

    void *handle = cctx->handles[handle_idx];
    if (handle) {
        cctx->pal->timer_stop((qwrt_pal_t *)cctx->pal, handle);
        cctx->handles[handle_idx] = NULL;

        /* Free the resolve function if it exists */
        if (!JS_IsUndefined(cctx->timer_resolves[handle_idx])) {
            JS_FreeValue(ctx, cctx->timer_resolves[handle_idx]);
            cctx->timer_resolves[handle_idx] = JS_UNDEFINED;
        }

        /* Free the callback data (PAL won't fire after stop) */
        if (cctx->timer_cbds[handle_idx]) {
            pal_cb_data_t *cbd = (pal_cb_data_t *)cctx->timer_cbds[handle_idx];
            JS_FreeValue(ctx, cbd->resolve);
            JS_FreeValue(ctx, cbd->reject);
            js_free(ctx, cbd);
            cctx->timer_cbds[handle_idx] = NULL;
        }
    }

    return JS_UNDEFINED;
}

/* ================================================================
 * Timer start - special case: returns handle synchronously,
 * callback resolves promise when timer fires
 * ================================================================ */

static void pal_timer_cb(void *user_data, int status, const char *data, size_t data_len)
{
    QWRT_UNUSED(data); QWRT_UNUSED(data_len);
    pal_cb_data_t *cbd = (pal_cb_data_t *)user_data;
    JSContext *jsctx = cbd->ctx;
    qwrt_t *rt = cbd->rt;
    JSValue result;

    /* Find the context for this JSContext */
    qwrt_ctx_t *cctx = get_ctx_from_jsctx(rt, jsctx);

    if (status == 0) {
        /* Timer fired successfully */
        result = JS_UNDEFINED;
        JS_Call(jsctx, cbd->resolve, JS_UNDEFINED, 1, &result);
        JS_FreeValue(jsctx, result);
    } else {
        /* Timer error (shouldn't normally happen) */
        result = JS_NewString(jsctx, "timer error");
        JS_Call(jsctx, cbd->reject, JS_UNDEFINED, 1, &result);
        JS_FreeValue(jsctx, result);
    }

    rt->has_pending_jobs = 1;

    /* For repeat timers, keep cbd alive for the next fire.
     * For one-shot timers, free everything. */
    if (!cbd->repeat) {
        /* Free the resolve/reject functions (our own copies) */
        JS_FreeValue(jsctx, cbd->resolve);
        JS_FreeValue(jsctx, cbd->reject);

        /* Clear the timer_resolves and timer_cbds slots since promise is now settled */
        if (cctx && cbd->handle_idx >= 0 && cbd->handle_idx < QWRT_MAX_HANDLES) {
            if (!JS_IsUndefined(cctx->timer_resolves[cbd->handle_idx])) {
                JS_FreeValue(jsctx, cctx->timer_resolves[cbd->handle_idx]);
                cctx->timer_resolves[cbd->handle_idx] = JS_UNDEFINED;
            }
            cctx->handles[cbd->handle_idx] = NULL;
            cctx->timer_cbds[cbd->handle_idx] = NULL;  /* cbd is being freed below */
        }

        js_free(jsctx, cbd);
    }
}

static JSValue js_pal_timer_start(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    QWRT_UNUSED(this_val);
    qwrt_t *rt = get_rt_from_ctx(ctx);
    if (!rt) {
        return JS_ThrowTypeError(ctx, "pal.timer_start not available");
    }
    qwrt_ctx_t *cctx = get_ctx_from_jsctx(rt, ctx);
    if (!cctx || !cctx->pal || !cctx->pal->timer_start) {
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
    pal_cb_data_t *cbd = alloc_cb_data(ctx, resolving_funcs[0], resolving_funcs[1], rt);
    if (!cbd) {
        JS_FreeValue(ctx, resolve_dup);
        JS_FreeValue(ctx, resolving_funcs[0]);
        JS_FreeValue(ctx, resolving_funcs[1]);
        return JS_ThrowOutOfMemory(ctx);
    }
    cbd->is_timer = 1;
    cbd->repeat = repeat;

    /* Start the timer */
    void *handle = cctx->pal->timer_start((qwrt_pal_t *)cctx->pal, (uint64_t)delay_ms, repeat,
                                         pal_timer_cb, cbd);
    if (!handle) {
        JS_FreeValue(ctx, resolve_dup);
        JS_FreeValue(ctx, cbd->resolve);
        JS_FreeValue(ctx, cbd->reject);
        js_free(ctx, cbd);
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
            cctx->pal->timer_stop((qwrt_pal_t *)cctx->pal, handle);
            JS_FreeValue(ctx, resolve_dup);
            JS_FreeValue(ctx, cbd->resolve);
            JS_FreeValue(ctx, cbd->reject);
            js_free(ctx, cbd);
            return JS_ThrowRangeError(ctx, "too many timers");
        }
        idx = cctx->handle_count;
        cctx->handle_count++;
    }

    cctx->handles[idx] = handle;
    cctx->timer_resolves[idx] = resolve_dup;
    cctx->timer_cbds[idx] = cbd;
    cbd->handle_idx = idx;

    /* Return {handle: number, promise: Promise} so polyfill can both
     * call timerStop(handle) and await the promise. */
    JSValue obj = JS_NewObject(ctx);
    if (JS_IsException(obj)) {
        cctx->pal->timer_stop((qwrt_pal_t *)cctx->pal, handle);
        cctx->handles[idx] = NULL;
        cctx->timer_resolves[idx] = JS_UNDEFINED;
        cctx->timer_cbds[idx] = NULL;
        JS_FreeValue(ctx, resolve_dup);
        JS_FreeValue(ctx, cbd->resolve);
        JS_FreeValue(ctx, cbd->reject);
        js_free(ctx, cbd);
        return JS_EXCEPTION;
    }
    JS_SetPropertyStr(ctx, obj, "handle", JS_NewInt32(ctx, idx));
    JS_SetPropertyStr(ctx, obj, "promise", promise);

    return obj;
}

/* ================================================================
 * Async primitives - each creates a Promise
 * ================================================================ */

static JSValue js_pal_http_request(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    QWRT_UNUSED(this_val);
    qwrt_t *rt = get_rt_from_ctx(ctx);
    if (!rt) {
        return JS_ThrowTypeError(ctx, "pal.http_request not available");
    }
    qwrt_ctx_t *cctx = get_ctx_from_jsctx(rt, ctx);
    if (!cctx || !cctx->pal || !cctx->pal->http_request) {
        return JS_ThrowTypeError(ctx, "pal.http_request not available");
    }

    if (argc < 4) {
        return JS_ThrowTypeError(ctx, "http_request requires url, method, headers, body arguments");
    }

    const char *url = JS_ToCString(ctx, argv[0]);
    const char *method = JS_ToCString(ctx, argv[1]);
    const char *headers = JS_ToCString(ctx, argv[2]);
    const char *body = NULL;
    size_t body_len = 0;

    if (!url || !method || !headers) {
        if (url) JS_FreeCString(ctx, url);
        if (method) JS_FreeCString(ctx, method);
        if (headers) JS_FreeCString(ctx, headers);
        return JS_EXCEPTION;
    }

    /* Body can be null/undefined or a string */
    if (!JS_IsUndefined(argv[3]) && !JS_IsNull(argv[3])) {
        body = JS_ToCStringLen(ctx, &body_len, argv[3]);
        if (!body) {
            JS_FreeCString(ctx, url);
            JS_FreeCString(ctx, method);
            JS_FreeCString(ctx, headers);
            return JS_EXCEPTION;
        }
    }

    /* Create promise capability */
    JSValue resolving_funcs[2];
    JSValue promise = JS_NewPromiseCapability(ctx, resolving_funcs);
    if (JS_IsException(promise)) {
        JS_FreeCString(ctx, url);
        JS_FreeCString(ctx, method);
        JS_FreeCString(ctx, headers);
        if (body) JS_FreeCString(ctx, body);
        return JS_EXCEPTION;
    }

    /* Allocate callback data */
    pal_cb_data_t *cbd = alloc_cb_data(ctx, resolving_funcs[0], resolving_funcs[1], rt);
    if (!cbd) {
        JS_FreeValue(ctx, resolving_funcs[0]);
        JS_FreeValue(ctx, resolving_funcs[1]);
        JS_FreeCString(ctx, url);
        JS_FreeCString(ctx, method);
        JS_FreeCString(ctx, headers);
        if (body) JS_FreeCString(ctx, body);
        return JS_ThrowOutOfMemory(ctx);
    }

    /* Call PAL */
    cctx->pal->http_request((qwrt_pal_t *)cctx->pal, url, method, headers, body, body_len,
                          pal_async_cb, cbd);

    /* Free strings (PAL should have copied what it needs) */
    JS_FreeCString(ctx, url);
    JS_FreeCString(ctx, method);
    JS_FreeCString(ctx, headers);
    if (body) JS_FreeCString(ctx, body);

    return promise;
}

static JSValue js_pal_http_request_stream(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    QWRT_UNUSED(this_val);
    qwrt_t *rt = get_rt_from_ctx(ctx);
    if (!rt) {
        return JS_ThrowTypeError(ctx, "pal.http_request_stream not available");
    }
    qwrt_ctx_t *cctx = get_ctx_from_jsctx(rt, ctx);
    if (!cctx || !cctx->pal || !cctx->pal->http_request_stream) {
        return JS_ThrowTypeError(ctx, "streaming not supported");
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

    size_t body_len = 0;
    const char *body = NULL;
    if (!JS_IsUndefined(argv[3]) && !JS_IsNull(argv[3])) {
        body = JS_ToCStringLen(ctx, &body_len, argv[3]);
        if (!body) {
            JS_FreeCString(ctx, url);
            JS_FreeCString(ctx, method);
            JS_FreeCString(ctx, headers);
            return JS_EXCEPTION;
        }
    }

    stream_bridge_ctx_t *sbctx = malloc(sizeof(stream_bridge_ctx_t));
    if (!sbctx) {
        JS_FreeCString(ctx, url);
        JS_FreeCString(ctx, method);
        JS_FreeCString(ctx, headers);
        if (body) JS_FreeCString(ctx, body);
        return JS_ThrowOutOfMemory(ctx);
    }
    sbctx->ctx = ctx;
    sbctx->rt = rt;
    sbctx->on_headers = JS_DupValue(ctx, argv[4]);
    sbctx->on_data = JS_DupValue(ctx, argv[5]);
    sbctx->on_end = JS_DupValue(ctx, argv[6]);

    qwrt_pal_stream_ops_t ops = {
        .on_headers = stream_on_headers,
        .on_data = stream_on_data,
        .on_end = stream_on_end,
        .user_data = sbctx,
    };

    cctx->pal->http_request_stream((qwrt_pal_t *)cctx->pal, url, method, headers, body, body_len, &ops);

    JS_FreeCString(ctx, url);
    JS_FreeCString(ctx, method);
    JS_FreeCString(ctx, headers);
    if (body) JS_FreeCString(ctx, body);

    return JS_UNDEFINED;
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
        /* Also catch "..." (three dots) as it can sometimes bypass naive checks */
        if (p[0] == '.' && p[1] == '.' && p[2] == '.' &&
            (p[3] == '\0' || p[3] == '/' || p[3] == '\\')) {
            return false;
        }
        p++;
    }

    return true;
}

/* ── File System Bridge ──────────────────────────────────────────── */

static JSValue js_pal_fs_read(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    QWRT_UNUSED(this_val);
    qwrt_t *rt = get_rt_from_ctx(ctx);
    if (!rt) {
        return JS_ThrowTypeError(ctx, "pal.fs_read not available");
    }
    qwrt_ctx_t *cctx = get_ctx_from_jsctx(rt, ctx);
    if (!cctx || !cctx->pal || !cctx->pal->fs_read) {
        return JS_ThrowTypeError(ctx, "pal.fs_read not available");
    }

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "fs_read requires path argument");
    }

    const char *path = JS_ToCString(ctx, argv[0]);
    if (!path) {
        return JS_EXCEPTION;
    }

    /* Reject path traversal attempts */
    if (!bridge_validate_path(path)) {
        JS_FreeCString(ctx, path);
        return JS_ThrowTypeError(ctx, "Path traversal detected");
    }

    /* Create promise capability */
    JSValue resolving_funcs[2];
    JSValue promise = JS_NewPromiseCapability(ctx, resolving_funcs);
    if (JS_IsException(promise)) {
        JS_FreeCString(ctx, path);
        return JS_EXCEPTION;
    }

    /* Allocate callback data */
    pal_cb_data_t *cbd = alloc_cb_data(ctx, resolving_funcs[0], resolving_funcs[1], rt);
    if (!cbd) {
        JS_FreeValue(ctx, resolving_funcs[0]);
        JS_FreeValue(ctx, resolving_funcs[1]);
        JS_FreeCString(ctx, path);
        return JS_ThrowOutOfMemory(ctx);
    }

    /* Call PAL */
    cctx->pal->fs_read((qwrt_pal_t *)cctx->pal, path, pal_async_cb, cbd);

    JS_FreeCString(ctx, path);

    return promise;
}

static JSValue js_pal_fs_write(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    QWRT_UNUSED(this_val);
    qwrt_t *rt = get_rt_from_ctx(ctx);
    if (!rt) {
        return JS_ThrowTypeError(ctx, "pal.fs_write not available");
    }
    qwrt_ctx_t *cctx = get_ctx_from_jsctx(rt, ctx);
    if (!cctx || !cctx->pal || !cctx->pal->fs_write) {
        return JS_ThrowTypeError(ctx, "pal.fs_write not available");
    }

    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "fs_write requires path and data arguments");
    }

    const char *path = JS_ToCString(ctx, argv[0]);
    if (!path) {
        return JS_EXCEPTION;
    }

    /* Reject path traversal attempts */
    if (!bridge_validate_path(path)) {
        JS_FreeCString(ctx, path);
        return JS_ThrowTypeError(ctx, "Path traversal detected");
    }

    size_t data_len = 0;
    const char *data = JS_ToCStringLen(ctx, &data_len, argv[1]);
    if (!data) {
        JS_FreeCString(ctx, path);
        return JS_EXCEPTION;
    }

    /* Create promise capability */
    JSValue resolving_funcs[2];
    JSValue promise = JS_NewPromiseCapability(ctx, resolving_funcs);
    if (JS_IsException(promise)) {
        JS_FreeCString(ctx, path);
        JS_FreeCString(ctx, data);
        return JS_EXCEPTION;
    }

    /* Allocate callback data */
    pal_cb_data_t *cbd = alloc_cb_data(ctx, resolving_funcs[0], resolving_funcs[1], rt);
    if (!cbd) {
        JS_FreeValue(ctx, resolving_funcs[0]);
        JS_FreeValue(ctx, resolving_funcs[1]);
        JS_FreeCString(ctx, path);
        JS_FreeCString(ctx, data);
        return JS_ThrowOutOfMemory(ctx);
    }

    /* Call PAL */
    cctx->pal->fs_write((qwrt_pal_t *)cctx->pal, path, data, data_len, pal_async_cb, cbd);

    JS_FreeCString(ctx, path);
    JS_FreeCString(ctx, data);

    return promise;
}

static JSValue js_pal_fs_exists(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    QWRT_UNUSED(this_val);
    qwrt_t *rt = get_rt_from_ctx(ctx);
    if (!rt) {
        return JS_ThrowTypeError(ctx, "pal.fs_exists not available");
    }
    qwrt_ctx_t *cctx = get_ctx_from_jsctx(rt, ctx);
    if (!cctx || !cctx->pal || !cctx->pal->fs_exists) {
        return JS_ThrowTypeError(ctx, "pal.fs_exists not available");
    }

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "fs_exists requires path argument");
    }

    const char *path = JS_ToCString(ctx, argv[0]);
    if (!path) {
        return JS_EXCEPTION;
    }

    /* Reject path traversal attempts */
    if (!bridge_validate_path(path)) {
        JS_FreeCString(ctx, path);
        return JS_ThrowTypeError(ctx, "Path traversal detected");
    }

    /* Create promise capability */
    JSValue resolving_funcs[2];
    JSValue promise = JS_NewPromiseCapability(ctx, resolving_funcs);
    if (JS_IsException(promise)) {
        JS_FreeCString(ctx, path);
        return JS_EXCEPTION;
    }

    /* Allocate callback data */
    pal_cb_data_t *cbd = alloc_cb_data(ctx, resolving_funcs[0], resolving_funcs[1], rt);
    if (!cbd) {
        JS_FreeValue(ctx, resolving_funcs[0]);
        JS_FreeValue(ctx, resolving_funcs[1]);
        JS_FreeCString(ctx, path);
        return JS_ThrowOutOfMemory(ctx);
    }

    /* Call PAL */
    cctx->pal->fs_exists((qwrt_pal_t *)cctx->pal, path, pal_async_cb, cbd);

    JS_FreeCString(ctx, path);

    return promise;
}

static JSValue js_pal_fs_remove(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    QWRT_UNUSED(this_val);
    qwrt_t *rt = get_rt_from_ctx(ctx);
    if (!rt) {
        return JS_ThrowTypeError(ctx, "pal.fs_remove not available");
    }
    qwrt_ctx_t *cctx = get_ctx_from_jsctx(rt, ctx);
    if (!cctx || !cctx->pal || !cctx->pal->fs_remove) {
        return JS_ThrowTypeError(ctx, "pal.fs_remove not available");
    }

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "fs_remove requires path argument");
    }

    const char *path = JS_ToCString(ctx, argv[0]);
    if (!path) {
        return JS_EXCEPTION;
    }

    /* Reject path traversal attempts */
    if (!bridge_validate_path(path)) {
        JS_FreeCString(ctx, path);
        return JS_ThrowTypeError(ctx, "Path traversal detected");
    }

    /* Create promise capability */
    JSValue resolving_funcs[2];
    JSValue promise = JS_NewPromiseCapability(ctx, resolving_funcs);
    if (JS_IsException(promise)) {
        JS_FreeCString(ctx, path);
        return JS_EXCEPTION;
    }

    /* Allocate callback data */
    pal_cb_data_t *cbd = alloc_cb_data(ctx, resolving_funcs[0], resolving_funcs[1], rt);
    if (!cbd) {
        JS_FreeValue(ctx, resolving_funcs[0]);
        JS_FreeValue(ctx, resolving_funcs[1]);
        JS_FreeCString(ctx, path);
        return JS_ThrowOutOfMemory(ctx);
    }

    /* Call PAL */
    cctx->pal->fs_remove((qwrt_pal_t *)cctx->pal, path, pal_async_cb, cbd);

    JS_FreeCString(ctx, path);

    return promise;
}

static JSValue js_pal_fs_list(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    QWRT_UNUSED(this_val);
    qwrt_t *rt = get_rt_from_ctx(ctx);
    if (!rt) {
        return JS_ThrowTypeError(ctx, "pal.fs_list not available");
    }
    qwrt_ctx_t *cctx = get_ctx_from_jsctx(rt, ctx);
    if (!cctx || !cctx->pal || !cctx->pal->fs_list) {
        return JS_ThrowTypeError(ctx, "pal.fs_list not available");
    }

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "fs_list requires path argument");
    }

    const char *path = JS_ToCString(ctx, argv[0]);
    if (!path) {
        return JS_EXCEPTION;
    }

    /* Reject path traversal attempts */
    if (!bridge_validate_path(path)) {
        JS_FreeCString(ctx, path);
        return JS_ThrowTypeError(ctx, "Path traversal detected");
    }

    /* Create promise capability */
    JSValue resolving_funcs[2];
    JSValue promise = JS_NewPromiseCapability(ctx, resolving_funcs);
    if (JS_IsException(promise)) {
        JS_FreeCString(ctx, path);
        return JS_EXCEPTION;
    }

    /* Allocate callback data */
    pal_cb_data_t *cbd = alloc_cb_data(ctx, resolving_funcs[0], resolving_funcs[1], rt);
    if (!cbd) {
        JS_FreeValue(ctx, resolving_funcs[0]);
        JS_FreeValue(ctx, resolving_funcs[1]);
        JS_FreeCString(ctx, path);
        return JS_ThrowOutOfMemory(ctx);
    }

    /* Call PAL */
    cctx->pal->fs_list((qwrt_pal_t *)cctx->pal, path, pal_async_cb, cbd);

    JS_FreeCString(ctx, path);

    return promise;
}

static JSValue js_pal_storage_get(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    QWRT_UNUSED(this_val);
    qwrt_t *rt = get_rt_from_ctx(ctx);
    if (!rt) {
        return JS_ThrowTypeError(ctx, "pal.storage_get not available");
    }
    qwrt_ctx_t *cctx = get_ctx_from_jsctx(rt, ctx);
    if (!cctx || !cctx->pal || !cctx->pal->storage_get) {
        return JS_ThrowTypeError(ctx, "pal.storage_get not available");
    }

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "storage_get requires key argument");
    }

    const char *key = JS_ToCString(ctx, argv[0]);
    if (!key) {
        return JS_EXCEPTION;
    }

    /* Create promise capability */
    JSValue resolving_funcs[2];
    JSValue promise = JS_NewPromiseCapability(ctx, resolving_funcs);
    if (JS_IsException(promise)) {
        JS_FreeCString(ctx, key);
        return JS_EXCEPTION;
    }

    /* Allocate callback data */
    pal_cb_data_t *cbd = alloc_cb_data(ctx, resolving_funcs[0], resolving_funcs[1], rt);
    if (!cbd) {
        JS_FreeValue(ctx, resolving_funcs[0]);
        JS_FreeValue(ctx, resolving_funcs[1]);
        JS_FreeCString(ctx, key);
        return JS_ThrowOutOfMemory(ctx);
    }

    /* Call PAL */
    cctx->pal->storage_get((qwrt_pal_t *)cctx->pal, key, pal_async_cb, cbd);

    JS_FreeCString(ctx, key);

    return promise;
}

static JSValue js_pal_storage_set(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    QWRT_UNUSED(this_val);
    qwrt_t *rt = get_rt_from_ctx(ctx);
    if (!rt) {
        return JS_ThrowTypeError(ctx, "pal.storage_set not available");
    }
    qwrt_ctx_t *cctx = get_ctx_from_jsctx(rt, ctx);
    if (!cctx || !cctx->pal || !cctx->pal->storage_set) {
        return JS_ThrowTypeError(ctx, "pal.storage_set not available");
    }

    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "storage_set requires key and value arguments");
    }

    const char *key = JS_ToCString(ctx, argv[0]);
    if (!key) {
        return JS_EXCEPTION;
    }

    size_t value_len = 0;
    const char *value = JS_ToCStringLen(ctx, &value_len, argv[1]);
    if (!value) {
        JS_FreeCString(ctx, key);
        return JS_EXCEPTION;
    }

    /* Create promise capability */
    JSValue resolving_funcs[2];
    JSValue promise = JS_NewPromiseCapability(ctx, resolving_funcs);
    if (JS_IsException(promise)) {
        JS_FreeCString(ctx, key);
        JS_FreeCString(ctx, value);
        return JS_EXCEPTION;
    }

    /* Allocate callback data */
    pal_cb_data_t *cbd = alloc_cb_data(ctx, resolving_funcs[0], resolving_funcs[1], rt);
    if (!cbd) {
        JS_FreeValue(ctx, resolving_funcs[0]);
        JS_FreeValue(ctx, resolving_funcs[1]);
        JS_FreeCString(ctx, key);
        JS_FreeCString(ctx, value);
        return JS_ThrowOutOfMemory(ctx);
    }

    /* Call PAL */
    cctx->pal->storage_set((qwrt_pal_t *)cctx->pal, key, value, value_len, pal_async_cb, cbd);

    JS_FreeCString(ctx, key);
    JS_FreeCString(ctx, value);

    return promise;
}

static JSValue js_pal_storage_del(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv)
{
    QWRT_UNUSED(this_val);
    qwrt_t *rt = get_rt_from_ctx(ctx);
    if (!rt) {
        return JS_ThrowTypeError(ctx, "pal.storage_del not available");
    }
    qwrt_ctx_t *cctx = get_ctx_from_jsctx(rt, ctx);
    if (!cctx || !cctx->pal || !cctx->pal->storage_del) {
        return JS_ThrowTypeError(ctx, "pal.storage_del not available");
    }

    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "storage_del requires key argument");
    }

    const char *key = JS_ToCString(ctx, argv[0]);
    if (!key) {
        return JS_EXCEPTION;
    }

    /* Create promise capability */
    JSValue resolving_funcs[2];
    JSValue promise = JS_NewPromiseCapability(ctx, resolving_funcs);
    if (JS_IsException(promise)) {
        JS_FreeCString(ctx, key);
        return JS_EXCEPTION;
    }

    /* Allocate callback data */
    pal_cb_data_t *cbd = alloc_cb_data(ctx, resolving_funcs[0], resolving_funcs[1], rt);
    if (!cbd) {
        JS_FreeValue(ctx, resolving_funcs[0]);
        JS_FreeValue(ctx, resolving_funcs[1]);
        JS_FreeCString(ctx, key);
        return JS_ThrowOutOfMemory(ctx);
    }

    /* Call PAL */
    cctx->pal->storage_del((qwrt_pal_t *)cctx->pal, key, pal_async_cb, cbd);

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
    qwrt_t *rt = get_rt_from_ctx(ctx);
    if (!rt) {
        return JS_ThrowTypeError(ctx, "pal.randomBytes not available");
    }
    qwrt_ctx_t *cctx = get_ctx_from_jsctx(rt, ctx);
    if (!cctx || !cctx->pal || !cctx->pal->random_bytes) {
        return JS_ThrowTypeError(ctx, "pal.randomBytes not available");
    }

    int64_t len = 0;
    if (argc >= 1 && JS_ToInt64(ctx, &len, argv[0])) {
        return JS_EXCEPTION;
    }
    if (len <= 0 || len > 65536) {
        return JS_ThrowRangeError(ctx, "randomBytes: length must be 1-65536");
    }

    /* Allocate temporary buffer, fill with random bytes */
    size_t ulen = (size_t)len;
    uint8_t *buf = js_malloc(ctx, ulen);
    if (!buf) {
        return JS_ThrowOutOfMemory(ctx);
    }

    cctx->pal->random_bytes((qwrt_pal_t *)cctx->pal, buf, ulen);

    /* Copy into ArrayBuffer (QuickJS owns the copy) */
    JSValue ab = JS_NewArrayBufferCopy(ctx, buf, ulen);
    js_free(ctx, buf);
    return ab;
}

/* ================================================================
 * Create the internal pal JS object (per-context version)
 * ================================================================ */

JSValue qwrt_create_pal_object_ctx(qwrt_t *rt, qwrt_ctx_t *ctx)
{
    QWRT_UNUSED(rt);
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

    /* Async functions (return Promises) */
    JS_SetPropertyStr(jsctx, pal, "httpRequest", JS_NewCFunction(jsctx, js_pal_http_request, "httpRequest", 4));
    JS_SetPropertyStr(jsctx, pal, "httpRequestStream", JS_NewCFunction(jsctx, js_pal_http_request_stream, "httpRequestStream", 7));
    JS_SetPropertyStr(jsctx, pal, "fsRead", JS_NewCFunction(jsctx, js_pal_fs_read, "fsRead", 1));
    JS_SetPropertyStr(jsctx, pal, "fsWrite", JS_NewCFunction(jsctx, js_pal_fs_write, "fsWrite", 2));
    JS_SetPropertyStr(jsctx, pal, "fsExists", JS_NewCFunction(jsctx, js_pal_fs_exists, "fsExists", 1));
    JS_SetPropertyStr(jsctx, pal, "fsRemove", JS_NewCFunction(jsctx, js_pal_fs_remove, "fsRemove", 1));
    JS_SetPropertyStr(jsctx, pal, "fsList", JS_NewCFunction(jsctx, js_pal_fs_list, "fsList", 1));
    JS_SetPropertyStr(jsctx, pal, "storageGet", JS_NewCFunction(jsctx, js_pal_storage_get, "storageGet", 1));
    JS_SetPropertyStr(jsctx, pal, "storageSet", JS_NewCFunction(jsctx, js_pal_storage_set, "storageSet", 2));
    JS_SetPropertyStr(jsctx, pal, "storageDel", JS_NewCFunction(jsctx, js_pal_storage_del, "storageDel", 1));

    /* Sync CSPRNG */
    JS_SetPropertyStr(jsctx, pal, "randomBytes", JS_NewCFunction(jsctx, js_pal_random_bytes, "randomBytes", 1));

    return pal;
}

/* ================================================================
 * Inject polyfill via __pal_inject__ temp global (per-context version)
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

    /* Set __pal_inject__ as a global temp var */
    JS_SetPropertyStr(jsctx, global, "__pal_inject__", pal);

    JS_FreeValue(jsctx, global);

    /* Evaluate the polyfill code.
     * Expected format: (function(pal){ ... })(__pal_inject__);
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

    /* After polyfill runs, move __pal_inject__ to __pal__ so extension init
     * hooks can register functions on the same pal object that the polyfill's
     * closures reference. */
    global = JS_GetGlobalObject(jsctx);
    JSAtom inject_atom = JS_NewAtom(jsctx, "__pal_inject__");
    JSValue pal_ref = JS_GetProperty(jsctx, global, inject_atom);
    JS_DeleteProperty(jsctx, global, inject_atom, 0);
    JS_FreeAtom(jsctx, inject_atom);
    if (!JS_IsUndefined(pal_ref) && !JS_IsException(pal_ref)) {
        JS_SetPropertyStr(jsctx, global, "__pal__", pal_ref);
    } else {
        JS_FreeValue(jsctx, pal_ref);
    }
    JS_FreeValue(jsctx, global);

    return 0;
}

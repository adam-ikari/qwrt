---
title: Async Operations
description: Asynchronous PAL operations in Qwrt.js — callback-based I/O, timer integration, and the deferred callback queue.
---

# Async Operations

Most PAL methods are asynchronous — they start an operation and deliver the result later via a callback. This page explains the patterns for implementing async operations correctly.

## The Core Pattern

```c
static void mypal_http_request(qwrt_pal_t *pal,
                                const char *url, const char *method,
                                const char *headers, const char *body,
                                size_t body_len,
                                qwrt_pal_cb_t cb, void *cb_data) {
    pal_mypal_t *mp = (pal_mypal_t *)pal->user_data;

    // 1. Validate inputs
    if (!url || !method) {
        cb(cb_data, QWRT_ERR_INVALID_ARG, NULL, 0);
        return;  // synchronous error — callback fires before returning
    }

    // 2. Allocate request context
    request_t *req = calloc(1, sizeof(*req));
    if (!req) {
        cb(cb_data, QWRT_ERR_NO_MEMORY, NULL, 0);
        return;
    }
    req->cb = cb;
    req->cb_data = cb_data;

    // 3. Start the async operation
    int err = start_http(mp, url, method, headers, body, body_len, req);
    if (err != 0) {
        free(req);
        cb(cb_data, QWRT_ERR_NETWORK, NULL, 0);
        return;
    }

    // 4. Callback fires later (from event loop or completion handler)
}
```

## Callback Rules

1. **Fire exactly once** per operation. Never call the callback twice, and never skip calling it.
2. **Fire synchronously on immediate errors** (invalid args, no memory). The caller doesn't need to check for "did it queue or not."
3. **Fire asynchronously on success** (from the event loop or a completion handler).
4. **Copy data before the callback returns** if you need it afterwards — the callback consumer may free or modify it.

## Using the Deferred Callback Bridge

PAL callbacks that fire from non-JS threads (libuv callbacks, timer interrupts, network threads) must not call into JavaScript directly. Instead, enqueue via `qwrt_defer_callback`:

```c
// Internal: called by qwrt_tick on the JS thread
static void deliver_response(void *rt_ptr, void *data) {
    qwrt_t *rt = (qwrt_t *)rt_ptr;
    deferred_t *d = (deferred_t *)data;

    // Now safe to create JS values, resolve promises, etc.
    JSContext *ctx = qwrt_get_jsctx(rt);
    JSValue result = JS_ParseJSON(ctx, d->json, d->json_len, "<response>");
    // ... resolve the Promise ...

    free(d->json);
    free(d);
}

// PAL callback — fires on libuv thread
static void on_http_done(void *user_data, const char *response, size_t len) {
    request_ctx_t *rctx = (request_ctx_t *)user_data;

    deferred_t *d = malloc(sizeof(*d));
    d->json = strdup(response);
    d->json_len = len;
    d->resolve_cb = rctx->resolve_cb;
    d->reject_cb = rctx->reject_cb;

    qwrt_defer_callback(rctx->rt, deliver_response, d);
    free(rctx);
}
```

## Handling Cancellation

For operations that support cancellation (HTTP requests, timers):

```c
struct pal_mypal_t {
    qwrt_pal_t pal;
    request_t *active_request;  // currently in-flight request, or NULL
};

static void mypal_http_abort(qwrt_pal_t *pal) {
    pal_mypal_t *mp = (pal_mypal_t *)pal->user_data;

    if (!mp->active_request) return;  // no-op if nothing in flight

    // Close the underlying socket/connection
    close_socket(mp->active_request->sock);
    // Cancel the timer
    stop_timer(mp->active_request->timer);

    // Deliver cancellation to the callback
    mp->active_request->cb(mp->active_request->cb_data,
                           QWRT_ERR_CANCELLED, NULL, 0);

    free_request(mp->active_request);
    mp->active_request = NULL;
}
```

## Timer Implementation

```c
static void *mypal_timer_start(qwrt_pal_t *pal, uint64_t delay_ms,
                                int repeat, qwrt_pal_cb_t cb, void *cb_data) {
    pal_mypal_t *mp = (pal_mypal_t *)pal->user_data;

    my_timer_t *t = calloc(1, sizeof(*t));
    if (!t) return NULL;

    t->cb = cb;
    t->cb_data = cb_data;
    t->delay_ms = delay_ms;
    t->repeat = repeat;
    t->active = 1;

    // Register with your platform's timer facility
    t->native_handle = platform_timer_create(delay_ms, repeat, on_timer_fire, t);
    if (!t->native_handle) {
        free(t);
        return NULL;
    }

    return t;  // opaque handle for timer_stop
}

static void mypal_timer_stop(qwrt_pal_t *pal, void *handle) {
    if (!handle) return;
    my_timer_t *t = (my_timer_t *)handle;

    t->active = 0;
    platform_timer_destroy(t->native_handle);
    free(t);
}
```

## Error Handling Best Practices

1. **Check all allocations** — return `QWRT_ERR_NO_MEMORY` on failure
2. **Validate inputs** — return `QWRT_ERR_INVALID_ARG` for NULL where not allowed
3. **Clean up on error** — free partial allocations before calling the error callback
4. **Don't leak callbacks** — if an operation is cancelled, the callback must still fire (with `QWRT_ERR_CANCELLED`)
5. **Timeout handling** — if your HTTP request times out, call the callback with `QWRT_ERR_TIMEOUT`

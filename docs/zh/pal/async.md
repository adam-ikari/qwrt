---
title: 异步操作
description: Qwrt.js 中的异步 PAL 操作 — 基于回调的 I/O、定时器集成和延迟回调队列。
---

# 异步操作

大多数 PAL 方法是异步的——它们启动一个操作并在稍后通过回调传递结果。本页说明正确实现异步操作的模式。

## 核心模式

```c
static void mypal_http_request(qwrt_pal_t *pal,
                                const char *url, const char *method,
                                const char *headers, const char *body,
                                size_t body_len,
                                qwrt_pal_cb_t cb, void *cb_data) {
    pal_mypal_t *mp = (pal_mypal_t *)pal->user_data;

    // 1. 验证输入
    if (!url || !method) {
        cb(cb_data, QWRT_ERR_INVALID_ARG, NULL, 0);
        return;  // 同步错误 — 回调在返回前触发
    }

    // 2. 分配请求上下文
    request_t *req = calloc(1, sizeof(*req));
    if (!req) {
        cb(cb_data, QWRT_ERR_NO_MEMORY, NULL, 0);
        return;
    }
    req->cb = cb;
    req->cb_data = cb_data;

    // 3. 启动异步操作
    int err = start_http(mp, url, method, headers, body, body_len, req);
    if (err != 0) {
        free(req);
        cb(cb_data, QWRT_ERR_NETWORK, NULL, 0);
        return;
    }

    // 4. 回调稍后触发（来自事件循环或完成处理函数）
}
```

## 回调规则

1. **恰好触发一次**每个操作。绝不重复调用回调，也绝不跳过调用。
2. **立即错误时同步触发**（无效参数、内存不足）。调用者无需检查"是否已入队"。
3. **成功时异步触发**（来自事件循环或完成处理函数）。
4. **在回调返回前复制数据**如果你之后还需要的话——回调消费者可能释放或修改它。

## 使用延迟回调桥接

从非 JS 线程（libuv 回调、定时器中断、网络线程）触发的 PAL 回调不得直接调用 JavaScript。相反，通过 `qwrt_defer_callback` 入队：

```c
// 内部：由 qwrt_tick 在 JS 线程上调用
static void deliver_response(void *rt_ptr, void *data) {
    qwrt_t *rt = (qwrt_t *)rt_ptr;
    deferred_t *d = (deferred_t *)data;

    // 现在可以安全创建 JS 值、resolve Promise 等
    JSContext *ctx = qwrt_get_jsctx(rt);
    JSValue result = JS_ParseJSON(ctx, d->json, d->json_len, "<response>");
    // ... resolve Promise ...

    free(d->json);
    free(d);
}

// PAL 回调 — 在 libuv 线程上触发
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

## 处理取消

对于支持取消的操作（HTTP 请求、定时器）：

```c
struct pal_mypal_t {
    qwrt_pal_t pal;
    request_t *active_request;  // 当前正在进行的请求，或 NULL
};

static void mypal_http_abort(qwrt_pal_t *pal) {
    pal_mypal_t *mp = (pal_mypal_t *)pal->user_data;

    if (!mp->active_request) return;  // 无操作如果无进行中请求

    // 关闭底层套接字/连接
    close_socket(mp->active_request->sock);
    // 取消定时器
    stop_timer(mp->active_request->timer);

    // 向回调传递取消信息
    mp->active_request->cb(mp->active_request->cb_data,
                           QWRT_ERR_CANCELLED, NULL, 0);

    free_request(mp->active_request);
    mp->active_request = NULL;
}
```

## 定时器实现

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

    // 注册到平台的定时器设施
    t->native_handle = platform_timer_create(delay_ms, repeat, on_timer_fire, t);
    if (!t->native_handle) {
        free(t);
        return NULL;
    }

    return t;  // timer_stop 使用的不透明句柄
}

static void mypal_timer_stop(qwrt_pal_t *pal, void *handle) {
    if (!handle) return;
    my_timer_t *t = (my_timer_t *)handle;

    t->active = 0;
    platform_timer_destroy(t->native_handle);
    free(t);
}
```

## 错误处理最佳实践

1. **检查所有分配** — 失败时返回 `QWRT_ERR_NO_MEMORY`
2. **验证输入** — 不允许 NULL 的参数返回 `QWRT_ERR_INVALID_ARG`
3. **出错时清理** — 在调用错误回调前释放部分分配
4. **不要泄漏回调** — 如果操作被取消，回调仍必须触发（带 `QWRT_ERR_CANCELLED`）
5. **超时处理** — 如果 HTTP 请求超时，以 `QWRT_ERR_TIMEOUT` 调用回调

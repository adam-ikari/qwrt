---
title: 流式 HTTP
description: Qwrt.js PAL 中的流式 HTTP 响应处理 — on_headers、on_data、on_end 回调和分块传输编码。
---

# 流式 HTTP

流式 HTTP 响应通过 `qwrt_pal_stream_ops_t` 传递——三个回调而非一个。这使得分块传输编码、Server-Sent Events（SSE）和 LLM 流式输出成为可能。

## 何时实现流式传输

当你的 PAL 支持以下场景时，实现 `http_request_stream`：

- **分块传输编码** — 响应主体分片到达
- **大型载荷** — 避免在内存中缓冲整个响应
- **Server-Sent Events** — 持久连接，增量数据
- **LLM 流式输出** — token 逐个到达

如果你不需要流式传输，设置 `http_request_stream = NULL` 并仅实现 `http_request`。桥接层会优雅降级。

## 流回调

```c
typedef struct qwrt_pal_stream_ops {
    void (*on_headers)(void *user_data, int status, const char *headers_json);
    void (*on_data)(void *user_data, const char *data, size_t len);
    void (*on_end)(void *user_data, int error_status);
    void *user_data;
} qwrt_pal_stream_ops_t;
```

### 时序保证

```
正常流程：
  on_headers(200, "{...}") → on_data(chunk1) → on_data(chunk2) → on_end(OK)

连接期间出错：
  on_headers(QWRT_ERR_NETWORK, NULL) → on_end(QWRT_ERR_NETWORK)

                   (跳过 on_data — 未收到主体数据)

中止：
  on_headers(200, "{...}") → on_data(chunk1) → 调用 http_abort()
      → on_end(QWRT_ERR_CANCELLED)

空主体：
  on_headers(204, "{...}") → on_end(OK)
```

## 实现

```c
typedef struct stream_ctx {
    qwrt_pal_stream_ops_t ops;  // 调用者 ops 的副本
    int sock;                    // 底层套接字或连接句柄
    void *timer;                 // 超时定时器
    int aborted;                 // 由 http_abort 设置
    char header_buf[4096];      // 解析过程中累积头部
    size_t header_len;
    int headers_complete;
} stream_ctx_t;

static void mypal_http_request_stream(qwrt_pal_t *pal,
                                       const char *url, const char *method,
                                       const char *headers, const char *body,
                                       size_t body_len,
                                       qwrt_pal_stream_ops_t *ops) {
    pal_mypal_t *mp = (pal_mypal_t *)pal->user_data;

    stream_ctx_t *sctx = calloc(1, sizeof(*sctx));
    if (!sctx) {
        ops->on_headers(ops->user_data, QWRT_ERR_NO_MEMORY, NULL);
        ops->on_end(ops->user_data, QWRT_ERR_NO_MEMORY);
        return;
    }

    sctx->ops = *ops;  // 复制 ops 结构体
    sctx->aborted = 0;
    sctx->headers_complete = 0;

    // 连接并开始读取...
    sctx->sock = connect_and_send(url, method, headers, body, body_len);
    if (sctx->sock < 0) {
        sctx->ops.on_headers(sctx->ops.user_data, QWRT_ERR_NETWORK, NULL);
        sctx->ops.on_end(sctx->ops.user_data, QWRT_ERR_NETWORK);
        free(sctx);
        return;
    }

    // 注册可读事件
    mp->active_stream = sctx;
    start_read(sctx->sock, on_socket_readable, sctx);
}

// 当套接字上有数据到达时调用
static void on_socket_readable(stream_ctx_t *sctx, const char *data, size_t len) {
    if (sctx->aborted) return;

    if (!sctx->headers_complete) {
        // 从缓冲区解析头部...
        int status;
        char *headers_json = parse_headers(data, len, &status);
        if (headers_json) {
            sctx->headers_complete = 1;
            sctx->ops.on_headers(sctx->ops.user_data, status, headers_json);
            free(headers_json);
            // 头部之后的剩余数据是主体 — 送入 on_data
            size_t header_end = find_header_end(data, len);
            if (header_end < len) {
                sctx->ops.on_data(sctx->ops.user_data,
                                  data + header_end, len - header_end);
            }
        }
    } else {
        // 主体数据块
        sctx->ops.on_data(sctx->ops.user_data, data, len);
    }
}

static void mypal_http_abort(qwrt_pal_t *pal) {
    pal_mypal_t *mp = (pal_mypal_t *)pal->user_data;

    if (!mp->active_stream) return;

    mp->active_stream->aborted = 1;
    close_socket(mp->active_stream->sock);
    stop_timer(mp->active_stream->timer);

    mp->active_stream->ops.on_end(mp->active_stream->ops.user_data,
                                   QWRT_ERR_CANCELLED);
    free(mp->active_stream);
    mp->active_stream = NULL;
}
```

## 构建响应 JSON

使用 `pal_common.h` 中的共享辅助函数构建 HTTP 响应 JSON：

```c
#include "pal_common.h"

// 构建头部 JSON
char *headers_json = pal_build_headers_json(header_keys, header_vals, count);

// 构建完整响应
char *response = pal_build_http_json(200, headers_json, body, body_len);

// 发送给回调
sctx->ops.on_headers(sctx->ops.user_data, 200, headers_json);
```

完整 API 参见[共享辅助函数](/pal/common-helpers)。

## 超时处理

每个流应有自己的超时定时器：

```c
static void on_stream_timeout(void *user_data) {
    stream_ctx_t *sctx = (stream_ctx_t *)user_data;

    if (sctx->aborted) return;

    sctx->aborted = 1;
    close_socket(sctx->sock);

    // 传递超时错误
    if (!sctx->headers_complete) {
        sctx->ops.on_headers(sctx->ops.user_data, QWRT_ERR_TIMEOUT, NULL);
    }
    sctx->ops.on_end(sctx->ops.user_data, QWRT_ERR_TIMEOUT);
    free(sctx);
}
```
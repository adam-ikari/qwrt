---
title: Streaming HTTP
description: Streaming HTTP response handling in Qwrt.js PAL — on_headers, on_data, on_end callbacks, and chunked transfer encoding.
---

# Streaming HTTP

Streaming HTTP responses are delivered through `qwrt_pal_stream_ops_t` — three callbacks instead of one. This enables chunked transfer encoding, Server-Sent Events (SSE), and LLM streaming.

## When to Implement Streaming

Implement `http_request_stream` when your PAL supports:

- **Chunked transfer encoding** — response body arrives in pieces
- **Large payloads** — avoid buffering the entire response in memory
- **Server-Sent Events** — persistent connections with incremental data
- **LLM streaming** — tokens arrive one by one

If you don't need streaming, set `http_request_stream = NULL` and implement only `http_request`. The bridge falls back gracefully.

## Stream Callbacks

```c
typedef struct qwrt_pal_stream_ops {
    void (*on_headers)(void *user_data, int status, const char *headers_json);
    void (*on_data)(void *user_data, const char *data, size_t len);
    void (*on_end)(void *user_data, int error_status);
    void *user_data;
} qwrt_pal_stream_ops_t;
```

### Sequence Guarantees

```
Normal flow:
  on_headers(200, "{...}") → on_data(chunk1) → on_data(chunk2) → on_end(OK)

Error during connect:
  on_headers(QWRT_ERR_NETWORK, NULL) → on_end(QWRT_ERR_NETWORK)

                   (on_data is skipped — no body arrived)

Aborted:
  on_headers(200, "{...}") → on_data(chunk1) → http_abort() called
      → on_end(QWRT_ERR_CANCELLED)

Empty body:
  on_headers(204, "{...}") → on_end(OK)
```

## Implementation

```c
typedef struct stream_ctx {
    qwrt_pal_stream_ops_t ops;  // copy of the caller's ops
    int sock;                    // underlying socket or connection handle
    void *timer;                 // timeout timer
    int aborted;                 // set by http_abort
    char header_buf[4096];      // accumulate headers during parsing
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

    sctx->ops = *ops;  // copy the ops struct
    sctx->aborted = 0;
    sctx->headers_complete = 0;

    // Connect and start reading...
    sctx->sock = connect_and_send(url, method, headers, body, body_len);
    if (sctx->sock < 0) {
        sctx->ops.on_headers(sctx->ops.user_data, QWRT_ERR_NETWORK, NULL);
        sctx->ops.on_end(sctx->ops.user_data, QWRT_ERR_NETWORK);
        free(sctx);
        return;
    }

    // Register for readable events
    mp->active_stream = sctx;
    start_read(sctx->sock, on_socket_readable, sctx);
}

// Called when data arrives on the socket
static void on_socket_readable(stream_ctx_t *sctx, const char *data, size_t len) {
    if (sctx->aborted) return;

    if (!sctx->headers_complete) {
        // Parse headers from the buffer...
        int status;
        char *headers_json = parse_headers(data, len, &status);
        if (headers_json) {
            sctx->headers_complete = 1;
            sctx->ops.on_headers(sctx->ops.user_data, status, headers_json);
            free(headers_json);
            // Remaining data after headers is body — feed to on_data
            size_t header_end = find_header_end(data, len);
            if (header_end < len) {
                sctx->ops.on_data(sctx->ops.user_data,
                                  data + header_end, len - header_end);
            }
        }
    } else {
        // Body chunk
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

## Building Response JSON

Use the shared helpers from `pal_common.h` to build HTTP response JSON:

```c
#include "pal_common.h"

// Build headers JSON
char *headers_json = pal_build_headers_json(header_keys, header_vals, count);

// Build the full response
char *response = pal_build_http_json(200, headers_json, body, body_len);

// Send to the callback
sctx->ops.on_headers(sctx->ops.user_data, 200, headers_json);
```

See [Shared Helpers](/pal/common-helpers) for the full API.

## Timeout Handling

Each stream should have its own timeout timer:

```c
static void on_stream_timeout(void *user_data) {
    stream_ctx_t *sctx = (stream_ctx_t *)user_data;

    if (sctx->aborted) return;

    sctx->aborted = 1;
    close_socket(sctx->sock);

    // Deliver timeout error
    if (!sctx->headers_complete) {
        sctx->ops.on_headers(sctx->ops.user_data, QWRT_ERR_TIMEOUT, NULL);
    }
    sctx->ops.on_end(sctx->ops.user_data, QWRT_ERR_TIMEOUT);
    free(sctx);
}
```

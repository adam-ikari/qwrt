# PAL Callback Types

The PAL uses two callback types: a single-shot completion callback and a streaming callback trio.

## Single-Shot Callback: `qwrt_pal_cb_t`

```c
typedef void (*qwrt_pal_cb_t)(void *user_data, int status,
                               const char *data, size_t data_len);
```

Used by: `http_request`, `fs_read`, `fs_write`, `fs_exists`, `fs_remove`, `fs_list`, `storage_get`, `storage_set`, `storage_del`, `timer_start`, `channel_recv`.

### Parameters

| Parameter | Description |
|-----------|-------------|
| `user_data` | Opaque pointer passed through from the call site |
| `status` | A `qwrt_pal_err_t` value: `QWRT_OK` (0) on success, `< 0` on error |
| `data` | Result payload (JSON string, file content, etc.), or NULL on error |
| `data_len` | Length of `data` in bytes, 0 if `data` is NULL |

### Callback Contract

- The callback fires **exactly once** per operation
- The callback fires on the **event loop thread**, not the JS thread
- The callback **must not** call into JavaScript directly — use `qwrt_defer_callback` instead
- `data` is owned by the PAL until the callback returns; the bridge copies it if needed

## Streaming Callback: `qwrt_pal_stream_ops_t`

```c
typedef struct qwrt_pal_stream_ops {
    void (*on_headers)(void *user_data, int status, const char *headers_json);
    void (*on_data)(void *user_data, const char *data, size_t len);
    void (*on_end)(void *user_data, int error_status);
    void *user_data;
} qwrt_pal_stream_ops_t;
```

Used by: `http_request_stream`.

### Call Sequence

```
on_headers  ── called exactly once (may be the only call on error)
    │
on_data     ── called zero or more times (body chunks)
    │
on_data     ── ...
    │
on_end      ── called exactly once (stream finished, aborted, or errored)
```

### `on_headers`

```c
void (*on_headers)(void *user_data, int status, const char *headers_json);
```

Called when the HTTP status line and headers are parsed.

- `status` — HTTP status code (200, 404, 500, etc.)
- `headers_json` — JSON object of response headers, e.g. `{"Content-Type":"text/html","Content-Length":"1024"}`

On connection failure (DNS, TLS, timeout), `on_headers` may be called with a negative status and `on_data`/`on_end` skipped. The PAL should still call `on_end` after an error `on_headers`.

### `on_data`

```c
void (*on_data)(void *user_data, const char *data, size_t len);
```

Called for each body chunk. `len` may be 0 (empty chunk, typically skipped). The data pointer is valid only for the duration of the call.

### `on_end`

```c
void (*on_end)(void *user_data, int error_status);
```

Called when the stream ends.

- `error_status` — `QWRT_OK` on clean close, `QWRT_ERR_CANCELLED` on abort, `QWRT_ERR_NETWORK` on connection error, etc.

After `on_end`, no further callbacks fire for this stream.

## Thread Safety

All callbacks fire on the **event loop thread** (the thread that calls `pal->run_cycle`). They must not call `qwrt_eval`, `qwrt_call`, or any other JS-executing API directly.

Instead, enqueue work for the JS thread:

```c
static void my_http_callback(void *user_data, int status,
                              const char *data, size_t len) {
    my_ctx_t *ctx = (my_ctx_t *)user_data;

    // BAD: calling JS from PAL callback thread
    // qwrt_eval(ctx->rt, "handleResponse()", NULL);

    // GOOD: defer to the JS thread
    qwrt_defer_callback(ctx->rt, process_response, data_copy);
}
```

The deferred callback runs when the host calls `qwrt_tick(rt)`.

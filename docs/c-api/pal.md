# PAL Interface

The Platform Abstraction Layer (PAL) is a vtable of function pointers that every backend must implement. This page documents the C types that define that contract.

## `qwrt_pal_t`

```c
typedef struct qwrt_pal_t qwrt_pal_t;

struct qwrt_pal_t {
    void *user_data;
    uint32_t version;
    const char *name;

    /* Core I/O */
    void (*http_request)(qwrt_pal_t *pal, ...);
    void (*http_request_stream)(qwrt_pal_t *pal, ...);
    void (*http_abort)(qwrt_pal_t *pal);

    /* Filesystem */
    void (*fs_read)(qwrt_pal_t *pal, ...);
    void (*fs_write)(qwrt_pal_t *pal, ...);
    void (*fs_exists)(qwrt_pal_t *pal, ...);
    void (*fs_remove)(qwrt_pal_t *pal, ...);
    void (*fs_list)(qwrt_pal_t *pal, ...);

    /* Key-value storage */
    void (*storage_get)(qwrt_pal_t *pal, ...);
    void (*storage_set)(qwrt_pal_t *pal, ...);
    void (*storage_del)(qwrt_pal_t *pal, ...);

    /* Timers */
    void *(*timer_start)(qwrt_pal_t *pal, ...);
    void (*timer_stop)(qwrt_pal_t *pal, void *handle);

    /* Time & entropy */
    uint64_t (*time_now)(qwrt_pal_t *pal);
    uint64_t (*hrtime)(qwrt_pal_t *pal);
    void (*random_bytes)(qwrt_pal_t *pal, uint8_t *buf, size_t len);

    /* Logging & memory */
    void (*log)(qwrt_pal_t *pal, int level, const char *msg);
    void *(*mem_alloc)(qwrt_pal_t *pal, size_t size);
    void (*mem_free)(qwrt_pal_t *pal, void *ptr);

    /* Event loop */
    int (*run_cycle)(qwrt_pal_t *pal, int timeout_ms);

    /* Process management */
    void *(*spawn)(qwrt_pal_t *pal, ...);
    void *(*spawn_get_stdin)(qwrt_pal_t *pal, void *proc);
    void *(*spawn_get_stdout)(qwrt_pal_t *pal, void *proc);
    int (*channel_send)(qwrt_pal_t *pal, ...);
    void (*channel_recv)(qwrt_pal_t *pal, ...);
    void (*channel_close)(qwrt_pal_t *pal, void *ch);
    int (*join)(qwrt_pal_t *pal, void *proc, int timeout_ms);
    void (*terminate)(qwrt_pal_t *pal, void *proc);

    /* Lifecycle hooks */
    int (*init)(qwrt_pal_t *pal);
    void (*destroy)(qwrt_pal_t *pal);

    /* Reserved */
    void *reserved[4];
};
```

## Field Groups

### Identity & Lifecycle

| Field | Required | Description |
|-------|----------|-------------|
| `user_data` | — | Opaque pointer for PAL private state |
| `version` | — | Interface version (must be 1) |
| `name` | No | Human-readable PAL name for diagnostics |
| `init` | No | Called once by `qwrt_create()`; return `QWRT_OK` or error |
| `destroy` | No | Called once by `qwrt_destroy()`; release all resources |

### Core I/O

| Field | Required | Description |
|-------|----------|-------------|
| `http_request` | Yes | Perform an HTTP request, deliver full response body to callback |
| `http_request_stream` | No | Streaming HTTP (SSE, LLM streaming) |
| `http_abort` | No | Cancel in-flight streaming request |

### Filesystem

| Field | Required | Description |
|-------|----------|-------------|
| `fs_read` | No | Read file contents |
| `fs_write` | No | Write/overwrite a file |
| `fs_exists` | No | Check if a file exists |
| `fs_remove` | No | Delete a file |
| `fs_list` | No | List directory entries |

### Event Loop

| Field | Required | Description |
|-------|----------|-------------|
| `run_cycle` | No | Drive one iteration of the PAL's event loop. NULL means no loop to drive. |

## `qwrt_pal_cb_t`

```c
typedef void (*qwrt_pal_cb_t)(void *user_data, int status,
                               const char *data, size_t data_len);
```

Standard callback for async PAL operations.

| Parameter | Description |
|-----------|-------------|
| `user_data` | Opaque pointer passed through from the call site |
| `status` | A `qwrt_pal_err_t` value (0 = success, <0 = error) |
| `data` | Result payload (JSON string, file content, etc.) |
| `data_len` | Length of `data` in bytes (0 if data is NULL) |

## `qwrt_pal_stream_ops_t`

```c
typedef struct qwrt_pal_stream_ops {
    void (*on_headers)(void *user_data, int status, const char *headers_json);
    void (*on_data)(void *user_data, const char *data, size_t len);
    void (*on_end)(void *user_data, int error_status);
    void *user_data;
} qwrt_pal_stream_ops_t;
```

Streaming callbacks for HTTP responses. The sequence is:
1. `on_headers` — HTTP status + headers (once, may be the only call on error)
2. `on_data` — body chunk (zero or more times)
3. `on_end` — stream finished or aborted (exactly once)

## See Also

- [PAL Error Codes](/c-api/errors) — standard error return values
- [PAL Documentation](/pal/) — implementing your own backend
- [PAL Callback Types](/pal/callbacks) — detailed callback documentation
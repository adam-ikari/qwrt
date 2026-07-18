---
title: PAL Interface Reference
description: Complete reference of the qwrt_pal_t interface — function pointers, callback types, streaming operations, and lifetime requirements.
---

# PAL Interface Reference

Complete reference for every field in `qwrt_pal_t`.

## Identity & Lifecycle

### `user_data`

```c
void *user_data;
```

Opaque pointer for the PAL implementation's private state. Set by the constructor, never touched by qwrt core.

### `version`

```c
uint32_t version;
```

PAL interface version. MUST be `1` for the current ABI. Old PALs that leave this at 0 (zero-initialized) are treated as "pre-versioning" and still work.

### `name`

```c
const char *name;
```

Human-readable name for diagnostics: `"libuv"`, `"mock"`, `"freertos"`. Must be a static string or otherwise outlive the struct. May be NULL.

### `init`

```c
int (*init)(qwrt_pal_t *pal);
```

Called once by `qwrt_create()` before any other method. Allocate resources, open connections, etc. Return `QWRT_OK` on success, `QWRT_ERR_*` on failure. If this fails, `qwrt_create()` returns NULL. **OPTIONAL** — NULL means no initialization needed.

### `destroy`

```c
void (*destroy)(qwrt_pal_t *pal);
```

Called once by `qwrt_destroy()` after all contexts are freed. Release all resources, cancel pending async operations. After this returns, no further PAL methods are called. **OPTIONAL** — NULL means the embedder frees the PAL manually.

## HTTP

### `http_request`

```c
void (*http_request)(qwrt_pal_t *pal,
                     const char *url, const char *method,
                     const char *headers, const char *body,
                     size_t body_len,
                     qwrt_pal_cb_t cb, void *cb_data);
```

Perform an HTTP request and deliver the full response body. The callback receives `QWRT_OK` + a JSON string on success, or `QWRT_ERR_*` on failure.

Response JSON format:
```json
{"status": 200, "headers": {"Content-Type": "application/json"}, "body": "..."}
```

### `http_request_stream`

```c
void (*http_request_stream)(qwrt_pal_t *pal,
                            const char *url, const char *method,
                            const char *headers, const char *body,
                            size_t body_len,
                            qwrt_pal_stream_ops_t *ops);
```

Streaming HTTP request. Response delivered through `qwrt_pal_stream_ops_t` callbacks instead of a single final callback. Use for large/long-lived responses (SSE, LLM streaming). **OPTIONAL** — NULL if the PAL doesn't support streaming.

### `http_abort`

```c
void (*http_abort)(qwrt_pal_t *pal);
```

Abort the currently-active streaming HTTP request. Delivers `QWRT_ERR_CANCELLED` to the stream's `on_end` callback. Safe to call when no stream is active (no-op). **OPTIONAL** — NULL if streaming is not supported.

## Filesystem

### `fs_read`

```c
void (*fs_read)(qwrt_pal_t *pal, const char *path,
                qwrt_pal_cb_t cb, void *cb_data);
```

Read the full contents of a file. Callback receives `QWRT_OK` + file content, or `QWRT_ERR_*`.

### `fs_write`

```c
void (*fs_write)(qwrt_pal_t *pal, const char *path,
                 const char *data, size_t data_len,
                 qwrt_pal_cb_t cb, void *cb_data);
```

Write data to a file (creates or overwrites). Callback receives `QWRT_OK` or `QWRT_ERR_*`.

### `fs_exists`

```c
void (*fs_exists)(qwrt_pal_t *pal, const char *path,
                  qwrt_pal_cb_t cb, void *cb_data);
```

Check whether a file exists. Callback receives `QWRT_OK` (exists) or `QWRT_ERR_NOT_FOUND` (doesn't exist).

### `fs_remove`

```c
void (*fs_remove)(qwrt_pal_t *pal, const char *path,
                  qwrt_pal_cb_t cb, void *cb_data);
```

Delete a file. Callback receives `QWRT_OK` or `QWRT_ERR_*`.

### `fs_list`

```c
void (*fs_list)(qwrt_pal_t *pal, const char *path,
                qwrt_pal_cb_t cb, void *cb_data);
```

List entries in a directory. Callback receives `QWRT_OK` + JSON array of entry names.

## Storage

### `storage_get`

```c
void (*storage_get)(qwrt_pal_t *pal, const char *key,
                    qwrt_pal_cb_t cb, void *cb_data);
```

Get a value by key. Callback receives `QWRT_OK` + value, or `QWRT_ERR_NOT_FOUND`.

### `storage_set`

```c
void (*storage_set)(qwrt_pal_t *pal, const char *key,
                    const char *value, size_t value_len,
                    qwrt_pal_cb_t cb, void *cb_data);
```

Set a key-value pair (creates or overwrites). Callback receives `QWRT_OK` or `QWRT_ERR_*`.

### `storage_del`

```c
void (*storage_del)(qwrt_pal_t *pal, const char *key,
                    qwrt_pal_cb_t cb, void *cb_data);
```

Delete a key. Callback receives `QWRT_OK` or `QWRT_ERR_*` (including `QWRT_ERR_NOT_FOUND` if absent).

## Timers

### `timer_start`

```c
void *(*timer_start)(qwrt_pal_t *pal, uint64_t delay_ms, int repeat,
                      qwrt_pal_cb_t cb, void *cb_data);
```

Start a timer. `delay_ms` is the delay before first (or only) fire. If `repeat` is non-zero, the timer fires repeatedly. Callback receives `QWRT_OK` on each fire. Returns an opaque handle for `timer_stop()`, or NULL on `QWRT_ERR_NO_MEMORY`.

### `timer_stop`

```c
void (*timer_stop)(qwrt_pal_t *pal, void *handle);
```

Stop and free a timer. Safe to call with NULL (no-op).

## Time

### `time_now`

```c
uint64_t (*time_now)(qwrt_pal_t *pal);
```

Current wall-clock time in milliseconds since Unix epoch.

### `hrtime`

```c
uint64_t (*hrtime)(qwrt_pal_t *pal);
```

High-resolution monotonic time in nanoseconds since an arbitrary (but fixed) epoch. Used by `performance.now()`.

## Utilities

### `log`

```c
void (*log)(qwrt_pal_t *pal, int level, const char *msg);
```

Emit a log message. Level semantics follow syslog: 0=EMERG, 7=DEBUG. **OPTIONAL** — NULL means logs are silently dropped.

### `mem_alloc`

```c
void *(*mem_alloc)(qwrt_pal_t *pal, size_t size);
```

Allocate memory. If NULL, `malloc()` is used by the core.

### `mem_free`

```c
void (*mem_free)(qwrt_pal_t *pal, void *ptr);
```

Free memory allocated by `mem_alloc`. If NULL, `free()` is used.

### `random_bytes`

```c
void (*random_bytes)(qwrt_pal_t *pal, uint8_t *buf, size_t len);
```

Fill `buf[0..len-1]` with cryptographically-secure random bytes. Synchronous by design — hardware RNGs and `/dev/urandom` are fast enough.

## Event Loop

### `run_cycle`

```c
int (*run_cycle)(qwrt_pal_t *pal, int timeout_ms);
```

Drive one iteration of the PAL's event loop. Returns the number of events processed, 0 if timeout elapsed, or `< 0` if the loop should stop. **OPTIONAL** — NULL means no event loop to drive.

## Process Management

### `spawn`

```c
void *(*spawn)(qwrt_pal_t *pal,
               const char *cmd,
               const char *const *args,
               const char *const *env);
```

Spawn a child process. `args` is NULL-terminated (args[0] = cmd by convention). `env` is NULL-terminated "KEY=VALUE" pairs, or NULL to inherit. Returns opaque handle or NULL on failure. **OPTIONAL**.

### `channel_*`

```c
void *(*spawn_get_stdin)(qwrt_pal_t *pal, void *proc);
void *(*spawn_get_stdout)(qwrt_pal_t *pal, void *proc);
int   (*channel_send)(qwrt_pal_t *pal, void *ch, const char *data, size_t len);
void  (*channel_recv)(qwrt_pal_t *pal, void *ch, qwrt_pal_cb_t cb, void *cb_data);
void  (*channel_close)(qwrt_pal_t *pal, void *ch);
```

IPC channel to child process stdin/stdout. All **OPTIONAL** — NULL if platform doesn't support child processes.

### `join` / `terminate`

```c
int  (*join)(qwrt_pal_t *pal, void *proc, int timeout_ms);
void (*terminate)(qwrt_pal_t *pal, void *proc);
```

Wait for child to exit (`join`) or force-kill (`terminate`). **OPTIONAL**.

## Reserved

```c
void *reserved[4];
```

Must be initialized to NULL. Future versions of qwrt will assign meaning to these slots.

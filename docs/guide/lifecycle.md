---
title: Runtime Lifecycle
description: Qwrt.js runtime lifecycle — create, configure, use, and destroy. Understand qwrt_create, qwrt_destroy, and the message loop.
---

# Runtime Lifecycle

Every qwrt program follows the same lifecycle: **create → use → destroy**.

## Creating a Runtime

```c
qwrt_config_t config = {
    .initial_script = "postMessage('ready');",  // eval'd on qwrt's thread at create
    .message_cb = on_message,                   // outbound messages
    .debug = 0,                                 // Enable debug output (0 or 1)
};
qwrt_t *rt = qwrt_create(&config);
if (!rt) {
    // Creation failed — initial_script threw, or thread/loop init failed
}
```

`qwrt_create` does the following:
1. Starts qwrt's internal thread and initializes the embedded libuv loop
2. Creates the `JSRuntime` and initial context
3. Registers the build-time extension set (the `QWRT_EXTENSIONS` table —
   built-ins like compress/crypto/textcodec/wamr when their `QWRT_WITH_*` is on,
   plus any user extensions added via `QWRT_EXTRA_SOURCES`)
4. Injects the WinterTC-compatible runtime into the initial context
5. Eval's `initial_script` on the internal thread — a throw makes `qwrt_create`
   return `NULL`

`qwrt_create` blocks until the internal thread is ready and `initial_script`
has been eval'd. The runtime owns all its resources — there is no external PAL
to keep alive.

## Destroying a Runtime

```c
qwrt_destroy(rt);  // graceful shutdown, host thread only, NULL-safe
```

`qwrt_destroy`:
1. Requests the internal thread to exit and joins it
2. Destroys all contexts (calls extension `destroy` hooks)
3. Frees the `JSRuntime` and the embedded libuv loop
4. Frees the runtime

`qwrt_destroy(NULL)` is safe (no-op).

## Thread Safety

- **All JS runs on qwrt's internal thread** — the host thread never calls into JS
- **`qwrt_post_message` is thread-safe** — call it from any thread; the JSON is copied
- **`message_cb` fires on the qwrt thread** — your callback must be thread-safe
- **`qwrt_destroy` is host-thread-only** — call it from the thread that called `qwrt_create`

## Memory Model

- All per-runtime state lives on `qwrt_t` — there is **zero mutable file-scope state**
- QuickJS class IDs are runtime-scoped (shared across contexts within one `qwrt_t`)
- Recover `qwrt_t*` from a `JSContext*` via `qwrt_get_rt_from_ctx(ctx)` (internal)

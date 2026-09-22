---
title: Runtime Lifecycle
description: qzjs runtime lifecycle — create, configure, use, and destroy. Understand qz_create, qz_destroy, and the message loop.
---

# Runtime Lifecycle

Every qzjs program follows the same lifecycle: **create → use → destroy**.

## Creating a Runtime

```c
qz_config_t config = {
    .initial_script = "postMessage('ready');",  // eval'd on qzjs's thread at create
    .message_cb = on_message,                   // outbound messages
    .debug = 0,                                 // Enable debug output (0 or 1)
};
qz_t *rt = qz_create(&config);
if (!rt) {
    // Creation failed — initial_script threw, or thread/loop init failed
}
```

`qz_create` does the following:
1. Starts qzjs's internal thread and initializes the embedded libuv loop
2. Creates the `JSRuntime` and initial context
3. Registers the build-time extension set (the `QZ_EXTENSIONS` table —
   built-ins like compress/crypto/textcodec/wamr when their `QZ_WITH_*` is on,
   plus any user extensions added via `QZ_EXTRA_SOURCES`)
4. Injects the WinterTC-compatible runtime into the initial context
5. Eval's `initial_script` on the internal thread — a throw makes `qz_create`
   return `NULL`

`qz_create` blocks until the internal thread is ready and `initial_script`
has been eval'd. The runtime owns all its resources — there is no external PAL
to keep alive.

## Destroying a Runtime

```c
qz_destroy(rt);  // graceful shutdown, host thread only, NULL-safe
```

`qz_destroy`:
1. Requests the internal thread to exit and joins it
2. Destroys all contexts (calls extension `destroy` hooks)
3. Frees the `JSRuntime` and the embedded libuv loop
4. Frees the runtime

`qz_destroy(NULL)` is safe (no-op).

## Thread Safety

- **All JS runs on qzjs's internal thread** — the host thread never calls into JS
- **`qz_post_message` is thread-safe** — call it from any thread; the JSON is copied
- **`message_cb` fires on the qzjs thread** — your callback must be thread-safe
- **`qz_destroy` is host-thread-only** — call it from the thread that called `qz_create`

## Memory Model

- All per-runtime state lives on `qz_t` — there is **zero mutable file-scope state**
- QuickJS class IDs are runtime-scoped (shared across contexts within one `qz_t`)
- Recover `qz_t*` from a `JSContext*` via `qz_get_rt_from_ctx(ctx)` (internal)

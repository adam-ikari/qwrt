---
title: Runtime Lifecycle
description: Amoib.js runtime lifecycle — create, configure, use, and destroy. Understand am_create, am_destroy, and the message loop.
---

# Runtime Lifecycle

Every amoib program follows the same lifecycle: **create → use → destroy**.

## Creating a Runtime

```c
am_config_t config = {
    .initial_script = "postMessage('ready');",  // eval'd on amoib's thread at create
    .message_cb = on_message,                   // outbound messages
    .debug = 0,                                 // Enable debug output (0 or 1)
};
am_t *rt = am_create(&config);
if (!rt) {
    // Creation failed — initial_script threw, or thread/loop init failed
}
```

`am_create` does the following:
1. Starts amoib's internal thread and initializes the embedded libuv loop
2. Creates the `JSRuntime` and initial context
3. Registers the build-time extension set (the `AM_EXTENSIONS` table —
   built-ins like compress/crypto/textcodec/wamr when their `AM_WITH_*` is on,
   plus any user extensions added via `AM_EXTRA_SOURCES`)
4. Injects the WinterTC-compatible runtime into the initial context
5. Eval's `initial_script` on the internal thread — a throw makes `am_create`
   return `NULL`

`am_create` blocks until the internal thread is ready and `initial_script`
has been eval'd. The runtime owns all its resources — there is no external PAL
to keep alive.

## Destroying a Runtime

```c
am_destroy(rt);  // graceful shutdown, host thread only, NULL-safe
```

`am_destroy`:
1. Requests the internal thread to exit and joins it
2. Destroys all contexts (calls extension `destroy` hooks)
3. Frees the `JSRuntime` and the embedded libuv loop
4. Frees the runtime

`am_destroy(NULL)` is safe (no-op).

## Thread Safety

- **All JS runs on amoib's internal thread** — the host thread never calls into JS
- **`am_post_message` is thread-safe** — call it from any thread; the JSON is copied
- **`message_cb` fires on the amoib thread** — your callback must be thread-safe
- **`am_destroy` is host-thread-only** — call it from the thread that called `am_create`

## Memory Model

- All per-runtime state lives on `am_t` — there is **zero mutable file-scope state**
- QuickJS class IDs are runtime-scoped (shared across contexts within one `am_t`)
- Recover `am_t*` from a `JSContext*` via `am_get_rt_from_ctx(ctx)` (internal)

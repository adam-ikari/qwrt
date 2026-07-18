---
title: Runtime Lifecycle
description: Qwrt.js runtime lifecycle — create, configure, use, and destroy. Understand qwrt_create, qwrt_destroy, qwrt_reset, and the event loop.
---

# Runtime Lifecycle

Every qwrt program follows the same lifecycle: **create → use → destroy**.

## Creating a Runtime

```c
qwrt_config_t config = {
    .pal = pal,          // Platform Abstraction Layer (required)
    .debug = 0,          // Enable debug output (0 or 1)
};
qwrt_t *rt = qwrt_create(&config);
if (!rt) {
    // Creation failed — check PAL init, memory, etc.
}
```

`qwrt_create` does the following:
1. Calls `pal->init(pal)` if the hook is provided
2. Creates a JSRuntime and initial context
3. Registers the build-time extension set (the `QWRT_EXTENSIONS` table —
   built-ins like compress/crypto/textcodec/wamr when their `QWRT_WITH_*` is on,
   plus any user extensions added via `QWRT_EXTRA_SOURCES`)
4. Injects the WinterCG-compatible runtime into the initial context

The PAL must outlive the runtime. `qwrt_destroy` does NOT free the PAL — the caller owns it.

## Destroying a Runtime

```c
qwrt_destroy(rt);
// pal is still valid — caller must free it separately
pal_uv_destroy(pal);  // or pal_mock_destroy, pal_freertos_destroy, etc.
```

`qwrt_destroy`:
1. Destroys all contexts (calls extension `destroy` hooks)
2. Drains any remaining deferred callbacks (but does not execute them)
3. Frees the JSRuntime
4. Calls `pal->destroy(pal)` if the hook is provided

`qwrt_destroy(NULL)` is safe (no-op).

## Resetting a Runtime

Reset destroys all contexts and creates a fresh initial context, keeping the same PAL:

```c
qwrt_config_t new_config = { .pal = pal, .debug = 0 };
if (qwrt_reset(rt, &new_config) != 0) {
    // Reset failed
}
```

This is useful for:
- Clearing all JS state without recreating the PAL
- Changing debug mode
- Recovering from a corrupted JS state

## Thread Safety

- **JSContext is thread-bound**: all `qwrt_*` calls must come from the thread that called `qwrt_create`
- **No internal locking**: the caller is responsible for thread discipline
- **PAL callbacks**: fire on the event loop thread; use `qwrt_defer_callback` to safely dispatch to the JS thread

## Memory Model

- All per-runtime state lives on `qwrt_t` — there is **zero mutable file-scope state**
- QuickJS class IDs are runtime-scoped (shared across contexts within one `qwrt_t`)
- Recover `qwrt_t*` from a `JSContext*` via `qwrt_get_rt_from_ctx(ctx)` (internal)

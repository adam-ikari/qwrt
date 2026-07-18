---
title: Extensions
description: Build-time native C extensions for Qwrt.js — qwrt_ext_t interface, QWRT_EXTENSIONS macro, lifecycle hooks, and per-runtime data.
---

# Extensions

Extensions are native C modules that add global objects and functions to JS contexts. They implement the `qwrt_ext_t` interface with lifecycle hooks.

## Built-in Extensions

| Extension | Option | JS API |
|-----------|--------|-------|
| `ext_compress` | `QWRT_WITH_COMPRESS` | gzip/zlib/deflate compression |
| `ext_crypto` | `QWRT_WITH_CRYPTO_EXT` | SHA, HMAC, PBKDF2, AES-GCM |
| `ext_textcodec` | `QWRT_WITH_TEXTCODEC` | UTF-8, Base64 encode/decode |
| `ext_wamr` | `QWRT_WITH_WAMR` | WebAssembly via WAMR (default) |
| `ext_wasm3` | `QWRT_WITH_WASM3` | WebAssembly via wasm3 (optional) |

**Note:** `ext_wamr` and `ext_wasm3` are mutually exclusive — both register the `WebAssembly` global, so only one can be enabled per build.

Built-in extensions are automatically registered on every new context.

## Extension Interface

```c
typedef struct qwrt_ext_t {
    const char *name;          // Human-readable name for diagnostics
    int (*init)(qwrt_ext_t *ext, qwrt_t *rt);      // Called on context creation
    void (*destroy)(qwrt_ext_t *ext, qwrt_t *rt);   // Called on context destruction
    int (*suspend)(qwrt_ext_t *ext, qwrt_t *rt);    // Called on context suspend
    int (*resume)(qwrt_ext_t *ext, qwrt_t *rt);     // Called on context resume
    void *user_data;           // Opaque extension state
} qwrt_ext_t;
```

## Writing a Custom Extension

```c
#include <qwrt/qwrt.h>
#include <quickjs.h>

static int my_ext_init(qwrt_ext_t *ext, qwrt_t *rt) {
    JSContext *ctx = qwrt_get_jsctx(rt);
    if (!ctx) return -1;

    // Add a global function
    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "hello",
        JS_NewCFunction(ctx, my_hello_fn, "hello", 0));
    JS_FreeValue(ctx, global);

    return 0;  // success
}

static void my_ext_destroy(qwrt_ext_t *ext, qwrt_t *rt) {
    // Clean up any extension resources
    // JSContext cleanup is handled by qwrt
}

static int my_ext_suspend(qwrt_ext_t *ext, qwrt_t *rt) {
    // Save state, close connections, etc.
    return 0;
}

static int my_ext_resume(qwrt_ext_t *ext, qwrt_t *rt) {
    // Restore state, reopen connections, etc.
    return 0;
}

qwrt_ext_t my_extension = {
    .name = "my_extension",
    .init = my_ext_init,
    .destroy = my_ext_destroy,
    .suspend = my_ext_suspend,
    .resume = my_ext_resume,
    .user_data = NULL,
};
```

## Registering Extensions

Extensions are registered at **build time** via the `QWRT_EXTENSIONS` macro
(defined in `include/qwrt/qwrt_ext_registry.h`). There is no runtime
registration API — the extension set is fixed when the qwrt library is compiled.

### Built-in extensions

Built-in extensions (compress/crypto/textcodec/wamr) are auto-registered when
their `QWRT_WITH_*` CMake option is on. They appear in `QWRT_DEFAULT_EXTENSIONS`
as conditional slots (a disabled built-in becomes a NULL slot that's skipped at
init).

### Adding a custom extension (non-invasive)

A parent project adds its own extension **without editing qwrt source**: compile
the extension's `.c` into the qwrt target (so its `&my_extension` symbol is
visible to `context.c`) and append it to `QWRT_EXTENSIONS`:

```cmake
# In the parent project's CMakeLists.txt, before add_subdirectory(qwrt):
set(QWRT_EXTENSIONS "QWRT_DEFAULT_EXTENSIONS, &my_extension")
set(QWRT_EXTRA_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/my_extension.c)
add_subdirectory(deps/qwrt)
```

`QWRT_EXTRA_SOURCES` adds the source to the `qwrt` target; `QWRT_EXTENSIONS`
overrides the table to append `&my_extension` after the default set. To **trim**
a built-in, list only the entries you want instead of `QWRT_DEFAULT_EXTENSIONS`.

## Lifecycle Hooks

- **`init`** — called when the extension is registered on a context (at `qwrt_create` / `qwrt_spawn` / `qwrt_reset`). Register JS globals, allocate resources. Return 0 on success, <0 on failure.
- **`destroy`** — called when the context is destroyed. Free extension resources. JSContext cleanup is automatic — you only need to free your own allocations.
- **`suspend`** — called when the context is suspended. Save state, pause timers, close connections.
- **`resume`** — called when the context is resumed. Restore state, resume timers, reopen connections.

All hooks receive both the extension and the runtime. Get the active `JSContext*` via `qwrt_get_jsctx(rt)`.

### Per-runtime data in init

The `qwrt_ext_t.user_data` field lives on the **shared compile-time** extension
struct — it is NOT per-instance. To get per-runtime data inside `init` (which
runs during `qwrt_create`, before the host has the `rt`), set
`config.host_data` before `qwrt_create` and read it via
`qwrt_get_runtime_data(rt)`:

```c
/* host: */
qwrt_config_t cfg = { .pal = pal, .host_data = my_per_rt_state };
qwrt_t *rt = qwrt_create(&cfg);

/* extension init: */
static int my_ext_init(qwrt_ext_t *ext, qwrt_t *rt) {
    my_state_t *st = (my_state_t *)qwrt_get_runtime_data(rt);
    /* st is the per-instance data the host set via config */
    ...
}
```

This resolves the init-time ordering deadlock: the `rt` is valid inside `init`
even though the host hasn't received it yet.

---
title: Extensions
description: Build-time native C extensions for Amoib.js — am_ext_t interface, AM_EXTENSIONS macro, lifecycle hooks, and per-runtime data.
---

# Extensions

Extensions are native C modules that add global objects and functions to JS contexts. They implement the `am_ext_t` interface with lifecycle hooks.

## Built-in Extensions

| Extension | Option | JS API |
|-----------|--------|-------|
| `ext_compress` | `AM_WITH_COMPRESS` | gzip/zlib/deflate compression |
| `ext_crypto` | `AM_WITH_CRYPTO_EXT` | SHA, HMAC, PBKDF2, AES-GCM |
| `ext_textcodec` | `AM_WITH_TEXTCODEC` | UTF-8, Base64 encode/decode |
| `ext_wamr` | `AM_WITH_WAMR` | WebAssembly via WAMR (default) |
| `ext_wasm3` | `AM_WITH_WASM3` | WebAssembly via wasm3 (optional) |

**Note:** `ext_wamr` and `ext_wasm3` are mutually exclusive — both register the `WebAssembly` global, so only one can be enabled per build.

Built-in extensions are automatically registered on every new context.

## Extension Interface

```c
typedef struct am_ext_t {
    const char *name;          // Human-readable name for diagnostics
    int (*init)(am_ext_t *ext, am_t *rt);      // Called on context creation
    void (*destroy)(am_ext_t *ext, am_t *rt);   // Called on context destruction
    int (*suspend)(am_ext_t *ext, am_t *rt);    // Called on context suspend
    int (*resume)(am_ext_t *ext, am_t *rt);     // Called on context resume
    void *user_data;           // Opaque extension state
} am_ext_t;
```

## Writing a Custom Extension

```c
#include <amoib/amoib.h>
#include <quickjs.h>
#include "am_internal.h"   // am_get_active_jsctx (internal helper)

static int my_ext_init(am_ext_t *ext, am_t *rt) {
    JSContext *ctx = am_get_active_jsctx(rt);
    if (!ctx) return -1;

    // Add a global function
    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "hello",
        JS_NewCFunction(ctx, my_hello_fn, "hello", 0));
    JS_FreeValue(ctx, global);

    return 0;  // success
}

static void my_ext_destroy(am_ext_t *ext, am_t *rt) {
    // Clean up any extension resources
    // JSContext cleanup is handled by amoib
}

static int my_ext_suspend(am_ext_t *ext, am_t *rt) {
    // Save state, close connections, etc.
    return 0;
}

static int my_ext_resume(am_ext_t *ext, am_t *rt) {
    // Restore state, reopen connections, etc.
    return 0;
}

am_ext_t my_extension = {
    .name = "my_extension",
    .init = my_ext_init,
    .destroy = my_ext_destroy,
    .suspend = my_ext_suspend,
    .resume = my_ext_resume,
    .user_data = NULL,
};
```

## Registering Extensions

Extensions are registered at **build time** via the `AM_EXTENSIONS` macro
(defined in `include/amoib/am_ext_registry.h`). There is no runtime
registration API — the extension set is fixed when the amoib library is compiled.

### Built-in extensions

Built-in extensions (compress/crypto/textcodec/wamr) are auto-registered when
their `AM_WITH_*` CMake option is on. They appear in `AM_DEFAULT_EXTENSIONS`
as conditional slots (a disabled built-in becomes a NULL slot that's skipped at
init).

### Adding a custom extension (non-invasive)

A parent project adds its own extension **without editing amoib source**: compile
the extension's `.c` into the amoib target (so its `&my_extension` symbol is
visible to `context.c`) and append it to `AM_EXTENSIONS`:

```cmake
# In the parent project's CMakeLists.txt, before add_subdirectory(amoib):
set(AM_EXTENSIONS "AM_DEFAULT_EXTENSIONS, &my_extension")
set(AM_EXTRA_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/my_extension.c)
add_subdirectory(deps/amoib)
```

`AM_EXTRA_SOURCES` adds the source to the `amoib` target; `AM_EXTENSIONS`
overrides the table to append `&my_extension` after the default set. To **trim**
a built-in, list only the entries you want instead of `AM_DEFAULT_EXTENSIONS`.

## Lifecycle Hooks

- **`init`** — called when the extension is registered on a context (at `am_create`, or when a worker context is created). Register JS globals, allocate resources. Return 0 on success, <0 on failure.
- **`destroy`** — called when the context is destroyed. Free extension resources. JSContext cleanup is automatic — you only need to free your own allocations.
- **`suspend`** — called when the context is suspended. Save state, pause timers, close connections.
- **`resume`** — called when the context is resumed. Restore state, resume timers, reopen connections.

All hooks receive both the extension and the runtime. Get the active `JSContext*` via `am_get_active_jsctx(rt)` (internal, `src/am_internal.h`).

### Per-runtime data in init

The `am_ext_t.user_data` field lives on the **shared compile-time** extension
struct — it is NOT per-instance. To get per-runtime data inside `init` (which
runs during `am_create`, before the host has the `rt`), set
`config.host_data` before `am_create` and read it via
`am_get_runtime_data(rt)`:

```c
/* host: */
am_config_t cfg = { .pal = pal, .host_data = my_per_rt_state };
am_t *rt = am_create(&cfg);

/* extension init: */
static int my_ext_init(am_ext_t *ext, am_t *rt) {
    my_state_t *st = (my_state_t *)am_get_runtime_data(rt);
    /* st is the per-instance data the host set via config */
    ...
}
```

This resolves the init-time ordering deadlock: the `rt` is valid inside `init`
even though the host hasn't received it yet.

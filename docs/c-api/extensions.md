# Extensions (C API)

Extensions are native C modules that add global objects and functions to JS contexts. They implement the `qwrt_ext_t` interface with lifecycle hooks.

## `qwrt_ext_t`

```c
typedef struct qwrt_ext_t {
    const char *name;
    int (*init)(qwrt_ext_t *ext, qwrt_t *rt);
    void (*destroy)(qwrt_ext_t *ext, qwrt_t *rt);
    int (*suspend)(qwrt_ext_t *ext, qwrt_t *rt);
    int (*resume)(qwrt_ext_t *ext, qwrt_t *rt);
    void *user_data;
} qwrt_ext_t;
```

| Field | Description |
|-------|-------------|
| `name` | Human-readable name for diagnostics |
| `init` | Called on context creation — register JS globals, allocate resources. Return 0 on success, <0 on failure. |
| `destroy` | Called on context destruction — free extension resources. JSContext cleanup is automatic. |
| `suspend` | Called on context suspend — save state, pause timers, close connections. |
| `resume` | Called on context resume — restore state, resume timers, reopen connections. |
| `user_data` | Opaque extension state. **Note:** This is shared across all runtimes — for per-instance data, use `config.host_data`. |

## Registration Model

Extensions are registered at **build time** via the `QWRT_EXTENSIONS` macro (defined in `include/qwrt/qwrt_ext_registry.h`). There is no runtime registration API — the extension set is fixed when the qwrt library is compiled.

```c
// include/qwrt/qwrt_ext_registry.h
#define QWRT_DEFAULT_EXTENSIONS \
    QWRT_EXT_IF_WITH(COMPRESS,   &qwrt_compress_ext) \
    QWRT_EXT_IF_WITH(CRYPTO_EXT, &qwrt_crypto_ext)   \
    QWRT_EXT_IF_WITH(TEXTCODEC,  &qwrt_textcodec_ext) \
    QWRT_EXT_IF_WITH(WAMR,       &qwrt_wamr_ext)
```

A parent project adds custom extensions by overriding `QWRT_EXTENSIONS` before including the qwrt subdirectory:

```cmake
set(QWRT_EXTENSIONS "QWRT_DEFAULT_EXTENSIONS, &my_extension")
set(QWRT_EXTRA_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/my_extension.c)
add_subdirectory(deps/qwrt)
```

## Built-in Extensions

| Extension | CMake Option | JS API |
|-----------|-------------|-------|
| `ext_compress` | `QWRT_WITH_COMPRESS` | gzip/zlib/deflate |
| `ext_crypto` | `QWRT_WITH_CRYPTO_EXT` | crypto.subtle (SHA, HMAC, PBKDF2, AES-GCM) |
| `ext_textcodec` | `QWRT_WITH_TEXTCODEC` | TextEncoder, TextDecoder |
| `ext_wamr` | `QWRT_WITH_WAMR` | WebAssembly (WAMR, default) |
| `ext_wasm3` | `QWRT_WITH_WASM3` | WebAssembly (wasm3, optional) |

## Writing an Extension

```c
#include <qwrt/qwrt.h>
#include <quickjs.h>

static int my_ext_init(qwrt_ext_t *ext, qwrt_t *rt) {
    JSContext *ctx = qwrt_get_jsctx(rt);
    if (!ctx) return -1;

    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "hello",
        JS_NewCFunction(ctx, my_hello_fn, "hello", 0));
    JS_FreeValue(ctx, global);

    return 0;
}

static void my_ext_destroy(qwrt_ext_t *ext, qwrt_t *rt) {
    // Free any extension-specific resources
}

qwrt_ext_t my_extension = {
    .name = "my_extension",
    .init = my_ext_init,
    .destroy = my_ext_destroy,
    .suspend = NULL,
    .resume = NULL,
    .user_data = NULL,
};
```

## Per-Runtime Data

`qwrt_ext_t.user_data` is shared across all runtimes. For per-instance state, use `config.host_data`:

```c
qwrt_config_t cfg = { .pal = pal, .host_data = my_per_rt_state };
qwrt_t *rt = qwrt_create(&cfg);

// Inside extension init:
my_state_t *st = (my_state_t *)qwrt_get_runtime_data(rt);
```

## See Also

- [Extensions Guide](/guide/extensions) — detailed extension documentation
- [Extension Registry Header](/c-api/runtime) — runtime lifecycle
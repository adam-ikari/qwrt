# Extensions

Extensions are native C modules that add global objects and functions to JS contexts. They implement the `qwrt_ext_t` interface with lifecycle hooks.

## Built-in Extensions

| Extension | Option | JS API |
|-----------|--------|-------|
| `ext_compress` | `QWRT_WITH_COMPRESS` | gzip/zlib/deflate compression |
| `ext_crypto` | `QWRT_WITH_CRYPTO_EXT` | SHA, HMAC, PBKDF2, AES-GCM |
| `ext_textcodec` | `QWRT_WITH_TEXTCODEC` | UTF-8, Base64 encode/decode |
| `ext_wasm3` | `QWRT_WITH_WASM3` | WebAssembly via wasm3 |
| `ext_wamr` | `QWRT_WITH_WAMR` | WebAssembly via WAMR |

Built-in extensions are automatically registered on every new context.

## Extension Interface

```c
typedef struct qwrt_ext_t {
    const char *name;          // Human-readable name for diagnostics
    int version;               // Extension version
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
    .version = 1,
    .init = my_ext_init,
    .destroy = my_ext_destroy,
    .suspend = my_ext_suspend,
    .resume = my_ext_resume,
    .user_data = NULL,
};
```

## Registering Extensions

### At Runtime Creation

```c
const qwrt_ext_t *exts[] = { &my_extension, NULL };
qwrt_config_t config = {
    .pal = pal,
    .extensions = exts,
};
qwrt_t *rt = qwrt_create(&config);
```

### At Runtime (Dynamic)

```c
qwrt_register_ext(rt, &my_extension);
```

This calls `ext->init` immediately on the active context.

## Lifecycle Hooks

- **`init`** — called when the extension is registered on a context. Register JS globals, allocate resources. Return 0 on success, <0 on failure.
- **`destroy`** — called when the context is destroyed. Free extension resources. JSContext cleanup is automatic — you only need to free your own allocations.
- **`suspend`** — called when the context is suspended. Save state, pause timers, close connections.
- **`resume`** — called when the context is resumed. Restore state, resume timers, reopen connections.

All hooks receive both the extension and the runtime. Get the active `JSContext*` via `qwrt_get_jsctx(rt)`.

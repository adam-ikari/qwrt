---
title: Embedding Patterns
description: Patterns for embedding Qwrt.js in C applications — host data, custom extensions, PAL integration, and multi-instance setups.
---

# Embedding Patterns

Common patterns for embedding qwrt in C applications.

## Basic Embedding

```c
#include <qwrt/qwrt.h>
#include <pal_uv.h>

int main(void) {
    qwrt_pal_t *pal = pal_uv_create(uv_default_loop());
    qwrt_t *rt = qwrt_create(&(qwrt_config_t){ .pal = pal });

    // Your application logic
    char *result = NULL;
    qwrt_eval(rt, "your_js_code_here", &result);
    qwrt_free(result);

    // Event loop
    pal->run_cycle(pal, 100); qwrt_tick(rt, 100);

    qwrt_destroy(rt);
    pal_uv_destroy(pal);
    return 0;
}
```

## Calling C Functions from JS

Register C functions as JS globals:

```c
#include <quickjs.h>

static JSValue greet(JSContext *ctx, JSValue this_val,
                     int argc, JSValue *argv) {
    QWRT_UNUSED(this_val);
    const char *name = "World";
    if (argc > 0) name = JS_ToCString(ctx, argv[0]);
    printf("Hello, %s!\n", name);
    if (argc > 0) JS_FreeCString(ctx, name);
    return JS_UNDEFINED;
}

// Register in a custom extension's init hook:
static int my_ext_init(qwrt_ext_t *ext, qwrt_t *rt) {
    QWRT_UNUSED(ext);
    JSContext *ctx = qwrt_get_jsctx(rt);
    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "greet",
        JS_NewCFunction(ctx, greet, "greet", 1));
    JS_FreeValue(ctx, global);
    return 0;
}
```

## Calling JS from C with Structured Data

```c
// Define a JS function
qwrt_eval(rt,
    "function process(data) {"
    "  return { doubled: data.value * 2, ok: true };"
    "}"
, NULL);

// Call it with JSON arguments
char *result = NULL;
qwrt_call(rt, "process", "[{\"value\": 21}]", &result);
// result = "{\"doubled\":42,\"ok\":true}"
printf("JS returned: %s\n", result);
qwrt_free(result);
```

## Per-Request Context Isolation

Create a fresh context for each incoming request, destroy it when done:

```c
void handle_request(qwrt_t *rt, const char *js_code) {
    qwrt_config_t cfg = { .pal = request_pal, .debug = 0 };
    int ctx_id = qwrt_spawn(rt, &cfg);

    qwrt_suspend(rt);
    qwrt_resume(rt, ctx_id);

    char *result = NULL;
    int ret = qwrt_eval(rt, js_code, &result);
    qwrt_free(result);

    qwrt_suspend(rt);
    qwrt_resume(rt, 0);  // back to main context
    qwrt_destroy_ctx(rt, ctx_id);
}
```

## Using the Mock PAL for Testing

```c
#include <pal_mock.h>

void test_my_js(void) {
    pal_mock_t *mock = pal_mock_create();

    // Pre-seed responses for fetch
    pal_mock_add_response(mock, "https://api.example.com/data",
        "{\"status\":200,\"headers\":{},\"body\":\"{\\\"ok\\\":true}\"}");

    qwrt_pal_t *pal = pal_mock_get_pal(mock);
    qwrt_t *rt = qwrt_create(&(qwrt_config_t){ .pal = pal });

    char *result = NULL;
    qwrt_eval(rt,
        "fetch('https://api.example.com/data')"
        "  .then(r => r.json())"
        "  .then(d => console.log(d.ok))",
        &result);
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_mock_destroy(mock);
}
```

## Multiple Independent Runtimes

Since qwrt has zero global state, you can run multiple `qwrt_t` instances:

```c
qwrt_t *rt1 = qwrt_create(&(qwrt_config_t){ .pal = pal1 });
qwrt_t *rt2 = qwrt_create(&(qwrt_config_t){ .pal = pal2 });

// Each is completely independent
qwrt_eval(rt1, "var x = 1;", NULL);
qwrt_eval(rt2, "var x = 2;", NULL);

// Drive both event loops
while (running) {
    pal1->run_cycle(pal1, 10);
    pal2->run_cycle(pal2, 10);
    qwrt_tick(rt1);
    qwrt_tick(rt2);
}

qwrt_destroy(rt1);
qwrt_destroy(rt2);
```

## Error Handling Patterns

```c
char *result = NULL;
int ret = qwrt_eval(rt, code, &result);

if (ret < 0) {
    // JS exception — result contains the error message
    fprintf(stderr, "JS error: %s\n", result ? result : "unknown");
    qwrt_free(result);
} else if (result) {
    // Success with a return value
    printf("OK: %s\n", result);
    qwrt_free(result);
}
// else: success with no return value (e.g., console.log)
```

## Memory Management

- Results from `qwrt_eval`/`qwrt_call`/`qwrt_compile` must be freed with `qwrt_free`
- `qwrt_free(NULL)` is safe
- The PAL is owned by the caller — free it after `qwrt_destroy`
- Per-runtime host data: set `config.host_data` before `qwrt_create`; an
  extension's `init` hook reads it via `qwrt_get_runtime_data(rt)` during
  create (the rt is valid inside init, before the host receives it). Note:
  `qwrt_ext_t.user_data` is on the shared compile-time extension struct — use
  `qwrt_get_runtime_data`/`qwrt_set_runtime_data` for per-instance data, not
  `user_data` (which is shared across runtimes).

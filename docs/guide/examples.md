# Examples

Complete runnable examples showing how to use Qwrt.js in your C applications. Each example builds with `cmake -DQWRT_BUILD_EXAMPLES=ON`.

## Minimal

The simplest possible qwrt program. Creates a runtime, evaluates JavaScript, prints the result.

```c
// example_minimal.c
#include <qwrt/qwrt.h>
#include <pal_uv.h>
#include <stdio.h>

int main(void) {
    qwrt_pal_t *pal = pal_uv_create(uv_default_loop());
    qwrt_t *rt = qwrt_create(&(qwrt_config_t){ .pal = pal });
    if (!rt) return 1;

    char *result = NULL;
    if (qwrt_eval(rt, "1 + 1", &result) == 0) {
        printf("1 + 1 = %s\n", result);
        qwrt_free(result);
    }

    pal->run_cycle(pal, 100); qwrt_tick(rt);

    qwrt_destroy(rt);
    pal_uv_destroy(pal);
    return 0;
}
```

**Key concepts:**
- `pal_uv_create()` — create the libuv PAL for Linux/macOS
- `qwrt_create()` — create runtime with config
- `qwrt_eval()` — evaluate JavaScript, get result as JSON string
- `qwrt_free()` — free result memory
- Event loop: `pal->run_cycle()` + `qwrt_tick()`
- `qwrt_destroy()` — clean up

## Mock

Uses the mock PAL for deterministic testing with no network or filesystem. Useful for unit tests and CI.

```c
// example_mock.c
#include <qwrt/qwrt.h>
#include <pal_mock.h>
#include <stdio.h>

int main(void) {
    qwrt_pal_t *pal = pal_mock_create();
    qwrt_t *rt = qwrt_create(&(qwrt_config_t){ .pal = pal });
    if (!rt) return 1;

    // Mock PAL lets you inject storage data
    char *result = NULL;
    qwrt_eval(rt, "console.log('Hello from mock!'); 42", &result);
    printf("Result: %s\n", result);
    qwrt_free(result);

    // No event loop needed for mock PAL
    qwrt_tick(rt);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
    return 0;
}
```

**Key differences from uv examples:**
- `pal_mock_create()` instead of `pal_uv_create()`
- `pal_mock_destroy()` instead of `pal_uv_destroy()`
- No event loop (`run_cycle` is NULL in mock)

## Fetch

Makes HTTP requests from JavaScript using the `fetch()` API. Requires the libuv PAL with networking.

```c
// example_fetch.c
#include <qwrt/qwrt.h>
#include <pal_uv.h>
#include <stdio.h>

int main(void) {
    qwrt_pal_t *pal = pal_uv_create(uv_default_loop());
    qwrt_t *rt = qwrt_create(&(qwrt_config_t){ .pal = pal });
    if (!rt) return 1;

    char *result = NULL;
    int rc = qwrt_eval(rt,
        "(async function() {"
        "  const res = await fetch('https://httpbin.org/json');"
        "  const data = await res.json();"
        "  return JSON.stringify(data);"
        "})()",
        &result);

    printf("Fetch: %s\n", result ? result : "ERROR");
    qwrt_free(result);

    pal->run_cycle(pal, 100); qwrt_tick(rt);

    qwrt_destroy(rt);
    pal_uv_destroy(pal);
    return 0;
}
```

**Key concepts:**
- Uses async/await inside qwrt_eval
- Promise resolution handled by the runtime
- Event loop drives the HTTP request/response

## Timers

Demonstrates setTimeout, setInterval, and timer cleanup.

```c
// example_timers.c
#include <qwrt/qwrt.h>
#include <pal_uv.h>
#include <stdio.h>

int main(void) {
    qwrt_pal_t *pal = pal_uv_create(uv_default_loop());
    qwrt_t *rt = qwrt_create(&(qwrt_config_t){ .pal = pal });
    if (!rt) return 1;

    char *result = NULL;
    qwrt_eval(rt,
        "(async function() {"
        "  const start = Date.now();"
        "  await new Promise(r => setTimeout(r, 50));"
        "  return 'Slept ' + (Date.now() - start) + 'ms';"
        "})()",
        &result);

    pal->run_cycle(pal, 100); qwrt_tick(rt);

    printf("Result: %s\n", result ? result : "ERROR");
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_uv_destroy(pal);
    return 0;
}
```

## Storage

Uses the key-value storage API to persist data between evaluations.

```c
// example_storage.c
#include <qwrt/qwrt.h>
#include <pal_uv.h>
#include <stdio.h>

int main(void) {
    qwrt_pal_t *pal = pal_uv_create(uv_default_loop());
    qwrt_t *rt = qwrt_create(&(qwrt_config_t){ .pal = pal });
    if (!rt) return 1;

    // Set a value
    qwrt_eval(rt, "localStorage.setItem('key', 'hello')", NULL);
    pal->run_cycle(pal, 100); qwrt_tick(rt);

    // Read it back
    char *result = NULL;
    qwrt_eval(rt, "localStorage.getItem('key')", &result);
    printf("storage.key = %s\n", result ? result : "ERROR");
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_uv_destroy(pal);
    return 0;
}
```

## Bytecode

Precompiles JavaScript to QuickJS bytecode for faster startup and smaller distribution.

```c
// example_bytecode.c
#include <qwrt/qwrt.h>
#include <pal_mock.h>
#include <stdio.h>

int main(void) {
    qwrt_pal_t *pal = pal_mock_create();
    qwrt_t *rt = qwrt_create(&(qwrt_config_t){ .pal = pal });
    if (!rt) return 1;

    // Compile source to bytecode
    size_t bc_len = 0;
    uint8_t *bc = qwrt_compile(rt, "1 + 1", 5, &bc_len);
    if (!bc) { printf("Compile failed\n"); return 1; }

    // Evaluate the bytecode
    char *result = NULL;
    qwrt_eval_bytecode(rt, bc, bc_len, &result);
    printf("Bytecode result: %s\n", result ? result : "ERROR");
    qwrt_free(result);
    qwrt_free(bc);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
    return 0;
}
```

## Error Handling

All qwrt functions that can fail return error codes. Use them:

```c
char *result = NULL;
int rc = qwrt_eval(rt, "throw new Error('oops')", &result);
if (rc != 0) {
    printf("Error: %s\n", result ? result : "unknown");
    // result contains the JS exception message
}
if (result) qwrt_free(result);
```

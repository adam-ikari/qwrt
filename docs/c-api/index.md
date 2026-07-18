# C API Reference

Qwrt.js exposes a small, focused C API surface. Every function operates on an opaque `qwrt_t*` runtime handle. The API is single-threaded — all calls must come from the thread that created the runtime.

## API Groups

| Group | Description |
|-------|-------------|
| [Runtime Lifecycle](/c-api/runtime) | `qwrt_create`, `qwrt_destroy`, `qwrt_tick` |
| [JS Evaluation](/c-api/eval) | `qwrt_eval`, `qwrt_eval_bytecode`, `qwrt_call`, `qwrt_compile` |
| [Multi-Context](/guide/multi-context) | `qwrt_spawn`, `qwrt_suspend`, `qwrt_resume`, `qwrt_destroy_ctx` |
| [PAL Interface](/c-api/pal) | `qwrt_pal_t`, callback types, streaming |
| [Extensions](/c-api/extensions) | `qwrt_ext_t`, lifecycle hooks |
| [Error Codes](/c-api/errors) | `qwrt_pal_err_t`, standardized error returns |
| [Host Data](/c-api/runtime#host-data) | `qwrt_get_runtime_data`, `qwrt_set_runtime_data` |

## Quick Example

```c
#include <qwrt/qwrt.h>
#include <pal_mock.h>
#include <stdio.h>

int main(void) {
    qwrt_pal_t *pal = pal_mock_create();
    qwrt_t *rt = qwrt_create(&(qwrt_config_t){ .pal = pal });
    if (!rt) { fprintf(stderr, "create failed\n"); return 1; }

    char *result = NULL;
    if (qwrt_eval(rt, "1 + 1", &result) == 0) {
        printf("1+1 = %s\n", result);
        qwrt_free(result);
    }

    qwrt_tick(rt);  // drain microtasks
    qwrt_destroy(rt);
    pal_mock_destroy(pal);
    return 0;
}
```

## Build Integration

```cmake
find_package(qwrt REQUIRED)
target_link_libraries(your_app PRIVATE qwrt::qwrt)
```

## Thread Model

Qwrt.js is **single-threaded** by design. The `JSContext` is bound to the thread that called `qwrt_create`. All subsequent `qwrt_*` calls must come from the same thread. PAL callbacks (from libuv, etc.) fire on the event-loop thread and are deferred via `qwrt_tick` to replay in the valid context.

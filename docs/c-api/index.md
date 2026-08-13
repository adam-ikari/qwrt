# C API Reference

Qwrt.js exposes a small, focused C API surface. Every function operates on an opaque `qwrt_t*` runtime handle. The API is single-threaded — all calls must come from the thread that created the runtime.

## API Groups

| Group | Description |
|-------|-------------|
| [Runtime Lifecycle](/c-api/runtime) | `qwrt_create`, `qwrt_destroy`, `qwrt_post_message` |
| [JS Evaluation](/c-api/eval) | Evaluating JavaScript in the runtime |
| [Multi-Context](/guide/multi-context) | Isolated JS contexts within one runtime |
| [Extensions](/c-api/extensions) | `qwrt_ext_t`, lifecycle hooks |
| [Host Data](/c-api/runtime#host-data) | `qwrt_get_runtime_data`, `qwrt_set_runtime_data` |

## Quick Example

```c
#include <qwrt/qwrt.h>
#include <stdio.h>

static void on_message(qwrt_t *rt, const char *json, size_t len, void *data) {
    (void)rt; (void)data;
    printf("received: %.*s\n", (int)len, json);
}

int main(void) {
    qwrt_config_t cfg = {0};
    cfg.initial_script = "postMessage(1 + 1);";
    cfg.message_cb = on_message;
    qwrt_t *rt = qwrt_create(&cfg);
    if (!rt) { fprintf(stderr, "create failed\n"); return 1; }

    qwrt_post_message(rt, "{\"cmd\":\"echo\",\"data\":\"hi\"}", 26);

    qwrt_destroy(rt);
    return 0;
}
```

## Build Integration

```cmake
find_package(qwrt REQUIRED)
target_link_libraries(your_app PRIVATE qwrt::qwrt)
```

## Thread Model

Qwrt.js is **single-threaded** by design. All JS runs on qwrt's own internal
thread (which also runs the embedded libuv loop) — the host thread never calls
into JS. There is no `qwrt_eval` and no `qwrt_tick`. The host communicates over
JSON messages: `qwrt_post_message` is thread-safe (inbound), and `message_cb`
fires on the qwrt thread (your callback must be thread-safe). `qwrt_destroy` is
host-thread-only.

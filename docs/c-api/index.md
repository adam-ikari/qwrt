# C API Reference

qzjs exposes a small, focused C API surface. Every function operates on an opaque `qz_t*` runtime handle. The API is single-threaded — all calls must come from the thread that created the runtime.

## API Groups

| Group | Description |
|-------|-------------|
| [Runtime Lifecycle](/c-api/runtime) | `qz_create`, `qz_destroy`, `qz_post_message` |
| [JS Evaluation](/c-api/eval) | Evaluating JavaScript in the runtime |
| [Multi-Context](/guide/multi-context) | Isolated JS contexts within one runtime |
| [Extensions](/c-api/extensions) | `qz_ext_t`, lifecycle hooks |
| [Host Data](/c-api/runtime#host-data) | `qz_get_runtime_data`, `qz_set_runtime_data` |

## Quick Example

```c
#include <qzjs/qzjs.h>
#include <stdio.h>

static void on_message(qz_t *rt, const char *json, size_t len, void *data) {
    (void)rt; (void)data;
    printf("received: %.*s\n", (int)len, json);
}

int main(void) {
    qz_config_t cfg = {0};
    cfg.initial_script = "postMessage(1 + 1);";
    cfg.message_cb = on_message;
    qz_t *rt = qz_create(&cfg);
    if (!rt) { fprintf(stderr, "create failed\n"); return 1; }

    qz_post_message(rt, "{\"cmd\":\"echo\",\"data\":\"hi\"}", 26);

    qz_destroy(rt);
    return 0;
}
```

## Build Integration

```cmake
find_package(qzjs REQUIRED)
target_link_libraries(your_app PRIVATE qzjs::qzjs)
```

## Thread Model

qzjs is **single-threaded** by design. All JS runs on qzjs's own internal
thread (which also runs the embedded libuv loop) — the host thread never calls
into JS. The host communicates over
JSON messages: `qz_post_message` is thread-safe (inbound), and `message_cb`
fires on the qzjs thread (your callback must be thread-safe). `qz_destroy` is
host-thread-only.

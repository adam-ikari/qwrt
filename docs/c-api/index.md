# C API Reference

Amoib.js exposes a small, focused C API surface. Every function operates on an opaque `am_t*` runtime handle. The API is single-threaded — all calls must come from the thread that created the runtime.

## API Groups

| Group | Description |
|-------|-------------|
| [Runtime Lifecycle](/c-api/runtime) | `am_create`, `am_destroy`, `am_post_message` |
| [JS Evaluation](/c-api/eval) | Evaluating JavaScript in the runtime |
| [Multi-Context](/guide/multi-context) | Isolated JS contexts within one runtime |
| [Extensions](/c-api/extensions) | `am_ext_t`, lifecycle hooks |
| [Host Data](/c-api/runtime#host-data) | `am_get_runtime_data`, `am_set_runtime_data` |

## Quick Example

```c
#include <amoib/amoib.h>
#include <stdio.h>

static void on_message(am_t *rt, const char *json, size_t len, void *data) {
    (void)rt; (void)data;
    printf("received: %.*s\n", (int)len, json);
}

int main(void) {
    am_config_t cfg = {0};
    cfg.initial_script = "postMessage(1 + 1);";
    cfg.message_cb = on_message;
    am_t *rt = am_create(&cfg);
    if (!rt) { fprintf(stderr, "create failed\n"); return 1; }

    am_post_message(rt, "{\"cmd\":\"echo\",\"data\":\"hi\"}", 26);

    am_destroy(rt);
    return 0;
}
```

## Build Integration

```cmake
find_package(amoib REQUIRED)
target_link_libraries(your_app PRIVATE amoib::amoib)
```

## Thread Model

Amoib.js is **single-threaded** by design. All JS runs on amoib's own internal
thread (which also runs the embedded libuv loop) — the host thread never calls
into JS. There is no `am_eval` and no `am_tick`. The host communicates over
JSON messages: `am_post_message` is thread-safe (inbound), and `message_cb`
fires on the amoib thread (your callback must be thread-safe). `am_destroy` is
host-thread-only.

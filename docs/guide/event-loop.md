# Event Loop

qzjs owns its own event loop on an internal thread. and no host-driven loop — the host does not pump anything.

## Who Runs the Loop

`qz_create` starts a dedicated internal thread (`uv_thread_t`) that runs a
libuv loop (`uv_loop_t` embedded in the runtime). That loop drives all async
work — HTTP, file I/O, timers — and all JS runs on the same thread, so Promise
microtasks are flushed naturally between loop iterations. The host thread never
touches the loop.

```mermaid
flowchart TB
    HOST["Host thread"] -->|"qz_post_message: JSON in"| AM["qzjs internal thread"]
    AM -->|"libuv loop (uv_run) + microtask flush"| AM
    AM -->|"message_cb: JSON out"| HOST
    AM --> LIBUV["libuv: timers · I/O · fs"]
```

## How the Host Drives Work

The host cannot eval and does not tick. It drives the runtime by posting JSON
messages and receiving replies:

```c
#include <qzjs/qzjs.h>
#include <stdio.h>

static void on_message(qz_t *rt, const char *json, size_t len, void *data) {
    (void)rt; (void)data;
    printf("received: %.*s\n", (int)len, json);
}

int main(void) {
    qz_config_t cfg = {0};
    cfg.initial_script =
        "globalThis.onmessage = function (e) { postMessage('pong'); };";
    cfg.message_cb = on_message;
    qz_t *rt = qz_create(&cfg);
    if (!rt) return 1;

    qz_post_message(rt, "{\"cmd\":\"ping\"}", 14);
    // on_message fires on the qzjs thread when the reply is ready.
    // The host is free to do its own work meanwhile — never blocked by qzjs.

    qz_destroy(rt);
    return 0;
}
```

`qz_post_message` is thread-safe (the JSON is copied), so it may be called
from any thread. `message_cb` fires on the qzjs thread — your callback must be
thread-safe.

## Why This Design

- qzjs runs
  itself, and the host thread stays free for its own work.
- All async events and JS callbacks are serialized on qzjs's single internal
  thread — no locks, no races inside the runtime.
- Microtasks are flushed automatically between loop iterations.

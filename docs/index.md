---
layout: home

hero:
  name: "Qzjs.js"
  text: "Embeddable WinterTC Runtime"
  tagline: Strict C99 · Internal thread + libuv loop · JSON host boundary
  actions:
    - theme: brand
      text: Get Started
      link: /guide/
    - theme: alt
      text: JS API
      link: /js-api/

features:
  - icon: 🔌
    title: Message-based host boundary
    details: Host and runtime exchange JSON over `qz_post_message` / `message_cb`. Inbound is thread-safe; outbound fires on the runtime thread. No `eval`, no `tick`.
  - icon: 🧵
    title: Own thread + libuv loop
    details: qzjs runs its own internal thread with an embedded libuv loop. The host never pumps an event loop.
  - icon: 📦
    title: Zero system dependencies
    details: QuickJS-ng, mbedTLS, miniz, libuv, and WAMR all build from source via CMake. About 2.45 MiB stripped in the minimal profile.
  - icon: ⚡
    title: Strict C99
    details: Builds as C99 alongside its dependencies. Release `qzjs -e 'console.log(1)'` starts in under 5 ms; peak RSS stays near 3 MB.
  - icon: 🌐
    title: WinterTC-compatible runtime
    details: 21 modules — fetch, crypto.subtle, streams, WebSocket, BroadcastChannel, EventSource, timers, fs, serve() and more. Precompiled to bytecode, available as globals.
  - icon: 🔒
    title: No global state
    details: Per-runtime isolation through an opaque `qz_t`. Multiple independent instances run in one process.

---

## Quick Start

```bash
# Clone with all submodules
git clone --recursive https://github.com/adam-ikari/qzjs.git
cd qzjs

# Configure and build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

```c
#include <qzjs/qzjs.h>
#include <stdio.h>

static void on_message(qz_t *rt, const char *json, size_t len, void *data) {
    (void)rt; (void)data;
    printf("received: %.*s\n", (int)len, json);
}

int main(void) {
    qz_config_t cfg = {0};
    cfg.initial_script = "postMessage({hello: 'world'});";
    cfg.message_cb = on_message;
    qz_t *rt = qz_create(&cfg);
    if (!rt) return 1;
    qz_post_message(rt, "{\"cmd\":\"echo\",\"data\":\"hi\"}", 26);
    qz_destroy(rt);
    return 0;
}
```

## Architecture

```mermaid
flowchart TB
    subgraph AM["Qzjs.js"]
        direction TB
        Core["qzjs.c (core API)"]
        Thread["thread.c — internal thread + libuv loop"]
        Msgq["msgq.c — message queue"]
        Worker["worker.c — dispatch (onmessage/postMessage)"]
        UvIO["uv_io.c — libuv I/O"]
        Core --> Thread
        Thread --> Msgq
        Msgq --> Worker
        Thread --> UvIO
        JS["WinterTC modules: fetch · console · crypto · streams · timers · …"]
        ExtList["Extensions: compress · crypto · textcodec · wamr"]
        Worker -.injects.-> JS
    end
    HOST["Host"] -->|"qz_post_message: JSON in"| Msgq
    Worker -->|"message_cb: JSON out"| HOST
    UvIO --> LIBUV["libuv"]
```

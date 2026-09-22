---
layout: home

hero:
  name: "Amoib.js"
  text: "Embeddable JS + Wasm Runtime"
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
    details: Host and runtime exchange JSON over `am_post_message` / `message_cb`. Inbound is thread-safe; outbound fires on the runtime thread. No `eval`, no `tick`.
  - icon: 🧵
    title: Own thread + libuv loop
    details: amoib runs its own internal thread with an embedded libuv loop. The host never pumps an event loop.
  - icon: 📦
    title: Zero system dependencies
    details: QuickJS-ng, mbedTLS, miniz, libuv, and WAMR all build from source via CMake. About 2.45 MiB stripped in the minimal profile.
  - icon: ⚡
    title: Strict C99
    details: Builds as C99 alongside its dependencies. Release `amoib -e 'console.log(1)'` starts in under 5 ms; peak RSS stays near 3 MB.
  - icon: 🌐
    title: WinterTC-compatible runtime
    details: 21 modules — fetch, crypto.subtle, streams, WebSocket, BroadcastChannel, EventSource, timers, fs, serve() and more. Precompiled to bytecode, available as globals.
  - icon: 🔒
    title: No global state
    details: Per-runtime isolation through an opaque `am_t`. Multiple independent instances run in one process.

---

## Quick Start

```bash
# Clone with all submodules
git clone --recursive https://github.com/adam-ikari/amoib.git
cd amoib

# Configure and build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

```c
#include <amoib/amoib.h>
#include <stdio.h>

static void on_message(am_t *rt, const char *json, size_t len, void *data) {
    (void)rt; (void)data;
    printf("received: %.*s\n", (int)len, json);
}

int main(void) {
    am_config_t cfg = {0};
    cfg.initial_script = "postMessage({hello: 'world'});";
    cfg.message_cb = on_message;
    am_t *rt = am_create(&cfg);
    if (!rt) return 1;
    am_post_message(rt, "{\"cmd\":\"echo\",\"data\":\"hi\"}", 26);
    am_destroy(rt);
    return 0;
}
```

## Architecture

```mermaid
flowchart TB
    subgraph AM["Amoib.js"]
        direction TB
        Core["amoib.c (core API)"]
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
    HOST["Host"] -->|"am_post_message: JSON in"| Msgq
    Worker -->|"message_cb: JSON out"| HOST
    UvIO --> LIBUV["libuv"]
```

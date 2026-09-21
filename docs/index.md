---
layout: home

hero:
  name: "Qwrt.js"
  text: "Embeddable QuickJS Runtime"
  tagline: C99 · Host-owned thread + libuv loop · JSON message boundary · Zero system dependencies
  actions:
    - theme: brand
      text: Get Started
      link: /guide/
    - theme: alt
      text: JS API
      link: /js-api/

features:
  - icon: 🔌
    title: Message-Based Host Boundary
    details: Host ⇄ runtime speak JSON over `qwrt_post_message` / `message_cb`. Thread-safe inbound, fires on the runtime thread outbound. No `eval`, no `tick` — the boundary is clean.
  - icon: 🧵
    title: Own Thread + Event Loop
    details: qwrt starts its own internal thread running an embedded libuv loop. The host never pumps an event loop or blocks on JS.
  - icon: 📦
    title: Zero System Dependencies
    details: QuickJS-ng, mbedTLS, miniz, libuv, WAMR — all built from source via CMake. No system packages. ~2.45 MiB stripped (minimal profile).
  - icon: ⚡
    title: Strict C99 + Embeddable
    details: Any C99 codebase, any host compiler. ~7.6 ms cold start (Ryzen-class), ~3.3 MB peak RSS — 23× lighter than node for the same eval workload.
  - icon: 🌐
    title: WinterTC Compatible
    details: A WinterTC-compatible surface — fetch, crypto.subtle, streams, WebSocket, BroadcastChannel, EventSource, timers, fs, serve(), and more. Precompiled to bytecode, available as globals.
  - icon: 🔒
    title: No Global State
    details: Zero mutable file-scope state. Per-runtime isolation via opaque `qwrt_t` — safe to run multiple independent instances in one process.
---

## Quick Start

```bash
# Clone with all submodules
git clone --recursive https://github.com/adam-ikari/qwrt.git
cd qwrt

# Configure and build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

```c
#include <qwrt/qwrt.h>
#include <stdio.h>

static void on_message(qwrt_t *rt, const char *json, size_t len, void *data) {
    (void)rt; (void)data;
    printf("received: %.*s\n", (int)len, json);
}

int main(void) {
    qwrt_config_t cfg = {0};
    cfg.initial_script = "postMessage({hello: 'world'});";
    cfg.message_cb = on_message;
    qwrt_t *rt = qwrt_create(&cfg);
    if (!rt) return 1;
    qwrt_post_message(rt, "{\"cmd\":\"echo\",\"data\":\"hi\"}", 26);
    qwrt_destroy(rt);
    return 0;
}
```

## Architecture

```mermaid
flowchart TB
    subgraph QWRT["Qwrt.js"]
        direction TB
        Core["qwrt.c (core API)"]
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
    HOST["Host"] -->|"qwrt_post_message: JSON in"| Msgq
    Worker -->|"message_cb: JSON out"| HOST
    UvIO --> LIBUV["libuv"]
```

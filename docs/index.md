---
layout: home

hero:
  name: "Qwrt.js"
  text: "Embeddable QuickJS Runtime"
  tagline: C99 · WinterTC Compatible · Internal Thread + libuv Loop · Zero System Dependencies
  actions:
    - theme: brand
      text: Get Started
      link: /guide/
    - theme: alt
      text: JS API
      link: /js-api/

features:
  - icon: ⚡
    title: Strict C99
    details: Embeddable in any C99 codebase. No host compiler requirements beyond C99.
  - icon: 📦
    title: Zero System Dependencies
    details: QuickJS-ng, mbedTLS, miniz, libuv, WAMR — all built from source via CMake. No system packages required.
  - icon: 🌐
    title: WinterTC Compatible
    details: A WinterTC-compatible JavaScript runtime — the standard Web APIs embedders expect, precompiled to bytecode.
  - icon: 🔌
    title: Message-Based Host Boundary
    details: Host ⇄ runtime speak JSON over qwrt_post_message / message_cb. Thread-safe inbound, clean outbound callback.
  - icon: 🧵
    title: Own Thread + Event Loop
    details: qwrt runs its own internal thread with an embedded libuv loop. The host never pumps an event loop.
  - icon: 🔒
    title: No Global State
    details: Zero mutable file-scope state. Per-runtime isolation via opaque qwrt_t — safe to run multiple independent instances in one process.
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

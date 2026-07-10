---
layout: home

hero:
  name: "qwrt"
  text: "Embeddable QuickJS Runtime"
  tagline: C99 · WinterCG Polyfill · Platform Abstraction Layer · Zero System Dependencies
  actions:
    - theme: brand
      text: Get Started
      link: /guide/
    - theme: alt
      text: PAL Reference
      link: /pal/
    - theme: alt
      text: JS API
      link: /js-api/

features:
  - icon: ⚡
    title: Strict C99
    details: Embeddable in any C99 codebase. C11 deps (QuickJS-ng, libuv) are isolated to their own compilation units with no standard leakage.
  - icon: 📦
    title: Zero System Dependencies
    details: QuickJS-ng, mbedTLS, miniz, libuv, wasm3 — all built from source via CMake add_subdirectory. No system packages required.
  - icon: 🌐
    title: 21 WinterCG Modules
    details: fetch, console, crypto.subtle, ReadableStream, timers, fs, URL, TextEncoder, AbortController, and more — precompiled to bytecode.
  - icon: 🔌
    title: Platform Abstraction Layer
    details: Same JS runs on libuv (Linux/macOS), FreeRTOS (ESP32-S3), and mock (testing). ~30 function pointers to implement your own backend.
  - icon: 🧵
    title: Multi-Context Isolation
    details: Spawn, suspend, and resume isolated JS contexts within one runtime. Each context has its own PAL, permissions, and extension state.
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
#include <pal_uv.h>

int main(void) {
    qwrt_pal_t *pal = pal_uv_create(NULL);
    qwrt_t *rt = qwrt_create(&(qwrt_config_t){ .pal = pal });

    // Evaluate JavaScript
    char *result = NULL;
    qwrt_eval(rt, "1 + 1", &result);
    printf("1 + 1 = %s\n", result);  // "2"
    qwrt_free(result);

    // Drive the event loop
    while (pal->run_cycle(pal, 100) > 0) qwrt_tick(rt);

    qwrt_destroy(rt);
    return 0;
}
```

## Architecture

```
┌──────────────────────────────────────────────────────────┐
│                        qwrt                               │
│  ┌──────────┐  ┌───────────┐  ┌───────────────┐         │
│  │  qwrt.c  │  │ context.c │  │  extension.c  │         │
│  │(core API)│  │(multi-ctx)│  │(ext register) │         │
│  └────┬─────┘  └─────┬─────┘  └───────┬───────┘         │
│       └──────────────┼────────────────┘                  │
│                bridge.c (JS↔PAL bridge)                   │
│                       │                                   │
│              qwrt_pal_t (PAL interface)                   │
│  ┌──────────┬─────────┼───────────┐                      │
│  │  pal_uv  │pal_freertos│ pal_mock│                      │
│  │ (libuv)  │(ESP-IDF)  │(testing)│                      │
│  └──────────┴─────────┴───────────┘                      │
│                                                           │
│  JS Polyfill: fetch │ console │ crypto │ streams │ ...    │
│  Extensions: compress │ crypto │ textcodec │ wasm3       │
└──────────────────────────────────────────────────────────┘
```

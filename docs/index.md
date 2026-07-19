---
layout: home

hero:
  name: "Qwrt.js"
  text: "Embeddable QuickJS Runtime"
  tagline: C99 · WinterCG Compatible · Platform Abstraction Layer · Zero System Dependencies
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
    title: WinterCG Compatible
    details: A WinterCG-compatible JavaScript runtime — the standard Web APIs embedders expect, precompiled to bytecode.
  - icon: 🔌
    title: Platform Abstraction Layer
    details: Run the same JS across platforms through a thin PAL contract (~30 function pointers). Implement your own backend without touching the core.
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
    qwrt_pal_t *pal = pal_uv_create(uv_default_loop());
    qwrt_t *rt = qwrt_create(&(qwrt_config_t){ .pal = pal });

    // Evaluate JavaScript
    char *result = NULL;
    qwrt_eval(rt, "1 + 1", &result);
    printf("1 + 1 = %s\n", result);  // "2"
    qwrt_free(result);

    // Drive the event loop
    pal->run_cycle(pal, 100); qwrt_tick(rt, 100);

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
        Ctx["context.c (multi-context)"]
        Ext["extension.c (extension registry)"]
        Bridge["bridge.c — JS ↔ PAL bridge"]
        Core --> Bridge
        Ctx --> Bridge
        Ext --> Bridge
        Bridge --> PAL["qwrt_pal_t (PAL interface)"]
        PAL --> PalUV["pal_uv (libuv)"]
        PAL --> PalFR["pal_freertos (ESP-IDF)"]
        PAL --> PalMock["pal_mock (testing)"]
        JS["WinterCG modules: fetch · console · crypto · streams · timers · …"]
        ExtList["Extensions: compress · crypto · textcodec · wamr"]
        Bridge -.injects.-> JS
        Ext -.registers.-> ExtList
    end
```

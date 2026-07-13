# Overview

qwrt is an **embeddable QuickJS-ng runtime wrapper** written in **strict C99**. It provides a small C API on top of the QuickJS-ng engine, a **WinterCG-compatible runtime**, and a **Platform Abstraction Layer (PAL)** so the same JavaScript code runs on Linux, macOS, and ESP32-S3.

## What qwrt Gives You

- **Full ES2023 JavaScript engine** — QuickJS-ng under the hood, fast startup, low memory
- **21 WinterCG modules** — `fetch`, `console`, `crypto.subtle`, `ReadableStream`, timers, `fs`, `URL`, `TextEncoder`, and more
- **Platform Abstraction Layer** — ~30 function pointers; ship three backends, add your own
- **Multi-context** — spawn/suspend/resume isolated JS contexts within one runtime
- **Native extensions** — compression (miniz), crypto (mbedTLS), text codec, WebAssembly (wasm3)
- **Zero system dependencies** — all deps built from source via CMake
- **Single-threaded** — no locks, no atomics; JSContext is thread-bound

## When to Use qwrt

| Use Case | Why qwrt |
|----------|----------|
| **IoT / MCU scripting** | C99, tiny footprint, FreeRTOS PAL for ESP32-S3 |
| **Plugin systems** | Multi-context isolation, per-context PAL permissions |
| **Edge compute** | WinterCG APIs feel familiar to JS developers |
| **Testing & simulation** | `pal_mock` for deterministic tests, no network needed |
| **CLI tools with JS config** | Embed a JS engine without pulling in Node.js |

## When NOT to Use qwrt

- You need **Node.js/npm ecosystem** — qwrt has no package manager
- You need **DOM** — qwrt is a server/runtime, not a browser
- You need **multi-threaded JS** — qwrt is single-threaded by design
- You need **JIT performance** — QuickJS is an interpreter, not a JIT compiler

## Project Structure

```
qwrt/
├── include/qwrt/       # Public headers (qwrt.h)
├── src/                 # Core runtime (qwrt.c, bridge.c, context.c, ...)
├── platform/            # PAL implementations
│   ├── uv/              #   pal_uv (libuv, Linux/macOS)
│   ├── mock/            #   pal_mock (testing)
│   ├── freertos/        #   pal_freertos (ESP32-S3)
│   └── pal_common.c     #   Shared PAL helpers
├── polyfill/src/        # WinterCG module source (22 modules)
├── test/                # Test suite (C + gtest)
├── deps/                # Git submodules (quickjs-ng, libuv, mbedtls, ...)
└── docs/                # This documentation
```

## Next Steps

- [Quick Start](/guide/quickstart) — clone, build, run your first script
- [PAL Overview](/pal/) — understand the Platform Abstraction Layer
- [JS API Reference](/js-api/) — what WinterCG APIs are available

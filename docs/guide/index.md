---
title: Overview
description: Qwrt.js is an embeddable QuickJS-ng runtime wrapper in strict C99 — a WinterTC-compatible JS runtime with its own internal thread and libuv event loop.
---

# Overview

qwrt is an **embeddable QuickJS-ng runtime wrapper** written in **strict C99**. It provides a small C API on top of the QuickJS-ng engine and a **WinterTC-compatible runtime**. qwrt owns its own internal thread and libuv event loop, and communicates with the host over JSON messages.

## What qwrt Gives You

- **ECMAScript engine (ES2020)** — QuickJS-ng under the hood, fast startup, low memory
- **WinterTC-compatible runtime** — `fetch`, `console`, `crypto.subtle`, `ReadableStream`, timers, `fs`, `URL`, `TextEncoder`, and more
- **Own thread + event loop** — qwrt starts an internal thread running a libuv loop; the host never pumps it
- **Message-based host boundary** — `qwrt_post_message` (in) / `message_cb` (out), JSON in both directions
- **Native extensions** — compression (miniz), crypto (mbedTLS), text codec, WebAssembly (WAMR, wasm3 optional)
- **Zero system dependencies** — all deps built from source via CMake; libuv is built from the deps submodule
- **Single-threaded runtime** — no locks, no atomics; all JS runs on qwrt's internal thread

## When to Use qwrt

| Use Case | Why qwrt |
|----------|----------|
| **Embedded / edge scripting** | C99, tiny footprint, libuv event loop built in |
| **Plugin systems** | Per-runtime isolation, multi-context handled inside the runtime |
| **Edge compute** | WinterTC APIs feel familiar to JS developers |
| **Testing & simulation** | `mock_libuv` for deterministic tests, no network needed |
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
├── src/                 # Core runtime
│   ├── qwrt.c           #   Core API (create/destroy/post_message)
│   ├── thread.c         #   Internal thread + libuv loop
│   ├── uv_io.c          #   libuv I/O (network, fs, timers)
│   ├── msgq.c           #   Message queue (host ⇄ runtime)
│   ├── worker.c         #   Message dispatch (onmessage/postMessage)
│   ├── bridge.c         #   JS ↔ runtime bridge
│   └── context.c        #   Multi-context
├── polyfill/src/        # WinterTC module source
├── test/                # Test suite (C + gtest + mock_libuv)
├── deps/                # Git submodules (quickjs-ng, libuv, mbedtls, ...)
└── docs/                # This documentation
```

## Next Steps

- [Quick Start](/guide/quickstart) — clone, build, run your first script
- [Standalone CLI](/guide/cli) — run scripts / one-liners / REPL without embedding
- [Event Loop](/guide/event-loop) — how the internal thread and libuv loop work
- [JS API Reference](/js-api/) — what WinterTC APIs are available

---
title: Examples
description: Runnable examples demonstrating how to use qzjs — host↔JS messaging, WebAssembly, Web Crypto, HTTP/WebSocket servers, workers, and C extensions.
---

# Examples

Every example lives in [`examples/`](https://github.com/adam-ikari/qzjs/tree/master/examples) and is
compile-checked at build time (`-DQZ_BUILD_EXAMPLES=ON`). C examples are
built by CMake; JS examples run via the `qzjs` CLI. Each example directory
has a `README.md` with its exact run command and expected output.

> Run examples against a **Release** build. A `QZ_BUILD_TESTS=ON` build links
> the core against `mock_libuv`, whose idle path blocks in a fixed 1-second
> poll fallback instead of waking at the timer's due time — timers therefore
> fire quantized to whole seconds. `timers` will print wrong elapsed times
> there. Production/Release builds use real libuv and are unaffected.

## Building

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DQZ_BUILD_EXAMPLES=ON
cmake --build build --parallel
```

## C examples (embedding)

| Example | What it shows | Run |
|---------|---------------|-----|
| [`hello`](https://github.com/adam-ikari/qzjs/tree/master/examples/hello) | Minimal embed: create runtime, eval JS, host↔JS message round-trip | `./build/examples/hello/qz_hello` |
| [`messages`](https://github.com/adam-ikari/qzjs/tree/master/examples/messages) | JSON request/response state machine (`echo`/`add`/`date`) | `./build/examples/messages/qz_messages` |
| [`worker`](https://github.com/adam-ikari/qzjs/tree/master/examples/worker) | `new Worker(url)` parallel execution | `./build/examples/worker/qz_worker` |

C examples spawn `qzjs-rt` (ISOLATED process model); the build injects the
runtime path automatically, so they run as-is.

## JS examples (CLI)

| Example | What it shows | Run |
|---------|---------------|-----|
| [`wasm`](https://github.com/adam-ikari/qzjs/tree/master/examples/wasm) | Execute a WebAssembly module (`WebAssembly.instantiate`) | `./build/qzjs examples/wasm/wasm.js` |
| [`crypto`](https://github.com/adam-ikari/qzjs/tree/master/examples/crypto) | WebCrypto: SHA-256 + AES-GCM encrypt/decrypt | `./build/qzjs examples/crypto/crypto.js` |
| [`httpserver`](https://github.com/adam-ikari/qzjs/tree/master/examples/httpserver) | Pure-JS `serve()` HTTP server with static files | `./build/qzjs examples/httpserver/server.js` |
| [`websocket`](https://github.com/adam-ikari/qzjs/tree/master/examples/websocket) | WebSocket echo server via `serve()` ws routes | `./build/qzjs examples/websocket/websocket.js` |
| [`timers`](https://github.com/adam-ikari/qzjs/tree/master/examples/timers) | `setTimeout`/`setInterval` + `Promise.all` async | `./build/qzjs examples/timers/timers.js` |
| [`fs`](https://github.com/adam-ikari/qzjs/tree/master/examples/fs) | `qzjs.fs` read/write/list/unlink | `./build/qzjs examples/fs/fs.js` |
| [`broadcast`](https://github.com/adam-ikari/qzjs/tree/master/examples/broadcast) | `BroadcastChannel` cross-instance messaging | `./build/qzjs examples/broadcast/broadcast.js` |
| [`stream-pipeline`](https://github.com/adam-ikari/qzjs/tree/master/examples/stream-pipeline) | `ReadableStream`/`TransformStream` pipeline | `./build/qzjs examples/stream-pipeline/pipeline.js` |
| [`fetch-proxy`](https://github.com/adam-ikari/qzjs/tree/master/examples/fetch-proxy) | Outbound `fetch` proxy | `./build/qzjs examples/fetch-proxy/main.js` |
| [`grpc-hello`](https://github.com/adam-ikari/qzjs/tree/master/examples/grpc-hello) | gRPC unary server (`-DQZ_WITH_GRPC=ON`) | `./build/qzjs examples/grpc-hello/grpc-hello.js` |
| [`worker-orchestrate`](https://github.com/adam-ikari/qzjs/tree/master/examples/worker-orchestrate) | Orchestrate multiple workers | `./build/qzjs examples/worker-orchestrate/orchestrate.js` |

## C extension

| Example | What it shows | Run |
|---------|---------------|-----|
| [`extension`](https://github.com/adam-ikari/qzjs/tree/master/examples/extension) | Compile a C extension into qzjs and expose a native global to JS | see [`extension/README.md`](https://github.com/adam-ikari/qzjs/tree/master/examples/extension) |

Extensions must be compiled into the qzjs library (`-DQZ_EXTRA_SOURCES` +
`-DQZ_EXTENSIONS` + `-DQZ_EXTRA_HEADERS`); the example documents the exact
build command.

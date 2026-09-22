# Amoib.js — Embeddable QuickJS Runtime

> 🌐 Website & API reference: **https://adam-ikari.github.io/amoib/**

amoib is a lightweight, **libuv-native** QuickJS-ng runtime wrapper for embedding
JavaScript in C applications. It provides a WinterTC-compatible runtime of
standard Web APIs (fetch, console, crypto, streams, timers, fs, …) and a small,
thread-safe C API for host ↔ runtime messaging and multi-context execution.
amoib owns its own internal thread running a libuv event loop — the host never
touches JS directly.

## Features

- **QuickJS-ng engine** — full ES2023 support, fast startup (Release `amoib -e 'console.log(1)'` median 4.82 ms after lazy WAMR init), low memory
- **libuv-native execution** — amoib owns an internal thread + libuv loop; no host-side event-loop pumping
- **WinterTC-compatible runtime** — 21 modules: fetch, console, crypto.subtle, ReadableStream, setTimeout, fs, URL, TextEncoder, and more (verified as an ECMA-429 interface matrix + project gtest harness — the WPT runner was removed; this is interface parity, not byte-for-byte browser parity)
- **Streaming HTTP + TLS** — mbedTLS for HTTPS, chunked transfer decoding, certificate verification
- **Native extensions** — compression (miniz), crypto (mbedTLS), text codec (UTF-8/Base64), WebAssembly (WAMR default, wasm3 alternative)
- **Multi-context + Web Workers** — spawn isolated contexts (soft suspend/resume to disk); `new Worker(url)` runs real parallel threads, or dedicated processes when built with `-DAM_PROCESS_MODEL=ISOLATED` (the default since the multi-process M-P2 milestone)
- **Host ↔ runtime messaging** — JSON messages via `am_post_message` / `message_cb`; `postMessage` / `onmessage` on the JS side

## Quick Start

### Build

```bash
git clone --recursive https://github.com/adam-ikari/amoib.git
cd amoib
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```
The WinterTC polyfill ships as precompiled bytecode with the JavaScript source
stripped (`qjsc -s`), shrinking the embedded polyfill bytecode by ~87%.
Polyfill initialization is lazy: core infrastructure (console, timers, event
targets, abort, URL, encoding, performance, navigator, structured clone, ...)
is set up at injection, while heavier or scenario-specific APIs (fetch,
streams, blob, worker, message-channel, caches, websocket, serve, fs,
storage, crypto.subtle, ...) initialize on first access. JS consumers observe
no difference — every name is visible (`in`/`Object.keys`) before first use
and resolves to the same descriptors as an eager install — so unused feature
APIs cost no setup time, closures, or resident instances.
The native WAMR runtime initializes lazily on first WebAssembly use as well —
end-to-end CLI startup (Release) dropped from 10.33 ms to **4.82 ms** median
for scripts that never touch `WebAssembly`.

### Minimal Example

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

    /* thread-safe inbound message; the runtime processes it on its own thread */
    am_post_message(rt, "{\"cmd\":\"echo\",\"data\":\"hi\"}", 23);

    am_destroy(rt);  /* graceful shutdown: request stop → join → free */
    return 0;
}
```

`am_create` blocks until the internal thread is ready and `initial_script`
has been evaluated (a thrown exception makes `am_create` return NULL).
`message_cb` fires on the amoib thread for every `postMessage` from JS and must
be thread-safe.

### Examples

Runnable samples live in [`examples/`](examples/), built with `AM_BUILD_EXAMPLES=ON`:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DAM_BUILD_EXAMPLES=ON
cmake --build build -j$(nproc)
./build/examples/hello/am_hello   # host ↔ JS messaging
./build/examples/worker/am_worker # real-thread Web Worker round-trip
```

### Build with Tests

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DAM_BUILD_TESTS=ON
cmake --build build -j$(nproc)
cd build && ctest --output-on-failure
```

## Standalone CLI

amoib ships a standalone runtime executable (built by default, `AM_BUILD_CLI=ON`)
that runs WinterTC Web APIs directly — no Node.js APIs (`process`, `require`,
`Buffer` are absent by design).

```bash
cmake --build build -j$(nproc)   # produces build/amoib

./build/amoib script.js a b c     # run a script, args via globalThis.arguments
./build/amoib -e 'await fetch(url)' # evaluate a one-liner
./build/amoib                     # interactive REPL (Ctrl-D to exit)
./build/amoib --help
./build/amoib --version
```

- **`globalThis.arguments`** — script args as an array (WinterCG
  `proposal-cli-api` direction; excludes the executable and script path)
- **`globalThis.env`** — process environment as a plain object
- **Async exit** — the runtime waits for pending async work (fetch, timers,
  streams) to complete before exiting, so top-level `await`-style scripts run to
  completion
- **Console routing** — `console.log`/`info`/`debug` → stdout,
  `console.warn`/`error` → stderr

```bash
./build/amoib -e 'console.log(JSON.stringify(globalThis.arguments))' a b c
# => ["a","b","c"]
```

## Architecture

```mermaid
flowchart TB
    subgraph HOST["Host process"]
        App["C application"]
    end
    subgraph AM["am_t (one amoib = one JSRuntime)"]
        Thread["internal thread (uv_thread_t)"]
        Loop["libuv loop (uv_loop_t)"]
        Ctx["JSContext + contexts"]
        Msg["message FIFO (inbound)"]
        IOBridge["bridge.c — JS ↔ libuv (uv_io.c)"]
        Loop --> Ctx
        Thread --> Loop
        IOBridge --> Loop
    end
    App -- "am_post_message (thread-safe, JSON)" --> Msg
    Msg --> Thread
    Ctx -- "postMessage" --> IOBridge
    IOBridge -- "message_cb (on amoib thread)" --> App
    Ctx -. "new Worker(url) → new am_t (own thread + loop)" .-> AM
```

By default (`AM_PROCESS_MODEL=ISOLATED`, multi-process M-P2) the host and the
main runtime are **separate processes**: `am_create` spawns
`amoib-rt --amoib-rt-server`, then the two sides exchange FlatBuffers-framed
envelopes over a socketpair — a runtime crash (or a hard-killed runtime) cannot
take the host down. The C API is unchanged and the switch is transparent; build
with `-DAM_PROCESS_MODEL=THREAD` for the single-process baseline (one thread
per runtime). In both models JS runs on amoib's own thread(s): the host drives
work by posting JSON messages and receiving replies through `message_cb`.

## API Reference

### Core API

| Function | Description |
|----------|-------------|
| `am_create(config)` | Create runtime; blocks until internal thread ready + `initial_script` eval'd. Returns NULL on failure. |
| `am_destroy(rt)` | Graceful shutdown: request thread exit → join → free. Host thread only, NULL-safe. |
| `am_post_message(rt, json, len)` | Thread-safe inbound JSON message (copied). Returns 0 / -1. |
| `am_get_runtime_data(rt)` / `am_set_runtime_data(rt, data)` | Per-runtime opaque pointer accessors. |
| `am_free(ptr)` | Free malloc'd blocks. NULL-safe. |

### Configuration (`am_config_t`)

| Field | Description |
|-------|-------------|
| `initial_script` | Eval'd on the amoib thread at create; a throw → `am_create` returns NULL. |
| `message_cb` | Outbound message callback, fires on the amoib thread (must be thread-safe). |
| `debug` | DAP debugger bits (see Debugging). |
| `host_data` | Per-runtime opaque pointer, read via `am_get_runtime_data`. |

### Multi-context

Multi-context (spawn/suspend/resume and `amContext.*`) and Web Workers are
JS-level APIs — see the [docs](https://adam-ikari.github.io/amoib/) for
`amContext.spawn` / `suspend` / `resume` and `new Worker(url)`.

### Extensions

Extensions are registered at build time via the `AM_EXTENSIONS` macro (see
`include/amoib/am_ext_registry.h`); there is no runtime registration API.
Built-in extensions (compress/crypto/textcodec/wamr) are auto-registered when
their `AM_WITH_*` is on. A parent project adds its own extension to the table
non-invasively via the CMake `AM_EXTENSIONS` / `AM_EXTRA_SOURCES` variables.

## CMake Options

`AM_WITH_*` toggles optional native extensions layered on the runtime. libuv
itself is a **hard dependency** (always built from `deps/libuv`) — there is no
platform-backend option anymore.

### Feature Toggles (`AM_WITH_*`)

| Option | Default | Description |
|--------|---------|-------------|
| `AM_WITH_WAMR` | ON | WAMR WebAssembly engine (Fast Interp + AOT) |
| `AM_WITH_WASM3` | OFF | wasm3 WebAssembly engine (alternative; mutually exclusive with WAMR) |
| `AM_WITH_TLS` | ON | mbedTLS HTTPS (forces `AM_WITH_CRYPTO_EXT=ON`) |
| `AM_WITH_COMPRESS` | ON | miniz compression extension |
| `AM_WITH_CRYPTO_EXT` | ON | crypto.subtle extension (undefined when OFF) |
| `AM_WITH_TEXTCODEC` | ON | UTF-8/Base64 extension |
| `AM_WITH_NONUTF_ENCODINGS` | OFF | non-UTF encoding labels (Latin-1, replacement) in TextDecoder |

### Build Profiles (`AM_PROFILE`)

`AM_PROFILE` 是 `AM_WITH_*` 各项的预设包，**只改未显式指定的项**：
`-DAM_PROFILE=minimal -DAM_WITH_TLS=ON` 中显式的 `TLS=ON` 赢。空值
（默认）时各项行为与历史默认逐位一致。取值 `minimal | standard`，其他值
configure 报错。所有命名档位均满足 ECMA-429 WinterTC 全量必选集。

| Profile | 宏效果 | amoib 尺寸（strip 后，实测） | ECMA-429 WinterTC | 适用场景 |
|---------|--------|---------------------------|-------------------|----------|
| `standard`（与空 profile 等效） | 与历史默认相同：WAMR/TLS/COMPRESS/CRYPTO_EXT/TEXTCODEC=ON | 同默认构建 | ✅ 全量必选满足 | 通用运行时 |
| `minimal` | 同 standard 但 **TLS=OFF**（fetch 降级 http-only；ECMA-429 不含 HTTPS） | **2.45 MiB**（Release/-O3）；1.81 MiB（MinSizeRel/-Os） | ✅ 全量必选仍满足：atob/btoa、WebAssembly（WAMR）、crypto.subtle、CompressionStream 全部在 | 嵌入式/尺寸敏感，仍需过 WinterTC 一致性 |

实测命令与行为探测（2026-09-12，x86_64 Linux）：

```bash
cmake -S . -B build_profile_min -DAM_PROFILE=minimal -DCMAKE_BUILD_TYPE=Release
cmake --build build_profile_min -j
strip build_profile_min/amoib   # 2,573,536 B
./build_profile_min/amoib -e 'console.log(typeof btoa, typeof WebAssembly, typeof crypto?.subtle, typeof CompressionStream)'
# minimal: function object object function
```


### gRPC Stack (`AM_WITH_GRPC`)

`AM_WITH_GRPC`（默认 OFF）现在是 CMake option：ON 时构建系统向
polyfill rebuild 传 `AM_WITH_GRPC=1`，把 gRPC/HTTP2 栈（h2 + HPACK +
protobuf + grpc，~3.5k 行 JS）编进 `src/polyfill_default.c`。依赖 npm +
esbuild + qjsc（polyfill rebuild 本来就依赖，无新增前提）。手工路径仍是
`AM_WITH_GRPC=1 node polyfill/build.js`。

> Note: before `AM_WITH_GRPC` was a CMake option, the stack was gated only
> inside the polyfill **build** step (`AM_WITH_GRPC=1 node
> polyfill/build.js`). The CMake option drives the same rebuild
> automatically; the manual path still works.

### Build Targets (`AM_BUILD_*`)

| Option | Default | Description |
|--------|---------|-------------|
| `AM_BUILD_TESTS` | OFF | Build test suite |
| `AM_BUILD_DEBUGGER` | OFF | DAP step-debugger (patches QuickJS-ng; adds `src/debugger.c` + `src/debugger_dap.c`) |

### Library Outputs

| Target | Description |
|--------|-------------|
| `libamoib.a` | Static core. Deliberately does **not** link libuv — uv symbols resolve at the final executable. |
| `libam_full.a` | CMake link-interface aggregator: amoib + real libuv + mbedTLS + miniz + WAMR + pthread/dl/rt. |
| `amoib.pc` | pkg-config. `pkg-config --cflags --libs amoib` yields the full static link line (all vendored archives). |

## WinterTC Modules

| Module | Globals | Backend |
|--------|---------|---------|
| fetch | `fetch`, `Headers`, `Request`, `Response` | libuv (uv_io.c) |
| console | `console` | stdout |
| crypto | `crypto`, `crypto.subtle` | ext_crypto (mbedTLS) |
| streams | `ReadableStream`, `WritableStream` | — |
| timers | `setTimeout`, `setInterval` | libuv timers |
| fs | `fs.read`, `fs.write` | libuv (uv_io.c) |
| storage | `storage.get/set/delete` | libuv in-memory map |
| encoding | `TextEncoder`, `TextDecoder` | ext_textcodec |
| url | `URL`, `URLSearchParams` | — |
| abort | `AbortController`, `AbortSignal` | — |
| performance | `performance.now()` | libuv hrtime |
| event-target | `EventTarget`, `Event` | — |
| blob | `Blob`, `File`, `FormData` | — |
| message-channel | `MessageChannel`, `MessagePort` | — |
| navigator | `navigator` | — |
| structured-clone | `structuredClone` | — |
| error-events | `ErrorEvent` | — |

## Dependencies

All dependencies are built from source via CMake `add_subdirectory` — amoib
never links system libraries, and each dep's objects live in the main build
tree (subject to `-j` and incremental rebuild). All are git submodules with
pinned versions. amoib and all its dependencies build under **strict C99** —
quickjs-ng and libuv ship C11 `<stdatomic.h>` code, but amoib applies small
patches (GCC/Clang `__atomic_*` builtins, no C11) so they compile under
`-std=c99`.

| Dependency | Source | Required | Purpose |
|------------|--------|----------|---------|
| QuickJS-ng | git submodule | Yes | JS engine (C99; atomics patched) |
| libuv | git submodule | Yes | Event loop / I/O backend (C99; atomics patched) |
| mbedTLS | git submodule | No (AM_WITH_TLS) | TLS / crypto (C99) |
| miniz | git submodule | No (AM_WITH_COMPRESS) | Compression (C90) |
| WAMR | git submodule | No (AM_WITH_WAMR) | WebAssembly engine (default) |
| wasm3 | git submodule | No (AM_WITH_WASM3) | WebAssembly engine (alternative) |

### npm Polyfill 构建依赖

polyfill 构建期引入 npm 依赖（esbuild + 3 个库），均 devDependencies，不随二进制发布：

| 包名 | 版本 | 许可 | 目标 polyfill | 说明 |
|------|------|------|--------------|------|
| `urlpattern-polyfill` | 10.1.0 | MIT | `polyfill/src/url-pattern.js` | URLPattern 规范实现 |
| `@ungap/structured-clone` | 1.4.0 | ISC | `polyfill/src/structured-clone.js` | 深拷贝算法，保留 amoib 扩展分支（MessagePort transfer、ArrayBuffer transfer、DataView offset/len、Blob/File、DOMException） |
| `web-streams-polyfill` | 4.3.0 | MIT | `polyfill/src/streams.js` | 三大流类规范实现（ReadableStream / WritableStream / TransformStream） |

> 注：whatwg-url 未引入（tr46 IDNA 485KB 依赖链过大 + esbuild IIFE 时序冲突），保留自研。

## Thread Safety

- **All JS runs on amoib's internal thread** — the host never calls into JS directly.
- `am_create` / `am_destroy` are host-thread calls; `am_create` blocks until the internal thread is ready.
- `am_post_message` is **thread-safe** (any thread may call it; the JSON is copied).
- `message_cb` fires on the amoib thread — the host callback must be thread-safe.
- Worker contexts each run on their own thread + loop (real parallelism).

## Debugging

amoib ships a DAP (Debug Adapter Protocol) step-debugger built into the
library — step-debug any embedded program in VS Code. Enable with
`-DAM_BUILD_DEBUGGER=ON` (patches QuickJS-ng to add breakpoint/step
primitives; zero overhead when OFF). Run your program with `AM_DEBUG=1`
(or set bit 1 of `am_config_t.debug`) and VS Code attaches with
`request:"attach"`. See [docs/dev/debugging.md](docs/dev/debugging.md) for
the full setup, launch.json, and limitations.

## Testing

Tests are GoogleTest `.cpp` suites in `test/`, linked against `amoib` plus
`mock_libuv` (a fake `uv_*` API for deterministic offline tests — see
`test/mock_libuv.h` and the `HostCtx` harness in `test/test_host.h`).

```bash
# Unit tests
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DAM_BUILD_TESTS=ON
cmake --build build -j$(nproc)
cd build && ctest --output-on-failure

# With valgrind
valgrind --leak-check=full ./build/test/test_am_gtest
```

Tests are labelled for selection (`ctest -L <label>`):
- `offline` — local, deterministic (default; what CI runs)
- `network` — outbound HTTP/HTTPS
- `benchmark` — performance, not pass/fail
- `test262` — QuickJS-ng ECMAScript conformance

```bash
ctest -L offline          # CI default — green
ctest -L network          # only when network is available
```

## License

MIT

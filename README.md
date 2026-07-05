# qwrt — Embeddable QuickJS Runtime

qwrt is a lightweight QuickJS-ng runtime wrapper with a Platform Abstraction
Layer (PAL) for embedding JavaScript in C applications. It provides a
WinterCG-compatible JS polyfill (fetch, console, crypto, streams, timers)
and a clean C API for JS execution, multi-context management, and native
extensions.

## Features

- **QuickJS-ng engine** — full ES2023 support, fast startup, low memory
- **Platform Abstraction Layer** — libuv (Linux/macOS), FreeRTOS (ESP32-S3), mock (testing)
- **WinterCG polyfill** — 21 modules: fetch, console, crypto.subtle, ReadableStream, setTimeout, fs, URL, TextEncoder, and more
- **Streaming HTTP + TLS** — mbedTLS for HTTPS, chunked transfer decoding, certificate verification
- **Native extensions** — compression (miniz), crypto (mbedTLS), text codec (UTF-8/Base64), WebAssembly (wasm3)
- **Multi-context** — spawn/suspend/resume isolated JS contexts within one runtime
- **Single-threaded** — JSContext is thread-bound; event loop driven by `pal->run_cycle`

## Quick Start

### Build

```bash
git clone --recursive https://github.com/your-org/qwrt.git
cd qwrt
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Build examples (in examples/):
# cmake -B build -DQWRT_BUILD_EXAMPLES=ON && cmake --build build
```

### Minimal Example

```c
#include <qwrt/qwrt.h>
#include <pal_uv.h>
#include <uv.h>
#include <stdio.h>

int main() {
    /* Create PAL (libuv, own event loop) */
    qwrt_pal_t *pal = pal_uv_create(uv_default_loop());

    /* Create runtime */
    qwrt_config_t config = { .pal = pal, .debug = 0, .extensions = NULL };
    qwrt_t *rt = qwrt_create(&config);

    /* Evaluate JavaScript */
    char *result = NULL;
    qwrt_eval(rt, "1 + 1", &result);
    printf("result: %s\n", result);  /* "2" */
    qwrt_free(result);

    /* Use WinterCG APIs (fetch, console, etc.) */
    qwrt_eval(rt, "console.log('Hello from QuickJS!');", NULL);

    /* Drive the event loop (for async operations) */
    while (pal->run_cycle(pal, 100) > 0) {
        qwrt_tick(rt);
    }

    /* Cleanup */
    qwrt_destroy(rt);
    pal_uv_destroy(pal);
    return 0;
}
```

### Build with Tests

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DQWRT_BUILD_TESTS=ON
cmake --build build -j$(nproc)
cd build && ctest --output-on-failure
```

## Architecture

```
┌─────────────────────────────────────────────────────┐
│                    qwrt                              │
│                                                      │
│  ┌─────────────┐  ┌───────────┐  ┌───────────────┐ │
│  │  qwrt.c     │  │ context.c │  │ extension.c   │ │
│  │  (core API) │  │ (multi-ctx)│  │ (ext register)│ │
│  └──────┬──────┘  └─────┬─────┘  └───────┬───────┘ │
│         └───────────────┼─────────────────┘         │
│                   bridge.c (JS↔PAL bridge)           │
│                         │                            │
│              qwrt_pal_t (PAL interface)              │
│  ┌──────────┬───────────┼───────────┐                │
│  │ pal_uv   │pal_freertos│ pal_mock │                │
│  │ (libuv)  │(ESP-IDF)   │ (testing)│                │
│  └──────────┴───────────┴───────────┘                │
│                                                      │
│  JS Polyfill: fetch | console | crypto | streams ... │
│  Extensions: compress | crypto | textcodec | wasm3   │
└─────────────────────────────────────────────────────┘
```

## API Reference

### Lifecycle

| Function | Description |
|----------|-------------|
| `qwrt_create(config)` | Create runtime. Returns NULL on failure. |
| `qwrt_destroy(rt)` | Destroy runtime and free all resources. |
| `qwrt_reset(rt, config)` | Reset runtime (rebuild JS context, keep PAL). |

### JS Execution

| Function | Description |
|----------|-------------|
| `qwrt_eval(rt, code, &result)` | Evaluate JS code. Result freed via `qwrt_free`. |
| `qwrt_eval_bytecode(rt, buf, len, &result)` | Evaluate precompiled QuickJS bytecode. |
| `qwrt_call(rt, func, args_json, &result)` | Call a global JS function. |
| `qwrt_tick(rt)` | Process pending JS microtasks (Promise callbacks). |
| `qwrt_free(ptr)` | Free memory returned by qwrt_eval/qwrt_call. |

### Multi-Context

| Function | Description |
|----------|-------------|
| `qwrt_spawn(rt, config)` | Spawn a new JS context. Returns context_id. |
| `qwrt_suspend(rt)` | Suspend current context. |
| `qwrt_resume(rt, context_id)` | Resume a specific context. |
| `qwrt_destroy_ctx(rt, context_id)` | Destroy a context. |
| `qwrt_get_active_ctx_id(rt)` | Get current context ID. |

### Extensions

| Function | Description |
|----------|-------------|
| `qwrt_register_ext(rt, ext)` | Register a native extension at runtime. |

### PAL Interface

The PAL is a struct of function pointers (`qwrt_pal_t`). All async operations
invoke callbacks on the event loop thread. See `include/qwrt/qwrt.h` for the
full interface.

| Category | Functions |
|----------|-----------|
| HTTP | `http_request`, `http_request_stream`, `http_abort` |
| Filesystem | `fs_read`, `fs_write`, `fs_exists`, `fs_remove`, `fs_list` |
| Storage | `storage_get`, `storage_set`, `storage_del` |
| Timers | `timer_start`, `timer_stop` |
| Time | `time_now` (ms), `hrtime` (ns) |
| Utilities | `log`, `mem_alloc`, `mem_free`, `random_bytes` |
| Event Loop | `run_cycle(timeout_ms)` — optional, drives I/O |

## CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `QWRT_WITH_TLS` | ON | mbedTLS (HTTPS + crypto) |
| `QWRT_WITH_COMPRESS` | ON | miniz compression extension |
| `QWRT_WITH_CRYPTO_EXT` | ON | crypto.subtle extension |
| `QWRT_WITH_TEXTCODEC` | ON | UTF-8/Base64 extension |
| `QWRT_WITH_WASM3` | ON | wasm3 WebAssembly engine |
| `QWRT_WITH_WAMR` | OFF | WAMR WebAssembly engine (alternative) |
| `QWRT_BUILD_TESTS` | OFF | Build test suite |
| `QWRT_BUILD_EXAMPLES` | OFF | Build examples |

## PAL Implementations

| PAL | Platform | HTTP | TLS | FS | Storage |
|-----|----------|------|-----|-----|---------|
| `pal_uv` | Linux/macOS | libuv TCP + mbedTLS | mbedTLS | POSIX | In-memory |
| `pal_freertos` | ESP32-S3 | lwIP + mbedTLS | mbedTLS + cert bundle | LittleFS | NVS |
| `pal_mock` | Testing | Mock responses | — | In-memory KV | In-memory KV |

## JS Polyfill Modules

| Module | Globals | PAL Dependency |
|--------|---------|----------------|
| fetch | `fetch`, `Headers`, `Request`, `Response` | `http_request_stream` |
| console | `console` | `log` |
| crypto | `crypto`, `crypto.subtle` | `random_bytes` + ext_crypto |
| streams | `ReadableStream`, `WritableStream` | — |
| timers | `setTimeout`, `setInterval` | `timer_start/stop` |
| fs | `fs.read`, `fs.write` | `fs_*` |
| storage | `storage.get/set/delete` | `storage_*` |
| encoding | `TextEncoder`, `TextDecoder` | ext_textcodec |
| url | `URL`, `URLSearchParams` | — |
| abort | `AbortController`, `AbortSignal` | — |
| performance | `performance.now()` | `hrtime` |
| event-target | `EventTarget`, `Event` | — |
| blob | `Blob`, `File`, `FormData` | — |
| message-channel | `MessageChannel`, `MessagePort` | — |
| navigator | `navigator` | — |
| structured-clone | `structuredClone` | — |
| error-events | `ErrorEvent` | — |

## Dependencies

All dependencies are built from source via CMake `add_subdirectory` — qwrt
never links system libraries, and each dep's objects live in the main build
tree (subject to `-j` and incremental rebuild). Most are vendored directly in
the repo; only `libuv/` is a git submodule (fetch it with
`git submodule update --init libuv`). qwrt itself is strict C99; deps that
require C11 (QuickJS-ng, libuv) compile under their own standard with qwrt's
C99 isolation preserved.

| Dependency | Source | Required | Purpose |
|------------|--------|----------|---------|
| QuickJS-ng | vendored source | Yes | JS engine (C11) |
| mbedTLS | vendored source | No (QWRT_WITH_TLS) | TLS / crypto (C99) |
| miniz | vendored source | No (QWRT_WITH_COMPRESS) | Compression (C90) |
| libuv | git submodule | No (QWRT_PAL_UV) | Event loop, pal_uv (C11) |
| wasm3 | vendored source | No (QWRT_WITH_WASM3) | WebAssembly (must be pre-built by hand) |

## Thread Safety

- **JSContext is thread-bound**: all `qwrt_*` calls must be from the thread
  that called `qwrt_create`.
- **`run_cycle`**: drives the PAL event loop on the same thread.
- **Callbacks**: PAL callbacks fire on the event loop thread; use
  `qwrt_defer_callback` to safely dispatch to the JS thread.
- **No internal locking**: the caller is responsible for thread discipline.

## Testing

```bash
# Unit tests
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DQWRT_BUILD_TESTS=ON
cmake --build build -j$(nproc)
cd build && ctest --output-on-failure

# With valgrind
valgrind --leak-check=full ./build/test/test_qwrt
```

Tests are labelled for selection (`ctest -L <label>`):
- `offline` — local, deterministic (default; what CI runs)
- `network` — outbound HTTP/HTTPS (e.g. `test_fetch_httpbin`, `test_tls`)
- `benchmark` — performance, not pass/fail
- `compliance` — WinterCG conformance suite (known-incomplete, tracked separately)

```bash
ctest -L offline          # CI default — green
ctest -L network          # only when network is available
ctest -L compliance       # track remaining polyfill gaps
```


## ESP32-S3

qwrt builds for ESP32-S3 via ESP-IDF. See the platform documentation for
setup instructions.

```bash
# In your ESP-IDF project:
# Set EXTRA_COMPONENT_DIRS to point at qwrt/esp-idf/
idf.py set-target esp32s3
idf.py build
```

## License

MIT

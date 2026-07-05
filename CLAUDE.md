# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What qwrt is

qwrt is an embeddable **QuickJS-ng runtime wrapper** written in C99. It exposes a
small C API on top of the QuickJS-ng engine, plus a WinterCG-compatible JS
polyfill (fetch, console, crypto, streams, timers, fs, …) and a Platform
Abstraction Layer (PAL) so the same code runs on libuv (Linux/macOS),
FreeRTOS (ESP32-S3), and a mock backend for tests. It is **standalone** — it
contains no LLM/agent/business logic and must not reference upper-layer
applications.

## Build & test

```bash
# Configure (QuickJS-ng is auto-built into quickjs-ng/build/ on first run)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# With tests
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DQWRT_BUILD_TESTS=ON
cmake --build build -j$(nproc)
cd build && ctest --output-on-failure

# Run a single ctest target
cd build && ctest -R test_qwrt --output-on-failure
# Or invoke the binary directly (some tests need WORKING_DIRECTORY = build/test):
./build/test/test_qwrt

# Memory check
valgrind --leak-check=full ./build/test/test_qwrt
```

Feature toggles are CMake options prefixed `QWRT_WITH_*` (see README "CMake
Options"). Notable: `QWRT_WITH_TLS` (mbedTLS), `QWRT_WITH_COMPRESS` (miniz),
`QWRT_WITH_CRYPTO_EXT`, `QWRT_WITH_TEXTCODEC`, `QWRT_WITH_WASM3` (default ON)
vs `QWRT_WITH_WAMR` (default OFF, alternative WASM engine). **libuv is not a
toggle** — it is one PAL backend (in `platform/uv/`), built from source as the
`libuv/` submodule (never the system libuv): at configure time qwrt auto-builds
`libuv/build/libuv.a` via `execute_process` if missing, then links `qwrt_uv`/
`qwrt_full` against it. If the submodule is absent, pal_uv is skipped (pal_mock
/ pal_freertos cover other platforms). `git submodule update --init libuv` to
fetch it.

Tests link `qwrt` + `qwrt_mock` by default; network/TLS/stream tests additionally
link `qwrt_uv` and are gated behind `LIBUV_FOUND`/`QWRT_WITH_TLS`. A few
tests are GoogleTest `.cpp` (fetched via FetchContent).

### Building the JS polyfill

The polyfill is **precompiled to QuickJS bytecode and inlined as
`src/polyfill_default.c`** (an auto-generated C array — do not hand-edit). To
rebuild after editing anything under `polyfill/src/`:

```bash
cd polyfill && npm install   # first time only, pulls esbuild
npm run build                # bundles via esbuild, runs qjsc, regenerates polyfill_default.c
```

Flow: `polyfill/src/index.js` → esbuild bundles into an IIFE →
`build.js` post-processes so `pal` arrives as an IIFE closure parameter from
`globalThis.__pal_inject__` (see `polyfill/src/pal.js` and the header comment in
`build.js`) → `qjsc -C -b` compiles to bytecode → written to
`src/polyfill_default.c` and `dist/polyfill.bytecode`. `build.js` looks for
`qjsc` at `QJSC` env var or `../third_party/quickjs-ng/build/qjsc`; the actual
checkout lives at `quickjs-ng/` (root), so set `QJSC` if the default path is
wrong. The polyfill is injected into a context by `qwrt_inject_polyfill_ctx`
(bridge.c), which sets `__pal_inject__` to a `pal` JS object for the duration of
the eval.

## Architecture

The runtime is layered. Read these together to understand it:

- **`include/qwrt/qwrt.h`** — the public surface: `qwrt_pal_t` (a struct of
  function pointers — the PAL contract), `qwrt_ext_t` (extension hooks:
  `init`/`destroy`/`suspend`/`resume`), `qwrt_config_t`, and the core
  lifecycle/eval/multi-context API.
- **`src/qwrt_internal.h`** — the real internal layout. `qwrt_t` holds a fixed
  array of up to `QWRT_MAX_CONTEXTS` (64) `qwrt_ctx_t*`, plus a deferred-callback
  queue. Each `qwrt_ctx_t` owns a `JSContext*`, a per-context PAL pointer (so
  different contexts can have different permissions), handle/timer tables
  (`QWRT_MAX_HANDLES` = 256), and the polyfill bytes (saved for `qwrt_reset`
  re-injection). `QWRT_MAGIC` validates the opaque `qwrt_t*`.
- **`src/qwrt.c`** — core API (`qwrt_create`/`eval`/`tick`/…).
- **`src/context.c`** — multi-context lifecycle (`spawn`/`suspend`/`resume`/
  `destroy_ctx`, `qwrt_get_active_ctx`).
- **`src/extension.c`** — runs `qwrt_ext_t` hooks across all extensions for a
  context; `qwrt_ext_register` adds one at runtime.
- **`src/bridge.c`** — the JS↔PAL bridge. Builds the per-context `pal` JS object
  (`qwrt_create_pal_object_ctx`), injects the polyfill, and manages the
  deferred-callback queue.
- **`src/ext_*.c`** — native extensions (compress/crypto/textcodec/wasm3/wamr),
  each implementing `qwrt_ext_t`.
- **`platform/{uv,mock,freertos}/pal_*.c`** — PAL implementations. Each is gated
  by a `QWRT_PAL_*` option (see below): `pal_mock` (`QWRT_PAL_MOCK`, default ON,
  host-toolchain friendly, used by tests), `pal_uv` (`QWRT_PAL_UV`, default ON,
  needs libuv + mbedTLS for HTTPS), `pal_freertos` (`QWRT_PAL_FREERTOS`, default
  OFF, ESP-IDF-only).

### Key execution model

- **Single-threaded.** `JSContext` is bound to the thread that called
  `qwrt_create`. All `qwrt_*` calls must come from that thread.
- **Async PAL → JS thread.** PAL callbacks (libuv, etc.) fire on the event-loop
  thread; they must not call into JS directly. They enqueue via
  `qwrt_defer_callback(rt, fn, data)`, and `qwrt_tick(rt)` drains that queue so
  `JS_Call` happens in a valid context. This is the central bridge concern.
- **Event loop.** The host drives `pal->run_cycle(pal, timeout_ms)` then
  `qwrt_tick(rt)` in a loop. `run_cycle` is OPTIONAL (may be NULL → host pumps
  `qwrt_tick` itself).

## Conventions (from CONTRIBUTING.md)

- C99, 4 spaces, no tabs, no trailing whitespace. `snake_case` functions/vars,
  `SHOUTING_CASE` macros. `/* */` comments (not `//`) in C. Include guards are
  `#ifndef QWRT_…_H` (no `#pragma once`).
- Conventional Commits (`feat(qwrt):`, `fix(pal_uv):`, …).
- Adding a **PAL**: `platform/<name>/pal_<name>.{c,h}`, implement required
  `qwrt_pal_t` pointers (optional ones like `http_abort`/`run_cycle` may be
  NULL), add a `QWRT_PAL_<NAME>` option + `qwrt_<name>` target (default OFF for
  platform-specific, ON for host-friendly), test against `pal_mock`.
- Adding an **extension**: `src/ext_<name>.c` + `include/qwrt/ext_<name>.h`,
  implement `qwrt_ext_t` (at least `init`+`destroy`), register JS via
  `JS_SetPropertyStr` in `init`, add a `QWRT_WITH_<NAME>` option, list it in the
  default extensions in `qwrt_create`.
- Tests register with `add_test` (NOT a POST_BUILD step — a POST_BUILD run aborts
  the whole build on one failure, see the comment in `test/CMakeLists.txt`).

## Repo layout notes

- `libuv/` is a git submodule (the only true submodule). `mbedtls/`, `miniz/`,
  and `quickjs-ng/` are **vendored source — checked directly into the repo as
  trees, NOT submodules** (so `git submodule update --init` does not touch them).
  **All dependencies are built from source — qwrt links no system libraries.**
  At configure time qwrt auto-builds (via `execute_process` into each dep's
  `build/`): `quickjs-ng/build/libqjs.a`, `libuv/build/libuv.a` (when
  `QWRT_PAL_UV` is on), and `mbedtls/build/library/libmbedtls.a` +
  `libmbedx509.a` + `libmbedcrypto.a` (when `QWRT_WITH_TLS` and/or
  `QWRT_WITH_CRYPTO_EXT` is on). miniz is built via `add_subdirectory`.
  `wasm3`/`wamr` are optional and must be pre-built by hand if enabled (CMake
  errors give the exact commands).
- `docs/` has design docs (`qwrt-architecture-design.md`, `pal-design.md`,
  `esp32s3-design.md`) — the architecture doc is in Chinese.

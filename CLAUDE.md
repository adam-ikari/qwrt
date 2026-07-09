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
vs `QWRT_WITH_WAMR` (default OFF, alternative WASM engine). PAL backends use a
separate `QWRT_PAL_*` prefix: `QWRT_PAL_UV` (default ON), `QWRT_PAL_MOCK`
(default ON), `QWRT_PAL_FREERTOS` (default OFF, ESP-IDF only).

**qwrt is strict C99** (`CMAKE_C_STANDARD 99`, `CMAKE_C_EXTENSIONS OFF` →
`-std=c99`, enforced with `-Wall -Wextra -Werror` via `qwrt_enable_warnings`).
Vendored deps that *require* C11 (quickjs-ng, libuv) are built via
`add_subdirectory` and **must not** leak their `CMAKE_C_STANDARD` into qwrt —
`CMakeLists.txt` saves/restores global C standard around each such subdir.
`QWRT_UNUSED(x)` (qwrt_internal.h) marks QuickJS-callback fixed-signature
params that would otherwise trip `-Wunused-parameter`.

**No mutable file-scope state.** `src/*.c` contains zero mutable globals —
all per-runtime state (QuickJS class IDs, wasm3 environment) lives on
`qwrt_t` (per-runtime, since one qwrt_t owns one JSRuntime and QuickJS
classes are runtime-scoped). Recover `qwrt_t*` from a `JSContext*` via
`qwrt_get_rt_from_ctx(ctx)`, or from a `JSRuntime*` (finalizers) via
`qwrt_get_rt_from_jsrt(jsrt)`. Deterministic lookup tables (e.g. CRC32) are
`static const`.

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
`qjsc` at `QJSC` env var or `../deps/quickjs-ng/build/qjsc`; the actual
checkout lives at `deps/quickjs-ng/` (as a git submodule), so set `QJSC` if the default path is
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

### Bridge layer discipline (`src/bridge.c`)

The `js_pal_*` wrappers in `bridge.c` are the only C between the PAL (C
function pointers) and the polyfill (JS, which closures over the `pal` JS
object). C is *required* here for three things nothing else can do:

1. **JSValue ↔ C conversion** (`JS_ToCString`, `JS_GetUint8Array`,
   `JS_NewArrayBufferCopy`, `JS_NewString`, …) — the polyfill can't touch
   QuickJS internal representations.
2. **The PAL call** — invoking the `qwrt_pal_t` function pointer.
3. **Promise + thread boundary** — `JS_NewPromiseCapability`, storing
   resolve/reject handles, and `qwrt_defer_callback` so PAL callbacks (event
   loop thread) replay as `JS_Call` on the runtime thread.

**Everything else stays out of the bridge.** A `js_pal_*` wrapper should do
*only* the three things above. In particular: input validation, default
values, length caps, level mappings, and string formatting are **not** the
bridge's job — they belong in the JS polyfill (caller-facing semantics) or
the PAL implementation (platform policy), not in C. Keep the bridge thin.
Example: the Web Crypto 65536-byte cap on `getRandomValues` lives in
`polyfill/src/crypto.js`, not in `js_pal_random_bytes`.

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

- `deps/` contains all third-party dependencies as **git submodules** with
  pinned versions: `libuv/` (v1.52.1), `mbedtls/` (v3.6.6), `miniz/` (3.1.2),
  `quickjs-ng/` (v0.15.1), `wasm3/` (v0.5.0). `wamr/` is optional and must be
  pre-built by hand if enabled (CMake errors give the exact commands).
  **All dependencies are built from source — qwrt links no system libraries.**
  Each is pulled in via `add_subdirectory(... EXCLUDE_FROM_ALL)` (never
  `execute_process`), so its `.o` files live in the main build tree and are
  subject to `-j` / incremental rebuild. Targets: quickjs-ng → `qjs` (C11),
  libuv → `uv_a` (C11, when `QWRT_PAL_UV`), mbedtls → `mbedtls`/`mbedx509`/
  `mbedcrypto` (C99, when `QWRT_WITH_TLS`/`QWRT_WITH_CRYPTO_EXT`), miniz →
  `miniz` (C90, when `QWRT_WITH_COMPRESS`), wasm3 → `m3` (C99, when
  `QWRT_WITH_WASM3`). qwrt's C99 / `-Werror` are PRIVATE to the qwrt targets
  only — vendored deps compile under their own standard.
- **Never edit vendored dep source** — control them only via CMake
  variables/options in the root `CMakeLists.txt`. Deps are pinned to specific
  git tags; update by checking out the desired tag in the submodule.
- `docs/` has design docs (`qwrt-architecture-design.md`, `pal-design.md`,
  `esp32s3-design.md`) — the architecture doc is in Chinese.

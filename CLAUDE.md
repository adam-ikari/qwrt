# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What qwrt is

qwrt is an embeddable **QuickJS-ng runtime wrapper** written in C99. It exposes a
small C API on top of the QuickJS-ng engine, plus a WinterTC-compatible
runtime of standard Web APIs (fetch, console, crypto, streams, timers, fs, …).
It is **libuv-native**: qwrt owns an internal thread running a libuv event
loop; the host never touches JS directly and talks to the runtime over JSON
messages (`qwrt_post_message` → `message_cb`). It is **standalone** — it
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

# Tests are labelled for selection: `offline` (CI default; local, deterministic),
# `network` (outbound HTTP/HTTPS), `benchmark` (perf, not pass/fail), `dap`.
ctest -L offline --output-on-failure

# Memory check
valgrind --leak-check=full ./build/test/test_qwrt
```

Feature toggles are CMake options prefixed `QWRT_WITH_*` (see README "CMake
Options"). Notable: `QWRT_WITH_TLS` (mbedTLS), `QWRT_WITH_COMPRESS` (miniz),
`QWRT_WITH_CRYPTO_EXT`, `QWRT_WITH_TEXTCODEC`, `QWRT_WITH_WAMR` (default ON —
the WASM engine; WAMR-2.4.5, Fast Interp + AOT) and `QWRT_WITH_WASM3` (default
OFF — alternative engine; the two are mutually exclusive). **libuv is a hard
dependency** (always built via `add_subdirectory` → `uv_a`, C99) — there is no
PAL-backend option anymore. Tests use **mock_libuv** (`test/mock_libuv.{c,h}`),
a fake `uv_*` API for deterministic offline tests, instead of a PAL mock.

**First configure needs network.** With `QWRT_BUILD_TESTS=ON`, googletest is
FetchContent'd from GitHub; with WAMR ON (the default), WAMR's build also
FetchContent's asmjit from GitHub (for AOT). Both fail offline. For an offline
build: `cmake -B build -DQWRT_WITH_WAMR=OFF -DQWRT_BUILD_TESTS=OFF`. CI mirrors
this — most matrix jobs pass `-DQWRT_WITH_WAMR=OFF`; one dedicated job covers
WAMR.

**qwrt is strict C99** (`CMAKE_C_STANDARD 99`, `CMAKE_C_EXTENSIONS OFF` →
`-std=c99`, enforced with `-Wall -Wextra -Werror` via `qwrt_enable_warnings`).
quickjs-ng and libuv ship C11 `<stdatomic.h>` code, but qwrt applies C99
atomics patches (`deps/quickjs-ng-c99-atomics.patch`,
`deps/libuv-c99-atomics.patch`) that swap the C11 `_Atomic`/`atomic_*` ops for
GCC/Clang `__atomic_*` builtins, so they compile under `-std=c99` (qwrt forces
`C_STANDARD 99` on the `qjs`/`uv_a` targets). The patches are applied **in-place
to the submodule working trees** at configure time (`patch -p1 -f`), so
`git status` shows `m deps/libuv`, `m deps/quickjs-ng` after any configure —
this is expected, do not revert or commit it; the next configure re-applies.
`QWRT_UNUSED(x)` (qwrt_internal.h) marks QuickJS-callback fixed-signature
params that would otherwise trip `-Wunused-parameter`.

**No mutable file-scope state.** `src/*.c` contains zero mutable globals —
all per-runtime state (QuickJS class IDs, WASM engine environment, the embedded
`uv_loop_t`, thread handles) lives on `qwrt_t` (per-runtime, since one qwrt_t
owns one JSRuntime and QuickJS classes are runtime-scoped). Recover `qwrt_t*`
from a `JSContext*` via `qwrt_get_rt_from_ctx(ctx)`, or from a `JSRuntime*`
(finalizers) via `qwrt_get_rt_from_jsrt(jsrt)`. Deterministic lookup tables
(e.g. CRC32) are `static const`.

Tests link `qwrt` + `mock_libuv` (not a PAL); network/TLS/stream tests
additionally link real libuv (`uv_a`) and are gated behind
`LIBUV_FOUND`/`QWRT_WITH_TLS`. A few tests are GoogleTest `.cpp` (fetched via
FetchContent). **The `qwrt` static library does NOT link `uv_a`** — it compiles
against libuv headers only; uv symbols resolve at the final target
(tests: `mock_libuv`; `qwrt_full`: `uv_a`).

### Building the WinterTC modules

The WinterTC-compatible runtime is **precompiled to QuickJS bytecode and inlined as
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
wrong. The WinterTC modules are injected into a context by `qwrt_inject_polyfill_ctx`
(bridge.c), which sets `__pal_inject__` to a `pal` JS object for the duration of
the eval.

## Architecture

The runtime is layered. Read these together to understand it:

- **`include/qwrt/qwrt.h`** — the public surface: `qwrt_config_t` (initial_script /
  message_cb / debug / host_data), `qwrt_ext_t` (extension hooks:
  `init`/`destroy`/`suspend`/`resume`), and the core API — 6 functions:
  `qwrt_create`, `qwrt_destroy`, `qwrt_post_message`, `qwrt_get_runtime_data`,
  `qwrt_set_runtime_data`, `qwrt_free`. The PAL-era `qwrt_eval`/`qwrt_tick`
  APIs are **gone**: the host does not eval JS or drive a loop — it posts JSON
  messages and receives them via `message_cb`.
- **`src/qwrt_internal.h`** — the real internal layout. `qwrt_t` holds a fixed
  array of up to `QWRT_MAX_CONTEXTS` (64) `qwrt_ctx_t*`, an embedded `uv_loop_t`
  (BY VALUE), the internal `uv_thread_t`, a lock-guarded inbound message FIFO,
  worker/handle tables, and the module bytecode (saved for `qwrt_reset`
  re-injection). `QWRT_MAGIC` validates the opaque `qwrt_t*`. The `uv.h`
  include switches to `mock_libuv.h` under `QWRT_USE_MOCK_LIBUV`.
- **`src/qwrt.c`** — core lifecycle (`qwrt_create` spawns the internal thread
  and blocks until ready, `qwrt_destroy` shuts it down gracefully,
  `qwrt_runtime_init`/`qwrt_eval_internal`/`qwrt_thread_teardown` internals).
- **`src/thread.c`** — the internal thread: runs `uv_run(UV_RUN_ONCE)`, drains
  the inbound FIFO via `qwrt_wake_cb`, and flushes all JS microtasks
  (`qwrt_flush_microtasks`) after each loop iteration.
- **`src/msgq.c`** — the lock-guarded inbound message FIFO + `qwrt_wake_cb`
  (uv_async wakeup) + message encode/decode.
- **`src/uv_io.c`** — direct libuv I/O: timers, fs, HTTP, TLS. The old PAL
  backend logic now calls libuv directly (still exposed to JS via the `pal`
  object / `qwrt_io_*` functions).
- **`src/context.c`** — multi-context lifecycle (`spawn`/`suspend`/`resume`/
  `destroy_ctx`, `qwrt_get_active_ctx`).
- **`src/worker.c`** — Web Worker support: `new Worker(url)` spawns a new qwrt_t
  (independent JSRuntime + independent loop) on a new thread.
- **`src/extension.c`** — runs `qwrt_ext_t` hooks across the build-time
  extension table (no runtime registration; the table is fixed at compile time
  via the `QWRT_EXTENSIONS` macro in `include/qwrt/qwrt_ext_registry.h`).
- **`src/bridge.c`** — the JS↔libuv bridge. Builds the per-context `pal` JS
  object (`qwrt_create_pal_object_ctx`), injects the WinterTC modules,
  dispatches host messages (`onmessage`), and encodes/decodes JSON between the
  host and JS.
- **`src/ext_*.c`** — native extensions (compress/crypto/textcodec/wamr,
  optionally wasm3/web-wasm), each implementing `qwrt_ext_t`.
- **`test/mock_libuv.{c,h}`** — fake `uv_*` API for deterministic offline tests
  (synchronous scheduler; real pthread mutex/cond/thread).

### Key execution model

- **Single qwrt thread owns all JS.** qwrt_create spawns one internal thread
  (`uv_thread_t`) that runs the embedded `uv_loop_t`. Every JS evaluation, uv
  callback, and microtask runs on that thread. The host thread never touches
  `JSContext` directly.
- **Message boundary, not eval.** Host → runtime: `qwrt_post_message`
  (thread-safe, JSON is copied into the FIFO); the loop thread drains the FIFO
  and dispatches to JS `onmessage`. Runtime → host: JS `postMessage` →
  `message_cb` (runs on the qwrt thread — the callback must be thread-safe).
- **uv callbacks enter JS directly** on the loop thread (no deferred queue —
  the old `qwrt_defer_callback` mechanism is gone). After each `uv_run`,
  `qwrt_flush_microtasks` drains the entire `JS_ExecutePendingJob` queue, so
  one `qwrt_post_message` round-trip implies the prior eval's promise chain
  completed.
- **Graceful shutdown.** `qwrt_destroy` requests the loop thread to exit
  (`uv_stop`), joins it, then frees the runtime.

### Bridge layer discipline (`src/bridge.c`)

The `js_pal_*` wrappers in `bridge.c` are the only C between the libuv I/O
layer (`src/uv_io.c`) and the WinterTC modules (JS, which closures over the `pal` JS
object). C is *required* here for three things nothing else can do:

1. **JSValue ↔ C conversion** (`JS_ToCString`, `JS_GetUint8Array`,
   `JS_NewArrayBufferCopy`, `JS_NewString`, …) — the JS modules can't touch
   QuickJS internal representations.
2. **The I/O call** — invoking the `qwrt_io_*` functions (direct libuv), not a
   PAL function pointer.
3. **Promise + thread boundary** — `JS_NewPromiseCapability`, storing
   resolve/reject handles, so uv callbacks settle promises on the loop thread.

**Everything else stays out of the bridge.** A `js_pal_*` wrapper should do
*only* the three things above. In particular: input validation, default
values, length caps, level mappings, and string formatting are **not** the
bridge's job — they belong in the JS WinterTC modules (caller-facing semantics) or
`uv_io.c` (platform policy), not in C. Keep the bridge thin.
Example: the Web Crypto 65536-byte cap on `getRandomValues` lives in
`polyfill/src/crypto.js`, not in `js_pal_random_bytes`.

## Conventions (from CONTRIBUTING.md)

- C99, 4 spaces, no tabs, no trailing whitespace. `snake_case` functions/vars,
  `SHOUTING_CASE` macros. `/* */` comments (not `//`) in C. Include guards are
  `#ifndef QWRT_…_H` (no `#pragma once`).
- Conventional Commits (`feat(qwrt):`, `fix(uv_io):`, …).
- Adding an **extension**: `src/ext_<name>.c` + `include/qwrt/ext_<name>.h`,
  implement `qwrt_ext_t` (at least `init`+`destroy`), register JS via
  `JS_SetPropertyStr` in `init`, add a `QWRT_WITH_<NAME>` option, list it in the
  default extensions in `qwrt_create`.
- Tests register with `add_test` (NOT a POST_BUILD step — a POST_BUILD run aborts
  the whole build on one failure, see the comment in `test/CMakeLists.txt`).

## Repo layout notes

- `deps/` contains all third-party dependencies as **git submodules** with
  pinned versions: `libuv/` (v1.52.1 — the sole platform backend), `mbedtls/`
  (v3.6.6), `miniz/` (3.1.2), `quickjs-ng/` (v0.15.1), `wamr/` (WAMR-2.4.5,
  default WASM engine), `wasm3/` (v0.5.0, alternative WASM engine).
  **All dependencies are built from source — qwrt links no system libraries.**
  Each is pulled in via `add_subdirectory(... EXCLUDE_FROM_ALL)` (never
  `execute_process`), so its `.o` files live in the main build tree and are
  subject to `-j` / incremental rebuild. Targets: quickjs-ng → `qjs` (C99;
  atomics patched), libuv → `uv_a` (C99; atomics patched, always built),
  mbedtls → `mbedtls`/`mbedx509`/
  `mbedcrypto` (C99, when `QWRT_WITH_TLS`/`QWRT_WITH_CRYPTO_EXT`), miniz →
  `miniz` (C90, when `QWRT_WITH_COMPRESS`), wasm3 → `m3` (C99, when
  `QWRT_WITH_WASM3`), wamr → `vmlib` (libiwasm.a, when `QWRT_WITH_WAMR`).
  qwrt's C99 / `-Werror` are PRIVATE to the qwrt targets
  only — vendored deps compile under their own standard.
- **Never edit vendored dep source** — control them only via CMake
  variables/options in the root `CMakeLists.txt`. Deps are pinned to specific
  git tags; update by checking out the desired tag in the submodule.
- `src/debugger.c` + `src/debugger_dap.c` (+ `include/qwrt/qwrt_debug{,_dap}.h`)
  implement the DAP step-debugger, gated by `QWRT_BUILD_DEBUGGER` (default OFF;
  applies `deps/quickjs-ng-debugger.patch` to the engine).
- `libqwrt_full` bundles core + uv + mock + extensions for host embedding;
  `libqwrt.pc` (pkg-config) is also produced.
- `.github/workflows/ci.yml` — Actions matrix: most jobs build with
  `-DQWRT_WITH_WAMR=OFF` for speed, plus dedicated `wamr`/`wasm3` and ASan+UBSan
  jobs; tests run as `ctest -L offline`.
- `docs/` is a VitePress site (`.vitepress/`, `zh/`) plus design docs
  (`qwrt-architecture-design.md` — PAL sections marked deprecated after the
  libuv-native migration) and the current migration spec
  (`docs/superpowers/specs/2026-08-12-qwrt-libuv-native-design.md`).

<!-- BEGIN brain.md -->
## Project Brain

This project keeps a **Project Brain**: a persistent memory layer of its durable decisions, requirements, and constraints. Read `./BRAIN.md` for the full read/write contract.
@import ./BRAIN.md

Maintain the brain as part of normal coding work — not as a separate task. While discussing or implementing features:
- **Start of a task:** load relevant context with the `brain` CLI (`list-pages`, `read-page`, `read-root`). Prefer a narrow read over scanning everything.
- **When a decision, requirement, constraint, or durable insight settles** (in chat or while coding): capture it immediately via the `brain` CLI. Do not wait to be asked and do not batch it for later.
- **Pure implementation with no new decision:** do not write to the brain.
- **When overturning a prior conclusion:** update the page (`update-truth` and/or `append-timeline` with `kind: reversal`, or `archive-page`).
- Only store what will still matter in six months and is hard to reconstruct from the code alone.
- All reads and writes go through the `brain` CLI — never hand-edit brain files.

The brain skills (`brain-setup`, `brain-page`, `brain-ingest`, `brain-bootstrap`) are installed in your global skills directory. Prefer `brain init` to scaffold a new project.
<!-- END brain.md -->

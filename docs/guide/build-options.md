---
title: Build Options
description: Complete reference of Amoib.js CMake options — AM_WITH_* feature toggles and AM_BUILD_* build targets.
---

# Build Options

amoib's CMake options live on **two separate levels**: `AM_WITH_*` toggles
**optional features** (native extensions layered on top of the runtime), while
`AM_BUILD_*` controls what gets built (tests, examples, debugger). libuv is a
**hard dependency** — it is always built from the `deps/libuv` submodule and
there is no option to disable it. Defaults are sensible for a full-featured
Linux build.

## Feature Toggles (`AM_WITH_*`)

These toggle optional native extensions on top of the WinterTC-compatible
runtime.

| Option | Default | Description |
|--------|---------|-------------|
| `AM_WITH_TLS` | ON | mbedTLS for HTTPS. Forces `AM_WITH_CRYPTO_EXT=ON` — a TLS client without `crypto.subtle` (no cert hashing, no WebCrypto key derivation) is not a complete WinterTC runtime. Disable to remove mbedTLS entirely. |
| `AM_WITH_COMPRESS` | ON | miniz compression extension. Adds gzip/zlib/deflate to the JS API. |
| `AM_WITH_CRYPTO_EXT` | ON | `crypto.subtle` extension: SHA-256/384/512, HMAC, PBKDF2, AES-GCM via mbedTLS. May be used without TLS (HTTP-only); required by `AM_WITH_TLS`. When OFF, `crypto.subtle` is `undefined` (no JS fallback). |
| `AM_WITH_TEXTCODEC` | ON | UTF-8 and Base64 TextEncoder/TextDecoder. |
| `AM_WITH_NONUTF_ENCODINGS` | OFF | Enable non-UTF encoding labels (Latin-1, replacement) in TextDecoder. |
| `AM_WITH_WAMR` | ON | WAMR WebAssembly engine (Fast Interpreter + AOT). Default WASM engine. |
| `AM_WITH_WASM3` | OFF | wasm3 WebAssembly interpreter (alternative, more portable). |

**Note:** `AM_WITH_WAMR` and `AM_WITH_WASM3` are mutually exclusive — both register the `WebAssembly` global.

## Build Profiles (`AM_PROFILE`)

`AM_PROFILE` is a preset bundle of the `AM_WITH_*` feature toggles. It
**only changes options not explicitly given** on the command line:
`-DAM_PROFILE=minimal -DAM_WITH_TLS=ON` keeps the explicit `TLS=ON`.
Empty (default) behaves bit-for-bit like the historical per-option defaults.
Any other value fails configure.

| Profile | Macro effect | amoib size (stripped, measured) | ECMA-429 WinterTC |
|---------|--------------|-------------------------------|-------------------|
| `standard` (equivalent to empty) | Same as historical defaults: WAMR/TLS/COMPRESS/CRYPTO_EXT/TEXTCODEC=ON | Same as default build | ✅ full mandatory set met |
| `minimal` | Same as standard but **TLS=OFF** (fetch degrades to http-only; ECMA-429 has no HTTPS requirement) | **2.45 MiB** (Release/-O3); 1.81 MiB (MinSizeRel/-Os) | ✅ still met: atob/btoa, WebAssembly (WAMR), crypto.subtle, CompressionStream all present |

Re-running configure in the **same build directory** with a different
`AM_PROFILE` recalculates the five `AM_WITH_*` cache entries to the new
profile's defaults (status message `AM_PROFILE changed: ...`). Explicit
`-DAM_WITH_X` values do **not** survive a profile switch — prefer a fresh
build directory when mixing presets with explicit overrides.

## gRPC Stack (`AM_WITH_GRPC`)

| Option | Default | Description |
|--------|---------|-------------|
| `AM_WITH_GRPC` | OFF | Embed the gRPC/HTTP2 stack (h2 + HPACK + protobuf + grpc, ~3.5k lines JS) in the polyfill bundle. Needs npm + esbuild + qjsc (same prerequisites as the polyfill rebuild); warns and skips when the toolchain is missing. Manual path: `AM_WITH_GRPC=1 node polyfill/build.js`. |

## Build Targets (`AM_BUILD_*`)

| Option | Default | Description |
|--------|---------|-------------|
| `AM_BUILD_TESTS` | OFF | Build the test suite. Enables FetchContent for GoogleTest. |
| `AM_BUILD_EXAMPLES` | OFF | Build the examples in `examples/` (httpserver, grpc-hello, stream-pipeline, worker orchestration). |
| `AM_BUILD_CLI` | ON | Build the `amoib` CLI (`build/amoib`, `build/amoib-ctl`, `build/amoib-rt`). |
| `AM_BUILD_DEBUGGER` | OFF | Build the DAP step-debugger, applying the QuickJS-ng debugger patch and adding `src/debugger.c` + `src/debugger_dap.c` to `libamoib`. |

> **Note:** `AM_BUILD_DEBUGGER` and `AM_WITH_NONUTF_ENCODINGS` are gate-checked
> by the `AM_WITH_*` feature matrix in CI — see `.github/workflows/ci.yml`.

## Common Configurations

### Development (full debug, all features)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug \
      -DAM_BUILD_TESTS=ON -DAM_WITH_TLS=ON \
      -DAM_WITH_COMPRESS=ON -DAM_WITH_CRYPTO_EXT=ON \
      -DAM_WITH_TEXTCODEC=ON -DAM_WITH_WAMR=ON
```

### Minimal (embedded, no networking)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=MinSizeRel \
      -DAM_WITH_TLS=OFF -DAM_WITH_COMPRESS=OFF \
      -DAM_WITH_CRYPTO_EXT=OFF -DAM_WITH_TEXTCODEC=OFF \
      -DAM_WITH_WAMR=OFF
```

### Release (production, all features)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release \
      -DAM_WITH_TLS=ON -DAM_WITH_COMPRESS=ON \
      -DAM_WITH_CRYPTO_EXT=ON -DAM_WITH_TEXTCODEC=ON \
      -DAM_WITH_WAMR=ON
```

## Compiler Flags

amoib and all dependencies compile under `-std=c99 -Wall -Wextra -Werror` (enforced via `am_enable_warnings`). quickjs-ng and libuv ship C11 atomics, but amoib patches them to use GCC/Clang `__atomic_*` builtins (`deps/*-c99-atomics.patch`), so no C11 is required.

### Suppressing Unused Parameter Warnings

QuickJS callbacks have fixed signatures that may include unused parameters. Use `AM_UNUSED(x)`:

```c
static JSValue my_callback(JSContext *ctx, JSValue this_val,
                           int argc, JSValue *argv) {
    AM_UNUSED(this_val);  // suppresses -Wunused-parameter
    // ...
}
```

## Output

| File | Description |
|------|-------------|
| `build/lib/libamoib.a` | Core runtime library (static, does not link libuv) |
| `build/lib/libam_full.a` | Aggregator: amoib + libuv + mbedTLS + miniz + WAMR + pthread/dl/rt |
| `build/test/test_*` | Test binaries (when `AM_BUILD_TESTS=ON`) |

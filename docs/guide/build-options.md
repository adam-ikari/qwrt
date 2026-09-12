---
title: Build Options
description: Complete reference of Qwrt.js CMake options — QWRT_WITH_* feature toggles and QWRT_BUILD_* build targets.
---

# Build Options

qwrt's CMake options live on **two separate levels**: `QWRT_WITH_*` toggles
**optional features** (native extensions layered on top of the runtime), while
`QWRT_BUILD_*` controls what gets built (tests, examples, debugger). libuv is a
**hard dependency** — it is always built from the `deps/libuv` submodule and
there is no option to disable it. Defaults are sensible for a full-featured
Linux build.

## Feature Toggles (`QWRT_WITH_*`)

These toggle optional native extensions on top of the WinterTC-compatible
runtime.

| Option | Default | Description |
|--------|---------|-------------|
| `QWRT_WITH_TLS` | ON | mbedTLS for HTTPS. Forces `QWRT_WITH_CRYPTO_EXT=ON` — a TLS client without `crypto.subtle` (no cert hashing, no WebCrypto key derivation) is not a complete WinterTC runtime. Disable to remove mbedTLS entirely. |
| `QWRT_WITH_COMPRESS` | ON | miniz compression extension. Adds gzip/zlib/deflate to the JS API. |
| `QWRT_WITH_CRYPTO_EXT` | ON | `crypto.subtle` extension: SHA-256/384/512, HMAC, PBKDF2, AES-GCM via mbedTLS. May be used without TLS (HTTP-only); required by `QWRT_WITH_TLS`. When OFF, `crypto.subtle` is `undefined` (no JS fallback). |
| `QWRT_WITH_TEXTCODEC` | ON | UTF-8 and Base64 TextEncoder/TextDecoder. |
| `QWRT_WITH_WAMR` | ON | WAMR WebAssembly engine (Fast Interpreter + AOT). Default WASM engine. |
| `QWRT_WITH_WASM3` | OFF | wasm3 WebAssembly interpreter (alternative, more portable). |

**Note:** `QWRT_WITH_WAMR` and `QWRT_WITH_WASM3` are mutually exclusive — both register the `WebAssembly` global.

## Build Profiles (`QWRT_PROFILE`)

`QWRT_PROFILE` is a preset bundle of the `QWRT_WITH_*` feature toggles. It
**only changes options not explicitly given** on the command line:
`-DQWRT_PROFILE=minimal -DQWRT_WITH_TLS=ON` keeps the explicit `TLS=ON`.
Empty (default) behaves bit-for-bit like the historical per-option defaults.
Any other value fails configure.

| Profile | Macro effect | qwrt size (stripped, measured) | ECMA-429 WinterTC |
|---------|--------------|-------------------------------|-------------------|
| `standard` (equivalent to empty) | Same as historical defaults: WAMR/TLS/COMPRESS/CRYPTO_EXT/TEXTCODEC=ON | Same as default build | ✅ full mandatory set met |
| `minimal` | Same as standard but **TLS=OFF** (fetch degrades to http-only; ECMA-429 has no HTTPS requirement) | **2.45 MiB** (Release/-O3); 1.81 MiB (MinSizeRel/-Os) | ✅ still met: atob/btoa, WebAssembly (WAMR), crypto.subtle, CompressionStream all present |

Re-running configure in the **same build directory** with a different
`QWRT_PROFILE` recalculates the five `QWRT_WITH_*` cache entries to the new
profile's defaults (status message `QWRT_PROFILE changed: ...`). Explicit
`-DQWRT_WITH_X` values do **not** survive a profile switch — prefer a fresh
build directory when mixing presets with explicit overrides.

## gRPC Stack (`QWRT_WITH_GRPC`)

| Option | Default | Description |
|--------|---------|-------------|
| `QWRT_WITH_GRPC` | OFF | Embed the gRPC/HTTP2 stack (h2 + HPACK + protobuf + grpc, ~3.5k lines JS) in the polyfill bundle. Needs npm + esbuild + qjsc (same prerequisites as the polyfill rebuild); warns and skips when the toolchain is missing. Manual path: `QWRT_WITH_GRPC=1 node polyfill/build.js`. |

## Build Targets (`QWRT_BUILD_*`)

| Option | Default | Description |
|--------|---------|-------------|
| `QWRT_BUILD_TESTS` | OFF | Build the test suite. Enables FetchContent for GoogleTest. |

## Common Configurations

### Development (full debug, all features)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug \
      -DQWRT_BUILD_TESTS=ON -DQWRT_WITH_TLS=ON \
      -DQWRT_WITH_COMPRESS=ON -DQWRT_WITH_CRYPTO_EXT=ON \
      -DQWRT_WITH_TEXTCODEC=ON -DQWRT_WITH_WAMR=ON
```

### Minimal (embedded, no networking)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=MinSizeRel \
      -DQWRT_WITH_TLS=OFF -DQWRT_WITH_COMPRESS=OFF \
      -DQWRT_WITH_CRYPTO_EXT=OFF -DQWRT_WITH_TEXTCODEC=OFF \
      -DQWRT_WITH_WAMR=OFF
```

### Release (production, all features)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release \
      -DQWRT_WITH_TLS=ON -DQWRT_WITH_COMPRESS=ON \
      -DQWRT_WITH_CRYPTO_EXT=ON -DQWRT_WITH_TEXTCODEC=ON \
      -DQWRT_WITH_WAMR=ON
```

## Compiler Flags

qwrt and all dependencies compile under `-std=c99 -Wall -Wextra -Werror` (enforced via `qwrt_enable_warnings`). quickjs-ng and libuv ship C11 atomics, but qwrt patches them to use GCC/Clang `__atomic_*` builtins (`deps/*-c99-atomics.patch`), so no C11 is required.

### Suppressing Unused Parameter Warnings

QuickJS callbacks have fixed signatures that may include unused parameters. Use `QWRT_UNUSED(x)`:

```c
static JSValue my_callback(JSContext *ctx, JSValue this_val,
                           int argc, JSValue *argv) {
    QWRT_UNUSED(this_val);  // suppresses -Wunused-parameter
    // ...
}
```

## Output

| File | Description |
|------|-------------|
| `build/lib/libqwrt.a` | Core runtime library (static, does not link libuv) |
| `build/lib/libqwrt_full.a` | Aggregator: qwrt + libuv + mbedTLS + miniz + WAMR + pthread/dl/rt |
| `build/test/test_*` | Test binaries (when `QWRT_BUILD_TESTS=ON`) |

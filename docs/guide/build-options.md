---
title: Build Options
description: Complete reference of Qwrt.js CMake options — QWRT_WITH_* feature toggles, QWRT_PAL_* platform backends, and QWRT_BUILD_* targets.
---

# Build Options

qwrt's CMake options live on **two separate levels**: `QWRT_PAL_*` selects the
**platform backend** (which `pal_*` implementation to compile), while
`QWRT_WITH_*` toggles **optional features** (native extensions layered on top
of the runtime). The two prefixes are independent — a PAL backend can be built
with or without any given feature. `QWRT_BUILD_*` is a third, unrelated group
that controls what gets built (tests, examples, debugger). Defaults are
sensible for a full-featured Linux/macOS build.

## Feature Toggles (`QWRT_WITH_*`)

These toggle optional native extensions on top of the WinterCG-compatible
runtime. They do **not** select a platform backend.

| Option | Default | Description |
|--------|---------|-------------|
| `QWRT_WITH_TLS` | ON | mbedTLS for HTTPS. Forces `QWRT_WITH_CRYPTO_EXT=ON` — a TLS client without `crypto.subtle` (no cert hashing, no WebCrypto key derivation) is not a complete WinterCG runtime. Disable to remove mbedTLS entirely. |
| `QWRT_WITH_COMPRESS` | ON | miniz compression extension. Adds gzip/zlib/deflate to the JS API. |
| `QWRT_WITH_CRYPTO_EXT` | ON | `crypto.subtle` extension: SHA-256/384/512, HMAC, PBKDF2, AES-GCM via mbedTLS. May be used without TLS (HTTP-only); required by `QWRT_WITH_TLS`. When OFF, `crypto.subtle` is `undefined` (no JS fallback). |
| `QWRT_WITH_TEXTCODEC` | ON | UTF-8 and Base64 TextEncoder/TextDecoder. |
| `QWRT_WITH_WAMR` | ON | WAMR WebAssembly engine (Fast Interpreter + AOT). Default WASM engine. |
| `QWRT_WITH_WASM3` | OFF | wasm3 WebAssembly interpreter (alternative, more portable). |

**Note:** `QWRT_WITH_WAMR` and `QWRT_WITH_WASM3` are mutually exclusive — both register the `WebAssembly` global.

## PAL Backends (`QWRT_PAL_*`)

These select the platform backend — **not on the same level** as the feature
toggles above. Each controls compilation of one `platform/*/` PAL
implementation.

| Option | Default | Description |
|--------|---------|-------------|
| `QWRT_PAL_UV` | ON | libuv backend (Linux/macOS). Requires libuv submodule. |
| `QWRT_PAL_MOCK` | ON | Mock backend for testing. Always build this for test support. |
| `QWRT_PAL_FREERTOS` | OFF | FreeRTOS backend (ESP32-S3). Requires ESP-IDF. |

## Build Targets (`QWRT_BUILD_*`)

| Option | Default | Description |
|--------|---------|-------------|
| `QWRT_BUILD_TESTS` | OFF | Build the test suite. Enables FetchContent for GoogleTest. |
| `QWRT_BUILD_EXAMPLES` | OFF | Build example programs in `examples/`. |
| `QWRT_BUILD_DEBUGGER` | OFF | DAP step-debugger. Patches QuickJS-ng to add breakpoint/step primitives and compiles `src/debugger.c` + `src/debugger_dap.c` into `libqwrt.a`. Zero overhead when OFF (patch not applied, sources not compiled). Enable with `QWRT_DEBUG=1` at runtime. See [Debugging](../dev/debugging.md). |

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
      -DQWRT_WITH_WAMR=OFF -DQWRT_PAL_UV=OFF
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
| `build/lib/libqwrt.a` | Core runtime library |
| `build/lib/libqwrt_uv.a` | libuv PAL backend |
| `build/lib/libqwrt_mock.a` | Mock PAL backend |
| `build/lib/libqwrt_freertos.a` | FreeRTOS PAL backend |
| `build/test/test_*` | Test binaries (when `QWRT_BUILD_TESTS=ON`) |

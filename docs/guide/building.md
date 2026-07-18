---
title: Building
description: CMake build options for Qwrt.js — feature toggles, PAL backends, C99 toolchain, and example configurations for development and production.
---

# Building

qwrt uses CMake with feature toggles. All dependencies are built from source — no system packages required.

## Basic Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Build types: `Release` (optimized), `Debug` (with symbols and assertions), `RelWithDebInfo`, `MinSizeRel`.

## CMake Options

### Feature Toggles (`QWRT_WITH_*`)

| Option | Default | Description |
|--------|---------|-------------|
| `QWRT_WITH_TLS` | ON | mbedTLS for HTTPS and crypto primitives |
| `QWRT_WITH_COMPRESS` | ON | miniz compression/decompression extension |
| `QWRT_WITH_CRYPTO_EXT` | ON | `crypto.subtle` (SHA, HMAC, PBKDF2, AES-GCM) |
| `QWRT_WITH_TEXTCODEC` | ON | UTF-8 / Base64 encoder/decoder |
| `QWRT_WITH_WAMR` | ON | WAMR WebAssembly engine (Fast Interp + AOT, default) |
| `QWRT_WITH_WASM3` | OFF | wasm3 WebAssembly engine (alternative, lighter weight) |

**Note:** `QWRT_WITH_WAMR` and `QWRT_WITH_WASM3` are mutually exclusive — only one WASM engine can be enabled at a time.

### PAL Backends (`QWRT_PAL_*`)

| Option | Default | Description |
|--------|---------|-------------|
| `QWRT_PAL_UV` | ON | libuv PAL (Linux/macOS) |
| `QWRT_PAL_MOCK` | ON | Mock PAL (testing) |
| `QWRT_PAL_FREERTOS` | OFF | FreeRTOS PAL (ESP32-S3, ESP-IDF only) |

### Build Targets

| Option | Default | Description |
|--------|---------|-------------|
| `QWRT_BUILD_TESTS` | OFF | Build test suite (26 test targets) |
| `QWRT_BUILD_EXAMPLES` | OFF | Build examples in `examples/` |

## Example Configurations

### Minimal (no TLS, no compression, stub WASM)

```bash
cmake -B build -DQWRT_WITH_TLS=OFF -DQWRT_WITH_COMPRESS=OFF \
      -DQWRT_WITH_CRYPTO_EXT=OFF -DQWRT_WITH_TEXTCODEC=OFF \
      -DQWRT_WITH_WAMR=OFF
```

### Full Development Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug \
      -DQWRT_BUILD_TESTS=ON -DQWRT_WITH_TLS=ON \
      -DQWRT_WITH_COMPRESS=ON -DQWRT_WITH_CRYPTO_EXT=ON \
      -DQWRT_WITH_TEXTCODEC=ON -DQWRT_WITH_WAMR=ON
cmake --build build -j$(nproc)
cd build && ctest --output-on-failure
```

### wasm3 Alternative Engine

```bash
cmake -B build -DQWRT_WITH_WAMR=OFF -DQWRT_WITH_WASM3=ON
cmake --build build -j$(nproc)
```

## C Standard Isolation

qwrt and all its dependencies build under **strict C99** (`-std=c99`). quickjs-ng and libuv ship C11 `<stdatomic.h>` code, but qwrt applies small patches (`deps/quickjs-ng-c99-atomics.patch`, `deps/libuv-c99-atomics.patch`) that swap the C11 `_Atomic`/`atomic_*` ops for GCC/Clang `__atomic_*` builtins — so no C11 is required anywhere.

## Output Artifacts

| Artifact | Path |
|----------|------|
| `libqwrt.a` | `build/lib/` |
| `libqwrt_uv.a` | `build/lib/` (when `QWRT_PAL_UV`) |
| `libqwrt_mock.a` | `build/lib/` (when `QWRT_PAL_MOCK`) |
| `libqwrt_freertos.a` | `build/lib/` (when `QWRT_PAL_FREERTOS`) |
| Test binaries | `build/test/` |

## ESP32-S3 Build

For ESP32-S3 with ESP-IDF:

```bash
# In your ESP-IDF project, add qwrt/esp-idf/ to EXTRA_COMPONENT_DIRS
idf.py set-target esp32s3
idf.py build
```

See the [pal_freertos documentation](/pal/pal-freertos) for details.

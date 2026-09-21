---
title: Building
description: CMake build options for Qwrt.js — feature toggles, C99 toolchain, and example configurations for development and production.
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

### Build Targets

| Option | Default | Description |
|--------|---------|-------------|
| `QWRT_BUILD_TESTS` | OFF | Build test suite (25 test targets) |
| `QWRT_BUILD_EXAMPLES` | OFF | Build examples in `examples/` |
| `QWRT_BUILD_CLI` | ON | Build the `qwrt` CLI plus the `qwrt-rt` worker and `qwrt-ctl` control-plane binaries |
| `QWRT_PROCESS_MODEL` | ISOLATED | `THREAD` (single-process multi-thread) or `ISOLATED` (dedicated child processes via fork+exec, default since the M-P2 milestone) |

## Example Configurations

### Minimal (WinterTC still met)

```bash
cmake -B build -DQWRT_PROFILE=minimal
cmake --build build -j$(nproc)
```

`minimal` keeps WebAssembly, `crypto.subtle`, `atob`/`btoa`, and compression
(2.45 MiB stripped, Release) — the smallest profile that satisfies the full
WinterTC mandatory set. See [Build Options](/guide/build-options) for the
profile table and the `QWRT_WITH_GRPC` CMake option.

### Full Development Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug \
      -DQWRT_BUILD_TESTS=ON
cmake --build build -j$(nproc)
cd build && ctest --output-on-failure
```

Full option reference including build profiles (`QWRT_PROFILE`) and the gRPC
stack (`QWRT_WITH_GRPC`): [Build Options](/guide/build-options).

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
| `libqwrt.a` | `build/` (static core — deliberately does not link libuv; uv symbols resolve at the final executable) |
| `libqwrt_full.a` | `build/` (CMake link-interface aggregator: qwrt + libuv + mbedTLS + miniz + WAMR + pthread/dl/rt) |
| `qwrt.pc` | `build/` (pkg-config — `pkg-config --cflags --libs qwrt` lists every vendored archive) |
| Test binaries | `build/test/` |
| `qwrt` | `build/` (CLI — `qwrt -e 'console.log(1)'`) |
| `qwrt-rt` | `build/` (worker-process binary, spawned by `qwrt_proc_spawn` via fork+exec) |
| `qwrt-ctl` | `build/` (control-plane endpoint client) |


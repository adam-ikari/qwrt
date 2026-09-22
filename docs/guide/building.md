---
title: Building
description: CMake build options for Amoib.js — feature toggles, C99 toolchain, and example configurations for development and production.
---

# Building

amoib uses CMake with feature toggles. All dependencies are built from source — no system packages required.

## Basic Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Build types: `Release` (optimized), `Debug` (with symbols and assertions), `RelWithDebInfo`, `MinSizeRel`.

## CMake Options

### Feature Toggles (`AM_WITH_*`)

| Option | Default | Description |
|--------|---------|-------------|
| `AM_WITH_TLS` | ON | mbedTLS for HTTPS and crypto primitives |
| `AM_WITH_COMPRESS` | ON | miniz compression/decompression extension |
| `AM_WITH_CRYPTO_EXT` | ON | `crypto.subtle` (SHA, HMAC, PBKDF2, AES-GCM) |
| `AM_WITH_TEXTCODEC` | ON | UTF-8 / Base64 encoder/decoder |
| `AM_WITH_WAMR` | ON | WAMR WebAssembly engine (Fast Interp + AOT, default) |
| `AM_WITH_WASM3` | OFF | wasm3 WebAssembly engine (alternative, lighter weight) |

**Note:** `AM_WITH_WAMR` and `AM_WITH_WASM3` are mutually exclusive — only one WASM engine can be enabled at a time.

### Build Targets

| Option | Default | Description |
|--------|---------|-------------|
| `AM_BUILD_TESTS` | OFF | Build test suite (25 test targets) |
| `AM_BUILD_EXAMPLES` | OFF | Build examples in `examples/` |
| `AM_BUILD_CLI` | ON | Build the `amoib` CLI plus the `amoib-rt` worker and `amoib-ctl` control-plane binaries |
| `AM_PROCESS_MODEL` | ISOLATED | `THREAD` (single-process multi-thread) or `ISOLATED` (dedicated child processes via fork+exec, default since the M-P2 milestone) |

## Example Configurations

### Minimal (WinterTC still met)

```bash
cmake -B build -DAM_PROFILE=minimal
cmake --build build -j$(nproc)
```

`minimal` keeps WebAssembly, `crypto.subtle`, `atob`/`btoa`, and compression
(2.45 MiB stripped, Release) — the smallest profile that satisfies the full
WinterTC mandatory set. See [Build Options](/guide/build-options) for the
profile table and the `AM_WITH_GRPC` CMake option.

### Full Development Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug \
      -DAM_BUILD_TESTS=ON
cmake --build build -j$(nproc)
cd build && ctest --output-on-failure
```

Full option reference including build profiles (`AM_PROFILE`) and the gRPC
stack (`AM_WITH_GRPC`): [Build Options](/guide/build-options).

### wasm3 Alternative Engine

```bash
cmake -B build -DAM_WITH_WAMR=OFF -DAM_WITH_WASM3=ON
cmake --build build -j$(nproc)
```

## C Standard Isolation

amoib and all its dependencies build under **strict C99** (`-std=c99`). quickjs-ng and libuv ship C11 `<stdatomic.h>` code, but amoib applies small patches (`deps/quickjs-ng-c99-atomics.patch`, `deps/libuv-c99-atomics.patch`) that swap the C11 `_Atomic`/`atomic_*` ops for GCC/Clang `__atomic_*` builtins — so no C11 is required anywhere.

## Output Artifacts

| Artifact | Path |
|----------|------|
| `libamoib.a` | `build/` (static core — deliberately does not link libuv; uv symbols resolve at the final executable) |
| `libam_full.a` | `build/` (CMake link-interface aggregator: amoib + libuv + mbedTLS + miniz + WAMR + pthread/dl/rt) |
| `amoib.pc` | `build/` (pkg-config — `pkg-config --cflags --libs amoib` lists every vendored archive) |
| Test binaries | `build/test/` |
| `amoib` | `build/` (CLI — `amoib -e 'console.log(1)'`) |
| `amoib-rt` | `build/` (worker-process binary, spawned by `am_proc_spawn` via fork+exec) |
| `amoib-ctl` | `build/` (control-plane endpoint client) |


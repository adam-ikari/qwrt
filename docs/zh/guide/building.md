---
title: 构建
description: Amoib.js 的 CMake 构建选项 — 功能开关、C99 工具链，以及开发和生产环境的示例配置。
---

# 构建

amoib 使用 CMake 并通过功能开关进行配置。所有依赖从源码构建 — 无需系统包。

## 基本构建

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

构建类型：`Release`（优化）、`Debug`（带符号和断言）、`RelWithDebInfo`、`MinSizeRel`。

## CMake 选项

### 功能开关（`AM_WITH_*`）

| 选项 | 默认值 | 描述 |
|--------|---------|-------------|
| `AM_WITH_TLS` | ON | 用于 HTTPS 和加密原语的 mbedTLS |
| `AM_WITH_COMPRESS` | ON | miniz 压缩/解压扩展 |
| `AM_WITH_CRYPTO_EXT` | ON | `crypto.subtle`（SHA、HMAC、PBKDF2、AES-GCM） |
| `AM_WITH_TEXTCODEC` | ON | UTF-8 / Base64 编解码器 |
| `AM_WITH_WAMR` | ON | WAMR WebAssembly 引擎（Fast Interp + AOT，默认） |
| `AM_WITH_WASM3` | OFF | wasm3 WebAssembly 引擎（替代方案，更轻量） |

**注意：** `AM_WITH_WAMR` 和 `AM_WITH_WASM3` 互斥 — 一次只能启用一个 WASM 引擎。libuv 是硬依赖，始终从源码构建。

### 构建目标

| 选项 | 默认值 | 描述 |
|--------|---------|-------------|
| `AM_BUILD_TESTS` | OFF | 构建测试套件（25 个测试目标） |
| `AM_BUILD_EXAMPLES` | OFF | 构建 `examples/` 中的示例 |
| `AM_BUILD_CLI` | ON | 构建 `amoib` CLI 以及 `amoib-rt` worker 与 `amoib-ctl` 控制面二进制 |
| `AM_PROCESS_MODEL` | ISOLATED | `THREAD`（单进程多线程）或 `ISOLATED`（经 fork+exec 的独立子进程，自 M-P2 里程碑起为默认） |

## 示例配置

### 最小构建（仍满足 WinterTC）

```bash
cmake -B build -DAM_PROFILE=minimal
cmake --build build -j$(nproc)
```

`minimal` 保留 WebAssembly、`crypto.subtle`、`atob`/`btoa` 和压缩
（2.45 MiB，strip 后 Release）——满足 WinterTC 全量必选集的最小档位。
档案表与 `AM_WITH_GRPC` CMake option 见
[构建选项](/zh/guide/build-options)。

### 完整开发构建

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug \
      -DAM_BUILD_TESTS=ON

cmake --build build -j$(nproc)
cd build && ctest --output-on-failure
```

完整选项参考（含构建档位 `AM_PROFILE` 与 gRPC 栈 `AM_WITH_GRPC`）：
[构建选项](/zh/guide/build-options)。

### wasm3 替代引擎

```bash
cmake -B build -DAM_WITH_WAMR=OFF -DAM_WITH_WASM3=ON
cmake --build build -j$(nproc)
```

## C 标准隔离

amoib 及其所有依赖均在**严格 C99**（`-std=c99`）下构建。quickjs-ng 和 libuv 自带 C11 `<stdatomic.h>` 代码，但 amoib 应用了小型补丁（`deps/quickjs-ng-c99-atomics.patch`、`deps/libuv-c99-atomics.patch`），将 C11 的 `_Atomic`/`atomic_*` 操作替换为 GCC/Clang 的 `__atomic_*` 内建函数 — 因此任何地方都不需要 C11。

## 构建产物

| 产物 | 路径 |
|----------|------|
| `libamoib.a` | `build/`（静态核心 — 刻意不链接 libuv） |
| `libam_full.a` | `build/`（CMake 链接接口聚合库：amoib + libuv + mbedTLS + miniz + WAMR） |
| `amoib.pc` | `build/`（pkg-config — `pkg-config --cflags --libs amoib` 列出全部 vendored 归档） |
| 测试二进制文件 | `build/test/` |
| `amoib` | `build/`（CLI — `amoib -e 'console.log(1)'`） |
| `amoib-rt` | `build/`（worker 进程二进制，由 `am_proc_spawn` 经 fork+exec 派生） |
| `amoib-ctl` | `build/`（控制面端点客户端） |
---
title: 构建
description: Qwrt.js 的 CMake 构建选项 — 功能开关、PAL 后端、C99 工具链，以及开发和生产环境的示例配置。
---

# 构建

qwrt 使用 CMake 并通过功能开关进行配置。所有依赖从源码构建 — 无需系统包。

## 基本构建

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

构建类型：`Release`（优化）、`Debug`（带符号和断言）、`RelWithDebInfo`、`MinSizeRel`。

## CMake 选项

### 功能开关（`QWRT_WITH_*`）

| 选项 | 默认值 | 描述 |
|--------|---------|-------------|
| `QWRT_WITH_TLS` | ON | 用于 HTTPS 和加密原语的 mbedTLS |
| `QWRT_WITH_COMPRESS` | ON | miniz 压缩/解压扩展 |
| `QWRT_WITH_CRYPTO_EXT` | ON | `crypto.subtle`（SHA、HMAC、PBKDF2、AES-GCM） |
| `QWRT_WITH_TEXTCODEC` | ON | UTF-8 / Base64 编解码器 |
| `QWRT_WITH_WAMR` | ON | WAMR WebAssembly 引擎（Fast Interp + AOT，默认） |
| `QWRT_WITH_WASM3` | OFF | wasm3 WebAssembly 引擎（替代方案，更轻量） |

**注意：** `QWRT_WITH_WAMR` 和 `QWRT_WITH_WASM3` 互斥 — 一次只能启用一个 WASM 引擎。

### PAL 后端（`QWRT_PAL_*`）

| 选项 | 默认值 | 描述 |
|--------|---------|-------------|
| `QWRT_PAL_UV` | ON | libuv PAL（Linux/macOS） |
| `QWRT_PAL_MOCK` | ON | Mock PAL（测试） |
| `QWRT_PAL_FREERTOS` | OFF | FreeRTOS PAL（ESP32-S3，仅 ESP-IDF） |

### 构建目标

| 选项 | 默认值 | 描述 |
|--------|---------|-------------|
| `QWRT_BUILD_TESTS` | OFF | 构建测试套件（26 个测试目标） |
| `QWRT_BUILD_EXAMPLES` | OFF | 构建 `examples/` 中的示例 |

## 示例配置

### 最小构建（无 TLS、无压缩、无 WASM）

```bash
cmake -B build -DQWRT_WITH_TLS=OFF -DQWRT_WITH_COMPRESS=OFF \
      -DQWRT_WITH_CRYPTO_EXT=OFF -DQWRT_WITH_TEXTCODEC=OFF \
      -DQWRT_WITH_WAMR=OFF
```

### 完整开发构建

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug \
      -DQWRT_BUILD_TESTS=ON -DQWRT_WITH_TLS=ON \
      -DQWRT_WITH_COMPRESS=ON -DQWRT_WITH_CRYPTO_EXT=ON \
      -DQWRT_WITH_TEXTCODEC=ON -DQWRT_WITH_WAMR=ON
cmake --build build -j$(nproc)
cd build && ctest --output-on-failure
```

### wasm3 替代引擎

```bash
cmake -B build -DQWRT_WITH_WAMR=OFF -DQWRT_WITH_WASM3=ON
cmake --build build -j$(nproc)
```

## C 标准隔离

qwrt 及其所有依赖均在**严格 C99**（`-std=c99`）下构建。quickjs-ng 和 libuv 自带 C11 `<stdatomic.h>` 代码，但 qwrt 应用了小型补丁（`deps/quickjs-ng-c99-atomics.patch`、`deps/libuv-c99-atomics.patch`），将 C11 的 `_Atomic`/`atomic_*` 操作替换为 GCC/Clang 的 `__atomic_*` 内建函数 — 因此任何地方都不需要 C11。

## 构建产物

| 产物 | 路径 |
|----------|------|
| `libqwrt.a` | `build/lib/` |
| `libqwrt_uv.a` | `build/lib/`（当 `QWRT_PAL_UV` 开启时） |
| `libqwrt_mock.a` | `build/lib/`（当 `QWRT_PAL_MOCK` 开启时） |
| `libqwrt_freertos.a` | `build/lib/`（当 `QWRT_PAL_FREERTOS` 开启时） |
| 测试二进制文件 | `build/test/` |

## ESP32-S3 构建

对于带 ESP-IDF 的 ESP32-S3：

```bash
# 在你的 ESP-IDF 项目中，将 qwrt/esp-idf/ 添加到 EXTRA_COMPONENT_DIRS
idf.py set-target esp32s3
idf.py build
```

详见 [pal_freertos 文档](/zh/pal/pal-freertos)。
---
title: 构建选项
description: Qwrt.js CMake 选项完整参考 — QWRT_WITH_* 功能开关、QWRT_PAL_* 平台后端以及 QWRT_BUILD_* 构建目标。
---

# 构建选项

qwrt 的 CMake 选项分为**两个独立的层级**：`QWRT_PAL_*` 选择**平台后端**（编译哪个 `pal_*` 实现），而 `QWRT_WITH_*` 控制**可选功能**（构建在运行时之上的原生扩展）。这两个前缀相互独立——一个 PAL 后端可以在有或没有特定功能的情况下构建。`QWRT_BUILD_*` 是第三个不相关的组，控制构建目标（测试、示例、调试器）。默认值适用于功能完整的 Linux/macOS 构建。

## 功能开关（`QWRT_WITH_*`）

这些开关控制 WinterTC 兼容运行时之上的可选原生扩展。它们**不**选择平台后端。

| 选项 | 默认值 | 描述 |
|--------|---------|-------------|
| `QWRT_WITH_TLS` | ON | mbedTLS，用于 HTTPS。强制开启 `QWRT_WITH_CRYPTO_EXT=ON`——一个没有 `crypto.subtle`（无证书哈希、无 WebCrypto 密钥派生）的 TLS 客户端不是完整的 WinterTC 运行时。关闭可完全移除 mbedTLS。 |
| `QWRT_WITH_COMPRESS` | ON | miniz 压缩扩展。为 JS API 添加 gzip/zlib/deflate。 |
| `QWRT_WITH_CRYPTO_EXT` | ON | `crypto.subtle` 扩展：通过 mbedTLS 提供 SHA-256/384/512、HMAC、PBKDF2、AES-GCM。可在无 TLS 的情况下使用（纯 HTTP）；`QWRT_WITH_TLS` 依赖此项。关闭时，`crypto.subtle` 为 `undefined`（无 JS 回退）。 |
| `QWRT_WITH_TEXTCODEC` | ON | UTF-8 和 Base64 的 TextEncoder/TextDecoder。 |
| `QWRT_WITH_WAMR` | ON | WAMR WebAssembly 引擎（快速解释器 + AOT）。默认 WASM 引擎。 |
| `QWRT_WITH_WASM3` | OFF | wasm3 WebAssembly 解释器（备选，更便携）。 |

**注意：** `QWRT_WITH_WAMR` 和 `QWRT_WITH_WASM3` 互斥——两者都注册 `WebAssembly` 全局对象。

## PAL 后端（`QWRT_PAL_*`）

这些选项选择平台后端——与上面的功能开关**不在同一层级**。每个选项控制一个 `platform/*/` PAL 实现的编译。

| 选项 | 默认值 | 描述 |
|--------|---------|-------------|
| `QWRT_PAL_UV` | ON | libuv 后端（Linux/macOS）。需要 libuv 子模块。 |
| `QWRT_PAL_MOCK` | ON | Mock 后端，用于测试。始终构建以支持测试。 |
| `QWRT_PAL_FREERTOS` | OFF | FreeRTOS 后端（ESP32-S3）。需要 ESP-IDF。 |

## 构建目标（`QWRT_BUILD_*`）

| 选项 | 默认值 | 描述 |
|--------|---------|-------------|
| `QWRT_BUILD_TESTS` | OFF | 构建测试套件。启用 FetchContent 以获取 GoogleTest。 |
| `QWRT_BUILD_EXAMPLES` | OFF | 构建 `examples/` 中的示例程序。 |
| `QWRT_BUILD_DEBUGGER` | OFF | DAP 步进调试器。为 QuickJS-ng 打补丁以添加断点/步进原语，并将 `src/debugger.c` + `src/debugger_dap.c` 编译进 `libqwrt.a`。关闭时零开销（不应用补丁，不编译源文件）。运行时通过 `QWRT_DEBUG=1` 启用。详见[调试](../dev/debugging.md)。 |

## 常见配置

### 开发环境（完整调试，全部功能）

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug \
      -DQWRT_BUILD_TESTS=ON -DQWRT_WITH_TLS=ON \
      -DQWRT_WITH_COMPRESS=ON -DQWRT_WITH_CRYPTO_EXT=ON \
      -DQWRT_WITH_TEXTCODEC=ON -DQWRT_WITH_WAMR=ON
```

### 最小构建（嵌入式，无网络）

```bash
cmake -B build -DCMAKE_BUILD_TYPE=MinSizeRel \
      -DQWRT_WITH_TLS=OFF -DQWRT_WITH_COMPRESS=OFF \
      -DQWRT_WITH_CRYPTO_EXT=OFF -DQWRT_WITH_TEXTCODEC=OFF \
      -DQWRT_WITH_WAMR=OFF -DQWRT_PAL_UV=OFF
```

### 发布构建（生产环境，全部功能）

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release \
      -DQWRT_WITH_TLS=ON -DQWRT_WITH_COMPRESS=ON \
      -DQWRT_WITH_CRYPTO_EXT=ON -DQWRT_WITH_TEXTCODEC=ON \
      -DQWRT_WITH_WAMR=ON
```

## 编译器标志

qwrt 及其所有依赖项在 `-std=c99 -Wall -Wextra -Werror` 下编译（通过 `qwrt_enable_warnings` 强制启用）。quickjs-ng 和 libuv 自带 C11 原子操作，但 qwrt 为其打补丁以使用 GCC/Clang 的 `__atomic_*` 内建函数（`deps/*-c99-atomics.patch`），因此无需 C11。

### 消除未使用参数警告

QuickJS 回调具有固定签名，可能包含未使用的参数。使用 `QWRT_UNUSED(x)`：

```c
static JSValue my_callback(JSContext *ctx, JSValue this_val,
                           int argc, JSValue *argv) {
    QWRT_UNUSED(this_val);  // 消除 -Wunused-parameter
    // ...
}
```

## 输出

| 文件 | 描述 |
|------|-------------|
| `build/lib/libqwrt.a` | 核心运行时库 |
| `build/lib/libqwrt_uv.a` | libuv PAL 后端 |
| `build/lib/libqwrt_mock.a` | Mock PAL 后端 |
| `build/lib/libqwrt_freertos.a` | FreeRTOS PAL 后端 |
| `build/test/test_*` | 测试二进制文件（当 `QWRT_BUILD_TESTS=ON` 时） |
---
slug: stack
title: Tech stack
role: tech-stack choices
updated: "2026-08-20T00:10:57"
---

# Tech stack

## Technology choices

| domain | candidates | decision | rationale |
|---|---|---|---|
| JS engine | QuickJS-ng, V8, JSC, Hermes | QuickJS-ng | 可嵌入、ES2020 支持、低内存、C 友好，可静态链接 |
| 事件循环 | libuv, 自研, io_uring 直驱 | libuv | 跨平台、成熟、与 mbedTLS/uvhttp 生态一致；内核 io_uring workaround（PVE 6.17）已记录 |
| WebAssembly | WAMR, wasm3, wasmtime | WAMR-2.4.5 默认 (Fast JIT)，wasm3 可选 | 嵌入式优先；wasm3 零依赖备选 |
| HTTP 服务器 | uvhttp, libuv http-parser, 自研 | uvhttp (v2.6.x, submodule) | 已集成 HTTPS/WS/static/gzip，与 libuv 同构；gzip LRU 缓存模块化 |
| 加密 | mbedTLS, OpenSSL, BoringSSL | mbedTLS (v3.6.x) | 嵌入式友好、MIT 许可、TLS + crypto.subtle 共用 |
| 压缩 | miniz, zlib, zstd | miniz (v3.1.2) | 单文件、无外部依赖、确定性输出（gtest 已验证） |
| 测试 | googletest + mock_libuv, WPT, cpputest | googletest + mock_libuv 离线 gtest；WPT runner 已移除 | 确定性离线测试；WinterTC 覆盖转向 gtest |
| 构建 | CMake, Meson | CMake (>=3.10) | 生态成熟、submodule 集成、ESP-IDF 兼容 |

## Decision mindmap

```mermaid
graph LR
  D[qwrt runtime] --> E[QuickJS-ng]
  D --> L[libuv]
  D --> X[WAMR/wasm3]
  D --> H[uvhttp]
  D --> C[mbedTLS]
  D --> M[miniz]
  D --> T[gtest + mock_libuv]
```

## Open items

- 标准合规深化（v0.3.0）：EventSource / PerformanceObserver 等缺失 API
- HTTPServer /hello 吞吐（qwrt 集成 ~19k vs uvhttp 原生 25k+）：QuickJS 请求处理路径优化
- WASM Fast JIT 内存占用基线复核

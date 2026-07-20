---
title: 概述
description: Qwrt.js 是一个严格 C99 的可嵌入 QuickJS-ng 运行时封装 — 带平台抽象层的 WinterTC 兼容 JS 运行时。
---

# 概述

qwrt 是一个用**严格 C99** 编写的**可嵌入 QuickJS-ng 运行时封装**。它在 QuickJS-ng 引擎之上提供了精简的 C API、**WinterTC 兼容的运行时**以及**平台抽象层（PAL）**，使得相同的 JavaScript 代码可以在 Linux、macOS 和 ESP32-S3 上运行。

## qwrt 为你提供什么

- **ECMAScript 引擎（ES2020）** — 底层基于 QuickJS-ng，启动快，内存占用低
- **WinterTC 兼容运行时** — `fetch`、`console`、`crypto.subtle`、`ReadableStream`、定时器、`fs`、`URL`、`TextEncoder` 等
- **平台抽象层** — 约 30 个函数指针；内置三个后端，可添加自己的后端
- **多上下文** — 在一个运行时内创建/挂起/恢复隔离的 JS 上下文
- **原生扩展** — 压缩（miniz）、加密（mbedTLS）、文本编解码、WebAssembly（WAMR，可选 wasm3）
- **零系统依赖** — 所有依赖通过 CMake 从源码构建
- **单线程** — 无锁、无原子操作；JSContext 绑定到线程

## 何时使用 qwrt

| 使用场景 | 为什么选择 qwrt |
|----------|----------------|
| **物联网 / MCU 脚本** | C99，体积小，ESP32-S3 的 FreeRTOS PAL |
| **插件系统** | 多上下文隔离，每上下文 PAL 权限 |
| **边缘计算** | WinterTC API 让 JS 开发者感到熟悉 |
| **测试与模拟** | `pal_mock` 用于确定性测试，无需网络 |
| **带 JS 配置的 CLI 工具** | 嵌入 JS 引擎，无需引入 Node.js |

## 何时不应使用 qwrt

- 你需要 **Node.js/npm 生态** — qwrt 没有包管理器
- 你需要 **DOM** — qwrt 是服务端/运行时，不是浏览器
- 你需要**多线程 JS** — qwrt 设计上就是单线程
- 你需要 **JIT 性能** — QuickJS 是解释器，不是 JIT 编译器

## 项目结构

```
qwrt/
├── include/qwrt/       # 公共头文件 (qwrt.h)
├── src/                 # 核心运行时 (qwrt.c, bridge.c, context.c, ...)
├── platform/            # PAL 实现
│   ├── uv/              #   pal_uv (libuv, Linux/macOS)
│   ├── mock/            #   pal_mock (测试)
│   ├── freertos/        #   pal_freertos (ESP32-S3)
│   └── pal_common.c     #   共享 PAL 辅助函数
├── polyfill/src/        # WinterTC 模块源码
├── test/                # 测试套件 (C + gtest)
├── deps/                # Git 子模块 (quickjs-ng, libuv, mbedtls, ...)
└── docs/                # 本文档
```

## 下一步

- [快速开始](/zh/guide/quickstart) — 克隆、构建、运行你的第一个脚本
- [PAL 概述](/zh/pal/) — 了解平台抽象层
- [JS API 参考](/zh/js-api/) — 可用的 WinterTC API
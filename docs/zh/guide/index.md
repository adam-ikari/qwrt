---
title: 概述
description: Qwrt.js 是一个严格 C99 的可嵌入 QuickJS-ng 运行时封装 — 自带内部线程和 libuv 事件循环的 WinterTC 兼容 JS 运行时。
---

# 概述

qwrt 是一个用**严格 C99** 编写的**可嵌入 QuickJS-ng 运行时封装**。它在 QuickJS-ng 引擎之上提供了精简的 C API 和 **WinterTC 兼容的运行时**。qwrt 拥有自己的内部线程和 libuv 事件循环，并通过 JSON 消息与宿主通信。

## qwrt 为你提供什么

- **ECMAScript 引擎（ES2020）** — 底层基于 QuickJS-ng，启动快，内存占用低
- **WinterTC 兼容运行时** — `fetch`、`console`、`crypto.subtle`、`ReadableStream`、定时器、`fs`、`URL`、`TextEncoder` 等
- **自带线程 + 事件循环** — qwrt 启动一个内部线程运行 libuv 循环；宿主从不泵动它
- **基于消息的宿主边界** — `qwrt_post_message`（入）/ `message_cb`（出），双向 JSON
- **原生扩展** — 压缩（miniz）、加密（mbedTLS）、文本编解码、WebAssembly（WAMR，可选 wasm3）
- **零系统依赖** — 所有依赖通过 CMake 从源码构建；libuv 从 deps 子模块构建
- **单线程运行时** — 无锁、无原子操作；所有 JS 在 qwrt 的内部线程上运行

## 何时使用 qwrt

| 使用场景 | 为什么选择 qwrt |
|----------|----------------|
| **嵌入式 / 边缘脚本** | C99，体积小，内置 libuv 事件循环 |
| **插件系统** | 按运行时隔离，多上下文在运行时内部处理 |
| **边缘计算** | WinterTC API 让 JS 开发者感到熟悉 |
| **测试与模拟** | `mock_libuv` 用于确定性测试，无需网络 |
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
├── src/                 # 核心运行时
│   ├── qwrt.c           #   核心 API (create/destroy/post_message)
│   ├── thread.c         #   内部线程 + libuv 循环
│   ├── uv_io.c          #   libuv I/O (网络, fs, 定时器)
│   ├── msgq.c           #   消息队列 (宿主 ⇄ 运行时)
│   ├── worker.c         #   消息分发 (onmessage/postMessage)
│   ├── bridge.c         #   JS ↔ 运行时桥接
│   └── context.c        #   多上下文
├── polyfill/src/        # WinterTC 模块源码
├── test/                # 测试套件 (C + gtest + mock_libuv)
├── deps/                # Git 子模块 (quickjs-ng, libuv, mbedtls, ...)
└── docs/                # 本文档
```

## 下一步

- [快速开始](/zh/guide/quickstart) — 克隆、构建、运行你的第一个脚本
- [事件循环](/zh/guide/event-loop) — 内部线程和 libuv 循环如何工作
- [JS API 参考](/zh/js-api/) — 可用的 WinterTC API

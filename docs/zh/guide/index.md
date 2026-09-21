---
title: 概述
description: Qwrt.js 是一个严格 C99 的可嵌入 QuickJS-ng 运行时封装 —— 自带内部线程和 libuv 事件循环的 WinterTC 兼容 JS 运行时。为把 JavaScript 嵌入自己 C 应用的宿主开发者设计。
---

# 概述

qwrt 是一个用**严格 C99** 编写的**可嵌入 QuickJS-ng 运行时封装**。它在
QuickJS-ng 引擎之上提供精简的 C API 和 **WinterTC 兼容的运行时**，自带内部
线程和 libuv 事件循环，通过 JSON 消息与宿主通信。

C 应用想把一部分逻辑放进 JavaScript 的话，qwrt 提供运行时；宿主不用自己搭事件循环和线程。

```
   你的 C 进程
        │  qwrt_create(cfg)          — 启动 qwrt 自有线程 + libuv 循环
        ▼
   [ qwrt runtime ] ─── 内部线程，所有 JS 在此运行
        ▲
        │  message_cb(json)          — 出站 JS→宿主
        │  qwrt_post_message(json)   — 入站 宿主→JS，线程安全
```

- **自有线程 + 事件循环** — qwrt 启动一个内部线程运行 libuv 循环；宿主从不泵动它
- **基于消息的宿主边界** — `qwrt_post_message`（入）/ `message_cb`（出），双向 JSON
- **单线程运行时** — 无锁、无原子操作；所有 JS 在 qwrt 的内部线程上运行
- **ECMAScript 引擎（ES2023）** — 底层基于 QuickJS-ng，启动快，内存占用低
- **WinterTC 兼容运行时** — `fetch`、`console`、`crypto.subtle`、`ReadableStream`、定时器、`fs`、`URL`、`TextEncoder`、WebSocket 等
- **原生扩展** — 压缩（miniz）、加密（mbedTLS）、文本编解码、WebAssembly（WAMR，可选 wasm3）
- **零系统依赖** — 所有依赖通过 CMake 从源码构建；libuv 从 deps 子模块构建

## 宿主集成路径

Guide 按宿主开发者的工作顺序组织：

1. **[快速开始](/zh/guide/quickstart)** — 构建 qwrt 并运行最小的 C 嵌入
2. **[主机集成](/zh/guide/host-integration)** — 完整闭环：create → 消息通信 → 出借能力 → destroy
3. **[运行时生命周期](/zh/guide/lifecycle)** — 线程所有权、就绪、优雅关闭
4. **[多上下文](/zh/guide/multi-context)** — 单个运行时内多个隔离上下文
5. **[扩展](/zh/guide/extensions)** — 把你自己的 C 函数注册为 JS 全局
6. **[字节码](/zh/guide/bytecode)** — 把 JS 预编译为 QuickJS 字节码（启动更快、不携带源码）

## 何时使用 qwrt

| 使用场景 | 为什么选择 qwrt |
|----------|----------------|
| **嵌入式 / 边缘脚本** | C99，体积小，内置 libuv 事件循环 |
| **插件系统** | 按运行时隔离，多上下文在运行时内部处理 |
| **需要脚本化的宿主应用** | 在 JS 里脚本化你 C 应用行为，无需交付 Node.js |
| **边缘计算** | WinterTC API 让 JS 开发者感到熟悉 |
| **测试与模拟** | `mock_libuv` 用于确定性测试，无需网络 |

## 何时不应使用 qwrt

- 你需要 **Node.js 模块系统** —— qwrt 没有 `require`/`import`（Node 内置模块不可用）。很多纯 JS npm 包能用（用 `python3 test/compat_check.py <pkg>` 测（见[兼容包](/zh/guide/compatible-packages#如何验证)））；依赖 Node 专属模块的不行。
- 你需要 **DOM** —— qwrt 提供 WinterTC/W3C 子集（fetch、WebSocket、streams、localStorage 等），但没有 `document`/`window`。
- 你需要 **共享内存并发** —— 主运行时单线程；Web Worker 是真并行线程或进程，但通过 structured-clone 消息通信，不共享内存。
- 你需要 **JIT 性能** —— QuickJS 是解释器，不是 JIT 编译器。

## 项目结构

```
qwrt/
├── include/qwrt/       # 公开头（qwrt.h）
├── src/                 # 核心运行时
│   ├── qwrt.c           #   Core API (create/destroy/post_message)
│   ├── thread.c         #   内部线程 + libuv 循环
│   ├── uv_io.c          #   libuv I/O（网络、fs、定时器）
│   ├── msgq.c           #   消息队列（宿主 ⇄ 运行时）
│   ├── worker.c         #   消息分发（onmessage/postMessage）
│   ├── bridge.c         #   JS ↔ 运行时桥接
│   └── context.c        #   多上下文
├── polyfill/src/        # WinterTC 模块源
├── test/                # 测试套件（C + gtest + mock_libuv）
├── deps/                # Git 子模块（quickjs-ng、libuv、mbedTLS……）
└── docs/                # 本文档
```

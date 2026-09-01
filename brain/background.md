---
slug: background
title: Project background
role: project background
updated: "2026-09-01T09:15:17"
---

# Project background

## Why

提供可嵌入的轻量级 JavaScript 运行时：C 宿主应用需要运行 JS（配置/脚本/插件），
但不想引入 V8/JSC 的体量，也不需要 Node.js 生态依赖。qwrt 以 libuv 驱动事件循环，
自带内部线程，宿主只通过线程安全的 C API 收发 JSON 消息，无需宿主侧参与事件泵。

## Goals

- 可嵌入：单一 qwrt_create 初始化，内部线程 + libuv 循环，宿主零轮询
- WinterTC 兼容：覆盖 Web 平台通用标准 API（fetch、streams、crypto、Worker、URL 等）
- 轻量：QuickJS-ng 引擎，低启动时间、低内存占用，严格 C99
- 确定性测试：mock_libuv 离线 gtest 全覆盖，CI 门禁（test262 + e2e）
- 原生扩展：压缩（miniz）、加密（mbedTLS）、WebAssembly（WAMR/wasm3）；HTTP 服务为纯 JS serve()（raw TCP + mbedTLS）

## Non-goals

- 不做 Node.js 兼容（无 npm 模块系统、无 Node API）；npm 包仅做运行时兼容验证
- 不做浏览器 DOM（无 DOM/HTML 解析）
- 不追求 V8 级别的 JIT 性能（目标场景是嵌入式/宿主脚本）

## Target user

- 需要把 JS 作为可脚本化插件层嵌入 C/C++ 应用的开发者（游戏、IoT、配置驱动、边缘）
- 需要轻量 Web Worker + MessagePort 并行模型的宿主
- 评估 WinterTC/WinterCG 运行时兼容性的嵌入式平台团队

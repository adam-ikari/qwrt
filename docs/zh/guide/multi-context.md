---
title: 多上下文与 Web Worker
description: Amoib.js 中的并行执行 — new Worker(url)、隔离上下文，以及 ISOLATED 与 THREAD 进程模型。
---

# 多上下文与 Web Worker

amoib 运行多个独立的 JS 执行上下文。**面向宿主**的并行方式是标准的
`new Worker(url)` Web API。在内部，每个 worker 拥有独立的 `JSContext`
（在默认进程模型下，还有独立进程）。

## Web Worker

用标准 API 从任意脚本创建 worker：

```js
// 主脚本
const w = new Worker("worker.js");   // file:// 脚本路径
w.onmessage = (e) => console.log("from worker:", e.data);
w.postMessage({ cmd: "start" });

// worker.js
globalThis.onmessage = (e) => postMessage("echo: " + e.data.cmd);
```

Worker 只通过 `postMessage`/`onmessage` 通信 — 它们与其创建者不共享全局、
DOM 或 `JSRuntime`。这是暴露给 JS 的唯一多上下文接口。

## 进程模型

`-DAM_PROCESS_MODEL` 构建选项控制 worker 如何运行：

| 模型 | Worker 执行 |
|-------|------------------|
| `THREAD` | 同一进程内的并行线程 |
| `ISOLATED`（默认） | 独立子进程（`amoib-rt`，经 fork+exec 派生） |

`ISOLATED`（自 M-P2 里程碑起为默认）给每个 worker 一个独立进程，拥有
独立地址空间与事件循环。`THREAD` 是单进程回退。两者对 JS 呈现相同的
`new Worker` API。

## 隔离上下文（内部）

在 C 层，amoib 维护一组隔离上下文（`am_ctx_t`），同一时刻只有一个活动
上下文。上下文拥有独立的全局、PAL 与扩展状态，可软挂起/恢复到磁盘。这套
机制是**内部的** — 没有公开的宿主 API 来派生/挂起/恢复上下文。它用于支撑
`new Worker` 与扩展生命周期，并通过内部上下文辅助
（`am_get_active_ctx`、`am_get_active_jsctx`、`am_get_ctx_by_id`，
位于 `src/context.c`）暴露给编译进 amoib 的 C 扩展。

扩展的 `init`/`destroy`/`suspend`/`resume` 钩子会在对应的上下文生命周期
节点触发；见[扩展](/zh/guide/extensions)。

---
title: Worker
description: Qzjs.js 的 Web Worker API —— Worker 类、postMessage、terminate、worker 侧全局与消息传递。
---

# Worker API

W3C 风格的 `Worker` 类，由真实的 qzjs 运行时线程支撑。每个 worker 是各自的
`qz_t`，拥有自己的线程、事件循环与 JS 运行时（执行模型 A）。

## 全局

| Global | 类型 | 说明 |
|--------|------|------|
| `Worker` | class | 派生一个运行脚本文件的 worker 线程。 |

## new Worker(url)

同步加载脚本并派生 worker 线程。构造器阻塞直到 worker 就绪，然后返回一个
以 worker id 为键的实例。

```js
let w = new Worker('file:///app/tasks.js');
w.onmessage = (ev) => console.log('result:', ev.data);
w.postMessage({ op: 'sum', values: [1, 2, 3] });
```

**v1 限制：** 只接受 `file://` URL——脚本经宿主文件系统读取，因此当前版本
worker 仅限本地。

脚本加载失败或线程派生失败时抛 `Error`。

## Worker 实例

| 成员 | 类型 | 说明 |
|--------|------|------|
| `postMessage(value)` | `function` | 向 worker 发消息。值被 [structured-clone](/zh/js-api/structured-clone) 成字节并异步投递。 |
| `terminate()` | `function` | 停止 worker。停止其事件循环并在父销毁时 join 线程。 |
| `onmessage` | `callback` | 触发 `MessageEvent`，其 `data` 由 worker 的回复反序列化而来。 |
| `onmessageerror` | `callback` | worker 消息反序列化失败时触发 `MessageEvent('messageerror')`。 |
| `addEventListener(type, cb, options?)` / `removeEventListener(type, cb)` | `function` | Worker 实例上 `message`、`error`、`messageerror` 的标准事件注册。 |
| `onerror` | `callback` | worker 脚本顶层抛出时触发。`event.data` 是 `{ type: 'error', error: <message> }`。 |

## Worker 侧全局

worker 脚本在 worker 的运行时中运行，可访问：

- `self` / `globalThis`、`postMessage`、`onmessage`、`close()`
- 全部 WinterTC 全局（`fetch`、`crypto.subtle`、`ReadableStream`、timers 等）
- `importScripts` / worker 引导

worker 侧 `postMessage` 的 `data` 同样经 structured clone 传递。

## 消息传递

主线程与 worker 之间以 structured clone 传递值（见
[structured-clone](/zh/js-api/structured-clone)）。支持 transferable 对象以
避免复制大缓冲区。`MessageChannel` / `MessagePort` 也可跨 worker 传递
（见 [message-channel](/zh/js-api/message-channel)）。

## 错误处理

- worker 脚本顶层抛错 → 主线程 `onerror`，`event.data` 携带消息。
- 反序列化失败 → `onmessageerror`。
- worker 崩溃/终止 → 相关的 `error` 事件（若已注册）。

## 说明

- worker 是真正的并行线程（各自 `qz_t`），不是主线程上模拟的协程。
- 后端可选 `THREAD`（进程内线程）或 `PROCESS`（fork+exec 独立进程）；构建时
  或运行时配置决定，语义等价。

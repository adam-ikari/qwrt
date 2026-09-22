---
title: EventSource
description: Qzjs.js 的 EventSource API —— 带自动重连的 Server-Sent Events（SSE）客户端。
---

# EventSource

Server-Sent Events 客户端。向 URL 打开一条长连接，接收 `text/event-stream`
流，在 `EventSource` 上派发具名事件；断线后带着最后的事件 ID 自动重连。

## 全局

| Global | 类型 |
|--------|------|
| `EventSource` | `class` |

## API

```js
const src = new EventSource('https://example.com/events');
src.onmessage = (ev) => console.log('default', ev.data);
src.addEventListener('update', (ev) => console.log('update', ev.data));
src.onerror = () => console.log('reconnecting…');
// src.close();   // 停止接收
```

| 成员 | 说明 |
|--------|------|
| `new EventSource(url, init?)` | 连接；`init.withCredentials` 控制凭据 |
| `onmessage` | 默认通道事件处理 |
| `addEventListener(type, fn)` | 命名事件订阅（从 `event:` 行解析） |
| `close()` | 停止接收并终止重连 |
| `readyState` | `0` 连接中、`1` 已打开、`2` 已关闭 |

## 说明

- 若服务器给出 `retry:` 字段则按其重连，否则用默认延迟。
- `Last-Event-ID` 头携带最后收到的 `id:`，让服务器可续传流。

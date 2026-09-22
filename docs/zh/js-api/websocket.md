---
title: WebSocket
description: qzjs 的 WebSocket API —— 全局 `WebSocket` 客户端与 serve() WebSocket 路由，RFC 6455 over raw TCP。
---

# WebSocket

qzjs 提供 WebSocket 的客户端和服务端：

- **客户端**：全局 `WebSocket` 类（RFC 6455 客户端，跑在裸 TCP 上）
- **服务端**：在 `serve()` 监听器上注册的 WebSocket 路由（见
  [serve → WebSocket 路由](/zh/js-api/serve#websocket-routes)）

## 全局

| Global | 类型 |
|--------|------|
| `WebSocket` | `class` |

## 客户端

```js
const ws = new WebSocket('ws://127.0.0.1:9000/echo');
ws.onopen = () => ws.send('hello');
ws.onmessage = (ev) => console.log('echo:', ev.data);
ws.onclose = (ev) => console.log('closed', ev.code, ev.reason);
ws.onerror = () => console.log('error');
```

客户端在原始 TCP 上连接，不依赖 Node.js `net`/`http`。
帧遵循 RFC 6455（客户端帧加掩码，服务端帧不加）。

## 服务端

WebSocket 端点在 `serve()` 的 `ws` 选项中声明，与 HTTP 同端口：

```js
serve({
  port: 9000,
  ws: {
    '/echo': (ws) => {
      ws.onmessage = (e) => ws.send('echo:' + e.data);
    },
  },
}, () => 'not-ws');
```

每个路由收到一个已连接的 `ws` 对象，路由为其注册 `onmessage` / `onclose`
处理。完整路由表形态（含子协议协商）见
[serve → WebSocket 路由](/zh/js-api/serve#websocket-routes)。

## 说明

- 升级与 HTTP 同 TCP 端口——没有独立 ws 端口。
- `close(code, reason)` 执行关闭握手（默认 code 1000）；服务端回显 code/reason。

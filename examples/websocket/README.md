# websocket — WebSocket echo 服务器

演示 `serve()` 的 `ws` 路由：`/echo` 升级为 WebSocket，`onmessage` 回显。
WebSocket 协议在纯 JS 应用层实现——qzjs 只提供监听 + 升级 + 收发帧原语。

## 运行

```bash
./build/qzjs examples/websocket/websocket.js
```

服务器监听 `ws://127.0.0.1:19000/echo`。另开终端用任意客户端连：

```bash
# 需要 ws 包（npm i ws）
node -e '
const W = require("ws");
const w = new W("ws://127.0.0.1:19000/echo");
w.on("open", () => w.send("hello"));
w.on("message", d => { console.log("echo:", d.toString()); process.exit(0); });
'
```

## 期望输出

服务器：

```
WebSocket echo server @ ws://127.0.0.1:19000/echo
client connected /echo
  收到: hello
client disconnected
```

客户端：`echo: echo: hello`

## 要点

- `serve({port, ws: {'/echo': handler}}, httpHandler)`：`ws` 路由按路径匹配，
  命中即升级；未命中 ws 的 HTTP 请求落到第二个参数 `httpHandler`。
- `conn.onopen` / `onmessage` / `onclose` / `onerror` 四事件。
- text 帧 `ev.data` 是字符串，binary 帧是 `Uint8Array`——示例按类型分支。
- 端口硬编码在文件顶部 `const PORT = 19000`。
- 协议能力不止 echo：分片、子协议协商、permessage-deflate 均已实现。

## 相关文档

- [JS API: WebSocket](/js-api/websocket)
- [JS API: serve](/js-api/serve) — HTTP / WS / gRPC 服务端

---
title: serve — HTTP 服务器
description: qzjs 的 HTTP 服务器 API —— 全局 `serve()`，含 WebSocket 路由、TLS 与 gRPC 服务器。
---

# serve — HTTP 服务器

纯 JS 的 HTTP/1.1 服务器，暴露为全局 `serve()`。请求解析、路由、WebSocket
升级、响应序列化等协议语义都在 JavaScript 中实现。

## 全局

| Global | 类型 | 说明 |
|--------|------|------|
| `serve` | `function` | 启动 HTTP 服务器。同一时刻只能有一个活跃。 |

## serve(options, handler)

启动一个监听服务器并返回服务器句柄。每个 HTTP 请求调用一次 `handler`；其返回值
（或 resolve 的 Promise 值）作为响应发送。

```js
let server = serve({ port: 8080 }, (req) => {
  if (req.pathname === '/hello') return 'Hello, world!';
  return { status: 404, headers: { 'Content-Type': 'text/plain' }, _body: 'Not found' };
});
```

### 选项

| 选项 | 默认 | 说明 |
|--------|---------|------|
| `port` | `8080` | 监听 TCP 端口（0–65535）。 |
| `hostname` | `'127.0.0.1'` | 绑定地址。默认仅回环——需接受其他主机连接时显式传 `'0.0.0.0'`。 |
| `idleTimeout` | `30000` | 连接空闲多少 ms 后关闭；`0` 禁用。 |
| `ws` | `{}` | WebSocket 升级路由表，以请求路径为 key。 |
| `tls` | `undefined` | `{ cert, key }` PEM 字符串启用 HTTPS（需 `QZ_WITH_TLS`）。 |

### 请求对象

`handler` 收到一个描述请求的普通对象：

| 字段 | 类型 | 说明 |
|-------|------|------|
| `method` | `string` | HTTP 方法，如 `'GET'`。 |
| `url` | `string` | 含查询的原始路径，如 `'/a?x=1'`。 |
| `pathname` | `string` | 不含查询的路径，如 `'/a'`。 |
| `search` | `string` | 含 `?` 的查询串，或 `''`。 |
| `headers` | `object` | 小写头名 → 值。 |
| `body` | `ReadableStream` \| `null` | `Content-Length > 0` 时为请求体，否则 `null`。 |
| `keepAlive` | `boolean` | 本次响应后连接是否保持。 |

请求体从原始 socket 字节直接喂入 `ReadableStream`，二进制请求体不会失真。

### 响应值

`handler` 可以返回（或 resolve 为）：

- **字符串** —— 以 `200 OK` 发送，`Content-Type: text/plain; charset=utf-8`。
- **对象** —— 含 `status`、`statusText`、`headers`（`Headers` 实例或普通对象）
  与 `_body`（字符串、`ArrayBuffer` 或 `Uint8Array`）。二进制 body 按原始字节发送。
- `null`/`undefined` —— `500 Internal Server Error`（handler 抛异常时同样如此）。

```js
serve({ port: 8080 }, (req) => {
  if (req.method !== 'POST') return { status: 405, _body: 'POST only' };
  let data = new Uint8Array([0xDE, 0xAD, 0xBE, 0xEF]);
  return {
    status: 201,
    headers: { 'Content-Type': 'application/octet-stream' },
    _body: data
  };
});
```

### 服务器句柄

`serve()` 返回 `{ closed, close() }`：

```js
let server = serve({ port: 8080 }, handler);
server.close();        // 停止监听，释放端口
```

同一时刻只能运行一个服务器。已有服务器活动时再调 `serve()` 会抛
`serve: a server is already running (call srv.close() first)`。

## WebSocket 路由

`options.ws` 把路径映射到升级处理器。每个处理器收到一个含 `onopen`、`onmessage`、
`onclose`、`onerror`、`send()`、`close()` 的连接对象。

```js
serve({
  port: 8080,
  ws: {
    '/chat': (conn) => {
      conn.onmessage = (ev) => {
        conn.send('echo: ' + ev.data);       // 文本帧以字符串到达
      };
      conn.onclose = () => console.log('disconnected');
    }
  }
}, (req) => 'HTTP fallback');
```

路由值也可以是一个含 `handler` 与可选 `protocols` 数组的对象，用于子协议协商：

```js
ws: {
  '/chat': {
    handler: (conn) => { conn.onmessage = (ev) => conn.send('pong'); },
    protocols: ['chat.v1']        // 若客户端提供则通过 Sec-WebSocket-Protocol 回显
  }
}
```

连接对象：

| 成员 | 类型 | 说明 |
|--------|------|------|
| `send(data)` | `function` | 发送文本（字符串）或二进制（Uint8Array）。 |
| `close(code, reason)` | `function` | 发送关闭帧并标记连接关闭。 |
| `onopen` | `callback` | socket 就绪时触发。 |
| `onmessage` | `callback` | 收到 `{ data }`——文本帧为字符串、二进制帧为 Uint8Array。 |
| `onclose` | `callback` | 收到 `{ code, reason, wasClean }`。 |
| `onerror` | `callback` | 连接错误。 |

当客户端提供且可用原生流式 deflate 原语时（见 [compress](/zh/js-api/compress)），
`permessage-deflate`（RFC 7692）压缩自动协商。

WebSocket 握手在构建时需 `QZ_WITH_TEXTCODEC=ON` 与 `QZ_WITH_CRYPTO_EXT=ON`
（经 `crypto.subtle` 计算 SHA-1 accept key）；否则升级抛 `WebSocket accept unavailable`。

无匹配 `ws` 路由的请求得 `404`；缺 `Sec-WebSocket-Key` 的升级得 `400`。

## TLS

传 `tls: { cert, key }` 在监听器上启用 HTTPS。构建时需 `QZ_WITH_TLS=ON`
（见 [构建选项](/zh/guide/build-options)）。

```js
serve({
  port: 8443,
  tls: { cert: pemCert, key: pemKey }
}, (req) => 'secure!');
```

gRPC 栈可用（`QZ_WITH_GRPC=ON`）且未显式设置 `tls.alpn` 时，服务器会自动注入 `alpn: ['h2', 'http/1.1']`。

## gRPC 服务器

传 `grpc: server` 在同一监听器上注册 [gRPC](/zh/js-api/grpc) 服务。gRPC 栈是
纯 JS 的 h2/HPACK/protobuf 实现；
与 HTTP/1.1 处理器同一 TCP 端口——监听器按 ALPN（TLS 连接为 `h2`）或
`PRI * HTTP/2.0` 连接前导（明文 h2c）分发。

```js
const server = grpc.createServer();
server.addService(reg, { Echo: (call) => ({ text: call.request.text }) });
serve({ port: 50051, grpc: server }, () => 'not-grpc');
```

构建时需 `QZ_WITH_GRPC=ON`（gRPC bundle 为可选，因为它向 polyfill 增加约
3.5k 行；见 [构建选项](/zh/guide/build-options)）。

完整 unary/流式走查见
[grpc-hello 示例](https://github.com/adam-ikari/qzjs/tree/master/examples/grpc-hello)
与 [gRPC API 参考](/zh/js-api/grpc)。

## 说明

- 支持 HTTP/1.1 keep-alive；客户端请求 `close` 或使用 HTTP/1.0 时响应后关闭连接。

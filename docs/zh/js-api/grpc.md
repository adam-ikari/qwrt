---
title: gRPC
description: Qzjs.js 的 gRPC API —— 纯 JS 的 HTTP/2 + HPACK + protobuf 客户端与服务端，暴露为全局 `grpc`（需 QZ_WITH_GRPC=ON）。
---

# gRPC

纯 JS 的 gRPC 栈，包含 HTTP/2 帧层、HPACK、protobuf 编解码，支持四种 RPC
形态（unary / 服务端流 / 客户端流 / 双向流）。构建时开启 `QZ_WITH_GRPC=ON`
后会挂上全局 `grpc`（这个 bundle 可选，会给 polyfill 增加约 3.5k 行 JS）。

## 全局

| Global | 类型 | 说明 |
|--------|------|------|
| `grpc` | `object` | gRPC 命名空间 |

## 加载 proto

```js
const PROTO = `
syntax = "proto3";
package bench;
service B { rpc Echo (R) returns (R); }
message R { string text = 1; }`;
const reg = grpc.loadProto(PROTO);
```

## 客户端

```js
const Echo = reg.service('bench.B').method('Echo');
const ch = grpc.createInsecureChannel('127.0.0.1:50051');
const reply = await ch.invoke(Echo, { text: 'hi' });
// reply === { text: 'echo: hi' }
```

| 方法 | 形态 |
|--------|-------|
| `ch.invoke(method, req)` | unary |
| `ch.invokeStream(method, req)` | 服务端流式（返回响应数组） |
| `ch.invokeClientStream(method, reqs[])` | 客户端流式（发数组，收一个响应） |
| `ch.invokeBidi(method, reqs[])` | 双向流式（发数组，收数组） |

## 服务端

```js
const server = grpc.createServer();
server.addService(reg, {
  Echo: (call) => ({ text: call.request.text }),
  // 服务端流用 async generator，客户端/双向流基于数组
});
serve({ port: 50051, grpc: server }, () => 'not-grpc');
```

`addService(reg, impls)` 接收完整的 `loadProto()` 结果（不是 service
对象）+ 以方法名为 key 的 `impls` 对象：

| 形态 | handler 签名 |
|-------|--------------|
| unary | `(call) => reply` |
| 服务端流 | `async function* (call) { yield reply; }` |
| 客户端流 | `(call) => reply`（`call.request` 是完整请求数组） |
| 双向流 | `(call) => replies[]`（`call.request` 是完整请求数组） |

## 示例

见 [`examples/grpc-hello`](https://github.com/adam-ikari/qzjs/tree/master/examples/grpc-hello)
——四种形态可运行的演示（unary + 流式，qzjs 客户端 → qzjs 服务端）。

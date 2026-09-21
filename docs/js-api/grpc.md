---
title: gRPC
description: The gRPC API in Qwrt.js — pure-JS HTTP/2 + HPACK + protobuf client and server, exposed as the global `grpc` object (requires QWRT_WITH_GRPC=ON).
---

# gRPC

Pure-JS gRPC stack: HTTP/2 frame layer, HPACK, protobuf codec, and the four
RPC method shapes (unary / server-stream / client-stream / bidi). Exposed as
the global `grpc` when `QWRT_WITH_GRPC=ON` (the bundle is opt-in; ~3.5k lines
of JS added to the polyfill).

## Global

| Global | Type | Description |
|--------|------|-------------|
| `grpc` | `object` | gRPC namespace |

## Loading a proto

Parse `.proto` text into a method registry:

```js
const PROTO = `
syntax = "proto3";
package bench;
service B { rpc Echo (R) returns (R); }
message R { string text = 1; }`;
const reg = grpc.loadProto(PROTO);
```

## Client

```js
const Echo = reg.service('bench.B').method('Echo');
const ch = grpc.createInsecureChannel('127.0.0.1:50051');
const reply = await ch.invoke(Echo, { text: 'hi' });
// reply === { text: 'echo: hi' }
```

| Method | Shape |
|--------|-------|
| `ch.invoke(method, req)` | unary |
| `ch.invokeStream(method, req)` | server-streaming (returns array of replies) |
| `ch.invokeClientStream(method, reqs[])` | client-streaming (send array, get one reply) |
| `ch.invokeBidi(method, reqs[])` | bidirectional (send array, get array) |

## Server

```js
const server = grpc.createServer();
server.addService(reg, {
  Echo: (call) => ({ text: call.request.text }),
  // async generator for server-streaming, array-based for client/bidi
});
serve({ port: 50051, grpc: server }, () => 'not-grpc');
```

`addService(reg, impls)` takes the full `loadProto()` result (not the
service object) plus an `impls` object keyed by method name. Each handler's
shape matches the RPC contract:

| Shape | Handler signature |
|-------|-------------------|
| unary | `(call) => reply` |
| server-stream | `async function* (call) { yield reply; }` |
| client-stream | `(call) => reply` (`call.request` is the full requests array) |
| bidi | `(call) => replies[]` (`call.request` is the full requests array) |

## Example

See [`examples/grpc-hello`](https://github.com/adam-ikari/qwrt/tree/master/examples/grpc-hello)
for a runnable four-shape demo (unary + streaming, qwrt client → qwrt server).

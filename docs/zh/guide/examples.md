---
title: 示例
description: 可运行示例——演示如何使用 qzjs：host↔JS 消息、WebAssembly、Web Crypto、HTTP/WebSocket 服务器、worker 与 C 扩展。
---

# 示例

所有示例都在 [`examples/`](https://github.com/adam-ikari/qzjs/tree/master/examples)，
构建时做编译校验（`-DQZ_BUILD_EXAMPLES=ON`）。C 示例由 CMake 构建；JS 示例经
`qzjs` CLI 运行。

## 构建

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DQZ_BUILD_EXAMPLES=ON
cmake --build build --parallel
```

## C 示例（嵌入）

| 示例 | 演示内容 | 运行 |
|---------|---------------|-----|
| [`hello`](https://github.com/adam-ikari/qzjs/tree/master/examples/hello) | 最简嵌入：创建运行时、求值 JS、host↔JS 消息往返 | `./build/examples/hello/qz_hello` |
| [`messages`](https://github.com/adam-ikari/qzjs/tree/master/examples/messages) | JSON 请求/响应状态机（`echo`/`add`/`date`） | `./build/examples/messages/qz_messages` |
| [`worker`](https://github.com/adam-ikari/qzjs/tree/master/examples/worker) | `new Worker(url)` 并行执行 | `./build/examples/worker/qz_worker` |

C 示例会派生 `qzjs-rt`（ISOLATED 进程模型）；构建时自动注入运行时路径，开箱即用。

## JS 示例（CLI）

| 示例 | 演示内容 | 运行 |
|---------|---------------|-----|
| [`wasm`](https://github.com/adam-ikari/qzjs/tree/master/examples/wasm) | 执行 WebAssembly 模块（`WebAssembly.instantiate`） | `./build/qzjs examples/wasm/wasm.js` |
| [`crypto`](https://github.com/adam-ikari/qzjs/tree/master/examples/crypto) | WebCrypto：SHA-256 + AES-GCM 加解密 | `./build/qzjs examples/crypto/crypto.js` |
| [`httpserver`](https://github.com/adam-ikari/qzjs/tree/master/examples/httpserver) | 纯 JS `serve()` HTTP 服务器 + 静态文件 | `./build/qzjs examples/httpserver/server.js` |
| [`websocket`](https://github.com/adam-ikari/qzjs/tree/master/examples/websocket) | 经 `serve()` ws 路由的 WebSocket echo 服务器 | `./build/qzjs examples/websocket/websocket.js` |
| [`timers`](https://github.com/adam-ikari/qzjs/tree/master/examples/timers) | `setTimeout`/`setInterval` + `Promise.all` 异步 | `./build/qzjs examples/timers/timers.js` |
| [`fs`](https://github.com/adam-ikari/qzjs/tree/master/examples/fs) | `qzjs.fs` 读/写/列目录/删除 | `./build/qzjs examples/fs/fs.js` |
| [`broadcast`](https://github.com/adam-ikari/qzjs/tree/master/examples/broadcast) | `BroadcastChannel` 跨实例消息 | `./build/qzjs examples/broadcast/broadcast.js` |
| [`stream-pipeline`](https://github.com/adam-ikari/qzjs/tree/master/examples/stream-pipeline) | `ReadableStream`/`TransformStream` 流水线 | `./build/qzjs examples/stream-pipeline/pipeline.js` |
| [`fetch-proxy`](https://github.com/adam-ikari/qzjs/tree/master/examples/fetch-proxy) | 出站 `fetch` 代理 | `./build/qzjs examples/fetch-proxy/main.js` |
| [`grpc-hello`](https://github.com/adam-ikari/qzjs/tree/master/examples/grpc-hello) | gRPC unary 服务器（`-DQZ_WITH_GRPC=ON`） | `./build/qzjs examples/grpc-hello/grpc-hello.js` |
| [`worker-orchestrate`](https://github.com/adam-ikari/qzjs/tree/master/examples/worker-orchestrate) | 编排多个 worker | `./build/qzjs examples/worker-orchestrate/orchestrate.js` |

## C 扩展

| 示例 | 演示内容 | 运行 |
|---------|---------------|-----|
| [`extension`](https://github.com/adam-ikari/qzjs/tree/master/examples/extension) | 把 C 扩展编译进 qzjs，向 JS 暴露原生全局 | 见 [`extension/README.md`](https://github.com/adam-ikari/qzjs/tree/master/examples/extension) |

扩展必须编译进 qzjs 库（`-DQZ_EXTRA_SOURCES` + `-DQZ_EXTENSIONS` +
`-DQZ_EXTRA_HEADERS`）；示例文档给出了精确构建命令。

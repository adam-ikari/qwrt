---
id: httpserver-ws-protocol
title: "HTTPServer WS 服务端协议增强（分片重组 + 子协议协商）"
category: decision
status: active
tags: [http-server, websocket, protocol]
created: "2026-08-26T09:29:58"
updated: "2026-08-26T09:30:07"
---

<!-- compiled_truth -->
polyfill/src/http-server.js（纯 JS 层，uvhttp C 层之上的 WS 协议增强）：1) 消息分片重组——收到 FIN=0 数据帧（0x1/0x2）时暂存 opcode+payload 于 WSConnection._fragOpcode/_fragParts，后续 Continuation 帧（0x0）累积，FIN=1 时拼接为完整消息交付 onmessage；combineBytes 合并 Uint8Array。2) 子协议协商——ws 路由除函数外支持对象形式 {handler, protocols}，握手时按客户端 Sec-WebSocket-Protocol 顺序回显第一个双方支持的协议（无匹配则不带头）。


## Timeline

- time: 2026-08-26T09:29:58
  kind: decision
  summary: "Created this page: HTTPServer WS 服务端协议增强（分片重组 + 子协议协商）"
  source: created via brain create-page
  affects: [httpserver-ws-protocol]

- time: 2026-08-26T09:30:07
  kind: decision
  summary: Rewrote compiled_truth to the new best understanding
  source: session continue
  affects: [httpserver-ws-protocol]

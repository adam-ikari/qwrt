---
id: httpserver-streaming-body
title: "HTTPServer 请求体流式（D2）"
category: decision
status: active
tags: [http-server, streaming, serve]
created: "2026-08-26T10:10:19"
updated: "2026-08-26T10:10:47"
---

<!-- compiled_truth -->
D2 请求体流式（破坏性 API 变更）：serve() 的 req.body 从同步字符串改为 ReadableStream（Web 标准语义），新增 req.text()/req.arrayBuffer() 异步读取。header 一到即调 handler，body 字节经 ondata 增量 enqueue 交付。核心改动：1) 连接缓冲从字符串改为字节（Uint8Array raw），parseRequest 改为 header-only（接收不含尾部 CRLFCRLF 的 header 字符串），字节级 indexOfHdrEnd 定位；2) body 数据绝不经过字符串往返（旧实现 decode→encode 破坏二进制 body，如 bytes(range(256)) 膨胀 2 倍）——ondata 收到的是 ArrayBuffer（tcp_io.c JS_Call 传 ab），需 new Uint8Array(data) 转换；3) 支持 header+body 同包（创建 stream 后立即喂 raw 中已缓冲 body 字节）与分块流式（ondata 顶部 bodyState 分支直接 feed 纯 body 字节，超出部分归下个请求）。兼容：既有 handler 用 req.body||'' 的两处测试改为 await req.text()。验证 e2e 15/15（含 test_streaming_body：分块大 body + 二进制 arrayBuffer）。


## Timeline

- time: 2026-08-26T10:10:19
  kind: decision
  summary: "Created this page: HTTPServer 请求体流式（D2）"
  source: created via brain create-page
  affects: [httpserver-streaming-body]

- time: 2026-08-26T10:10:47
  kind: decision
  summary: Rewrote compiled_truth to the new best understanding
  source: D2 implementation
  affects: [httpserver-streaming-body]

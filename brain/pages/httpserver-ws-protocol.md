---
id: httpserver-ws-protocol
title: "HTTPServer WS 服务端协议增强（分片重组 + 子协议协商）"
category: decision
status: active
tags: [http-server, websocket, protocol]
created: "2026-08-26T09:29:58"
updated: "2026-08-26T12:27:17"
---

<!-- compiled_truth -->
polyfill/src/http-server.js（纯 JS 层 WS 协议）：
1) 消息分片重组——FIN=0 数据帧（0x1/0x2）暂存 opcode+payload 于 _fragOpcode/_fragParts，Continuation 帧（0x0）累积，FIN=1 拼接交付。
2) 子协议协商——ws 路由支持对象 {handler, protocols}，回显首个双方支持的 Sec-WebSocket-Protocol。
3) permessage-deflate（RFC 7692）——协商：客户端 Sec-WebSocket-Extensions 含 permessage-deflate 且 pal 有流式 deflate 原语时回显；收发：RSV1 位标志压缩帧，发送端 deflatePush(data,true)（SYNC_FLUSH）后去尾 4 字节（00 00 ff ff，实际 miniz 尾部为 00 00 00 ff ff 5 字节，去 4 留 1 字节 00），接收端 inflatePush(payload)+追加 PMD_TAIL(00 00 ff ff)；上下文 takeover：deflate/inflate 上下文跨消息保留（WSConnection._deflate/_inflate），连接关闭时 deflateFree/inflateFree。
C 层（ext_compress.c）：新增 pal.deflateCreate/Push/Free 与 inflateCreate/Push/Free——封装 miniz mz_stream 的流式上下文（GC finalizer 兜底释放；onclose 显式 free）。关键陷阱：mz_deflate(MZ_SYNC_FLUSH) 无输入时每次调用都输出空 stored block（00 00 00 ff ff）导致死循环，故 avail_in==0 即终止；mz_inflate 输入耗尽返回 MZ_BUF_ERROR 是"需要更多输入"的正常信号非错误。
其他：e2e 16/16，ASan 0 泄漏；Ping/Pong 主动保活不做（应用层策略）。顺带修复 2 个既有泄漏：cli.c run_code 的 json_escape 返回值未 free；qwrt_free 未释放 config.initial_script（strdup）。


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

- time: 2026-08-26T12:27:17
  kind: decision
  summary: Rewrote compiled_truth to the new best understanding
  source: PMD implementation
  affects: [httpserver-ws-protocol]

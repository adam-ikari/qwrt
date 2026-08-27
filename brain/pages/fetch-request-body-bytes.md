---
id: fetch-request-body-bytes
title: "fetch 请求体字节化"
category: decision
status: active
tags: [fetch, wintertc, http, polyfill]
created: "2026-08-27T05:01:15"
updated: "2026-08-27T05:01:21"
---

<!-- compiled_truth -->
- **背景**：`fetch(url, {body})` 请求体此前在 JS 层被 `String()` 强转，二进制（Uint8Array/ArrayBuffer）与流式 body（ReadableStream）语义丢失；C 桥接层 `http_request_stream` 只接受 C string。
- **决策**（2026-08-27）：`serializeBody` 统一把 body 序列化为 `Uint8Array` —— string / Uint8Array / ArrayBuffer 同步返回，ReadableStream 异步 pump 收集全部 chunk（每 chunk 强制转字节，非 Uint8Array 走 TextEncoder）；`whenBodyReady` 等流读取完成后才发请求，读取失败 reject（TypeError），abort 后丢弃结果不再发请求。
- **C 层**：`js_pal_http_request_stream` 用 `JS_GetUint8Array` / `JS_GetArrayBuffer` 直读字节，失败再回落 C string；`uv_io_http_request_stream` 同步 `memcpy` 拷贝 body，无 JS buffer 悬垂（UAF）风险。
- **测试**：新增 4 个 gtest（PostStringBody / PostUint8ArrayBody / PostReadableStreamBody / PostStreamErrorRejects），ctest offline 13/13 全绿。
- **对称补充**：与[[httpserver-streaming-body]]（服务端 req.body 流式）对应，客户端请求体字节化闭环；剩余代理支持（B2）。


## Timeline

- time: 2026-08-27T05:01:15
  kind: decision
  summary: "Created this page: fetch 请求体字节化"
  source: created via brain create-page
  affects: [fetch-request-body-bytes]

- time: 2026-08-27T05:01:21
  kind: decision
  summary: "fetch 客户端请求体统一字节化：string/Uint8Array/ArrayBuffer 同步、ReadableStream 异步读取后再发请求"
  source: brain update-truth
  affects: [fetch-request-body-bytes]

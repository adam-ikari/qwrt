---
id: httpserver-ws-fixes
title: "HTTPServer WS 全链路：3 个 uvhttp 底层修复"
category: decision
status: active
tags: [http-server, websocket, uvhttp, llhttp]
created: "2026-08-16T09:52:40"
updated: "2026-08-16T10:28:30"
---

<!-- compiled_truth -->
uvhttp 在 qwrt 中共 4 处底层修复（均改 deps/uvhttp 源码）：1) HPE_PAUSED_UPGRADE 时 llhttp 暂停后未恢复、连接悬死——在 qwrt_http_router.c 分发前显式 llhttp_resume；2) WS 握手 101 响应后 uvhttp 内部仍尝试 HTTP 解析导致请求状态错乱——WS 升级连接跳过后续 HTTP 解析；3) WS 帧校验：opcode 0 应视为 continuation 而非非法、close 帧返回码处理；4) uvhttp_connection_tls_write 只调用一次 mbedtls_ssl_write，大 body（>16KB）时部分写/缓冲满（EAGAIN→WANT_WRITE）导致 HTTPS 大响应挂起——改为循环写+1ms 重试。验证：HTTPS 100KB 响应 200（修复前挂起）、gzip 压缩正常（Content-Encoding: gzip，对象路径 30000B→27B）、ASAN 0 错误。字符串响应不压缩（仅 Response 对象路径）为预期设计。


## Timeline

- time: 2026-08-16T09:52:40
  kind: decision
  summary: "Created this page: HTTPServer WS 全链路：3 个 uvhttp 底层修复"
  source: session 01a0032e
  affects: [httpserver-ws-fixes]

- time: 2026-08-16T10:28:30
  kind: decision
  summary: "第 4 个 uvhttp 修复：TLS 大响应挂起（mbedtls_ssl_write 部分写+EAGAIN 未处理），改为循环写；全套 HTTPS/static/gzip/WS 回归 + ASAN 0 错误"
  source: brain update-truth
  affects: [httpserver-ws-fixes]

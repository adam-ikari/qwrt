---
id: httpserver-ws-fixes
title: "HTTPServer WS 全链路：3 个 uvhttp 底层修复"
category: decision
status: active
tags: [http-server, websocket, uvhttp, llhttp]
created: "2026-08-16T09:52:40"
updated: "2026-08-23T11:39:23"
---

<!-- compiled_truth -->
uvhttp 在 qwrt 中的底层修复（均改 deps/uvhttp 源码）：1) HPE_PAUSED_UPGRADE 时 llhttp 暂停未恢复——分发前显式 llhttp_resume；2) WS 握手 101 后 uvhttp 仍尝试 HTTP 解析导致状态错乱——升级连接跳过；3) WS 帧校验：opcode 0=continuation、close 帧返回码；4) uvhttp_connection_tls_write 只调用一次 mbedtls_ssl_write，大 body 挂起——循环写+1ms 重试。第 5 个修复：uvhttp_ws_process_data 派发 on_close 时 close_reason 是 recv_buffer 内的非 NUL 结尾指针，按 C 字符串读取会串到陈旧字节（'bye' 变成 'byeping'）——复制到本地 126B 缓冲并补 NUL。

WS 客户端（pal.wsConnect/wsSend/wsClose）关键生命周期契约：uvhttp_ws_process_data 在调用 on_close/on_error 回调【之后】仍会继续使用 conn（close-frame echo、state=CLOSED 更新），因此客户端回调绝不能同步释放 c->ws_conn/wctx——释放推迟到 ws_read_cb 中 process_data 返回之后。c->close_sent 区分「JS 调用了 ws.close()」与「握手完成（on_close 触发）」：若二者混用，onmessage 里调 close() 会让 read_cb 在握手完成前就提前释放连接，关闭帧回显到达前 fd 已被 close。c->freed 幂等守卫 + uv_close 前 uv_read_stop 防迟到的 read 回调双重释放。验证：Release 全链路 9/9 e2e（含新 test_websocket_client）、46/46 polyfill gtest、ASAN 0 UAF；服务端 ws_ctx 双重释放（uvhttp_server_free 已释放 server->context）已修复。


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

- time: 2026-08-23T11:39:23
  kind: decision
  summary: Rewrote compiled_truth to the new best understanding
  source: ws client message exchange
  affects: [httpserver-ws-fixes]

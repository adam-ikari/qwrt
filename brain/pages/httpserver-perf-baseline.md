---
id: httpserver-perf-baseline
title: HTTPServer pure-JS performance baseline
category: decision
status: active
tags: [httpserver, perf, serve]
created: "2026-08-24T15:29:31"
updated: "2026-08-26T04:03:35"
---

<!-- compiled_truth -->
M2-D1 HTTP/1.1 协议细节完成：
- parseRequest: 提取 version、Connection头、keepAlive判断（HTTP/1.1默认keep-alive，HTTP/1.0默认close）
- keep-alive 支持：连接复用（两个请求走同一 TCP 连接）
- pipelining: buf余下部分保留给下一个请求（consumed 字节数）
- Connection: close 正确处理（HTTP/1.0 和显式 Connection: close）
- sendResponse: Connection头不再硬编码keep-alive，根据 currentKeepAlive 设置
- HTTP/1.0 兼容：默认关闭连接

验证：e2e 12/12（新增 test_keep_alive_reuse + test_connection_close_http10），ctest offline 13/13

未做（留 M2 后续）：chunked 编码、流式请求体（D2）、空闲超时（D3）


## Timeline

- time: 2026-08-24T15:29:31
  kind: decision
  summary: "Created this page: HTTPServer pure-JS performance baseline"
  source: created via brain create-page
  affects: [httpserver-perf-baseline]

- time: 2026-08-24T15:39:03
  kind: decision
  summary: "纯JS serve() 性能基线与CI回归守卫"
  source: "test/bench_httpserver.py + ci.yml e1d6e2fa"
  affects: [httpserver-perf-baseline]

- time: 2026-08-25T03:27:40
  kind: decision
  summary: "fsReadBinary + HTML文件支持（同步路径绕过uv_io_fs_read bug）"
  source: 2aa39130
  affects: [httpserver-perf-baseline]

- time: 2026-08-25T05:12:12
  kind: decision
  summary: "开发路线图 ROADMAP.md (M0-M5) 确立"
  source: 39b5980b
  affects: [httpserver-perf-baseline]

- time: 2026-08-25T05:17:32
  kind: decision
  summary: "ROADMAP.md 重写为整体项目路线图（7 领域 A-G）"
  source: 0289991b
  affects: [httpserver-perf-baseline]

- time: 2026-08-25T08:16:29
  kind: decision
  summary: "M1-A1 文件IO异步化回归完成 + 网络IO异步确认"
  source: "638810fe + 并发验证"
  affects: [httpserver-perf-baseline]

- time: 2026-08-25T08:50:05
  kind: decision
  summary: "M1 完成：gzip e2e恢复 + fs全路径测试"
  source: "4a95fc0b + ROADMAP"
  affects: [httpserver-perf-baseline]

- time: 2026-08-25T08:59:57
  kind: decision
  summary: "M1 里程碑正式收尾"
  source: "e2e 10/10 + ctest 13/13 + ROADMAP"
  affects: [httpserver-perf-baseline]

- time: 2026-08-26T04:03:35
  kind: decision
  summary: "D1 HTTP/1.1 协议细节完成"
  source: ddbd396b
  affects: [httpserver-perf-baseline]

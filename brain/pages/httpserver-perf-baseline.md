---
id: httpserver-perf-baseline
title: HTTPServer pure-JS performance baseline
category: decision
status: active
tags: [httpserver, perf, serve]
created: "2026-08-24T15:29:31"
updated: "2026-08-25T05:12:12"
---

<!-- compiled_truth -->
ROADMAP.md 已入库（M0 完成 → M5）：
- M1 当前：fs异步路径清理(exists/list/remove/write同步化)、删uv_io_fs_read死代码、恢复gzip e2e、fsWrite二进制往返
- M2: HTTP/1.1细节(chunked/keep-alive/管线化)、流式body、连接生命周期、WS增强(分片/permessage-deflate/PingPong)、TLS SNI
- M3: HTTP客户端、SSE server、中间件/代理example
- M4: CI全绿、asan/ubsan清零、覆盖率、文档
- M5: 性能(大响应C层直发、gzip缓存、fs缓存)
- 边界: 不内置路由/静态/缓存策略、不做Node全兼容、不复活uvhttp C服务器
- 每里程碑有验证矩阵(e2e/gtest/wrk)


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

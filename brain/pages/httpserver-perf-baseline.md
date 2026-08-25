---
id: httpserver-perf-baseline
title: HTTPServer pure-JS performance baseline
category: decision
status: active
tags: [httpserver, perf, serve]
created: "2026-08-24T15:29:31"
updated: "2026-08-25T08:16:29"
---

<!-- compiled_truth -->
文件 IO 异步化（M1-A1）完成：
- fsRead/fsReadBinary 改回异步 uv_io_fs_read；根因修复：uv_fs_read 直接写入 iov 目标缓冲（op->buf），删除多余 memcpy（realloc 后 UAF）
- 验证：单连接文本/二进制 roundtrip OK；5MB 文件读 + /fast 并发不阻塞；e2e 8 PASS; ctest offline 13/13

网络 IO 异步确认：
- libuv uv_read/uv_write 天然异步（listen/accept/read/write 不阻塞事件循环）
- 验证：2MB 大响应 + /fast 并发，fast 不被阻塞（0.85s 内完成）
- 边界：TLS 加密（mbedtls_ssl_write）在事件循环线程同步做 CPU AES，但不阻塞等待网络；严格异步化（移线程池）列入 M2

教训：compress gtest 曾有 2 FAILED，干净重建后 13/13 全过——为构建状态不一致假象，非代码回归。


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

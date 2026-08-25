---
id: httpserver-perf-baseline
title: HTTPServer pure-JS performance baseline
category: decision
status: active
tags: [httpserver, perf, serve]
created: "2026-08-24T15:29:31"
updated: "2026-08-25T08:59:57"
---

<!-- compiled_truth -->
M1 里程碑正式收尾：
- 异步文件 I/O 回归 ✅（fsRead/fsReadBinary 异步 uv_io_fs_read，UAF 修复）
- 网络 I/O 异步确认 ✅（libuv 天然异步：2MB 大响应+快请求并发不阻塞）
- gzip e2e 恢复 ✅（应用层 CompressionStream，serve 不自动压缩）
- fs 全路径测试 ✅（write/exists/read/readdir/unlink 异步往返）
- e2e 10/10 PASS（0 SKIP）
- ctest offline 13/13（100%）
- F1 本地验证完成；CI 全绿（ASan/UBSan）待 push 后确认

M2（HTTP/1.1 细节 / WS 增强 / 流式 body）可开始。


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

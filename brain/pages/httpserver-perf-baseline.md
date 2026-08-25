---
id: httpserver-perf-baseline
title: HTTPServer pure-JS performance baseline
category: decision
status: active
tags: [httpserver, perf, serve]
created: "2026-08-24T15:29:31"
updated: "2026-08-25T08:50:05"
---

<!-- compiled_truth -->
M1（稳定性夯实）完成：
- A1 异步文件IO回归 ✅（638810fe）
- B1 gzip e2e恢复 ✅：改为应用层（handler 用 CompressionStream 压缩 + Content-Encoding: gzip；serve 不自动压缩——符合架构边界）
- fs 全路径测试 ✅：test_fs_ops（writeFile/exists/readFile/readdir/unlink 异步往返）
- e2e 10/10 PASS（0 SKIP）；ctest offline 13/13

重要洞察：test_compress_gtest 曾误判为代码回归（2 FAILED），根因是：
1) 另一 worktree(phase4-httpserver-perf) 残留 test_compress_gtest 进程占 CPU
2) 系统 load >10 导致 host_poll 5s 预算耗尽
清理残留进程后 18/18 稳定通过——非代码问题。


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

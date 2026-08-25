---
id: httpserver-perf-baseline
title: HTTPServer pure-JS performance baseline
category: decision
status: active
tags: [httpserver, perf, serve]
created: "2026-08-24T15:29:31"
updated: "2026-08-25T05:17:32"
---

<!-- compiled_truth -->
ROADMAP.md 已从仅 http server 方向扩展为 qwrt 整体项目路线图，覆盖：
A 运行时核心（异步 IO 回归为 M1 重点）
B WinterTC 标准合规（21 模块 + gzip e2e 恢复）
C 原生扩展（WASM AOT/流式、TLS SNI、压缩缓存）
D 服务端能力（HTTP/1.1 细节、WS 增强、大响应性能）
E 工具链（CLI、REPL）
F 质量与性能（CI 全绿、覆盖率、安全审计）
G 生态与文档（API 参考、examples 扩充、打包）

架构原则新增：异步优先（所有 I/O 必须异步，阻塞式 fopen 被否决）。
M1（1 周）：异步文件 I/O 回归 + gzip 恢复 + fs 全路径测试。


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

---
id: httpserver-perf-benchmark
title: "HTTPServer 性能基准（uvhttp 驱动）"
category: decision
status: active
created: "2026-08-16T13:00:56"
updated: "2026-08-19T05:37:06"
---

<!-- compiled_truth -->
### Phase 4 优化（2026-08-19，分支 phase4-httpserver-perf）

**uvhttp gzip LRU 缓存（uvhttp 78c28f3）**：uvhttp_response.c 按 body 内容 xxhash64+len 做 64 条 LRU 缓存，相同 body 只压缩一次。wrk -t4 -c100 -d10s（Release）：/gzip 4811→10809 rps（+2.25x），/hello ~19k（波动），/big 100KB 3447（不变，不压缩）。正确性：Content-Encoding gzip、gunzip 正常。

**Task 4.3 大响应不改**：body 减半（100KB→50KB）rps 3378→5716（1.69x 接近线性）→ 瓶颈是单线程 body 串行传输，非 memcpy；memcpy 100KB 仅占 ~5-7% 请求周期，双段 iov 重构动 vendored 核心发送路径风险高、收益 ~7%，放弃。


## Timeline

- time: 2026-08-16T13:00:56
  kind: decision
  summary: "Created this page: HTTPServer 性能基准（uvhttp 驱动）"
  source: created via brain create-page
  affects: [httpserver-perf-benchmark]

- time: 2026-08-16T14:04:23
  kind: decision
  summary: Rewrote compiled_truth to the new best understanding
  source: brain update-truth
  affects: [httpserver-perf-benchmark]

- time: 2026-08-17T01:05:02
  kind: decision
  summary: "TLS WS 关闭清理与大帧修复（PR #335）"
  affects: [httpserver-perf-benchmark]

- time: 2026-08-19T05:37:06
  kind: decision
  summary: Rewrote compiled_truth to the new best understanding
  source: brain update-truth
  affects: [httpserver-perf-benchmark]

---
id: httpserver-perf-baseline
title: HTTPServer pure-JS performance baseline
category: decision
status: active
tags: [httpserver, perf, serve]
created: "2026-08-24T15:29:31"
updated: "2026-08-24T15:39:03"
---

<!-- compiled_truth -->
纯JS serve() 性能基线 (wrk, 2026-08-24 容器, -c64 -t2, 5s):
- tiny(8B): 12601 rps / 5.07ms
- small(1KB): 3141 rps / 20.25ms
- medium(16KB): 256 rps / 238.27ms ← JS序列化+tcpWrite是瓶颈
- post(echo): 10530 rps / 13.24ms

对比 uvhttp C实现旧数据: 100KB响应3378 rps。纯JS在16KB已掉到256 rps——大响应场景JS层开销显著，但serve()定位是WinterCG合规便利API非性能服务。

TLS: pal.tcpListen第5参数{cert,key}走mbedTLS服务器端握手 (6e9c36b3)，test_https已恢复。

CI回归守卫: .github/workflows/ci.yml httpserver-perf job, 阈值=基线50%。


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

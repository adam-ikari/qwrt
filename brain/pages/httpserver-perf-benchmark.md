---
id: httpserver-perf-benchmark
title: "HTTPServer 性能基准（uvhttp 驱动）"
category: decision
status: active
created: "2026-08-16T13:00:56"
updated: "2026-08-20T23:40:53"
---

<!-- compiled_truth -->
### Phase 4 优化（2026-08-19，分支 phase4-httpserver-perf）

**uvhttp gzip LRU 缓存（uvhttp 78c28f3）**：uvhttp_response.c 按 body 内容 xxhash64+len 做 64 条 LRU 缓存，相同 body 只压缩一次。wrk -t4 -c100 -d10s（Release）：/gzip 4811→10809 rps（+2.25x），/hello ~19k（波动），/big 100KB 3447（不变，不压缩）。正确性：Content-Encoding gzip、gunzip 正常。

### 方向B HTTPServer 性能优化（2026-08-20）

**缓存引用**：handler 和 Request 构造器在 serve() 时 root 到 g_state（JS_DupValue），每请求跳过 JS_GetGlobalObject + JS_GetPropertyStr 两次查找。

**Headers 直写（最大瓶颈）**：build_request_object 直接 new Headers() 实例并写 _map（Map.set，无正则），new Request 走 instanceof-Headers 分支直接复制 _map。C 侧对 header name 做 ASCII tolower 保持大小写不敏感可置。wrk -t4 -c100 -d8s（Release）：

| 路径 | 优化前 rps | 优化后 rps | 提升 |
|---|---|---|---|
| /hello (string) | 19098 | **26208** | **+37%** |
| /gzip (Accept-Encoding: gzip, 10KB body) | 10809 | **16143** | **+49%** |
| /big (100KB) | 3447 | **3910** | **+13%** |

gzip 正确性：Content-Encoding gzip、magic 1f8b、gunzip 正常。错误率 0。e2e 8/8 全过。

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

- time: 2026-08-19T08:55:34
  kind: decision
  summary: "uvhttp gzip 缓存模块化重构：删除 response.c 静态全局（g_gzip_cache/g_gzip_cache_tick），改为 uvhttp_gzip_cache 模块（LRU+内存预算+TTL+同key替换），挂载到 uvhttp_server_t（server_new 创建 / server_free 释放），response 借用指针（结构体布局不变）。符合 uvhttp 全局变量替换哲学。uvhttp 97/97、qwrt e2e 8/8、offline ctest 12/12 全过；gzip 路径无回归。"
  affects: [httpserver-perf-benchmark]

- time: 2026-08-20T23:40:53
  kind: decision
  summary: "方向B: Headers 直写 + 引用缓存，/hello +37% /gzip +49% /big +13%"
  source: session 2026-08-20 direction-b
  affects: [httpserver-perf-benchmark]

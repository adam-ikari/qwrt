---
id: httpserver-perf-benchmark
title: "HTTPServer 性能基准（uvhttp 驱动）"
category: decision
status: active
created: "2026-08-16T13:00:56"
updated: "2026-08-17T01:05:02"
---

<!-- compiled_truth -->
## 实测数据（wrk -t4 -c100 -d10s，qwrt Release 2026-08-16）

### plain HTTP（:18800）rps / p50 / p99
| 路径 | rps | p50 | p99 |
|---|---|---|---|
| /hello (string) | 32.0k | 3.01ms | 4.25ms |
| /json | 23.7k | 4.13ms | 4.67ms |
| /async (5ms 延迟) | 15.7k | 6.47ms | 7.82ms |
| /gzip (10KB) | 14.6k | 6.73ms | 8.03ms |
| /big (100KB) | 2.6k | 37.65ms | 119.77ms |
| static (index.html) | 50.4k | 1.96ms | 2.18ms |

### HTTPS（:18801 /hello）
27.5k rps，p50 3.41ms，p99 6.96ms（TLS 开销小——keep-alive 复用）

### WebSocket 并发 echo（50 连接 × 50 消息）
- plain（:18800）：20.5k msg/s 零错误
- TLS（:18802）：7.0k msg/s 零错误（mbedtls 加密开销）

### 对比基准（uvhttp benchmark_unified，8/11）
- / 路径：26.8k rps（c10）→ 24.3k（c500）→ 24.7k（c1000），5min 24.5k 零错误
- /compression/text（gzip）：2.57k rps（压缩为最大瓶颈）

### 瓶颈结论
1. 单线程事件循环：大响应（100KB）吞吐受限（memcpy + 单连接串行），CPU 单核饱和
2. 压缩：gzip 最重路径（14.6k vs 32k 非压缩）——miniz 单线程
3. TLS：握手机制占比高，keep-alive 复用后 ~15% 开销
4. 静态文件：最快（sendfile/直出），50k rps 上限
5. WS：帧级 echo 受 mbedtls 写与 JS 桥接限制（plain 20k / TLS 7k msg/s）

### 发现并修复的 uvhttp bug（PR #334，5 commits）
1. TLS 密文/明文共缓冲 → HTTPS keep-alive 第二请求丢失 → tls_cipher_buf 分离
2. TLS 同步写不触发 restart_read → 手动调度
3. WS 扩展长度截断（126/127）→ payload_length 字段
4. TLS WS ssl=NULL 明文发送 → 传 conn->ssl
5. wrapper 强转崩溃：uvhttp_server_ws_send 把 ws_conn->user_data（wrapper）直接强转 uvhttp_connection_t → TLS 并发 SIGSEGV → wrapper 公开化 + 正确解包

测试：test_websocket_frame_fixes.cpp 11 用例 + 更新 2 个旧断言，uvhttp ctest 100/100；qwrt 端 TLS WS 并发 50 连接稳定。


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

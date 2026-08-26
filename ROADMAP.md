# Qwrt.js 开发路线图（整体项目）

> 定位：**Qwrt.js — 可嵌入 QuickJS 运行时**。
> libuv-native、WinterTC 兼容、多上下文 + Web Workers、宿主↔运行时消息、
> 原生扩展（TLS/crypto/compress/WASM）、独立 CLI、服务端能力（serve）。
> 状态：滚动规划，随实际进展更新。最后更新：2026-08。

## 一、现状基线（2026-08）

### 运行时核心
- QuickJS-ng 引擎（ES2023，C99 补丁构建）、libuv 内部线程 + 事件循环
- 宿主↔运行时 JSON 消息（`qwrt_post_message` / `message_cb`，线程安全）
- 多上下文（spawn / suspend / resume，软挂起到磁盘）
- Web Workers（`new Worker(url)` 真并行线程）+ `structuredClone` / transferable
- DAP 调试器（`QWRT_BUILD_DEBUGGER`，QuickJS-ng 打补丁）

### WinterTC 标准面（21 模块）
fetch / console / crypto.subtle / streams(含 BYOB) / timers / fs / storage /
encoding / url / URLPattern / abort / performance / event-target / blob(File,
FormData) / message-channel / navigator / structured-clone / error-events /
BroadcastChannel / CacheStorage / EventSource(SSE)。

### 服务端能力（应用层协议，qwrt 给原语）
- `serve()` 纯 JS HTTP/HTTPS/WS 服务器（TCP/TLS listener 在 C，协议在 JS）
- TLS（mbedTLS 服务器端握手）；WS 客户端（RFC 6455 over raw TCP）
- 文件读取 `fs.readFile` / `readFileBinary`；压缩 `CompressionStream`
- 应用层 example：路由 / LRU 缓存 + ETag / gzip（`examples/httpserver/`）

### 原生扩展
- WebAssembly：WAMR（默认，Fast Interp + AOT）/ wasm3（备选）
- mbedTLS（TLS + crypto.subtle）、miniz（compress）、textcodec（UTF-8/Base64）

### 质量
- gtest（mock_libuv 离线确定性）ctest offline 13/13 全绿；e2e 10 PASS + 0 SKIP
- CI 12 job：minimal / feature-matrix / wamr / wasm3 / asan / ubsan / release /
  coverage / debugger / test262 / clang-tidy / httpserver-perf
- 服务端性能基线（wrk，阈值=基线 50%）：tiny 12.6k / small 3.1k / medium(16K) 256 / post 10.5k

## 二、架构原则

1. **能力原语 vs 协议策略**：C 层只给监听/回复、文件、压缩、加密等原语；
   路由、缓存、压缩策略、静态映射一律在应用层（JS）实现。
2. **异步优先**：所有 I/O（文件、网络、定时器）必须异步，绝不在事件循环上
   做阻塞式 fopen/fread。这是硬约束（文件 I/O 回归异步已完成；网络 I/O 基于
   libuv 的 uv_read/uv_write 天然异步，2MB 大响应与快速请求并发验证不互相
   阻塞。TLS 加密为 mbedTLS 同步 CPU 操作（不阻塞等网络），需严格异步化列入 M2）。
3. **质量门槛**：任何提交必须 gtest + e2e 全绿；性能改动附基准；asan/ubsan 清零。
4. **可回滚**：大方向变更走独立分支，验证后合入 master。
5. **决策入脑**：取舍写入 `brain/`（Project Brain）。

## 三、路线图（按领域，M = 里程碑）

### A. 运行时核心

| # | 工作项 | 说明 | 验证 |
|---|--------|------|------|
| A1 | **异步文件 I/O 回归** ✅ | `fsRead/fsReadBinary` 已改回异步（`uv_io_fs_read`），修复根因（`uv_fs_read` 直接写入 iov 目标缓冲，删除多余 memcpy / UAF）。`fsExists/fsList/fsRemove/fsWrite` 走 `uv_io_*` 异步原语，补测试即可。 | 多连接并发读文件不阻塞事件循环；e2e fs 往返；ctest offline 13/13 |
| A2 | 多上下文 / Worker 健壮性 | 软挂起恢复边界、transferable 泄漏、worker 错误事件流全覆盖。 | gtest + 压力 |
| A3 | 引擎升级跟进 | QuickJS-ng 上游合并策略（小补丁 diff 管理）。 | test262 通过率 |
| A4 | DAP 调试器完善 | `debugger_dap.c` TODO：uv_run 轮询超时驱动；多上下文断点。 | 集成测试 |

### B. WinterTC 标准合规

| # | 工作项 | 说明 | 验证 |
|---|--------|------|------|
| B1 | **恢复 gzip e2e** ✅ | `test_gzip_compression` 已恢复（应用层 handler 用 CompressionStream 压缩 + Content-Encoding；serve 不自动压缩）。 | e2e 10/10 |
| B2 | fetch 完善 | 重定向、流式 body、上传、代理、AbortSignal 全路径。 | gtest + 集成 |
| B3 | streams 覆盖 | BYOB 已补齐；补充 backpressure、tee、pipeThrough 边界。 | gtest + WPT 样例 |
| B4 | 标准缺口盘点 | 对照 WinterCG spec 逐模块勾选；缺项入 gtest。 | 覆盖矩阵 |

### C. 原生扩展

| # | 工作项 | 说明 | 验证 |
|---|--------|------|------|
| C1 | WASM 引擎 | WAMR AOT 路径、wasm3 特性对等、流式编译（test_wasm_streaming）。 | gtest |
| C2 | TLS/crypto | SNI 多证书、证书热加载；crypto.subtle wrapKey 已做，补其余算法。 | gtest |
| C3 | 压缩 | miniz 压缩缓存（相同 body 只压一次，可移植 uvhttp 时代 LRU）。 | 基准 + 正确性 |

### D. 服务端能力

| # | 工作项 | 说明 | 验证 |
|---|--------|------|------|
| D1 | **HTTP/1.1 细节** ✅ | keep-alive 管理、Connection 头尊重、pipelining 支持、Content-Length 严格消费、HTTP/1.0 兼容（默认 close）。 | e2e 12/12；ctest 13/13 |
| D2 | **请求体流式** ✅ | `req.body` 变 ReadableStream,header 一到即调 handler,body 字节经流式 enqueue 增量交付(不经字符串往返,二进制安全);`req.text()`/`req.arrayBuffer()` 便捷读取;字节缓冲 + 字节级 header 扫描,支持 header+body 同包。 | e2e 15/15(含 test_streaming_body:分块大 body + 二进制) |
| D3 | **连接生命周期** ✅ | 空闲超时（serve idleTimeout，默认30s，0禁用）、Connection: close 排空、优雅停止（onclose 清理 conns）。 | e2e 12/12；ctest 13/13 |
| D4 | **WS server 增强** ✅ | 分片 ✅、子协议协商 ✅、permessage-deflate ✅（RFC 7692：协商、RSV1 收发、上下文 takeover；C 层流式 deflate/inflate 原语 `pal.deflate*/inflate*`）。Ping/Pong 保活不做（应用层策略）。 | e2e 16/16；ASan 0 泄漏 |
| D5 | 大响应性能 | 基线 medium(16K) 256 rps；C 层直发 / Body 复用 / 减 JS↔C 往返。 | wrk 提升 |

### E. 工具链

| # | 工作项 | 说明 | 验证 |
|---|--------|------|------|
| E1 | CLI | `globalThis.arguments/env` 已做；补 `--` 选项、脚本 args 完整语义。 | test_cli_gtest |
| E2 | REPL | 多行输入、历史、补全。 | 手动 + gtest |

### F. 质量与性能

| # | 工作项 | 说明 | 验证 |
|---|--------|------|------|
| F1 | CI 全绿 ✅(本地) | 消除 SKIP（gzip）✅；feature-matrix / ASan / UBSan 待 push 后 CI 确认。 | CI 状态 |
| F2 | 覆盖率 | fs / tls / ws / worker 路径补测。 | coverage 报告 |
| F3 | 启动/内存 | 引擎预热、上下文复用、大文件零拷贝评估。 | 基准 |
| F4 | 安全审计 | 路径穿越、消息边界、TLS 证书校验、原型链污染。 | 专项 |

### G. 生态与文档

| # | 工作项 | 说明 | 验证 |
|---|--------|------|------|
| G1 | API 参考 | serve / fs / compress / crypto / worker 文档（网站 + repo）。 | 文档可跑通 |
| G2 | examples 扩充 | httpserver 已做；补 worker 编排、流式管道、代理。 | example 可跑 |
| G3 | 打包 | libqwrt.pc、静态库目标（libqwrt/libqwrt_full）、无系统依赖构建。 | CMake 验证 |

## 四、里程碑节奏

| 里程碑 | 内容 | 节奏 |
|--------|------|------|
| M1 ✅ | **异步 I/O 回归 + gzip 恢复 + fs 全路径测试**（A1/B1/F1本地完成；CI 待 push 确认） | 1 周 |
| M2 ✅ | **HTTP/1.1 细节 + WS 增强 + 流式 body**（D1–D4 全部完成） | 2–4 周 |
| M3 | fetch 完善 + streams 覆盖 + WASM 流式（B2–B3, C1） | 1–2 月 |
| M4 | 质量与性能（F 全项）+ 大响应优化（D5） | 持续 |
| M5 | 生态与文档（G）+ 安全审计（F4） | 持续 |

## 五、验证矩阵

| 里程碑 | gtest | e2e | 额外门槛 |
|--------|-------|-----|----------|
| M1 ✅ | 13/13 | 10/10 | fs 多连接并发不阻塞（已验）；ASan 无 fs 崩溃 |
| M2 | 全绿 | HTTP/1.1+WS 全绿 | wrk 无回退 |
| M3 | 全绿 | 全绿 | test262 通过率不降 |
| M4 | 全绿 | 全绿 | asan/ubsan 清零；wrk 大响应显著提升 |
| M5 | 全绿 | 全绿 | 文档/示例可跑通 |

## 六、明确不做（边界）

- **不内置**应用层路由 / 静态服务 / 缓存策略（example 演示如何做）。
- **不做** Node.js 兼容层全量（`process` / `require` / `Buffer` 刻意缺席；按需补 WinterCG 而非 Node API）。
- **不复活** uvhttp C 服务器（已被纯 JS serve() 取代；除非性能上 JS 无法满足再评估）。
- **不引入**系统库依赖（全部 `add_subdirectory` 源码构建，严格 C99）。

## 七、参考

- `README.md` — 项目定位 / API / WinterTC 模块表 / 构建选项
- `brain/pages/` — 各领域决策（worker-transferable、wintertc-byob-streams、wasm-engine-integration、httpserver-perf-baseline 等 16 页）
- `examples/httpserver/` — 应用层完整 HTTP 服务器示例
- `test/` — gtest 套件 + e2e + mock_libuv

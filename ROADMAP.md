# qwrt 开发路线图

> 状态：滚动规划，随实际进展更新。
> 最后更新：2026-08

## 现状基线（2026-08）

- **运行时**：C 语言 + libuv 深度集成；QuickJS-ng 引擎 + WAMR/wasm3 可切换。
- **已交付能力原语**（服务端）：
  - `serve()` 纯 JS HTTP/HTTPS/WSS 服务器（监听 + 回复）
  - TLS（mbedTLS 服务器端握手）
  - 文件读取 `qwrt.fs.readFile` / `readFileBinary`（同步路径，二进制安全）
  - 压缩 `CompressionStream` / `DecompressionStream`（miniz 后端）
  - WS 客户端（RFC 6455 over raw TCP）、EventSource(SSE)、CacheStorage
- **应用层示例**：`examples/httpserver/` — 路由/静态/缓存(LRU+ETag)/压缩/安全全在 JS 层。
- **质量**：gtest 10 套件 80 绿；e2e 8 PASS + 1 SKIP（`test_gzip_compression`）；CI 12+ job（minimal/feature-matrix/wamr/wasm3/asan/ubsan/release/coverage/debugger/test262/clang-tidy/httpserver-perf）。
- **性能基线**（wrk, `httpserver-perf` CI 守卫，阈值为基线 50%）：tiny(8B) 12.6k rps、small(1K) 3.1k、medium(16K) 256、post 10.5k。

## 架构原则（贯穿所有工作）

1. **能力原语 vs 协议策略**：qwrt（C）只提供监听/回复、读文件、压缩、加密等原语；路由、缓存、压缩策略、静态映射一律在应用层（JS）实现。
2. **质量门槛**：任何提交必须 gtest + e2e 全绿；性能相关改动必须附基准数据；CI 的 asan/ubsan 必须清零。
3. **可回滚**：大方向变更走独立分支（如 `phase4-httpserver-perf`），验证后合入 master。
4. **决策入脑**：每个方向的取舍写入 `brain/`（Project Brain）。

## 里程碑与工作项

### M0 — 能力原语就绪 ✅（已完成）

- serve() 纯 JS HTTP/WS、TLS、fs 读、gzip、example httpserver。
- 出口标准：e2e 8 PASS + 1 SKIP，gtest 全绿。

### M1 — 稳定性夯实（当前，1 周内）

| # | 工作项 | 说明 | 验证 |
|---|--------|------|------|
| 1.1 | **fs 异步路径清理** | `pal.fsExists/fsList/fsRemove/fsWrite` 仍是异步 `uv_io_*`（与已修的 fsRead 同源，可能有相同 UAF/清理 bug）。方案：同步化（对齐 fsReadBinary）或修复异步路径。 | 新增 fs e2e：exists/list/remove/write 往返断言 |
| 1.2 | **删除死代码** | `uv_io_fs_read` 已无调用者（fsRead 改同步），删除或标记。 | 编译零 warning |
| 1.3 | **恢复 gzip e2e** | `CompressionStream` 已可用，`test_gzip_compression` 取消 SKIP，断言 gzip 魔数 + gunzip 往返 + `Content-Encoding`/`Vary`。 | e2e 9/9 |
| 1.4 | **fsWrite/fsExists 二进制一致性** | 确保写→读往返对二进制字节无损。 | gtest + e2e |

**出口标准**：e2e 9/9，gtest 全绿，无已知 fs 崩溃路径。

### M2 — HTTPServer 协议完善（2–4 周）

| # | 工作项 | 说明 | 验证 |
|---|--------|------|------|
| 2.1 | HTTP/1.1 细节 | chunked 编码、keep-alive 正确性、管线化、`Content-Length` 严格校验。 | e2e 新增协议用例 |
| 2.2 | 请求体流式读取 | 现在 `req.body` 一次性读入；改为流式（大 body 上传）。 | e2e 大 body 上传 |
| 2.3 | 连接生命周期 | 空闲超时、`Connection: close` 排空、服务器停止时优雅退出。 | e2e + wrk 无泄漏 |
| 2.4 | WS server 增强 | 分片帧、permessage-deflate、Ping/Pong 保活、子协议协商。 | e2e WS 扩展 |
| 2.5 | TLS 增强 | 证书热加载、SNI 多证书。 | e2e HTTPS |

**出口标准**：HTTP/1.1 核心 + WS 增强 e2e 全覆盖；wrk 基准无回退。

### M3 — 网络能力扩展（1–2 月）

| # | 工作项 | 说明 | 验证 |
|---|--------|------|------|
| 3.1 | HTTP 客户端完善 | fetch 重定向、流式 body、上传、代理。 | gtest + 集成 |
| 3.2 | SSE/EventSource server | 服务端 SSE 推送（原语层面已可，补 example）。 | e2e |
| 3.3 | 中间件/反向代理示例 | 应用层示例：日志、鉴权、代理到上游。 | example |
| 3.4 | 静态大文件零拷贝 | 若性能需要，C 层提供零拷贝发送原语（`sendfile`）。 | wrk 基准 |

### M4 — 平台与质量（持续）

| # | 工作项 | 说明 | 验证 |
|---|--------|------|------|
| 4.1 | CI 全绿 | 消除 SKIP（gzip 恢复）；feature-matrix 覆盖新能力。 | CI 状态 |
| 4.2 | ASan/UBSan 清零 | 重点跑 fs/ws/tls 路径。 | CI asan/ubsan job |
| 4.3 | 覆盖率提升 | fs、tls、ws 路径补测。 | coverage 报告 |
| 4.4 | 文档 | serve/fs/compress API 参考、example 扩充。 | 文档可跑通 |

### M5 — 性能优化（按需）

| # | 工作项 | 说明 | 验证 |
|---|--------|------|------|
| 5.1 | 大响应性能 | 基线 medium(16K) 仅 256 rps；瓶颈 JS 序列化 + tcpWrite。方向：C 层直发、Body 复用、减少 JS↔C 往返。 | wrk 基准提升 |
| 5.2 | 压缩缓存 | 相同 body 只压缩一次（uvhttp 时代有 LRU，可移植到 JS 层 example/内置）。 | 基准 + 正确性 |
| 5.3 | fs 缓存 | 应用层 LRU 已有；评估 C 层页缓存必要性。 | 基准 |

## 验证矩阵（每里程碑出口）

| 里程碑 | e2e | gtest | 额外 |
|--------|-----|-------|------|
| M0 | 8 PASS + 1 SKIP | 80 | — |
| M1 | 9/9 | 全绿 | fs 无崩溃路径 |
| M2 | HTTP/1.1+WS 全绿 | 全绿 | wrk 无回退 |
| M3 | 全绿 | 全绿 | example 可跑 |
| M4 | 全绿 | 全绿 | asan/ubsan 清零 |
| M5 | 全绿 | 全绿 | wrk 显著提升 |

## 明确不做（边界）

- **不内置**应用层路由/静态服务/缓存策略（例子里已演示如何做）。
- **不引入** Node.js 兼容层全量（`process.env` 等按需补，非对齐 Node）。
- **不恢复** uvhttp C 服务器（已被纯 JS serve() 取代，除非性能上 JS 无法满足再评估）。

## 参考

- `examples/httpserver/` — 应用层完整 HTTP 服务器示例
- `brain/pages/httpserver-perf-baseline.md` — 纯 JS serve() 性能基线
- `test/test_httpserver_e2e.py` — 端到端测试

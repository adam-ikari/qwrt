# Brain Index

_Auto-generated. Last updated 2026-08-28T14:07:54.985Z._

- [console-output-routing](pages/console-output-routing.md) — category: decision | tags: [console, cli, behavior] | ## compiled_truth
- [crypto-subtle-gtest](pages/crypto-subtle-gtest.md) — category: decision | tags: [crypto, gtest, webcrypto] | ## 决策
- [crypto-subtle-wrapkey](pages/crypto-subtle-wrapkey.md) — category: decision | tags: [crypto, webcrypto, wrapkey] | - **背景**：crypto.subtle 此前缺 `wrapKey`/`unwrapKey`（WebCrypto 标准方法）。
- [examples-tree](pages/examples-tree.md) — category: decision | tags: [build, examples] | - 示例程序放在根目录 examples/ 下，每个示例一个子目录（examples/hello, examples/worker），不再放根目录 example.c。
- [f4-security-audit](pages/f4-security-audit.md) — category: decision | tags: [security, audit] | ## F4 安全审计结论
- [fetch-request-body-bytes](pages/fetch-request-body-bytes.md) — category: decision | tags: [fetch, wintertc, http, polyfill] | - **背景**：`fetch(url, {body})` 请求体此前在 JS 层被 `String()` 强转，二进制（Uint8Array/ArrayBuffer）与流式 body（ReadableStream）语义丢失；C 桥接层 `http_request_stream`
- [httpserver-perf-baseline](pages/httpserver-perf-baseline.md) — category: decision | tags: [httpserver, perf, serve] | M2-D3 连接生命周期完成：
- [httpserver-perf-benchmark](pages/httpserver-perf-benchmark.md) — category: decision | ### Phase 4 优化（2026-08-19，分支 phase4-httpserver-perf）
- [httpserver-streaming-body](pages/httpserver-streaming-body.md) — category: decision | tags: [http-server, streaming, serve] | D2 请求体流式（破坏性 API 变更）：serve() 的 req.body 从同步字符串改为 ReadableStream（Web 标准语义），新增 req.text()/req.arrayBuffer() 异步读取。
- [httpserver-ws-fixes](pages/httpserver-ws-fixes.md) — category: decision | tags: [http-server, websocket, uvhttp, llhttp] | uvhttp 在 qwrt 中的底层修复（均改 deps/uvhttp 源码）：1) HPE_PAUSED_UPGRADE 时 llhttp 暂停未恢复——分发前显式 llhttp_resume；2) WS 握手 101 后 uvhttp 仍尝试 HTTP 解析导致状态错乱——升
- [httpserver-ws-protocol](pages/httpserver-ws-protocol.md) — category: decision | tags: [http-server, websocket, protocol] | polyfill/src/http-server.js（纯 JS 层 WS 协议）：
- [libuv-io-uring-workaround](pages/libuv-io-uring-workaround.md) — category: decision | tags: [libuv, io-uring, linux, workaround] | deps/libuv/src/unix/linux.c 有一处本地补丁（未提交到上游）：让 `UV_USE_IO_URING=0` 真正禁用 io_uring。
- [startup-memory-benchmark](pages/startup-memory-benchmark.md) — category: decision | tags: [f3, benchmark, memory] | ### F3 启动/内存基准（2026-08-27，commit 1ff03860）
- [streams-b3-semantics](pages/streams-b3-semantics.md) — category: decision | tags: [streams, wintertc, ecma-429] | - **背景**：ROADMAP B3（streams 覆盖）对照 WHATWG Streams 语义审计 polyfill/src/streams.js，发现 pipeTo/tee/pipeThrough/releaseLock 四处真实缺口。
- [test262-ctest-fix](pages/test262-ctest-fix.md) — category: decision | tags: [test262, ctest, cmake] | ## 现象
- [urlpattern-modifier-fix](pages/urlpattern-modifier-fix.md) — category: decision | tags: [urlpattern, wintertc, ecma429, polyfill] | ## 问题
- [wasm-engine-integration](pages/wasm-engine-integration.md) — category: decision | tags: [wasm, wamr, threading] | # WAMR 线程环境（关键坑）
- [wintertc-byob-streams](pages/wintertc-byob-streams.md) — category: decision | tags: [wintertc, streams, byob, ecma-429] | - **背景**：ECMA-429（WinterTC Minimum common web API，2025 snapshot）要求 Streams 的三个 BYOB 接口必须暴露在 globalThis 上：`ReadableByteStreamController`、`Rea
- [wintertc-crypto-performance-globals](pages/wintertc-crypto-performance-globals.md) — category: decision | tags: [wintertc, ecma-429, crypto, performance] | - **背景**：ECMA-429（WinterTC Minimum common web API）WEBCRYPTO 要求 globalThis 暴露 `Crypto`/`CryptoKey`/`SubtleCrypto`/`crypto`，HR-TIME 要求 `Perfor
- [wintertc-ecma429-coverage](pages/wintertc-ecma429-coverage.md) — category: decision | tags: [wintertc, ecma-429, coverage, gtest, streams] | ## 结论
- [worker-error-events](pages/worker-error-events.md) — category: decision | tags: [worker, error-events, w3c] | ## compiled_truth
- [worker-transferable](pages/worker-transferable.md) — category: decision | tags: [worker, transferable, arraybuffer, structured-clone, wintertc] | - **MessagePort 跨线程 transfer（已实现，2026-08-18）**：父线程与 worker 线程是独立 JSRuntime，不能共享 JS 对象。
- [wpt-runner-removed](pages/wpt-runner-removed.md) — category: decision | tags: [wpt, wintertc, testing, libuv-native] | ## 决策

# Brain Index

_Auto-generated. Last updated 2026-08-19T01:54:08.403Z._

- [console-output-routing](pages/console-output-routing.md) — category: decision | tags: [console, cli, behavior] | ## compiled_truth
- [crypto-subtle-gtest](pages/crypto-subtle-gtest.md) — category: decision | tags: [crypto, gtest, webcrypto] | ## 决策
- [examples-tree](pages/examples-tree.md) — category: decision | tags: [build, examples] | - 示例程序放在根目录 examples/ 下，每个示例一个子目录（examples/hello, examples/worker），不再放根目录 example.c。
- [httpserver-perf-benchmark](pages/httpserver-perf-benchmark.md) — category: decision | ## 实测数据（wrk -t4 -c100 -d10s，qwrt Release 2026-08-16）
- [httpserver-ws-fixes](pages/httpserver-ws-fixes.md) — category: decision | tags: [http-server, websocket, uvhttp, llhttp] | uvhttp 在 qwrt 中共 4 处底层修复（均改 deps/uvhttp 源码）：1) HPE_PAUSED_UPGRADE 时 llhttp 暂停后未恢复、连接悬死——在 qwrt_http_router.c 分发前显式 llhttp_resume；2) WS 握手 10
- [libuv-io-uring-workaround](pages/libuv-io-uring-workaround.md) — category: decision | tags: [libuv, io-uring, linux, workaround] | deps/libuv/src/unix/linux.c 有一处本地补丁（未提交到上游）：让 `UV_USE_IO_URING=0` 真正禁用 io_uring。
- [test262-ctest-fix](pages/test262-ctest-fix.md) — category: decision | tags: [test262, ctest, cmake] | ## 现象
- [urlpattern-modifier-fix](pages/urlpattern-modifier-fix.md) — category: decision | tags: [urlpattern, wintertc, ecma429, polyfill] | ## 问题
- [wasm-engine-integration](pages/wasm-engine-integration.md) — category: decision | tags: [wasm, wamr, threading] | # WAMR 线程环境（关键坑）
- [worker-error-events](pages/worker-error-events.md) — category: decision | tags: [worker, error-events, w3c] | ## compiled_truth
- [worker-transferable](pages/worker-transferable.md) — category: decision | tags: [worker, transferable, arraybuffer, structured-clone, wintertc] | - **MessagePort 跨线程 transfer（已实现，2026-08-18）**：父线程与 worker 线程是独立 JSRuntime，不能共享 JS 对象。
- [wpt-runner-removed](pages/wpt-runner-removed.md) — category: decision | tags: [wpt, wintertc, testing, libuv-native] | ## 决策

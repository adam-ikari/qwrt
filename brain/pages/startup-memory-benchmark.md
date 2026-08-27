---
id: startup-memory-benchmark
title: "启动/内存基准（F3）"
category: decision
status: active
tags: [f3, benchmark, memory]
created: "2026-08-27T15:38:59"
updated: "2026-08-27T15:39:15"
---

<!-- compiled_truth -->
### F3 启动/内存基准（2026-08-27，commit 1ff03860）

**测试环境**：build_asan（Debug + ASan，-fsanitize=address），Linux 6.17.13-2-pve，AMD Ryzen 7 5800H。CLI = `build_asan/qwrt`。注意：ASan/Debug 数值偏高，Release 基线待 F3 优化时重测。

**启动基线**（`qwrt -e '1+1'`，5 次）：启动耗时 min 40.5ms / median 53.2ms / max 73.4ms；子进程峰值 RSS ~24.7MB。现状：每次 CLI 进程全新 JSRuntime + polyfill 求值，**无引擎预热、无上下文复用**（F3 待优化项）。

**大文件异步读**（4×256KB `qwrt.fs.readFile` 并发，不 await 即返回，host 等 wait_idle）：含启动 128ms 中位。fs_read 走 libuv threadpool，4KB/次分块读（PAL_UV_FS_BUF_INIT=4096），读回调把字节拷入 JS string；**无零拷贝路径**（F3 待优化项）。

**内存安全基准（本次 UAF 修复 1ff03860）**：同上场景（teardown 时线程池 request 在途），修复前崩溃（`JS_FreeRuntime: Assertion 'list_empty(&rt->gc_obj_list)'`），修复后 ASan 0 错误、EXIT 0、`ASAN_OPTIONS=detect_leaks=0` 下无泄漏报告。ASan 回归触发路径覆盖：wait_idle 自动退出（active_reqs 判忙）+ qwrt_destroy 兜底排空（16 轮 UV_RUN_NOWAIT）。

**验证方式**：`ctest -L offline`（13/13，mock-libuv 构建）+ `test/test_httpserver_e2e.py --qwrt-bin build_asan/qwrt`（16/16，real-libuv）+ 本页 CLI 基准脚本。后续 F3 优化（预热/复用/零拷贝）以此页数值为基线对比。


## Timeline

- time: 2026-08-27T15:38:59
  kind: decision
  summary: "Created this page: 启动/内存基准（F3）"
  source: created via brain create-page
  affects: [startup-memory-benchmark]

- time: 2026-08-27T15:39:15
  kind: decision
  summary: Rewrote compiled_truth to the new best understanding
  source: brain update-truth
  affects: [startup-memory-benchmark]

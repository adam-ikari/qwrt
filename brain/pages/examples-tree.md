---
id: examples-tree
title: "Examples Tree & QWRT_BUILD_EXAMPLES"
category: decision
status: active
tags: [build, examples]
created: "2026-08-14T08:41:03"
updated: "2026-08-14T08:41:11"
---

<!-- compiled_truth -->
- 示例程序放在根目录 examples/ 下，每个示例一个子目录（examples/hello, examples/worker），不再放根目录 example.c。
- 由顶层选项 QWRT_BUILD_EXAMPLES（默认 OFF）门控，examples/CMakeLists.txt 用 add_subdirectory 聚合。
- 每个示例链接 qwrt_full 目标（含 libuv），include 顶层 include/。
- worker 示例的 worker.js 路径由 CMake 以编译期宏注入 file:// 绝对 URL（QWRT_WORKER_SCRIPT），不在 C 源码里拼路径。
- 踩坑：QWRT_USE_MOCK_LIBUV 是测试专用定义。若 build 目录曾用 QWRT_BUILD_TESTS=ON 配置，libqwrt.a 会带 mock 符号，链接真实 libuv 的示例会挂起（qwrt_create 阻塞）。示例构建必须用 tests=OFF 的干净配置。
- hello 示例演示 host<->JS postMessage 往返；worker 示例演示真线程 Worker 回显。


## Timeline

- time: 2026-08-14T08:41:03
  kind: decision
  summary: "Created this page: Examples Tree & QWRT_BUILD_EXAMPLES"
  source: created via brain create-page
  affects: [examples-tree]

- time: 2026-08-14T08:41:11
  kind: decision
  summary: Rewrote compiled_truth to the new best understanding
  source: "2026-08-14 examples migration (commit feat: add runnable examples tree)"
  affects: [examples-tree]

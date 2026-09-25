---
id: examples-tree
title: "Examples Tree & QZ_BUILD_EXAMPLES"
category: decision
status: active
tags: [build, examples]
created: "2026-08-14T08:41:03"
updated: "2026-09-24T02:55:07"
---

<!-- compiled_truth -->
- 示例程序放在根目录 examples/ 下，每个示例一个子目录（examples/hello, examples/worker），不再放根目录 example.c。
- 由顶层选项 QZ_BUILD_EXAMPLES（默认 OFF）门控，examples/CMakeLists.txt 用 add_subdirectory 聚合。
- 每个示例链接 qz_full 目标（含 libuv），include 顶层 include/。
- worker 示例的 worker.js 路径由 CMake 以编译期宏注入 file:// 绝对 URL（QZ_WORKER_SCRIPT），不在 C 源码里拼路径。
- 踩坑：QZ_USE_MOCK_LIBUV 是测试专用定义。若 build 目录曾用 QZ_BUILD_TESTS=ON 配置，libqzjs.a 会带 mock 符号，链接真实 libuv 的示例会挂起（qz_create 阻塞）。示例构建必须用 tests=OFF 的干净配置。
- 踩坑：mock_libuv 的空闲路径阻塞在固定 1 秒轮询兜底（test/mock_libuv.c uv_run 的 uv_cond_timedwait(1000)），不在定时器到期时刻唤醒 → 定时器被量化到整秒。任何"耗时"示例（timers 等）在 QZ_BUILD_TESTS=ON 构建下会打印错误耗时。示例必须用 Release 构建跑。
- 每个示例目录都应带 README.md：精确运行命令（统一用文档约定名 ./build/qzjs）+ 实跑核对过的期望输出。
- JS 示例的运行 cwd 敏感：worker-orchestrate 的 WORKER_URL 是相对 cwd 的 file:// 路径，必须从仓库根运行。
- hello 示例演示 host<->JS postMessage 往返；worker 示例演示真线程 Worker 回显。


## Timeline

- time: 2026-08-14T08:41:03
  kind: decision
  summary: "Created this page: Examples Tree & QZ_BUILD_EXAMPLES"
  source: created via brain create-page
  affects: [examples-tree]

- time: 2026-08-14T08:41:11
  kind: decision
  summary: Rewrote compiled_truth to the new best understanding
  source: "2026-08-14 examples migration (commit feat: add runnable examples tree)"
  affects: [examples-tree]

- time: 2026-09-24T02:55:07
  kind: decision
  summary: "补充：mock_libuv 定时器量化坑 + 每示例 README 要求 + cwd 敏感"
  source: "2026-09 examples README sweep，实跑核对"
  affects: [examples-tree]

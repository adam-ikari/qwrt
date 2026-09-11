---
id: runtime-perf-baseline
title: "运行时性能基线（runtime-perf R1-R6，THREAD/PROCESS 双后端）"
category: reference
status: active
tags: [perf, worker, runtime, baseline]
created: "2026-09-08T16:56:22"
updated: "2026-09-09T01:01:11"
---

<!-- compiled_truth -->
> **测试策略（2026-09-09 用户拍板）：性能基准一律在 CI 环境（GitHub Actions ubuntu-latest）执行，不使用本机。** 本机（Ryzen 5800H / PVE）受负载（竞争 load 8-15）、构建类型（Debug/Release 混用）、环境漂移影响，数值不可复现；CI runner 机器一致、可复现，是唯一权威基线来源。本机仅在开发期做 `--quick` 快速冒烟（验证 harness 能跑），本机数值**不作权威基线、不记录为基线**。本页全部本机数值为**一次性测量（历史参考）**，权威基线见下方 CI 环境基线段 / CI cross-runtime job。

运行时性能基线（R1-R6，THREAD vs PROCESS 双后端）。比值=PROCESS/THREAD。来源 test/bench_runtime.py + ci.yml runtime-perf job（设计文档 docs/archive/plans/2026-09-04-runtime-perf-benchmark-design.md §5.2 指定本页）。

## 本地基线（Ryzen 5800H / PVE 6.17，Release build，commit f4ab5776，2026-09-04）——**历史参考**（2026-09-09 起基准仅 CI 环境，本机不测；数值保留作历史）
- R1 冷启动：median 15.9ms（n=5，Release build_rel，2026-09-09 跨运行时实测回填；Debug+ASan 参考 40–73ms，见 startup-memory-benchmark 页）
- R2 spawn ready：THREAD 9.46ms → PROCESS 14.24ms（**1.51×**，polyfill 注入主导；裸 new Worker 反而 PROCESS 更快 0.22×）
- R2b terminate：THREAD 7.5µs → PROCESS 10.1ms（**1353×**，PROCESS 三级终止同步阻塞）
- R3 往返：0B/1KB/64KB 比值 **1.19× / 1.00× / 0.97×**（与线程并列；64KB ~140ms/op 序列化主导）
- R4 吞吐：THREAD 5927 → PROCESS 1769 msg/s（**0.3×**，PROCESS 曾受洪水卡死限制，9b7c0781 已根治）
- R5 worker VmHWM：THREAD 22.4MB vs PROCESS 13.1MB（PROCESS 无宿主 polyfill 负担）
- R6 eval：int 28.2 / closure 12.9 / str 8.7 M ops/s（Release build_rel，2026-09-09 跨运行时实测回填；仅单后端采样，CPU 密集与后端无关）

## CI 环境基线（GitHub Actions ubuntu-latest，HEAD ccb3de18，2026-09-08 首跑成功）——权威基线
- R1 冷启动：median 7.48ms（n=3）
- R2 spawn ready：THREAD 4.64ms → PROCESS 5.93ms（**1.28×**）；raw spawn PROCESS 1.04ms vs THREAD 4.46ms（**0.23×**）
- R2b terminate：THREAD 5.6µs → PROCESS 10.08ms（**1799×**，PROCESS 三级终止同步阻塞）
- R3 往返：0B/1KB/64KB 比值 **0.78× / 1.03× / 0.99×**（0B PROCESS 反超 59.7 vs 76.4µs；64KB ~21.4ms/op）
- R4 吞吐：THREAD 19211 → PROCESS 18833 msg/s（**0.98×**，近并列）
- R5 worker VmHWM：THREAD 21.7MB vs PROCESS 12.8MB（PROCESS 无宿主 polyfill 负担）
- R6 eval：int 52.0 / closure 27.7 / str 13.6 M ops/s（仅单后端采样）
- 来源：run #34255495676 / HEAD ccb3de18，job "runtime perf (workers)" success，artifact runtime-perf-json。前置失败两连（无 artifact）：#34253382900（esbuild 缺 npm ci）、#34254235073（polyfill rebuild 先于 CMake build → qjsc not found）；本轮 ccb3de18 修顺序后成功。

## 跨运行时对比（qwrt vs Node/Bun/Txiki）——**历史参考**（2026-09-09 本机 Ryzen 5800H / build_rel Release，一次性测量；权威跨运行时基线见 CI cross-runtime job（node/bun））
- 驱动：test/bench_cross_runtime.py（新增）+ test/bench/runtime/bench-eval-cross.js（同一 JS 代码 qwrt/node/bun/tjs 四运行时共用）；指标 R1 冷启动 / R6 eval 吞吐（1M iter）/ R5 进程峰值 VmHWM。设计文档 §8。
- 数值表（R1 ms / R6 M ops/s / R5 KB）：
  - qwrt：15.88 / int 28.2 · closure 12.9 · str 8.7 / 14,580
  - node v22.22.2：31.98 / 1,042 · 1,038 · 23.4 / 183,236
  - bun 1.3.14：15.84 / 1,468 · 917 · 54.1 / 163,536
  - tjs v26.6.0：15.87 / 36.6 · 17.2 · 9.9 / 10,696
- 比值 vs qwrt：启动 node 2.01×（bun/tjs ~1.00×）；int/closure 吞吐 node 37/81×、bun 52/71×（str 仅 2.7/6.2×）；RSS node 12.6×、bun 11.2×；tjs 全轴 ~1×（同 QuickJS 引擎族，RSS 0.73× 反而更小）。两轮完整复跑一致（±10% 内；bun R1 双峰 15.8~32ms，复跑中位数落 15.8ms）
- 结论：**启动** qwrt≈tjs≈bun（15.8~15.9ms），node 2× 慢；**内存是最大差异化优势**（qwrt 14.6MB vs node/bun 160-183MB，11-13×）；**吞吐** JIT 快 37-81×，str 差距最小 2.7-6.2×；tjs 同引擎互证（1.15-1.34×）。qwrt/Txiki 定位「够用吞吐 + 极小内存/快启动」。
- Txiki 构建注记：官方 release 无 linux 预编译包（仅 macOS/Windows）；源码构建需 gcc-12（GCC 11 编 ada.h 的 constexpr std::string_view 失败）+ mbedtls framework submodule + patch `-Wno-unknown-pragmas`；tjs v26.6.0 改子命令 `tjs eval 'expr'` / `tjs run script.js [args]`，脚本参数经 `tjs.args`。**tjs 因构建成本暂不入 CI；node/bun 已入 CI cross-runtime job。**


## Timeline

- time: 2026-09-08T16:56:22
  kind: decision
  summary: "Created this page: 运行时性能基线（runtime-perf R1-R6，THREAD/PROCESS 双后端）"
  source: ci-perf-baseline session
  affects: [runtime-perf-baseline]

- time: 2026-09-08T16:56:35
  kind: decision
  summary: "双后端运行时基线：本地 Ryzen 5800H 已实测；CI (ubuntu-latest) 待首次成功 run（2026-09-08 首次失败，esbuild 缺失，ci.yml 已修）"
  source: brain update-truth
  affects: [runtime-perf-baseline]

- time: 2026-09-08T16:56:45
  kind: decision
  summary: "本地首轮基线落档（Ryzen 5800H / Release / f4ab5776）：spawn ready 1.51×、terminate 1353×、往返 ~1×、吞吐 0.3×、worker VmHWM 22.4 vs 13.1MB——证伪原 ≫10×/10~50× 预测量级"
  source: docs/archive/plans/2026-09-04-runtime-perf-benchmark-design.md
  affects: [runtime-perf-baseline]

- time: 2026-09-08T16:56:45
  kind: note
  summary: "CI 首次尝试失败：run #185 (34253382900, HEAD 48388bcd) runtime-perf job 'Rebuild polyfill' 缺 npm ci → esbuild 缺失，无 artifact。httpserver-perf 同 bug。修复：ci.yml 两 job 补 'Install polyfill deps (esbuild)': npm --prefix polyfill ci。CI 数值待 push 修复后重跑回填"
  source: GitHub Actions 2026-09-08
  affects: [runtime-perf-baseline]

- time: 2026-09-08T17:03:59
  kind: decision
  summary: "CI 首跑基线回填受阻：run #34254235073 runtime-perf job 失败（qjsc not found，polyfill rebuild 先于 CMake build），无 artifact"
  source: ci-baseline-fill session
  affects: [runtime-perf-baseline]

- time: 2026-09-08T17:13:34
  kind: decision
  summary: "CI 首跑成功回填实测数值（run #34255495676 / ccb3de18）"
  source: ci-baseline-fill2 session
  affects: [runtime-perf-baseline]

- time: 2026-09-08T17:13:48
  kind: decision
  summary: "CI 首跑成功（run #34255495676 / HEAD ccb3de18）：R1 7.48ms；spawn ready THREAD 4.64ms→PROCESS 5.93ms 1.28×；terminate 1799×；往返 0.78/1.03/0.99×；吞吐 0.98×（19211 vs 18833 msg/s）；worker VmHWM 21.7 vs 12.8MB；R6 int/closure/str 52.0/27.7/13.6 M ops/s——qjsc 顺序 bug 修复后 CI 基线落档"
  source: ci-baseline-fill2 session
  affects: [runtime-perf-baseline]

- time: 2026-09-09T00:14:44
  kind: decision
  summary: "追加跨运行时对比段（qwrt vs Node/Bun/Txiki，2026-09-09 本机实测）"
  source: "cross-runtime-bench session (bench_cross_runtime.py)"
  affects: [runtime-perf-baseline]

- time: 2026-09-09T00:14:59
  kind: decision
  summary: "跨运行时首测落档（2026-09-09 本机 Ryzen 5800H，qwrt build_rel Release）：R1 15.88ms / node 31.98（2.01×）、bun 15.84、tjs 15.87；R6 int qwrt 28.2 vs node 1042（37×）bun 1468（52×）tjs 36.6（1.3×）；R5 qwrt 14.6MB vs node 183MB（12.6×）bun 164MB（11.2×）tjs 10.7MB（0.73×）——内存最大差异化优势，启动三系并列，吞吐 JIT 快 37-81×（str 仅 2.7-6.2×）。Txiki 需 gcc-12 源码构建，暂不入 CI"
  source: "cross-runtime-bench session (test/bench_cross_runtime.py)"
  affects: [runtime-perf-baseline]

- time: 2026-09-09T01:01:11
  kind: decision
  summary: "基准测试策略：仅 CI 环境（用户拍板 2026-09-09）；本机数值降级为历史参考"
  source: brain update-truth
  affects: [runtime-perf-baseline]

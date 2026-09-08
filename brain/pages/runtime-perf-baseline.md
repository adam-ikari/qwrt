---
id: runtime-perf-baseline
title: "运行时性能基线（runtime-perf R1-R6，THREAD/PROCESS 双后端）"
category: reference
status: active
tags: [perf, worker, runtime, baseline]
created: "2026-09-08T16:56:22"
updated: "2026-09-08T17:03:59"
---

<!-- compiled_truth -->
运行时性能基线（R1-R6，THREAD vs PROCESS 双后端）。比值=PROCESS/THREAD。开发机与 CI runner 各记一套；来源 test/bench_runtime.py + ci.yml runtime-perf job（设计文档 docs/plans/2026-09-04-runtime-perf-benchmark-design.md §5.2 指定本页）。

## 本地基线（Ryzen 5800H / PVE 6.17，Release build，commit f4ab5776，2026-09-04）
- R1 冷启动：待补 Release 数值（Debug+ASan 参考 40–73ms，见 startup-memory-benchmark 页）
- R2 spawn ready：THREAD 9.46ms → PROCESS 14.24ms（**1.51×**，polyfill 注入主导；裸 new Worker 反而 PROCESS 更快 0.22×）
- R2b terminate：THREAD 7.5µs → PROCESS 10.1ms（**1353×**，PROCESS 三级终止同步阻塞）
- R3 往返：0B/1KB/64KB 比值 **1.19× / 1.00× / 0.97×**（与线程并列；64KB ~140ms/op 序列化主导）
- R4 吞吐：THREAD 5927 → PROCESS 1769 msg/s（**0.3×**，PROCESS 曾受洪水卡死限制，9b7c0781 已根治）
- R5 worker VmHWM：THREAD 22.4MB vs PROCESS 13.1MB（PROCESS 无宿主 polyfill 负担）
- R6 eval：CPU 密集与后端无关，仅单后端采样（数值待补）

## CI 环境基线（GitHub Actions ubuntu-latest）——仍待首次成功 run
- 2026-09-08 第二次尝试失败：run #34254235073 / HEAD 34757c47（commit "ci(perf): 修 runtime-perf/httpserver-perf 的 polyfill rebuild——补 npm ci"）。esbuild 已装好（npm ci 生效），但 "Rebuild polyfill" 步骤仍在 Configure/Build 之前 → quickjs-ng 尚未编译，build.js 找不到 qjsc → `Error: qjsc not found, cannot generate bytecode header: Command failed: /home/runner/work/qwrt/deps/quickjs-ng/build/qjsc ...`（exit 1）→ Benchmark skipped，无 artifact。httpserver-perf 同因失败。
- 根因：polyfill/build.js 的 findQjsc() 优先找 `build*/deps/quickjs-ng/qjsc`（CMake build 产物），但 "Rebuild polyfill" 步骤排在 cmake configure/build 之前，qjsc 尚不存在；legacy 路径 `deps/quickjs-ng/build/qjsc` 在 CI 也无（submodule 未单独 build）。
- 建议修复：将 "Rebuild polyfill" 步骤移到 "Build"（cmake --build）之后；或先 `cmake --build build --target qjsc` 再 rebuild polyfill。该 job continue-on-error:true，失败不阻塞 merge，但不产出 artifact。
- CI job 配置要点：runs-on ubuntu-latest；checkout submodules recursive；Node 22 + polyfill rebuild；Configure Release + QWRT_BUILD_TESTS=OFF + QWRT_WITH_TLS=ON；`python3 test/bench_runtime.py --qwrt-bin ./build/qwrt --quick --json /tmp/runtime-perf.json`；Summarize 解析末行 JSON；upload artifact `runtime-perf-json`。
- 下一步：主会话修 ci.yml 步骤顺序（rebuild 移到 build 后）并 push，重跑拉 artifact，回填本页 CI 数值表并与本地对比。


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
  source: docs/plans/2026-09-04-runtime-perf-benchmark-design.md
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

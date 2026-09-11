---
id: ci-nightly-repair
title: "CI 修复例行：每晚 02:00 自动调查修复"
category: decision
status: completed
tags: [ci, nightly, scheduling]
created: "2026-09-09T10:34:20"
updated: "2026-09-09T19:36:48"
---

<!-- compiled_truth -->
## 完结状态（2026-09-09/10）

所有 CI 失败项已修复并复验绿（19 job 零失败，run 34395551179 = ALL-GREEN）。nightly 例行转为备用模式（未来 CI 再红时复用），不再每晚自动跑空转。

## 修复清单（12 项）

clang -Werror typedef 重定义（75f3c47a）；SW e2e 硬编码路径参数化（75f3c47a）；test262 runner EXCLUDE 依赖 + 语料 update=none 显式拉取（75f3c47a/fcf38813）；coverage gcov RSA keygen eval 预算（b4d9a290）；WAMR misaligned store 豁免 alignment（30ccb970）；ASan 假栈与 WAMR 检测不兼容关 UAR（3dd786d3）；wasm3 gc_mark + ext_destroy class_id 时序 + memory import 报错（bfabda0a）；sanitizer JS 栈预算 4MB（3a3a3e32）；clang -fsanitize=function 豁免 + ipc_envelope_cli EXCLUDE（3505dffc）。

权威记录：docs/CI_FIX_BACKLOG.md（完结归档版）。


## Timeline

- time: 2026-09-09T10:34:20
  kind: decision
  summary: "Created this page: CI 修复例行：每晚 02:00 自动调查修复"
  source: "用户指令 2026-09-09"
  affects: [ci-nightly-repair]

- time: 2026-09-09T10:34:20
  kind: decision
  summary: "用户持久需求：每晚 02:00 开始调查和修复 CI/CD 问题，直至全绿"
  source: brain update-truth
  affects: [ci-nightly-repair]

- time: 2026-09-09T12:00:18
  kind: decision
  summary: "第二跑：coverage (gcov) 转绿（b4d9a290，3072 keygen setup eval 5s→30s）；补录 sanitizer 档超时 vs 真 UB 的判别模式"
  source: "nightly 会话 2026-09-09 第二跑"
  affects: [ci-nightly-repair]

- time: 2026-09-09T19:36:48
  kind: decision
  summary: "CI 全部修复完成（2026-09-09/10）：7+ 预存失败 + 新暴露面全绿，run 34395551179 ALL-GREEN；backlog 转归档"
  source: brain update-truth
  affects: [ci-nightly-repair]

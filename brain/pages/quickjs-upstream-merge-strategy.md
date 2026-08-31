---
id: quickjs-upstream-merge-strategy
title: "QuickJS-ng/libuv 上游合并策略"
category: decision
status: active
tags: [build, upstream]
created: "2026-08-31T11:59:47"
updated: "2026-08-31T12:00:17"
---

<!-- compiled_truth -->
## 现状

- deps/quickjs-ng 与 deps/libuv 均为「本地快照 + 补丁文件」双机制：源码作为 git submodule 快照锁定在仓库内，上游差异通过补丁文件维护。
- gitlink 已修复：deps/libuv 回指上游存在的 commit 20b08342（v1.52.1 祖先，可被 CI fetch），不再指向悬空 commit。
- 上游落后基线（2026-08）：quickjs-ng 落后上游 107 commits、libuv 落后 134 commits。

## 策略（最保守默认）

- **补丁文件随 repo 提交 + CMake configure 阶段 `patch -p1` apply**：保证任何机器 checkout 后重新 configure 即得一致 vmlib（纯 C，无系统依赖）。
- 上游新 commit 需人工 rebase 三补丁到上游后合入，本地先跑 test262 与 offline ctest 验证再合入：
  - quickjs-ng-c99-atomics（22 行）
  - quickjs-ng-debugger（352 行）
  - libuv-c99-atomics（30 行）
- 不引入 fork url、不依赖个人仓库。

## 证据

- `patch --dry-run` OK，补丁可干净回放。
- libuv 补丁后 builds 全绿、offline ctest 13/13。
- 无补丁状态下 test_compress_gtest 30% flaky（-std=c99 原子行为不稳），补丁后降至 10%，非产品回归。


## Timeline

- time: 2026-08-31T11:59:47
  kind: decision
  summary: "Created this page: QuickJS-ng/libuv 上游合并策略"
  source: created via brain create-page
  affects: [quickjs-upstream-merge-strategy]

- time: 2026-08-31T12:00:10
  kind: decision
  summary: Rewrote compiled_truth to the new best understanding
  source: brain update-truth
  affects: [quickjs-upstream-merge-strategy]

- time: 2026-08-31T12:00:17
  kind: decision
  summary: "gitlink 修复回指上游 20b08342 + 确认 patch 机制为唯一可复现路径"
  source: "CI submodule 修复会话"
  affects: [quickjs-upstream-merge-strategy]

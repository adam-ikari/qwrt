---
id: wpt-runner-removed
title: "WPT runner 移除：WinterTC 覆盖转向 gtest"
category: decision
status: active
tags: [wpt, wintertc, testing, libuv-native]
created: "2026-08-18T08:11:31"
updated: "2026-08-18T08:11:46"
---

<!-- compiled_truth -->
## 决策
WinterTC Web API 合规测试不再使用 WPT runner（vendored .any.js + testharness.js），改为 gtest 套件覆盖。

## 背景
- test/wpt_runner.c（依赖 pal_mock）在 libuv-native 重构（commit 207e0b7e，移除整个 PAL 抽象）时被删除。
- wpt_runner 当时是 wip 骨架，未接入 CMake；build 里的旧产物仍可跑旧数据但已过时。
- test/wpt/ 目录（vendored .any.js + testharness.js + wpt_shell_report.js）保留作参考，但不在构建/CI 中。

## 原因
1. mock_libuv（新模型）是确定性离线测试，与 gtest 统一（crypto-subtle-gtest 决策同源）
2. wpt_runner 依赖已删除的 pal_mock API（pal_mock_create + qwrt_eval）
3. 项目 13 个 gtest 套件已覆盖 WinterTC API（URL/URLPattern/FormData/Event/Blob/console/timers/crypto.subtle）

## 现状（2026-08-18）
- WinterTC API 覆盖位于 offline gtest：test_polyfill_gtest（URL/URLPattern/FormData/Event/console/Blob/timers）、test_crypto_subtle_gtest、test_fetch_stream_gtest、test_bridge_*_gtest
- 若未来要恢复 WPT，需按新模型重写 runner：HostCtx（test_host.h）+ kTestBootstrap eval 通道，处理 promise_test 的异步（host_poll_until_value）

## 参考
- [[crypto-subtle-gtest]] 同方向的 gtest 决策
- [[urlpattern-modifier-fix]] 记录了本次 WPT 基准验证（urlpattern-polyfill npm 包）


## Timeline

- time: 2026-08-18T08:11:31
  kind: decision
  summary: "Created this page: WPT runner 移除：WinterTC 覆盖转向 gtest"
  source: session 2026-08-18
  affects: [wpt-runner-removed]

- time: 2026-08-18T08:11:46
  kind: decision
  summary: Rewrote compiled_truth to the new best understanding
  source: brain update-truth
  affects: [wpt-runner-removed]

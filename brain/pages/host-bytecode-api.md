---
id: host-bytecode-api
title: "宿主字节码 API：策略反转与 JS_DetectModule 陷阱"
category: decision
status: active
tags: [bytecode, host-api, compatibility]
created: "2026-09-25T14:38:43"
updated: "2026-09-26T14:58:51"
---

<!-- compiled_truth -->
**DAP CI 连败根因（2026-09-26 已修）**：8ac7ed7c 项目改名（am_→qz_）漏改
deps/quickjs-ng-debugger.patch 内的引擎侧守卫宏 QWRT_DEBUG_SUPPORT；
CMake 向 qjs target 传 QZ_DEBUG_SUPPORT → 宏不匹配 → quickjs.c 的
DEBUGGER_CHECK 编译为空 → 断点/暂停永不触发，test_dap_gtest 双败 +
30s 超时，CI debugger(DAP) job 连续 12+ 次失败（含纯文档提交，极易误判
为近期改动引入）。修复：patch 3 处改名（commit 3853868a）。

**教训**：改名类提交必须 grep deps/*.patch 内的旧名——patch 是
working-tree-only 应用、不进 git 跟踪的 diff 语境，改名工具/人工审查
都容易跳过。CMake 侧 verify 锚点（grep JS_SetDebuggerHandler 等）检查
的是函数存在性而非宏生效，无法捕获此类失配。


## Timeline

- time: 2026-09-25T14:38:43
  kind: decision
  summary: "Created this page: 宿主字节码 API：策略反转与 JS_DetectModule 陷阱"
  source: created via brain create-page
  affects: [host-bytecode-api]

- time: 2026-09-25T15:02:07
  kind: decision
  summary: Rewrote compiled_truth to the new best understanding
  source: brain update-truth
  affects: [host-bytecode-api]

- time: 2026-09-25T15:02:14
  kind: decision
  summary: Rewrote compiled_truth to the new best understanding
  source: brain update-truth
  affects: [host-bytecode-api]

- time: 2026-09-26T02:13:55
  kind: decision
  summary: Rewrote compiled_truth to the new best understanding
  source: brain update-truth
  affects: [host-bytecode-api]

- time: 2026-09-26T14:58:51
  kind: decision
  summary: Rewrote compiled_truth to the new best understanding
  source: brain update-truth
  affects: [host-bytecode-api]

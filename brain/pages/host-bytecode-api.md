---
id: host-bytecode-api
title: "宿主字节码 API：策略反转与 JS_DetectModule 陷阱"
category: decision
status: active
tags: [bytecode, host-api, compatibility]
created: "2026-09-25T14:38:43"
updated: "2026-09-25T15:02:14"
---

<!-- compiled_truth -->
## 场景加固（2026-09-25 续测）

**空字节码 double-free（已修）**：qz_create 原条件 `initial_bytecode && initial_bytecode_len` 在 len==0 时不拷贝，rt->config.initial_bytecode 悬挂宿主缓冲，destroy 二次释放（valgrind 定位：Invalid free in qz_free）。修复：指针设置即归 rt 所有，malloc(n?:1) 恒拷贝；CLI 空文件前置报错。

**既有 60B leak（非本特性引入）**：CLI script 模式 valgrind 同样报 definitely lost 60 bytes 1 block——与字节码模式同量同模式，先于本改动存在。

**TLA 边界**：顶层 await 编译报 SyntaxError（GLOBAL 模式无 async flag），与 source initial_script/-e 行为一致——字节码路径与源码路径语义对齐是设计约束。

**校验和防护实证**：中间字节篡改 → SyntaxError: checksum error；截断/空文件 → 明确报错；双 runtime 各载不同字节码完全隔离。


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

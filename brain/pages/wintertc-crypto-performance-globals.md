---
id: wintertc-crypto-performance-globals
title: "ECMA-429 Crypto/SubtleCrypto/Performance 构造函数暴露"
category: decision
status: active
tags: [wintertc, ecma-429, crypto, performance]
created: "2026-08-19T03:34:59"
updated: "2026-08-19T03:35:18"
---

<!-- compiled_truth -->
- **背景**：ECMA-429（WinterTC Minimum common web API）WEBCRYPTO 要求 globalThis 暴露 `Crypto`/`CryptoKey`/`SubtleCrypto`/`crypto`，HR-TIME 要求 `Performance`/`performance`。此前 `Crypto`/`SubtleCrypto`/`Performance` 三个构造函数未暴露（`crypto`/`performance` 实例是对象字面量，`SubtleCrypto` 类存在但没挂全局）。这是 ECMA-429 全局接口的最后缺口。
- **实现（polyfill/src，2026-08-19）**：
  - `crypto.js`：crypto 对象字面量改为 `class Crypto`，暴露 `globalThis.Crypto` + `globalThis.crypto = new Crypto()`。构造里 `this.subtle = undefined`（由 crypto-subtle.js 填充）。getRandomValues/randomUUID 逻辑不变。
  - `crypto-subtle.js`：安装部分加 `globalThis.SubtleCrypto = SubtleCrypto`（class 已存在，只需挂全局）。
  - `performance.js`：performance 对象字面量改为 `class Performance`，暴露 `globalThis.Performance` + `globalThis.performance = new Performance()`。对象方法（now/mark/measure/clearMarks/getEntries 等）改为 class 方法（方法间去掉逗号；getter `timeOrigin` 语法不变）。闭包变量 marks/measures/nowMs 由 class 方法直接捕获。
- **验证**：`check_globals.js`（57 个 ECMA-429 要求接口）MISSING(0)——所有接口补齐。test_polyfill_gtest 新增 CryptoGlobals/PerformanceGlobals 两用例（检查 typeof function + instanceof + getRandomValues/performance.now 回归），19/19 通过；crypto_subtle 8/8、worker 12/12、fetch_stream 2/2、bridge_stream 1/1、context 3/3、qwrt 9/9 全过。
- **意义**：ECMA-429（WinterTC Minimum Common API）要求的 globalThis 接口集**全部补齐**（Streams BYOB 见 [[wintertc-byob-streams]]）。
- **构建注意**：polyfill 打包走 build.js 的 QJSC 自动探测 + absWorkingDir（保证产物可复现），无需手动 QJSC 环境变量。


## Timeline

- time: 2026-08-19T03:34:59
  kind: decision
  summary: "Created this page: ECMA-429 Crypto/SubtleCrypto/Performance 构造函数暴露"
  source: session 2026-08-19
  affects: [wintertc-crypto-performance-globals]

- time: 2026-08-19T03:35:18
  kind: decision
  summary: Rewrote compiled_truth to the new best understanding
  source: brain update-truth
  affects: [wintertc-crypto-performance-globals]

---
id: wintertc-byob-streams
title: "WinterTC BYOB streams 补齐（ReadableByteStreamController / BYOBReader / BYOBRequest）"
category: decision
status: active
tags: [wintertc, streams, byob, ecma-429]
created: "2026-08-19T02:39:22"
updated: "2026-08-19T02:39:43"
---

<!-- compiled_truth -->
- **背景**：ECMA-429（WinterTC Minimum common web API，2025 snapshot）要求 Streams 的三个 BYOB 接口必须暴露在 globalThis 上：`ReadableByteStreamController`、`ReadableStreamBYOBReader`、`ReadableStreamBYOBRequest`。qwrt 此前只实现了 default 系列（ReadableStream/DefaultController/DefaultReader），三个 BYOB 接口缺失。
- **实现（polyfill/src/streams.js，2026-08-19）**：
  - `ReadableStream` 构造识别 `underlyingSource.type === 'bytes'` → 设 `_type='bytes'`，用 `ReadableByteStreamController`（否则用 default controller）。
  - `getReader({mode:'byob'})` → `ReadableStreamBYOBReader`；对非 bytes stream 抛 TypeError；无参/mode default 保持兼容。
  - `BYOBReader.read(view)`：view 必须是非空 ArrayBufferView；从内部 `_queue`（Uint8Array chunks）把字节拷入用户 view（`_fillFromQueue`），支持**部分填充**（chunk 耗尽则出队，剩余部分用 subarray 保留），返回 `{done, value}`，value 是用户 view 的切片（长度=实际写入字节）。
  - `controller.byobRequest` getter：返回包装第一个 pending BYOB read 的 `ReadableStreamBYOBRequest`（无则 null）；`respond(bytesWritten)` / `respondWithNewView(newView)` 完成该 read。
  - `_pendingReads` entry 增加 `view` 字段区分 BYOB/default 挂起读；`_notifyReaders` 分流处理；`_maybePull()` 统一 pull 触发（防重入 `_pulling` 标志），BYOB pending 或 default 队列低于 hwm 时触发。
  - `ReadableByteStreamController.enqueue` 要求 ArrayBufferView 或 ArrayBuffer（否则 TypeError），统一转 Uint8Array 入队；`desiredSize` 按字节（`_queueBytes`）。
- **测试**：test/test_polyfill_gtest.cpp 新增 5 个用例（globals 暴露、getReader mode、从队列读+部分填充、空+close → {done:true}、byobRequest+respond）。offline ctest 13/13 全过（含 worker/fetch_stream/bridge_stream 回归）。
- **构建注意**：polyfill 重新打包需 `QJSC` 环境变量指向实际 qjsc（`build/deps/quickjs-ng/qjsc`），build.js 默认路径 `ROOT_DIR/../deps/quickjs-ng/build/qjsc` 在本机不存在。
- **剩余 ECMA-429 差距**（后续项）：`Performance`（大写构造函数）、`Crypto`、`SubtleCrypto` 仍未暴露为 globalThis 构造函数（`performance`/`crypto` 实例已有）。WASM 子对象（CompileError 等）是 `WebAssembly.*` 属性，非 globalThis 属性，属误报。


## Timeline

- time: 2026-08-19T02:39:22
  kind: decision
  summary: "Created this page: WinterTC BYOB streams 补齐（ReadableByteStreamController / BYOBReader / BYOBRequest）"
  source: session 2026-08-19
  affects: [wintertc-byob-streams]

- time: 2026-08-19T02:39:43
  kind: decision
  summary: Rewrote compiled_truth to the new best understanding
  source: brain update-truth
  affects: [wintertc-byob-streams]

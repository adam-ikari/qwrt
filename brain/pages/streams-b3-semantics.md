---
id: streams-b3-semantics
title: "streams B3 标准语义修复"
category: decision
status: active
tags: [streams, wintertc, ecma-429]
created: "2026-08-27T06:46:05"
updated: "2026-08-27T06:46:29"
---

<!-- compiled_truth -->
- **背景**：ROADMAP B3（streams 覆盖）对照 WHATWG Streams 语义审计 polyfill/src/streams.js，发现 pipeTo/tee/pipeThrough/releaseLock 四处真实缺口。
- **修复（polyfill/src/streams.js，2026-08-27，commit 8d6bf8b2）**：
  - `pipeTo`：收尾释放 dest writer 锁——正常路径在 `writer.close()` 完成后（或 preventClose 时）`writer.releaseLock()`，出错路径在 abort 后同样释放（规范 ReadableStreamPipeTo teardown）。修复前 `dest.getWriter()` 抛 "already has a writer"。
  - `ReadableStreamDefaultReader.releaseLock`：reject 未 settle 的 closed promise 与所有 pending read（TypeError），标记 `_released`；`read()` 检测 `_released` 抛 "Reader has been released"（此前返回 {done:true}）。BYOBReader 同样处理。
  - `tee()` 单分支 cancel：关闭该分支流（controller.close()），其 pending read 以 {done:true} 结束，另一分支继续从共享源拉取；此前单分支 cancel 后该分支读永久挂起。
  - `pipeThrough()`：校验 transform 结构（缺 readable/writable 抛 TypeError）与三流 locked 状态（源/transform.readable/transform.writable 任一 locked 抛 TypeError）。
- **测试**：test_polyfill_gtest.cpp 新增 5 用例（PipeToReleasesWriterLock 三路径、ReleaseLockRejectsPendingRead、ReadAfterReleaseRejects、TeeSingleCancelClosesBranch、PipeThroughValidation）。test_polyfill_gtest 51/51、ctest offline 13/13 全过。
- **构建注意**：polyfill/src 改动后须 `node polyfill/build.js` 重建 dist/polyfill.js + polyfill.bytecode + src/polyfill_default.c（dist 用 git add -f 强制跟踪）。
- **既有 flaky**：test_compress_consistency_gtest 偶发失败（RoundtripGzip 用例，~20%）为既有 flaky（git stash 回退对比证实与本次改动无关）。


## Timeline

- time: 2026-08-27T06:46:05
  kind: decision
  summary: "Created this page: streams B3 标准语义修复"
  source: created via brain create-page
  affects: [streams-b3-semantics]

- time: 2026-08-27T06:46:29
  kind: decision
  summary: "streams B3 语义缺口修复(commit 8d6bf8b2)"
  source: brain update-truth
  affects: [streams-b3-semantics]

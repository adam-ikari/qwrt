---
id: libuv-io-uring-workaround
title: "libuv io_uring workaround（PVE 6.17 内核）"
category: decision
status: active
tags: [libuv, io-uring, linux, workaround]
created: "2026-08-18T02:53:53"
updated: "2026-09-20T00:12:42"
---

<!-- compiled_truth -->
## 现状（2026-09-20 更新）

- io_uring workaround 已**纳入 deps/libuv-c99-atomics.patch**（与 c99-atomics 合一，patch 文件 SSOT）：删除上游 `uv__use_io_uring` 中 `!SQPOLL → return 1` 无条件启用分支，使 `UV_USE_IO_URING=0`（amoib.c setenv）在所有路径生效。
- libuv 已升级至 v1.x HEAD（84af0b18，8 commits，全 BSD/CI 类，与补丁零冲突）。gitlink 指上游 commit，fresh checkout 后由 CMake `patch -p1` 应用补丁文件。
- 此前"改动仅存工作区"的脆弱机制已消除：workaround 不再依赖未提交的工作区状态。
- 验证：PVE 6.17 本机（正是触发内核）UV_USE_IO_URING=0 与默认模式均正常 tick；offline ctest 23/23；mp1/nested e2e PASS；CI 35477802829 全绿。


## Timeline

- time: 2026-08-18T02:53:53
  kind: decision
  summary: "Created this page: libuv io_uring workaround（PVE 6.17 内核）"
  source: session 2026-08-18
  affects: [libuv-io-uring-workaround]

- time: 2026-08-18T02:54:07
  kind: decision
  summary: Rewrote compiled_truth to the new best understanding
  source: brain update-truth
  affects: [libuv-io-uring-workaround]

- time: 2026-09-20T00:12:42
  kind: decision
  summary: Rewrote compiled_truth to the new best understanding
  source: brain update-truth
  affects: [libuv-io-uring-workaround]

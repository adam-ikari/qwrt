---
id: libuv-io-uring-workaround
title: "libuv io_uring workaround（PVE 6.17 内核）"
category: decision
status: active
tags: [libuv, io-uring, linux, workaround]
created: "2026-08-18T02:53:53"
updated: "2026-08-18T02:54:07"
---

<!-- compiled_truth -->
deps/libuv/src/unix/linux.c 有一处本地补丁（未提交到上游）：让 `UV_USE_IO_URING=0` 真正禁用 io_uring。

## 背景与症状
某些内核（如 PVE 6.17）在 `io_uring_setup` 之后会破坏 futex/pthread_cond 的唤醒语义，
导致所有 cond-waiting 的宿主线程挂起。libuv 原代码在未设 SQPOLL 时无条件启用 io_uring，
`UV_USE_IO_URING=0` 环境变量根本不会生效（early return 跳过了该检查）。

## 改动
`uv__use_io_uring`：删掉 `!SQPOLL → return 1` 和旧内核版本分支，保留对
`UV_USE_IO_URING=0` 的尊重——io_uring_setup 后某些内核 futex/cond wakeup 失效，挂起任何
cond-waiting 宿主线程。

## 现状
- deps/libuv 内为未提交的本地改动（`m deps/libuv`），gitlink 仍指向上游 7e65007
- 未 push 上游（环境特定 workaround，非通用修复）
- 保留在工作区；若重建依赖需注意该 patch 不在上游

## 决策
保留为本地 patch，不提交到 qwrt（libuv 是 submodule，gitlink 不动）。若将来需要可复现，
考虑在 CMake 层注入或 fork 管理。


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

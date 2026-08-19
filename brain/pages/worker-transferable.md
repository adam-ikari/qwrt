---
id: worker-transferable
title: "Worker/structuredClone transferable 支持（ArrayBuffer transfer）"
category: decision
status: active
tags: [worker, transferable, arraybuffer, structured-clone, wintertc]
created: "2026-08-18T12:49:50"
updated: "2026-08-19T01:54:08"
---

<!-- compiled_truth -->
- **MessagePort 跨线程 transfer（已实现，2026-08-18）**：父线程与 worker 线程是独立 JSRuntime，不能共享 JS 对象。方案复用现有 worker 字节通道（`workerPost` 父→worker、`pal.postMessage` worker→父），C 层新增 `pal.portCreate()` 全局原子计数器分配唯一 port id 对（`{id1,id2}`，bridge.c，worker/父都注册）。
- 核心机制：
  - MessagePort 加 `_id`（全局唯一）/ `_peerId`（纠缠端）/ `_peerThread`（'local' | 'parent' | workerId）/ `_detached`。
  - 每个线程维护本地 port 表（id → MessagePort），入站 `__qwrt_dispatch__` 先反序列化、用 `__qwrt_deliver_port_msg__` 识别 `{__qwrt_port_msg: {target, payload}}` 包装并路由到本地对应 port，否则按普通 worker 消息处理。
  - 跨线程 postMessage：父侧 port2 → `pal.workerPost(workerId, wrappedBytes)`；worker 侧 port1 → `pal.postMessage(wrappedBytes)`。
  - 同线程 MessageChannel 仍是直接对象引用（`_entangledPort`），不经过字节通道。
- transfer（`w.postMessage(data, [port])` / worker 侧 `postMessage(data, [port])`）：把 transfer 里的 MessagePort 拆出为 `{__qwrt_ports:[{id,peerId,peerThread}]}` 包装（与 `__qwrt_payload` 一起整体序列化发送），原 port 标记 `_detached`，父侧对端 `_peerThread` 指向 workerId（worker 侧指向 'parent'）；接收侧 `__qwrt_port_from_ref__` 创建新代理并注册到本地表。
- **worker→父 方向（2026-08-19 补齐）**：worker 侧创建 channel 后 `postMessage(data, [port1])` 转移给父，父侧 `event.ports[0]` 收到代理。修复前 worker 侧 boot shim 硬编码 `peerThread: 'parent'`，父侧代理 `_peerThread='parent'`（字符串）使 `_sendRemote` 的 `this._peerThread > 0`（workerId 数字分支）不成立，无法经 `pal.workerPost` 路由回 worker（抛 "cannot route to parent from parent"）。修复：worker pal 暴露 `workerId`（bridge.c `js_pal_worker_id`，返回 `w->id`），boot shim 转移 ref 的 `peerThread` 改用 `pal.workerId()`；父侧代理 `_peerThread=workerId` 即可路由回 worker。路由机制本身双向对称（父侧 `_peerThread=workerId`→`workerPost`，worker 侧 `_peerThread='parent'`→`postMessage`），未改动架构。
- structuredClone 同线程 MessagePort transfer：transfer 校验允许 ArrayBuffer + MessagePort；clone() 里 MessagePort 在 transfer 列表 → 原 port detached + 返回新代理（`__qwrt_port_from_ref__`）；值里的 MessagePort（不在 transfer）→ DataCloneError。序列化字节流加 tag 0x20（MessagePort 引用编码 id/peerId/peerThread）。
- 测试：test_worker_gtest.cpp（transfer_messageport：父→worker 转移+worker event.ports[0] 回传；transfer_messageport_bidirectional：双向 echo+原 port detached；transfer_messageport_worker_to_parent：worker→父 转移+父侧经代理 ping→worker echo：worker 侧原 port1 detached；transfer_messageport_multi_worker：多 worker 并发各建 channel 转移给父，p1/p2 路由回各自 worker（_peerThread=1:2 不混淆）；transfer_messageport_terminate_worker：worker terminate 后父侧代理 port 发消息不抛错（静默丢弃））+ test_polyfill_gtest.cpp（StructuredCloneTransferMessagePort：transfer 不抛错+detached；值内 MessagePort→DataCloneError）。offline 13/13 + test262 1/1 通过（compress flaky 除外）。边界：多跳转移（父→worker→父）依赖纠缠关系重建，v1 未支持，不在 worker→父 单次转移范围内。


## Timeline

- time: 2026-08-18T12:49:50
  kind: decision
  summary: "Created this page: Worker/structuredClone transferable 支持（ArrayBuffer transfer）"
  source: 2026-08-18 session
  affects: [worker-transferable]

- time: 2026-08-18T12:50:01
  kind: decision
  summary: "Worker/structuredClone ArrayBuffer transfer v1 实现语义"
  source: 2026-08-18 session
  affects: [worker-transferable]

- time: 2026-08-18T12:53:46
  kind: decision
  summary: "刷新 _qwrtTransfer 描述：构造 options 副本而非修改用户对象（防 Object.freeze）"
  source: 2026-08-18 session
  affects: [worker-transferable]

- time: 2026-08-19T00:08:55
  kind: decision
  summary: "MessagePort 跨线程 transfer 实现完成（C 层 portCreate + JS 层跨线程路由）"
  source: 2026-08-18 session
  affects: [worker-transferable]

- time: 2026-08-19T01:40:33
  kind: decision
  summary: "worker→父 方向 MessagePort transfer 补齐（pal.workerId 暴露 + boot shim 路由）"
  source: brain update-truth
  affects: [worker-transferable]

- time: 2026-08-19T01:54:08
  kind: decision
  summary: "边界验证：多 worker 并发转移路由正确 + terminate 后代理 port 优雅失败"
  source: brain update-truth
  affects: [worker-transferable]

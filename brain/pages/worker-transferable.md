---
id: worker-transferable
title: "Worker/structuredClone transferable 支持（ArrayBuffer transfer）"
category: decision
status: active
tags: [worker, transferable, arraybuffer, structured-clone, wintertc]
created: "2026-08-18T12:49:50"
updated: "2026-08-18T12:53:46"
---

<!-- compiled_truth -->
- structuredClone 内通过把 transferSet 挂到 options 副本的 `_qwrtTransfer` 私有字段随递归传递（构造 `{transfer, _qwrtTransfer}` 副本，不改用户对象——防 Object.freeze）；被消息引用的对象在 ArrayBuffer 分支 `ts.delete(v)` + `value.transfer()`（内容转移为新 buffer 作为克隆结果）。


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

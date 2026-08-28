---
id: a2-worker-robustness
title: "A2 多上下文/Worker 健壮性（软挂起边界 + transferable 错误路径 + 错误事件流 + 压力）"
category: decision
status: active
tags: [worker, suspend, transferable, robustness, gtest]
created: "2026-08-28T15:53:09"
updated: "2026-08-28T15:53:31"
---

<!-- compiled_truth -->
- **范围**：ROADMAP A2「多上下文 / Worker 健壮性」= 软挂起恢复边界、transferable 泄漏、worker 错误事件流全覆盖，交付 gtest + 压力。全部落在 test_suspend_gtest.cpp（+4 例）与 test_worker_gtest.cpp（+8 例：6 错误路径 + 2 压力）。
- **软挂起边界语义（测试文档化）**：
  - destroy 已挂起槽位后 `resume(1,'',path)` 合法：状态完全来自 state 文件，恢复后再 suspend 字节一致（restore 真实回写）。
  - 坏 state 路径：文件被删后 resume → C 读盘失败 → TypeError 抛回 JS，主 context 不受影响。
  - **skipped 属性首次 restore 后永久丢失**（设计行为）：不可克隆属性（函数）记入 skipped，restore 无从重建 → 第二次快照 skipped 少该键。可克隆数据完整往返。断言写法：第二次 skipped ⊆ 第一次 + data 字节一致，而非两次快照字节全等。
  - suspend→resume(init) 循环 10 轮：rebuild 先 eval init 再 restore；restore 只写回捕获键、不删 init 新键 → init 写独有键可跨轮保留，槽位稳定复用，主 context 全程可用。
- **transferable 错误路径**：transfer 列表重复对象 / 非 transferable（普通对象）→ DataCloneError 且无副作用（buffer 不 detach、消息不发）；已 detach 的 ArrayBuffer 再进 transfer 列表 → DataCloneError（本次修复，见下）。
- **Worker 生命周期错误路径**：new Worker('http://…') → Error（loadScript 校验）、new Worker('file://不存在') → TypeError（fsReadSync 透传）；失败构造不占槽位，后续 spawn 正常。terminate 后 postMessage/重复 terminate 静默安全。父侧 onmessage handler 抛错 → reportError，worker 存活。跨线程回显有竞态：'a' 的回显可能恰在父侧替换 handler 后到达——断言目标消息到达即证存活，不做条数/顺序假设。
- **timer 回调异步错误进错误事件流（本次修复）**：timers.js 的 setTimeout/setInterval catch 原本只 console.error → worker 内 self.onerror 收不到异步异常。修复：catch 后调 `reportError(err)`（typeof 守卫，navigator.js 晚于 timers 挂载）再打日志。worker.c 顶层 throw 路径（ErrorEvent + {type:'error'} 父通知）不变。
- **detached ArrayBuffer transfer 校验（本次修复）**：structured-clone.js 两处校验（structuredClone options.transfer、serializeToBytes transfer）补 `t.detached` 检查 → `DOMException('ArrayBuffer has already been detached', 'DataCloneError')`。
- **压力**：4 worker × 20 消息洪泛（80 回显集合全等）；30 轮 ArrayBuffer transfer 往返链（父发→detach 校验→worker echo→内容逐轮校验）。
- **验证**：test_suspend_gtest 6/6、test_worker_gtest 23/23、全量 offline ctest 14/14（test_compress_gtest Deflate 用例偶发 flaky：负载尖峰下超边界，复跑即绿，与本改动无关）。


## Timeline

- time: 2026-08-28T15:53:09
  kind: decision
  summary: "Created this page: A2 多上下文/Worker 健壮性（软挂起边界 + transferable 错误路径 + 错误事件流 + 压力）"
  source: 2026-08-28 A2 session
  affects: [a2-worker-robustness]

- time: 2026-08-28T15:53:31
  kind: decision
  summary: Rewrote compiled_truth to the new best understanding
  source: 2026-08-28 A2 session
  affects: [a2-worker-robustness]

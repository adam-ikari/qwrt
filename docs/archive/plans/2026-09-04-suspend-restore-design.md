# 挂起/恢复设计 — 缺陷修复、语义对齐、扩展钩子激活

> 状态：实施设计文档（feasibility 已审计，方向已定，本文落定实现细节）。
> 日期：2026-09-04
> 前置：`docs/plans/2026-09-04-suspend-restore-feasibility.md`（现状审计、引擎边界、G1–G7 缺陷分析）。
> 范围：qwrt 运行时（QuickJS-ng 嵌入式）的 context 软挂起/恢复——修复 G1（suspend 不释放内存）/G2（destroy 带 pending job → UAF），激活 G3（扩展 suspend/resume 钩子死代码），明确设计边界（G4 非枚举属性、G5 运行时状态不可序列化），与多进程模型 §14 正交。
> 引用源码：`src/context.c`、`polyfill/src/context.js`、`src/extension.c`、`src/qwrt_internal.h`、`src/qwrt.c`（teardown）、`src/thread.c`（主循环/微任务 flush）、`deps/quickjs-ng/quickjs.{c,h}`（job 队列 API）、`test/test_suspend_gtest.cpp`（现有测试）。

---

## TL;DR（决策摘要）

1. **G2 修复（UAF，必修）**：在 `qwrt_ctx_destroy` 中、`JS_FreeContext` 之前，调用 QuickJS-ng 新增公开 API `JS_DrainPendingJobsForContext(rt->jsrt, ctx->jsctx)` 遍历 `rt->job_list` 删除 `e->ctx == ctx` 的 pending job。补丁走 `deps/quickjs-ng-*.patch` 机制（同 C99 atomics patch），CMake 无条件应用。
2. **G1 修复（内存释放）**：`qwrt_ctx_serialize` 成功写盘后调 `qwrt_ctx_destroy` 释放 JSContext，设 `rt->contexts[ctx_id] = NULL`（槽位空出）；恢复时 `qwrt_ctx_rebuild` 已有 `if (rt->contexts[ctx_id]) qwrt_ctx_destroy(...)` 兜底，复用 `qwrt_ctx_create_at` 重建。`ctx->suspended` 标记不再需要（destroy 后 ctx 结构已 free，槽位 NULL 即挂起态）。
3. **G3 修复（扩展钩子激活）**：`qwrt_ctx_serialize` 写盘前调 `qwrt_ext_suspend_all`，`qwrt_ctx_rebuild` restore 后调 `qwrt_ext_resume_all`。当前所有扩展的 suspend/resume 是 no-op，接线零行为变更，为未来扩展状态保存铺路。
4. **G4/G5（设计边界，不修）**：软挂起只捕获可枚举字符串键全局属性；运行时异步状态（pending Promise、定时器、活跃 I/O）不可序列化。文档标注为设计限制。
5. **G7（localStorage，无需改动）**：靠文件持久化 + `_pristine` 排除，挂起/恢复天然正确。
6. **多进程模型正交**：context serialize/rebuild 在 THREAD 基线交付（M-R2），不依赖进程模型（M-P\*）。G2 修复（drain pending job）在单进程和多进程下同等必要——`rt->job_list` 是 runtime 级。

---

## 1. 问题陈述

### 1.1 现状（Task 5 软挂起）

**挂起**（`qwrt_ctx_serialize`，context.c:366–393）：
1. 调目标 ctx 的 `__qwrt_ctx_capture__`（JS 侧遍历 `Object.keys(globalThis)`，排除 `_pristine`/`_infra`，结构化克隆可枚举全局属性）。
2. 取 ArrayBuffer 字节，同步写盘（`qwrt_write_file`）。
3. **不销毁 JSContext**——ctx 留在 `rt->contexts[]`，内存不释放。

**恢复**（`qwrt_ctx_rebuild`，context.c:395–435）：
1. 若槽位有残留 ctx → `qwrt_ctx_destroy`（兜底）。
2. `qwrt_ctx_create_at` 重建 JSContext + 重注入 polyfill + 初始化扩展。
3. eval `script_ref`（init 脚本）。
4. 读盘 → `__qwrt_ctx_restore__` 写回 globalThis。

### 1.2 缺陷

| 缺陷 | 严重度 | 现状 | 影响 |
|------|--------|------|------|
| **G1** | 中 | `qwrt_ctx_serialize` 只写文件不销毁 ctx | suspend 后内存不释放；注释说"销毁"实不销毁 |
| **G2** | 高（UAF） | `JS_freeContext` 不清 `rt->job_list` 中该 ctx 的 pending job | destroy 带 pending job 的 ctx → 悬垂 `e->ctx` → 下次 `JS_ExecutePendingJob` UAF |
| **G3** | 低 | `qwrt_ext_suspend_all`/`resume_all` 定义但零调用方，全 no-op | 扩展状态不保存/恢复（当前无实际后果） |
| **G4** | 低（设计限制） | `Object.keys` 只捕获可枚举字符串键 | 非枚举/Symbol/getter-throw 全局属性恢复后丢失 |
| **G5** | 中（设计限制） | pending Promise / 定时器 / 活跃 I/O 不可序列化 | 挂起不保存运行时异步状态 |
| **G6** | N/A（硬限制） | QuickJS-ng 无 heap snapshot API | 引擎级完整快照不可行 |
| **G7** | 无 | localStorage 靠文件持久化 + `_pristine` 排除 | 正确设计，无需改动 |

### 1.3 目标

- **G2 修复**：消除 UAF，destroy 安全。
- **G1 修复**：suspend 语义对齐——释放内存，槽位空出。
- **G3 激活**：接线扩展钩子（零行为变更）。
- **G4/G5 标注**：在设计文档和 JS 侧注释中明确边界。
- **不引入新缺陷**：现有 6 个测试场景（roundtrip / unknown_id / fresh_slot / bad_path / skipped / stress）全部不变。

---

## 2. G2 修复 — per-ctx pending job drain（UAF，必修）

### 2.1 根因

QuickJS-ng 的 `JS_EnqueueJob`（quickjs.c:2138–2158）将 job 存入 **runtime 级** `rt->job_list`，存储裸 `e->ctx` 指针（无 `JS_DupContext`）。`JS_FreeContext`（quickjs.c:2676–2752）不清理 `rt->job_list` 中属于该 ctx 的 entry——只有 `JS_FreeRuntime`（quickjs.c:2297–2303）在最后整体清空。

**复现路径**：
1. `qwrtContext.spawn` 子 context。
2. 子 context eval 创建 Promise（`Promise.resolve().then(...)`），但 `.then` 回调未执行。
3. `qwrtContext.suspend(1, path)` 或 `qwrtContext.destroy(1)`。
4. `qwrt_flush_microtasks`（thread.c:59，每轮 `uv_run` 后）→ `JS_ExecutePendingJob` → 访问已释放的 `e->ctx` → **UAF/crash**。

### 2.2 方案：QuickJS-ng 补丁新增公开 API

**API**（`deps/quickjs-ng/quickjs.h` 新增声明，`quickjs.c` 新增实现）：

```c
/* Drain all pending jobs belonging to ctx from rt->job_list.
 * Frees each job's argv and the job entry itself. Returns the number
 * of jobs drained. Safe to call before JS_FreeContext(ctx). */
JS_EXTERN int JS_DrainPendingJobsForContext(JSRuntime *rt, JSContext *ctx);
```

**实现**（quickjs.c，遍历 `rt->job_list` 链表）：
- 遍历 `rt->job_list`，对每个 `JSJobEntry *e`：
  - 若 `e->ctx == ctx`：从链表摘除，`JS_FreeValue(e->ctx, ...)` 释放 argv，`js_free_rt(rt, e)`。
  - 否则跳过。
- 返回删除条数。
- 与 `JS_FreeRuntime` 的 job 清理逻辑保持一致（同样的 argv 释放 + entry 释放），只是加了 `e->ctx == ctx` 过滤。

**补丁交付**：新建 `deps/quickjs-ng-drain-jobs.patch`，CMake `execute_process(patch -p1 ...)` 无条件应用（同 C99 atomics patch 机制，CMakeLists.txt:181–194）。补丁只新增公开 API（`JS_EXTERN` 函数声明 + 实现），不修改现有 `JS_EnqueueJob`/`JS_ExecutePendingJob`/`JS_FreeContext` 行为。

**为什么不选方式 2（全量 flush 后 destroy）**：`JS_ExecutePendingJob` 不区分 ctx——执行会触发**其他 ctx 的 pending job**（副作用不可控），且可能执行被挂起 ctx 的 job 引发新的 UAF。per-ctx drain 精准、无副作用。

### 2.3 调用点

**`qwrt_ctx_destroy`**（context.c:249–280），在 `JS_FreeContext` **之前**插入 drain：

```c
void qwrt_ctx_destroy(qwrt_t *rt, qwrt_ctx_t *ctx)
{
    if (!rt || !ctx) return;
    int context_id = ctx->context_id;

    /* Call extension destroy hooks */
    qwrt_ext_destroy_all(rt, ctx);

    /* Cleanup resources (timers, handles, etc.) */
    qwrt_ctx_cleanup_resources(rt, ctx);

    /* G2: drain pending jobs belonging to this ctx BEFORE freeing it.
     * JS_FreeContext does not touch rt->job_list; without this, any
     * pending job with e->ctx == ctx becomes a dangling pointer, and
     * the next JS_ExecutePendingJob (qwrt_flush_microtasks) triggers UAF. */
    if (ctx->jsctx && rt->jsrt) {
        JS_DrainPendingJobsForContext(rt->jsrt, ctx->jsctx);
    }

    /* Free the JSContext */
    if (ctx->jsctx) {
        JS_FreeContext(ctx->jsctx);
        ctx->jsctx = NULL;
    }

    rt->contexts[context_id] = NULL;
    rt->context_count--;
    if (rt->active_ctx_id == context_id)
        rt->active_ctx_id = -1;
    free(ctx);
}
```

**为什么 drain 在 `qwrt_ext_destroy_all` + `cleanup_resources` 之后**：扩展 destroy hook 和 cleanup 可能排入新的 pending job（如 timer cancel 回调），drain 必须在所有可能入队操作之后。

**`qwrt_thread_teardown`（qwrt.c:260–280）已有全量 drain**：步骤 2 的 `while (JS_ExecutePendingJob > 0)` 在 runtime 关闭时清空所有 job。G2 修复不影响 teardown——teardown 先 join workers、drain queue、abort stream、然后逐 ctx destroy（此时 drain per-ctx），再 free runtime。per-ctx drain + runtime-level drain 不冲突（前者删该 ctx 的，后者删剩余的）。

### 2.4 G2 修复验证

新增测试（test_suspend_gtest.cpp）：
- **`destroy_with_pending_job_no_uaf`**：spawn 子 ctx → eval `Promise.resolve().then(...)` 但不 flush 微任务 → `qwrtContext.destroy(1)` → 主 context `qwrt_flush_microtasks`（通过 eval 触发一轮 uv_run）→ 不崩溃 + 主 ctx 正常。
- **`suspend_with_pending_job_no_uaf`**：同上但用 `suspend` 代替 `destroy`（G1 修复后 suspend 也 destroy ctx）→ 同样不崩溃。

---

## 3. G1 修复 — suspend 释放内存

### 3.1 方案

**`qwrt_ctx_serialize`**（context.c:366–393）成功写盘后调 `qwrt_ctx_destroy`：

```c
int qwrt_ctx_serialize(qwrt_t *rt, int ctx_id, const char *state_path)
{
    // ... 现有：参数检查、capture、写盘 ...
    if (rc != QWRT_OK) {
        JS_FreeValue(ctx, bytes);
        return rc;   // 写盘失败不销毁 ctx，状态保留
    }
    JS_FreeValue(ctx, bytes);

    /* G1: 写盘成功后销毁 JSContext，释放内存。槽位由 qwrt_ctx_destroy
     * 置 NULL，后续 qwrt_ctx_rebuild 在该槽位重建。 */
    qwrt_ctx_destroy(rt, cctx);
    return QWRT_OK;
}
```

**关键不变量**：
- 写盘**失败**不销毁 ctx（状态保留，用户可重试或 destroy）。
- 写盘**成功**后 ctx 结构已 free，`rt->contexts[ctx_id]` = NULL。
- `qwrt_ctx_rebuild` 已有 `if (rt->contexts[ctx_id]) qwrt_ctx_destroy(...)` 兜底（context.c:402–404），对空槽位是 no-op，无需改动。
- `qwrt_get_ctx_by_id` 对空槽返回 NULL → 后续操作自然报错（`QWRT_ERR_NOT_FOUND`）。

### 3.2 `ctx->suspended` 字段处理

`qwrt_internal.h:153` 的 `int suspended` 字段在 G1 修复后**不再使用**（destroy 后 ctx 结构已 free，槽位 NULL 即"挂起"态）。

**决定**：保留字段不删（ABI 兼容，结构体仍 calloc 初始化为 0），但在 `qwrt_ctx_create_at` 中注释移除"1 if suspended"含义。`qwrt_ctx_create_at` 已经 `ctx->suspended = 0`（context.c:118），无变更需要。

### 3.3 JS 侧注释对齐

`polyfill/src/context.js` 顶部注释说"C 销毁该 JSContext"——G1 修复后与实现对齐。注释无需改动（它描述的就是目标行为，实现现在追上了）。

`bridge.c` 的 `pal.contextSuspend` 注释（bridge.c:1691 区域）说"序列化 + 销毁 ctx"——同样对齐。

### 3.4 G1 修复验证

现有测试 `child_state_roundtrip` 和 `resume_into_fresh_slot` 已覆盖核心路径：
- `child_state_roundtrip`：suspend → resume → suspend 字节一致。G1 修复后 suspend 真销毁 ctx，resume 重建——字节级一致仍成立（capture/restore 逻辑不变）。
- `resume_into_fresh_slot`：先 destroy 再 resume——与 G1 后的 suspend→resume 路径一致（都是空槽位重建）。

**新增**：`suspend_releases_memory`（ASan 构建）：suspend 后 `qwrt_get_ctx_by_id(rt, id)` 返回 NULL（槽位空），主 ctx 正常。这验证 G1 的核心不变量。

---

## 4. G3 修复 — 扩展钩子激活

### 4.1 现状

`qwrt_ext_suspend_all`/`qwrt_ext_resume_all`（extension.c:66–101）已实现，遍历 `ctx->extensions[]` 调各扩展的 `suspend`/`resume` 函数指针。但：
- **零调用方**：grep 全库无调用点。
- 所有扩展的 suspend/resume 实现都是 no-op（`return 0;`）。

### 4.2 方案

**接线**（context.c，两处）：

1. **`qwrt_ctx_serialize`**：在 capture **之前**调 `qwrt_ext_suspend_all(rt, cctx)`——让扩展在 ctx 还活着时保存状态（如 crypto DRBG 种子、wasm 引用计数）到自身存储（per-runtime 或 per-extension struct）。

2. **`qwrt_ctx_rebuild`**：在 `__qwrt_ctx_restore__` **之后**调 `qwrt_ext_resume_all(rt, cctx)`——让扩展从保存的状态恢复。

```c
// qwrt_ctx_serialize, before capture:
qwrt_ext_suspend_all(rt, cctx);   // G3: 让扩展保存状态（当前全 no-op）
// ... existing capture + write ...
qwrt_ctx_destroy(rt, cctx);      // G1: 释放

// qwrt_ctx_rebuild, after restore:
// ... existing create_at + eval + restore_bytes ...
qwrt_ext_resume_all(rt, cctx);   // G3: 让扩展恢复状态（当前全 no-op）
```

### 4.3 为什么当前 no-op 也接线

1. **消除死代码路径**：`qwrt_ext_suspend_all`/`resume_all` 从"定义但零调用"变为"调用但 no-op"——未来扩展按需实现真 suspend/resume 时，调用点已就位。
2. **零行为变更**：所有现有扩展的 suspend/resume 都是 `return 0`，接线不改变任何运行时行为。
3. **零测试影响**：现有测试不涉及扩展状态，接线后测试不变。

### 4.4 扩展状态归属

当前扩展状态是 **per-JSRuntime** 而非 per-JSContext（如 `rt->ec_drbg`，qwrt_internal.h:281–288）。G1 修复只销毁 JSContext（JSRuntime 存活），所以 per-runtime 扩展状态在 suspend/restore 期间天然存活。per-context 的扩展注册（JSClass、global API）由 `qwrt_ctx_create_at` 的 `qwrt_ext_init_all` 重建——这是现有行为，G3 接线不改变它。

---

## 5. 设计边界（G4/G5/G6 — 不修，文档标注）

### 5.1 G4：非枚举全局属性

`__qwrt_ctx_capture__` 用 `Object.keys(globalThis)` 只捕获**可枚举字符串键**。以下不被捕获：
- `Object.defineProperty(globalThis, 'x', {enumerable: false})` 定义的非枚举属性。
- `Symbol` key 属性。
- getter 抛异常的属性（`try { v = globalThis[n] } catch { continue }`）。

**决定**：不扩展为 `Object.getOwnPropertyNames`（含非枚举）。polyfill 基础设施由 `qwrt_ctx_create_at` 重新注入（`_pristine` 排除 + rebuild 重 eval），用户非枚举全局是罕见模式，YAGNI。在 `context.js` 注释中标注此限制。

### 5.2 G5：运行时异步状态不可序列化

软挂起**不捕获**以下运行时状态：
- **Pending Promise 反应**：`rt->job_list` 中的 `.then` 回调（函数不可克隆）。
- **定时器**：`ctx->handles[]`/`timer_resolves[]`/`timer_cbds[]`（C 层 libuv timer + JSValue resolve/reject）。
- **活跃 I/O**：`rt->loop` 上的 libuv handle（TCP listener、fetch stream），per-runtime 而非 per-context。

**G2 修复使此边界更安全**：suspend（含 G1 destroy）前，`qwrt_ctx_destroy` 的 `qwrt_ctx_cleanup_resources` 取消该 ctx 的所有定时器；`JS_DrainPendingJobsForContext` 清除该 ctx 的 pending job。但活跃 I/O（per-runtime）不在 context 级别处理范围——这与多进程模型 §14.2 正交铁则一致（context 挂起不波及 worker/I/O，I/O 归 runtime 生命周期）。

**用户契约**：挂起前应 drain async（等 pending Promise resolve、等 fetch 完成）。这与浏览器 Service Worker eviction 语义一致——不保证运行中异步操作的状态保存。

### 5.3 G6：无引擎级快照

QuickJS-ng 不提供 `JS_SnapshotRuntime`/`JS_DumpHeap`。`JS_WriteObject` 是 per-value，不支持 `JS_CLASS_USER`、函数闭包 upvalue、module 状态。自研完整快照需遍历 `gc_obj_list`（内部字段）+ 序列化闭包 + 重建 atom/class registry——不可行。

**结论**：软挂起（可枚举全局状态 + 重建 JSContext + 重 eval + 回写）是 QuickJS-ng 上的正确做法。进程级隔离由多进程模型 M-P\* 的 `exec` + 进程边界承担。

---

## 6. 挂起/恢复契约与状态机

### 6.1 Context 生命周期状态

```
                 spawn / create_at
    [empty] ──────────────────────→ [active]
      ↑                                │
      │ destroy                        │ serialize (G1: capture → write → destroy)
      │ (G2: drain)                    │
      ↓                                ↓
    [empty] ←──────── destroy ──── [empty]   (suspend 后槽位 NULL)
                                (G2: drain)

    rebuild: [empty] → create_at → eval(script) → restore_bytes → [active]
```

**状态**：
- **active**：`rt->contexts[id] != NULL`，JSContext 存活，可 eval/suspend/destroy。
- **empty**：`rt->contexts[id] == NULL`。suspend 后（G1）或 destroy 后进入此态。rebuild 从此态重建。

**不变量**：
- `qwrt_ctx_serialize` 成功后 `rt->contexts[id] == NULL`（G1）。
- `qwrt_ctx_rebuild` 对 empty 槽位 = `create_at` + eval + restore；对 occupied 槽位 = 先 destroy 再重建（已有兜底）。
- `qwrt_ctx_destroy` 安全地处理有 pending job 的 ctx（G2 drain）。
- 活跃 ctx 不可 suspend/destroy（`ctx_id == rt->active_ctx_id` 返回 `QWRT_ERR_BUSY`，context.c:371/440）。

### 6.2 操作语义

| 操作 | 前置 | 动作 | 后置 |
|------|------|------|------|
| `spawn(script)` | 空槽位 | `create` → eval script | active |
| `suspend(id, path)` | active, id ≠ active_ctx | `ext_suspend` → capture → write → **destroy** (G1) | empty, file 有状态 |
| `resume(id, script, path)` | empty（或 occupied，兜底 destroy） | `create_at` → eval script → read → `restore_bytes` → `ext_resume` (G3) | active |
| `destroy(id)` | active, id ≠ active_ctx | `ext_destroy` → `cleanup` → **drain** (G2) → `free_ctx` | empty |

### 6.3 与现有 `qwrt_ctx_serialize`/`rebuild`/`destroy` 的差异

| 函数 | 现状 | 修复后 |
|------|------|--------|
| `qwrt_ctx_serialize` | capture → write（不 destroy） | `ext_suspend` → capture → write → **destroy**（G1+G3） |
| `qwrt_ctx_rebuild` | destroy 兜底 → create_at → eval → restore | + **`ext_resume`** after restore（G3） |
| `qwrt_ctx_destroy` | ext_destroy → cleanup → free_ctx | + **drain pending jobs** before free_ctx（G2） |

---

## 7. 与多进程模型的关系

### 7.1 正交性

多进程模型 §14（M-R2）将 context 作为组合原语：
> context.c：`qwrt_ctx_spawn/suspend/resume/serialize/rebuild`（复用，零新码）

§14.2 正交铁则：worker 归 rt 不归 context；context 挂起/销毁**不**波及 worker——worker 入队消息照常派发，worker 生命周期只随 rt。

**G1/G2/G3 修复不改变这一正交性**：
- G1（destroy after serialize）只影响 `rt->contexts[]`，不碰 `rt->workers[]`。
- G2（drain pending job）是 runtime 级 job_list 的 per-ctx 过滤，与 worker 无关。
- G3（ext hooks）是 per-ctx 的，与 worker 无关。

### 7.2 多进程下的 G2

若 `worker_backend=PROCESS`（M-P1 后），主 RT 进程的 `qwrt_thread_teardown` 步骤 2（全量 `JS_ExecutePendingJob` drain）和步骤 4（per-ctx `qwrt_ctx_destroy`）仍按序执行。G2 的 per-ctx drain 在步骤 4 内、每个 `JS_freeContext` 前调用——单进程和多进程下同等必要。

### 7.3 组合测试（M-R2 验证门）

多进程模型 §14.3 的验证门："主 rt spawn 2 context + 2 thread worker → ctx suspend/resume 与 worker postMessage 交错无死锁；ctx destroy 后 worker 消息照常派发"——G1/G2 修复后，ctx destroy（含 suspend 触发的 destroy）安全地 drain pending job，不影响 worker 消息派发。

---

## 8. 测试计划

### 8.1 现有测试（不变）

6 个场景全部保持通过（G1/G2/G3 修复不改变 capture/restore/rebuild 的 JS 可观察行为）：
- `child_state_roundtrip`：suspend → resume → suspend 字节一致。
- `unknown_ctx_id`：不存在的 context id 报错。
- `resume_into_fresh_slot`：destroy → resume 回同槽。
- `resume_bad_state_path`：坏 state 文件报错。
- `skipped_uncloneable_stable`：函数记入 skipped，restore 后丢失。
- `stress_cycle_10_rounds`：10 轮 suspend/resume 无损。

### 8.2 新增测试

| 测试 | 验证 | 预期 |
|------|------|------|
| `destroy_with_pending_job_no_uaf` | G2 | spawn → eval Promise.then → destroy（不 flush 微任务）→ flush → 不崩溃，主 ctx 正常 |
| `suspend_with_pending_job_no_uaf` | G1+G2 | spawn → eval Promise.then → suspend（不 flush）→ flush → 不崩溃，resume 后状态完整 |
| `suspend_releases_slot` | G1 | suspend 后 `qwrt_get_ctx_by_id` 返回 NULL（槽位空出）；resume 回同 id 成功 |
| `suspend_then_double_suspend` | G1 | suspend（destroy）→ 再 suspend 同 id → `QWRT_ERR_NOT_FOUND`（槽位已空） |
| `ext_hooks_called_on_suspend_resume` | G3 | （需扩展支持或 mock）验证 `qwrt_ext_suspend_all`/`resume_all` 被调用 |

### 8.3 测试实现要点

- **UAF 测试**（G2）：关键是在 destroy/suspend 后**不手动 flush**，而是让主循环的 `qwrt_flush_microtasks`（thread.c:59）自然触发。测试通过 `host_eval` 在主 ctx 触发一轮 `uv_run`（eval 本身不触发微任务 flush——需在子 ctx eval Promise.then 后立即 destroy/suspend，然后 eval 主 ctx 代码触发 uv_run 一轮）。
- **`suspend_releases_slot`**：直接检查 `qwrt_get_ctx_by_id(rt, 1) == NULL`——需通过 C 层断言或 JS 侧间接验证（suspend 后 `qwrtContext.suspend(1, ...)` 再调应抛 NOT_FOUND，因为槽位已空）。
- **`ext_hooks_called`**：当前扩展全 no-op，无法直接验证调用。方案：(a) 在测试 build 中定义一个 test-only 扩展，其 suspend/resume 设 flag；或 (b) 验证 `qwrt_ext_suspend_all`/`resume_all` 的调用路径（C 单元测试直接调）。倾向 (b)——C 单元测试更直接。

---

## 9. 实施计划

### 阶段 1：G2 修复（UAF，阻塞所有其他工作）

1. **QuickJS-ng 补丁**：新建 `deps/quickjs-ng-drain-jobs.patch`，新增 `JS_DrainPendingJobsForContext` 公开 API。
   - `quickjs.h`：声明 `JS_EXTERN int JS_DrainPendingJobsForContext(JSRuntime *rt, JSContext *ctx);`
   - `quickjs.c`：实现——遍历 `rt->job_list`，删除 `e->ctx == ctx` 的 entry，free argv + entry，返回计数。
2. **CMake 集成**：在 `CMakeLists.txt` 中无条件 apply patch（同 C99 atomics patch 模式）。
3. **`qwrt_ctx_destroy` 调用**：在 `JS_freeContext` 前调 `JS_DrainPendingJobsForContext`。
4. **测试**：`destroy_with_pending_job_no_uaf` + `suspend_with_pending_job_no_uaf`。

### 阶段 2：G1 修复（内存释放）

1. **`qwrt_ctx_serialize`**：写盘成功后调 `qwrt_ctx_destroy`。
2. **验证**：现有 `child_state_roundtrip`/`resume_into_fresh_slot` 不退化 + 新增 `suspend_releases_slot`/`suspend_then_double_suspend`。

### 阶段 3：G3 修复（扩展钩子激活）

1. **`qwrt_ctx_serialize`**：capture 前调 `qwrt_ext_suspend_all`。
2. **`qwrt_ctx_rebuild`**：restore 后调 `qwrt_ext_resume_all`。
3. **验证**：C 单元测试验证调用路径（或 test-only 扩展设 flag）。

### 阶段 4：注释/文档对齐

1. `context.js` 顶部注释确认与实现对齐（G1 后实现追上注释描述）。
2. `bridge.c` `pal.contextSuspend`/`contextResume` 注释对齐。
3. G4 设计限制在 `context.js` capture 函数注释中标注。
4. 更新 CHANGELOG（如有）。

---

## 10. 开放决策（已裁决）

| # | 决策 | 裁决 | 理由 |
|---|------|------|------|
| 1 | G2 修复方式 | **方式 1**：QuickJS-ng 补丁 `JS_DrainPendingJobsForContext`，per-ctx drain | 精准、无副作用、与现有补丁机制一致；方式 2（全量 flush）执行其他 ctx 的 job，副作用不可控 |
| 2 | G1 修复 | **修复**：suspend 后 destroy ctx 释放内存 | "挂起"语义应释放资源；现有测试不受影响（roundtrip 不依赖 suspend 后 ctx 存活） |
| 3 | 扩展钩子接线（G3） | **接线**：suspend 前 `ext_suspend_all`，resume 后 `ext_resume_all` | 消除死代码路径；当前全 no-op，零行为变更；为未来扩展状态保存铺路 |
| 4 | 异步状态前置检查 | **不强制**（文档标注 G5 边界） | G2 drain 已消除 UAF；强制 `QWRT_ERR_BUSY` 会破坏现有 suspend 立即调用的用法；用户应在 drain async 后 suspend |
| 5 | 非枚举属性捕获（G4） | **不扩展** | polyfill 由 rebuild 重新注入；用户非枚举全局罕见；YAGNI |
| 6 | `ctx->suspended` 字段 | **保留不删，不再使用** | ABI 兼容；槽位 NULL 即挂起态；`create_at` 已 init 0 |

---

## 11. 证据索引

| 断言 | 证据位置 |
|------|----------|
| `qwrt_ctx_serialize` 不销毁 ctx（G1 现状） | `src/context.c:366–393`（只写文件，无 `qwrt_ctx_destroy` 调用） |
| `ctx->suspended` 从未设 1 | `src/context.c:118`（仅 init 0）；grep `suspended` 全库仅此一处赋值 |
| `qwrt_ctx_rebuild` 有 destroy 兜底 | `src/context.c:402–404` |
| `JS_EnqueueJob` 无 refcount（G2 根因） | `deps/quickjs-ng/quickjs.c:2150`（`e->ctx = ctx;` 无 Dup） |
| `JS_FreeContext` 不清 job_list | `deps/quickjs-ng/quickjs.c:2676–2752`（无 job_list 操作） |
| `JS_FreeRuntime` 才清 job_list | `deps/quickjs-ng/quickjs.c:2297–2303` |
| `qwrt_flush_microtasks` 每轮执行 job | `src/thread.c:59–60`（`JS_IsJobPending` busy 判定） |
| `qwrt_thread_teardown` 全量 drain | `src/qwrt.c:263–267`（`while JS_ExecutePendingJob > 0`） |
| 扩展 suspend/resume 无调用方（G3） | grep `qwrt_ext_suspend_all`/`resume_all` → 仅定义+声明，零调用 |
| 扩展 suspend/resume 全 no-op | `src/ext_crypto.c:1345`、`ext_wasm3.c:1970`、`ext_textcodec.c:292`、`ext_wamr.c:1821`、`ext_web_wasm.c:84` |
| `qwrt_ext_suspend_all` 签名 | `src/qwrt_internal.h:392`：`int qwrt_ext_suspend_all(qwrt_t *rt, qwrt_ctx_t *ctx)` |
| capture 只取枚举键（G4） | `polyfill/src/context.js:30–44`（`Object.keys(globalThis)` + `_pristine` 排除） |
| localStorage 靠文件持久化（G7） | `polyfill/src/local-storage.js:56–58`（`persist` → `pal.fsWriteSync` 原子写）、`:145`（setup `load()` 读回） |
| 多进程模型 §14 context 复用 | `docs/plans/2026-09-04-multi-process-model.md:558`（"复用，零新码"） |
| QuickJS-ng 补丁机制 | `CMakeLists.txt:181–194`（`execute_process(patch -p1 ...)` 无条件应用） |
| `QWRT_MAX_CONTEXTS` | `src/qwrt_internal.h:29`（64） |
| 测试覆盖 6 场景 | `test/test_suspend_gtest.cpp`（roundtrip / unknown_id / fresh_slot / bad_path / skipped / stress） |

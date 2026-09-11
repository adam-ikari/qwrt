# 挂起/恢复可行性分析 — 现状审计、引擎边界、增强路径

> 状态：可行性分析文档（调研结论，非实施计划）。为后续决策提供证据基础。
> 日期：2026-09-04
> 范围：qwrt 运行时（QuickJS-ng 嵌入式）的 context 挂起/恢复（suspend/resume）与运行时级快照（snapshot/checkpoint）能力。评估从「当前 Task 5 软挂起」到「完整运行时快照」之间的可行性光谱，识别引擎硬限制、现状缺陷、以及与多进程模型（§14）的组合关系。
> 附件/引用：`polyfill/src/context.js`（JS 捕获/恢复）、`src/context.c`（C 序列化/重建/销毁）、`src/bridge.c`（PAL 桥接）、`src/extension.c`（扩展钩子）、`polyfill/src/structured-clone.js`（序列化内核）、`deps/quickjs-ng/quickjs.c`（引擎边界）、`deps/quickjs-ng/quickjs.h`（API 面）、`test/test_suspend_gtest.cpp`（现有测试覆盖）、`docs/plans/2026-09-04-multi-process-model.md`（多进程组合模型 §13/§14）。

---

## TL;DR（核心结论）

1. **现有 Task 5 软挂起可用但受限**：捕获可枚举全局属性 → 结构化克隆字节 → 写盘；恢复在原槽位重建 JSContext + 重 eval + 回写状态。**不捕获非可枚举/不可克隆状态**（pending Promise、定时器、活跃 I/O、闭包、非枚举属性），也不释放内存。
2. **存在两个真实缺陷**：G1（suspend 注释说销毁 JSContext 实际未销毁，内存不释放）+ G2（销毁带 pending job 的 context → `rt->job_list` 悬垂指针 → UAF）。
3. **扩展 suspend/resume 钩子是死代码**：`qwrt_ext_t.suspend/resume` 已定义、全 no-op、无任何调用方——挂起/恢复不触发扩展状态保存/恢复。
4. **完整运行时快照（引擎级）不可行**：QuickJS-ng 无 heap snapshot API（`JS_WriteObject` 是 per-value，非 per-runtime；无 `JS_DumpRuntime`/`JS_SnapshotRuntime`）。WAMR 的 `wasm_module_serialize` 是模块级 AOT 缓存，不是运行时状态快照。进程级 CRIU/`fork()` 方案在多进程模型文档中已否决（JSRuntime 已初始化状态 fork 危险）。
5. **推荐路径**：修 G1/G2（correctness fix），增强 suspend 为真释放内存（destroy ctx after serialize），补 pending job drain，激活扩展钩子调用点；不做引擎级快照。与多进程模型 §14 的 context 组合原语天然兼容（context serialize/rebuild 复用，零新码）。

---

## 1. 现状审计

### 1.1 架构概览（Task 5 软挂起）

**JS 侧**（`polyfill/src/context.js`）：

- `__qwrt_ctx_capture__`：遍历 `Object.keys(globalThis)`，排除 `_pristine`（polyfill setup 完成后、本模块挂载前的全局键快照）和 `_infra`（`__qwrt_ctx_capture__`/`__qwrt_ctx_restore__`/`qwrtContext`），对每个剩余键用 `__qwrt_serialize__`（structured-clone 内核）序列化，不可克隆的记入 `skipped` 数组，返回 `{props, skipped}` 的字节。
- `__qwrt_ctx_restore__`：反序列化字节 → 遍历 `props`，用 `Object.defineProperty` 写回 `globalThis`（防 `__proto__` 污染），返回 `skipped` 列表。
- `qwrtContext.spawn/suspend/resume/destroy`：经 `pal.contextSpawn/contextSuspend/contextResume/contextDestroy` 驱动 C 层。

**C 侧**（`src/context.c` + `src/bridge.c`）：

- `qwrt_ctx_spawn`（context.c:343）：创建新 context（`qwrt_ctx_create` 找空槽），eval init script。
- `qwrt_ctx_serialize`（context.c:366）：调用目标 ctx 的 `__qwrt_ctx_capture__` → 取 ArrayBuffer → **同步写盘**（`qwrt_write_file`，fopen/fwrite/fclose）。**不销毁 JSContext。**
- `qwrt_ctx_rebuild`（context.c:395）：若槽位有残留 ctx → `qwrt_ctx_destroy` → `qwrt_ctx_create_at`（新 JSContext + 重新注入 polyfill + 初始化扩展）→ eval `script_ref` → 读盘 → `__qwrt_ctx_restore__` 写回 globalThis。
- `qwrt_ctx_destroy`（context.c:249）：`qwrt_ext_destroy_all` → `qwrt_ctx_cleanup_resources`（取消定时器）→ `JS_FreeContext` → 从 `rt->contexts[]` 摘除 → `free(ctx)`。

**桥接**（`bridge.c:1675-1743`）：`pal.contextSuspend` 调 `qwrt_ctx_serialize`；`pal.contextResume` 调 `qwrt_ctx_rebuild`；`pal.contextDestroy` 调 `qwrt_ctx_destroy_id`。

### 1.2 序列化内核覆盖面（`__qwrt_serialize__` / structured-clone.js）

`__qwrt_serialize__` 是 Worker postMessage 共用的序列化内核，支持：

| 类型 | 支持 | 备注 |
|------|------|------|
| null / undefined / boolean / number / string | ✅ | |
| Array / plain Object | ✅ | 循环引用（refs 数组） |
| ArrayBuffer / DataView | ✅ | |
| TypedArray（全部 9 种） | ✅ | Int8..Float64 + BigInt64/BigUint64 |
| Blob / File | ✅ | |
| Error（含子类型） | ✅ | name/message/stack |
| Map / Set | ✅ | |
| Date / RegExp / BigInt | ✅ | |
| MessagePort 引用 | ✅ | `__qwrt_port_from_ref__` 重建 |
| **函数** | ❌ | DataCloneError |
| **Symbol** | ❌ | DataCloneError（非全局 Symbol） |
| **非枚举属性** | ❌ | `Object.keys` 不遍历 |
| **pending Promise 反应** | ❌ | 微任务队列在 JSRuntime 层 |
| **定时器 / 活跃 I/O** | ❌ | libuv handle 在 C 层 |
| **闭包捕获的变量** | ❌ | 函数不可克隆 |
| **自定义原型对象** | ❌ | 原型非 `Object.prototype`/`null` → DataCloneError（structured-clone.js:476-479） |

### 1.3 测试覆盖（test/test_suspend_gtest.cpp）

6 个测试覆盖：

1. `child_state_roundtrip`：spawn → suspend(a.bin) → resume('', a.bin) → suspend(b.bin) → 字节级 a == b。验证往返一致性。
2. `unknown_ctx_id`：不存在的 context id 挂起 → 报错。
3. `resume_into_fresh_slot`：destroy 后 resume 回同槽，状态完全来自 state 文件。
4. `resume_bad_state_path`：suspend 后删文件 → resume → 报错，主 context 不受影响。
5. `skipped_uncloneable_stable`：含函数的 ctx → 函数记入 skipped，首次 restore 后永久丢失；可克隆状态完整往返。
6. `stress_cycle_10_rounds`：suspend → resume(init) 循环 10 轮，init 新键保留 + 捕获状态每轮无损。

**未覆盖**：pending Promise 的 ctx 挂起、有活跃定时器的 ctx 挂起、有 in-flight I/O 的 ctx 挂起、扩展状态（crypto/wasm/codec）的挂起恢复。

---

## 2. 引擎边界（QuickJS-ng）

### 2.1 JS_WriteObject / JS_ReadObject（字节码序列化）

`JS_WriteObject`（quickjs.h:1220）是 QuickJS-ng 的 per-value 序列化器，**不是 runtime snapshot**：

- 支持：null/undefined/bool/int/float/string/ArrayBuffer/TypedArray/SharedArrayBuffer/RegExp/Date/Map/Set/BigInt/Symbol(全局)/Object 引用/bytecode(function/module)。
- **不支持**：`JS_CLASS_USER`（宿主自定义类，line 38342-38348 `default → unsupported object class`）。
- 有 `JS_WRITE_OBJ_REFERENCE` 标志允许对象图内引用（循环引用），但仍是 per-value。
- SECURITY.md 明确：**字节码格式不抗恶意输入**，`JS_ReadObject` 加载不可信字节码等同执行不可信原生代码。

**结论**：`JS_WriteObject` 可序列化单个值/对象图，但不能快照整个 JSRuntime（gc_obj_list、atom table、class 注册、module 状态、job 队列、pending Promise 反应）。

### 2.2 JS_FreeContext 与 pending job 队列

**关键发现**：`JS_EnqueueJob`（quickjs.c:2138-2158）将 job 存入 `rt->job_list`（**runtime 级**链表），存储裸 `e->ctx` 指针，**无 refcount**（line 2150: `e->ctx = ctx;` 无 `JS_DupContext`）。

`JS_FreeContext`（quickjs.c:2676-2752）**不清理 `rt->job_list` 中属于该 ctx 的 job**——只释放 ctx 自身资源（modules、global、protos、shapes），从 `rt->gc_obj_list` 摘除，`js_free_rt` 释放。

`JS_FreeRuntime`（quickjs.c:2297-2303）才在最后清空整个 `rt->job_list`（遍历 free 所有 job 的 argv + 重置链表头）。

**后果**：在 runtime 仍在运行时 `JS_FreeContext(ctx)` 一个有 pending job 的 context → `rt->job_list` 中残留 `e->ctx = 已释放的 ctx` → 下次 `JS_ExecutePendingJob(rt->jsrt, &job_ctx)`（在 `qwrt_flush_microtasks` 中每轮主循环调用）→ **UAF（use-after-free）**。

`qwrt_thread_teardown`（qwrt.c:263-267）在销毁 contexts **之前**排空所有 pending job，所以 **runtime 整体销毁路径安全**。但 **单 context destroy（suspend/destroy 路径）不在 teardown 上下文中**，没有这个保护。

### 2.3 ctx->suspended 字段

`qwrt_internal.h:153` 定义 `int suspended`，在 `qwrt_ctx_create_at` 中初始化为 0（context.c:118），**全代码库无任何位置将其设为 1**。死字段。

### 2.4 无 heap snapshot API

QuickJS-ng 无 `JS_DumpHeap`/`JS_SnapshotRuntime`/`JS_WriteRuntime` API。`JS_DUMP_*` 宏（quickjs.h:474-480）是调试 dump（打印 leaked objects），不是可恢复的快照。`js_dump_function_bytecode`（quickjs.c:32671）是调试用途的 bytecode dump。

WAMR 的 `wasm_module_serialize`（wasm_c_api.h:559）是 AOT 模块缓存（跳过编译），不是运行时状态快照——只序列化已编译的 module bytes，不含实例内存/栈/全局状态。

---

## 3. 缺陷分析（G1–G7）

### G1：suspend 不释放内存（注释与实现不符）

**现状**：`context.js` 注释说"挂起 = ... 然后 C 销毁该 JSContext"；`bridge.c:1691` 注释说"序列化全局克隆字节写盘 + 销毁 ctx"。但 `qwrt_ctx_serialize`（context.c:366-393）**只写文件，不调 `qwrt_ctx_destroy`**。JSContext 留在 `rt->contexts[]` 中，内存不释放。

**影响**：suspend 后内存占用不减。如果"挂起"的语义预期是"暂停并释放资源"（类比浏览器 tab discard / Service Worker eviction），当前实现不满足。用户须显式调 `qwrtContext.destroy(id)` 才释放。

**严重度**：中（功能正确但语义/文档不符；内存不释放是资源浪费，非崩溃）。

### G2：销毁带 pending job 的 context → UAF

**现状**：`qwrt_ctx_destroy` → `JS_FreeContext` 不清理 `rt->job_list` 中属于该 ctx 的 pending job。若被销毁的 ctx 有 pending Promise 反应（`.then` callback 未执行），`rt->job_list` 中残留 `e->ctx = freed_ptr`。下次 `qwrt_flush_microtasks`（thread.c:21-27，每轮 `uv_run` 后调用）执行 `JS_ExecutePendingJob` → **UAF**。

**复现路径**：
1. `qwrtContext.spawn` 子 context。
2. 子 context eval 脚本创建 Promise，`Promise.resolve().then(() => { ... })`，但 `.then` 回调未执行（需在微任务 flush 前挂起）。
3. `qwrtContext.suspend(1, path)` 或 `qwrtContext.destroy(1)`。
4. `qwrt_flush_microtasks` → crash 或内存损坏。

**缓解**：`qwrt_ctx_rebuild` 在 destroy 旧 ctx 前没有 drain pending jobs。`qwrt_ctx_cleanup_resources` 只取消定时器，不排空 job 队列。

**严重度**：高（UAF，安全/稳定性问题）。

### G3：扩展 suspend/resume 钩子是死代码

**现状**：`qwrt_ext_t.suspend`/`resume` 函数指针已定义（qwrt.h:66-67），`qwrt_ext_suspend_all`/`qwrt_ext_resume_all` 已实现（extension.c:66-101），但：

- **无任何调用方**：grep `qwrt_ext_suspend_all`/`qwrt_ext_resume_all` 在 src/ 中只有定义和声明，零调用点。
- 所有扩展的 suspend/resume 实现都是 no-op（`return 0;`）：
  - `crypto_ext_suspend/resume`：`(void)ext; (void)rt; return 0;`
  - `wasm3_ext_suspend/resume`：`(void)ext; (void)rt; return 0;`
  - `textcodec_ext_suspend/resume`：同上
  - `wamr_ext_suspend/resume`：同上
  - `web_wasm_ext_suspend/resume`：同上
  - `compress_ext_suspend/resume`：`= NULL`

**影响**：挂起/恢复不保存/恢复扩展状态（crypto DRBG 种子、wasm 实例、codec 状态）。但鉴于当前扩展状态都是 per-JSRuntime（`rt->ec_drbg` 等，见 qwrt_internal.h:281-288）而非 per-context，且挂起不销毁 JSRuntime，这个缺口目前无实际后果——但若未来 G1 修复为真销毁 JSContext，扩展的 per-JSRuntime 状态仍会存活（JSRuntime 不销毁），只是 per-JSContext 的扩展注册（JSClass、global API）需重建（`qwrt_ctx_create_at` 的 `qwrt_ext_init_all` 已做）。

**严重度**：低（当前无实际影响；是架构预留但未接线）。

### G4：非枚举全局属性丢失

`__qwrt_ctx_capture__` 用 `Object.keys(globalThis)` 只捕获**可枚举字符串键**属性（context.js:30）。三类全局状态被漏：`Object.defineProperty(..., {enumerable: false})` 定义的属性、Symbol-keyed 属性、getter 抛异常的属性（`try { v = globalThis[n] } catch { continue }`，context.js:35）。

**影响**：恢复后这些属性缺失。polyfill 注入的 API（含 `localStorage`，它是 `enumerable: true` 但在 `_pristine` 快照之前挂载，见 G7）由 `_pristine` 排除、恢复时由 `qwrt_ctx_create_at` 重新注入，所以不丢。丢的是用户代码自己用非枚举/Symbol 定义的全局。

**严重度**：低（设计限制，文档可标注；用户可枚举属性覆盖 99% 用例）。

### G5：运行时状态不可序列化

**pending Promise 反应**：微任务队列在 `rt->job_list`（runtime 级），context.js 不捕获。即使捕获也无法序列化（函数不可克隆）。

**定时器**：`ctx->handles[]` / `ctx->timer_resolves[]` / `ctx->timer_cbds[]`（qwrt_internal.h:155-158）是 C 层 libuv timer + JSValue resolve/reject callback。挂起不取消定时器（`qwrt_ctx_serialize` 不调 `qwrt_ctx_cleanup_resources`），但如果 G1 修复为销毁 ctx，则 `qwrt_ctx_destroy` 调 `qwrt_ctx_cleanup_resources` 取消定时器——定时器状态丢失。

**活跃 I/O**：libuv handle（TCP listener、fetch stream、timer）在 `rt->loop` 上，per-runtime 而非 per-context。`rt->active_stream`（qwrt_internal.h:223）是 per-runtime 的活跃 HTTP op 指针。Context 挂起不影响 runtime 的 I/O。

**结论**：挂起只保存 JS 可枚举全局状态；运行时异步状态（pending job、定时器回调、I/O）不可序列化、不可恢复。这是软挂起与完整快照的根本分界。

**严重度**：中（设计限制；用户须在挂起前 drain async / 取消定时器）。

### G6：无引擎级运行时快照

QuickJS-ng 不提供 `JS_SnapshotRuntime`/`JS_DumpHeap` API。`JS_WriteObject` 是 per-value，不支持：
- **函数闭包**（`JS_TAG_FUNCTION_BYTECODE` 可序列化 bytecode 但不含闭包捕获的 upvalue/outer variable 状态）
- **module 实例状态**（已 import 的 module、module registry）
- **JSRuntime 级状态**（atom table、class registry、gc_obj_list、job_list）

自研完整运行时快照需要：
1. 遍历 gc_obj_list 所有对象（QuickJS 内部 API，非公开 stable API）
2. 序列化每个对象的内部状态（闭包 upvalue、property slot、prototype chain）
3. 重建 atom table + class registry + module registry
4. 序列化 pending job 队列（含 job 函数引用——函数本身不可结构化克隆）

**结论**：引擎级完整快照在 QuickJS-ng 上不可行（无 API + 内部结构依赖 + 闭包/job 不可序列化）。WAMR `wasm_module_serialize` 是 AOT 缓存，不是运行时快照。

**严重度**：N/A（硬限制，非缺陷）。

### G7：localStorage 与 suspend 的交互（现状正确，无需改动）

**两层存储要分清**：

1. **Web `localStorage`**（`polyfill/src/local-storage.js`）：数据在 JS 闭包 `map` + 磁盘 JSON 文件（`pal.localStoragePath()`，每次 `setItem`/`removeItem`/`clear` 经 `pal.fsWriteSync` 原子写 temp+rename）。`globalThis.localStorage` 是 `enumerable: true`（local-storage.js:147），但挂载发生在**所有 polyfill setup 之后、`context.js` 的 `_pristine` 快照之前**（context.js:15-18）→ 落在 `_pristine` 内 → 被 capture 排除。恢复时 `qwrt_ctx_create_at` 重注入 polyfill → `setupLocalStorage` 重跑 → `load()` 从文件读回 → **数据天然存活**（真相在磁盘，不在快照）。
2. **`qwrt.storage`（异步扩展 API）**：C 层 `rt->store`（`uv_io_store_entry_t *`，qwrt_internal.h:218-220），per-runtime 而非 per-context，`qwrt_thread_teardown` 步骤 6 释放。context 挂起/销毁完全不触碰它。

**结论**：context 挂起/恢复不需要保存 storage 状态——Web localStorage 靠文件持久化，`qwrt.storage` 归 runtime 生命周期。这是正确设计，非缺口。多进程模型 §10.2 的单所有者代理（storage 归主 RT，操作经 `kind=STORAGE` 信封）与此正交：它解决的是**跨进程并发写同一文件**，不改变「storage 状态不进 context 快照」这一条。

**严重度**：无（设计正确）。

---

## 4. 与多进程模型的组合关系

多进程模型 §14（M-R2）明确将 context 作为组合原语之一：

> | context | context.c：`qwrt_ctx_spawn/suspend/resume/serialize/rebuild`（复用，零新码） | 同一 JSRuntime 堆内多 JSContext；软隔离（可挂起/序列化/重建） |

§14.2 正交铁则：worker 归 rt 不归 context；context 挂起/销毁不波及 worker——其入队消息照常派发，worker 生命周期只随 rt。

**结论**：现有软挂起与多进程模型正交且兼容。context serialize/rebuild 在单进程组合轨道（M-R1/M-R2，THREAD 基线）交付，不依赖进程模型。多进程轨道（M-P*）不改变 context 语义。

**唯一交叉点**：若 G1 修复为真销毁 JSContext（释放内存），而 context 恰好在 `worker_backend=PROCESS` 的主 RT 上——destroy 前需 drain pending job（G2 修复），否则主 RT 的 `qwrt_flush_microtasks` 会访问已释放的 ctx。这在单进程模型下同样成立（job_list 是 runtime 级），与进程模型无额外耦合。

---

## 5. 增强路径与推荐

### 方案 A：修 G1/G2，不改语义（推荐第一步）

**G2 修复**（UAF，必修）：在 `qwrt_ctx_destroy` 中，`JS_FreeContext` **之前**排空属于该 ctx 的 pending job。两种方式：

- **方式 1（精准）**：遍历 `rt->job_list`，删除 `e->ctx == ctx` 的 job（free argv + del link）。需访问 QuickJS 内部 `JSJobEntry` 结构（`list_entry` 遍历，`js_free` 每个 entry）——非公开 API，需在 quickjs.c 补丁或 quickjs.h 新增 `JS_DrainPendingJobsForContext`。
- **方式 2（保守）**：在 `qwrt_ctx_destroy` 前调 `JS_ExecutePendingJob(rt->jsrt, &job_ctx)` 循环**直到清空**（等同 `qwrt_flush_microtasks`），然后 destroy。风险：执行其他 ctx 的 pending job（副作用不可控），但 QuickJS job 队列不区分 ctx。

**推荐方式 1**：新增 QuickJS-ng 补丁函数 `JS_DrainPendingJobsForContext(JSRuntime *rt, JSContext *ctx)`，遍历 `rt->job_list` 删除 `e->ctx == ctx` 的 entry（free argv + entry），返回删除数。`qwrt_ctx_destroy` 在 `JS_FreeContext` 前调用。与 `deps/quickjs-ng` 现有补丁机制一致（项目已对 quickjs-ng 打补丁）。

**G1 修复**（内存释放，可选）：`qwrt_ctx_serialize` 成功写盘后调 `qwrt_ctx_destroy` 释放 JSContext，设 `ctx->suspended = 1` 标记槽位已挂起。`qwrt_ctx_rebuild` 已有 `if (rt->contexts[ctx_id]) qwrt_ctx_destroy(...)` 兜底，无需改动。`ctx->suspended` 字段已有但未使用，激活即可。

**影响面**：
- `context.js` 注释与实现对齐。
- `bridge.c` 注释与实现对齐。
- `test_suspend_gtest.cpp` 现有测试不变（`child_state_roundtrip` 不依赖 suspend 后 ctx 存活；`resume_into_fresh_slot` 已测 destroy → resume 路径）。
- `qwrt_ctx_serialize` 后 `qwrt_get_ctx_by_id` 返回 NULL（ctx 已销毁）——需确认 `pal.contextSuspend` 的 JS 侧不依赖 suspend 后 ctx 存活（context.js 的 `qwrtContext.suspend` 只调 `pal.contextSuspend` 返回 undefined，不查 ctx）。

### 方案 B：增强软挂起（扩展状态 + 异步 drain）

在方案 A 基础上：

1. **激活扩展钩子**：`qwrt_ctx_serialize` 前调 `qwrt_ext_suspend_all`，`qwrt_ctx_rebuild` 后调 `qwrt_ext_resume_all`。当前所有扩展的 suspend/resume 是 no-op，但接线后扩展可按需实现状态保存/恢复（如 crypto DRBG 种子、wasm module 引用）。
2. **异步 drain 前置**：suspend 前要求 ctx 无 pending async work（类似 `qwrt_wait_idle` 但 per-ctx）。`qwrt_ctx_serialize` 检查 `JS_IsJobPending` 且 ctx 无活跃 timer → 否则返回 `QWRT_ERR_BUSY`。避免 G2 的 job 队列问题在源头消除。

**成本**：中等；需扩展实现真 suspend/resume 逻辑（当前 no-op 可保持，仅接线调用点）。

### 方案 C：完整运行时快照（不可行）

自研 QuickJS-ng 运行时快照需要遍历 `gc_obj_list`、序列化闭包 upvalue、重建 atom/class/module registry、序列化 pending job 队列。QuickJS-ng 不暴露这些内部结构（`gc_obj_list` 是 `JSRuntime` 内部字段，非公开 API）。`JS_WriteObject` 可序列化单个值但对 `JS_CLASS_USER` 抛 `unsupported object class`，对函数只序列化 bytecode（不含闭包 upvalue）。

**结论**：不可行。现有软挂起（可枚举全局状态 + 重建 JSContext + 重 eval + 回写）是 QuickJS-ng 上的正确做法。进程级隔离由多进程模型（M-P*）的 `exec` + 进程边界承担，不依赖 context 级快照。

---

## 6. 开放决策点

1. **G2 修复方式**：方式 1（QuickJS-ng 补丁 `JS_DrainPendingJobsForContext`，精准 per-ctx drain）vs 方式 2（全量 flush 后 destroy，副作用是执行其他 ctx 的 job）。**倾向方式 1**——精准、无副作用、与现有补丁机制一致。
2. **G1 修复**：suspend 后销毁 JSContext 释放内存（激活 `ctx->suspended` 标记）vs 保持现状（不释放，文档修正注释）。**倾向修复**——"挂起"语义应释放资源；现有测试不受影响。
3. **扩展钩子接线（方案 B）**：是否在 suspend/resume 路径调 `qwrt_ext_suspend_all`/`qwrt_ext_resume_all`。当前 no-op 但接线后为未来扩展状态保存铺路。**倾向接线**（零成本，消除死代码路径）。
4. **异步状态前置检查**：suspend 前是否强制要求 ctx idle（无 pending job / 无活跃 timer）。**倾向加检查**（返回 `QWRT_ERR_BUSY`），从源头消除 G2 并明确"挂起时机"契约。
5. **非枚举属性捕获**：是否扩展 capture 为 `Object.getOwnPropertyNames(globalThis)`（含非枚举）。**倾向不做**——polyfill 基础设施由 rebuild 重新注入；用户非枚举全局是罕见模式，YAGNI。

---

## 7. 证据索引

| 断言 | 证据位置 |
|------|----------|
| suspend 不销毁 JSContext | `src/context.c:366-393`（`qwrt_ctx_serialize` 只写文件，无 `qwrt_ctx_destroy` 调用） |
| `ctx->suspended` 从未设 1 | `src/context.c:118`（仅 init 0）；grep `suspended` 全库仅此一处赋值 |
| `qwrt_ctx_rebuild` 有 destroy 兜底 | `src/context.c:402-404`（`if (rt->contexts[ctx_id]) qwrt_ctx_destroy(...)`） |
| JS_EnqueueJob 无 refcount | `deps/quickjs-ng/quickjs.c:2150`（`e->ctx = ctx;` 无 Dup） |
| JS_FreeContext 不清 job_list | `deps/quickjs-ng/quickjs.c:2676-2752`（无 job_list 操作） |
| JS_FreeRuntime 才清 job_list | `deps/quickjs-ng/quickjs.c:2297-2303`（遍历 free + init_list_head） |
| qwrt_thread_teardown 先 drain job | `src/qwrt.c:263-267`（`while (JS_ExecutePendingJob > 0)`） |
| qwrt_flush_microtasks 每轮执行 job | `src/thread.c:21-27` + `:60`（`JS_IsJobPending` busy 判定） |
| 扩展 suspend/resume 无调用方 | grep `qwrt_ext_suspend_all`/`qwrt_ext_resume_all` → 仅定义+声明，零调用 |
| 扩展 suspend/resume 全 no-op | `src/ext_crypto.c:1345-1354`、`src/ext_wasm3.c:1970-1983` 等 |
| JS_WriteObject 不支持 JS_CLASS_USER | `deps/quickjs-ng/quickjs.c:38342-38348`（`default → unsupported object class`） |
| 无 heap snapshot API | `deps/quickjs-ng/quickjs.h` 无 `Snapshot`/`DumpHeap`/`WriteRuntime`；`JS_DUMP_*` 是调试 dump |
| WAMR serialize 是 module 级 | `deps/wamr/core/iwasm/include/wasm_c_api.h:559-560`（`wasm_module_serialize`） |
| 序列化内核覆盖面 | `polyfill/src/structured-clone.js:250-653`（tag dispatch，函数/symbol → DataCloneError） |
| capture 只取枚举键 | `polyfill/src/context.js:30-44`（`Object.keys(globalThis)` + `_pristine` 排除） |
| localStorage 靠文件持久化 | `polyfill/src/local-storage.js:56-58`（`persist` 经 `pal.fsWriteSync` 原子写）、`:145`（setup 时 `load()` 读回）；`globalThis.localStorage` 是 `enumerable: true`（`:146-148`）但在 `_pristine` 快照前挂载 → 被排除 |
| 多进程模型 §14 context 复用 | `docs/plans/2026-09-04-multi-process-model.md:558`（「复用，零新码」） |
| 测试覆盖 6 场景 | `test/test_suspend_gtest.cpp`（roundtrip / unknown_id / fresh_slot / bad_path / skipped / stress） |

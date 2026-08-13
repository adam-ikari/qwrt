# qwrt libuv-native 架构设计（PAL 删除 + Worker 支持）

日期：2026-08-12 · 状态：已确认（用户逐节拍板）
目标：**完成 PAL 抽象层删除，并落地 W3C 合规的 Web Worker 支持**。qwrt 仅基于 libuv（Linux）。

## 1. 背景与目标

qwrt 曾经通过 `qwrt_pal_t`（v1 在 `qwrt.h`、v2 在 `qwrt_pal.h`）抽象全部平台能力，理由是 esp32/FreeRTOS 与 Linux 共用一套代码。去掉 esp32 后，PAL 成为无当前需求支撑的猜测性设计（过度设计，原则一）。本次重构：

- 删除整个 PAL 抽象层（`qwrt_pal_t` v1+v2、`platform/{uv,mock,freertos,wasm}`、pal_common、`QWRT_PAL_*` 选项）
- `src/bridge.c` 直接调用 libuv，不再经过函数指针契约层
- qwrt 自建线程、自驱 libuv loop；宿主通过 postMessage 语义消息与 qwrt 通信（不注入、不共享 loop）
- 保留多上下文；落地真线程化 Web Worker（W3C 合规）；软挂起/恢复到文件系统
- WinterTC polyfill 与上层应用不变；`pal` JS 对象保留，底层直连 libuv

## 2. 架构总览（已确认决策）

```
宿主进程（任意线程可调 qwrt_post_message）
   │  qwrt_create(rt, config)   ← 阻塞直到运行时就绪
   │  qwrt_post_message(rt, json) ← 线程安全入队
   ▼
┌──────────────────────────────────────────────┐
│  qwrt_t（主运行时）                            │
│  · 内部线程（uv_thread_create）                │
│  · 自建自驱 libuv loop（uv_loop_t）           │
│  · 一个 JSRuntime + 多 JSContext（共享 loop） │
│  · 主循环：uv_run + 消息派发 + 微任务         │
│  · 入站队列：宿主消息 → 主 context onmessage  │
│  · 出站：JS postMessage → 宿主 message_cb     │
└──────────────┬───────────────────────────────┘
               │ new Worker(url) → 新线程 + 新 qwrt_t（独立 JSRuntime + 独立 loop）
               │ 跨运行时消息 = 结构化克隆字节 + 线程安全队列
               ▼
┌──────────────────────────────────────────────┐
│  Worker qwrt_t（子运行时）                    │
│  · 独立线程 + 独立 loop + 独立 JSRuntime      │
│  · 无宿主 message_cb；出站消息回父运行时 JS   │
└──────────────────────────────────────────────┘
```

- **执行模型 A**：qwrt 自建线程、完全自驱动。宿主不调 `qwrt_tick`（该函数从宿主 API 删除）。
- **宿主 ↔ qwrt = postMessage 语义**（W3C worker 语义，双向）：
  - 入站：宿主调 `qwrt_post_message(rt, msg)`（线程安全入队），qwrt 反序列化后派发成主 context 的 `onmessage` 事件。
  - 出站：qwrt 线程在 JS 执行 `postMessage` 时同步调用宿主注册的 `qwrt_message_cb`（回调跑在 qwrt 线程上，宿主回调必须线程安全）。
- **deferred 回调队列删除**：模型 A 下 libuv 回调与 JS 同线程（都跑在 qwrt 线程），且 uv_run 只在主循环、JS 执行间隙调用，libuv 回调可直接 `JS_Call`，无需入队回放。
- **多上下文保留**：一个 qwrt_t = 一个 JSRuntime + 多个 JSContext + 一个共享 loop。宿主只见主 context；非主 context 由 JS 内部管理（polyfill 层提供），是软挂起/恢复的载体。
- **Worker**：`new Worker(url)`（file/http），按 url 加载脚本 → 新线程 + 新 qwrt_t（独立 JSRuntime + 独立 loop），真实并行。协作式（复用 loop）方案已废弃。

## 3. 宿主嵌入契约

```c
typedef struct qwrt_config_t {
    /* 初始脚本（UTF-8 JS 源码）。qwrt_create 在运行时就绪前完成
     * polyfill 注入 + 本脚本求值，保证宿主开始发消息前 onmessage 已就位。
     * NULL 时运行时仅带 polyfill 启动（测试/空壳场景可用）。 */
    const char *initial_script;

    /* 出站消息回调。跑在 qwrt 线程上，宿主回调必须线程安全。
     * json 为 UTF-8 字符串，len 为其字节长度，data 为 config->host_data。 */
    void (*message_cb)(qwrt_t *rt, const char *json, size_t len, void *data);

    int debug;
    void *host_data;   /* per-runtime 不透明指针，透传给 message_cb 等 */
} qwrt_config_t;

qwrt_t *qwrt_create(const qwrt_config_t *config); /* 阻塞直到就绪；失败返回 NULL */
void    qwrt_destroy(qwrt_t *rt);                 /* 优雅停机：置 shutdown → join 线程 → 释放 */
void    qwrt_post_message(qwrt_t *rt, const char *json, size_t len); /* 线程安全，可任意线程调 */
```

**宿主 API 面（模型 A 的必然收缩）**：公共 API = `qwrt_create` / `qwrt_destroy` / `qwrt_post_message` + `qwrt_get_runtime_data` / `qwrt_set_runtime_data` / `qwrt_free`。**从公共 API 移除**：`qwrt_eval` / `qwrt_eval_bytecode` / `qwrt_call` / `qwrt_tick` / `qwrt_reset` / `qwrt_spawn` / `qwrt_suspend` / `qwrt_resume` / `qwrt_destroy_ctx` / `qwrt_get_active_ctx_id` / `qwrt_get_jsctx` / `qwrt_compile` / `qwrt_compile_module`。

理由：单线程约束下宿主无法同步触碰 qwrt 线程上的 JS——交互只经消息（`initial_script` 引导 + `post_message` 双向）。多上下文机制下沉为内部（见 6.1），由 JS `qwrtContext` API 经 bridge 驱动。预编译字节码改用 `qjsc` CLI（polyfill 构建已如此），`initial_script` v1 仅源码文本。

生命周期：

1. `qwrt_create`：创建 `qwrt_t` 骨架 → 起内部线程 → 线程内建 loop、JSRuntime、注入 polyfill、求值 `initial_script` → 发就绪信号（mutex + condvar）→ `qwrt_create` 阻塞等到就绪后返回 rt。就绪握手保证「宿主可以立刻发消息，不丢」。
2. 运行期：宿主在任意线程 `qwrt_post_message`；qwrt 线程处理；出站经 `message_cb` 回到宿主。
3. `qwrt_destroy`：仅宿主线程调用（从 `message_cb` 里调会自 join 死锁——文档明示）。置 shutdown 标志 + 唤醒 → 内部线程退出主循环、销毁 context/runtime/loop → join → 释放。可 NULL 安全。

错误传播：
- 初始化失败 / 初始脚本求值异常 → 线程记录错误，就绪握手携带失败码，`qwrt_create` 返回 NULL。
- 运行期未捕获异常 → polyfill 捕获，以错误消息信封（`{"type":"error",...}`）经 `message_cb` 上报，同时 `console.error`。不新增独立错误回调 API（避免过度设计）。

## 4. 执行模型（内部线程主循环）

内部线程循环（单线程约束：所有 JS 执行只在 qwrt 线程）：

```
while (!shutdown) {
    1. 处理入站消息队列：取消息 → JS 解析 → 派发 onmessage（主 context）
    2. uv_run(loop, UV_RUN_NOWAIT/ONCE)：libuv 回调在这里直接 JS_Call
    3. 冲刷 JS 微任务/待执行作业（JS_ExecutePendingJob / JS_RunJob）
    4. 无工作则阻塞等待（condvar 或 uv_async 唤醒）
}
```

关键不变式：**uv_run 只在主循环、JS 执行间隙被调用**。因此 libuv 回调触发时 JS 不在执行中途，`JS_Call` 直接安全调用——这正是删除 deferred 回调队列的根据。libuv 回调执行 JS 期间又可调度新的 uv 工作，正常由后续 uv_run 处理。

宿主 → qwrt 唤醒：入队时用 `uv_async_send`（线程安全）唤醒 loop。停机：shutdown 标志 + condvar。

## 5. 消息边界与格式

| 方向 | 编码 | 说明 |
|------|------|------|
| 宿主 → 主 context | JSON 字符串 | C 侧零依赖；QuickJS 内置 `JSON.parse`。解析后的值直接作为 `MessageEvent.data` 投递，无信封。入站消息必须是合法 JSON，解析失败时经 `message_cb` 上报 `{"type":"error","error":"bad-json"}` |
| 主 context JS → 宿主 | JSON 字符串 | `message_cb(json, len)`，同步调用（跑在 qwrt 线程） |
| JS ↔ Worker | 结构化克隆字节 | W3C HTML structured serialize；字节 blob 跨线程传输（复制，v1 不做 transferables） |

- **宿主边界 = JSON**：C 侧用 `JS_JSONParse` / `JS_JSONStringify`，无外部依赖，可调试。
- **JS↔JS（worker 之间）= 结构化克隆**：在 polyfill JS 实现 W3C 结构化克隆算法（对象图 + 支持 ArrayBuffer/TypedArray/Date/RegExp/Map/Set 等），产出字节 blob；bridge 只负责传输（分配、入队），不实现算法。克隆失败（含函数、DOM 节点等不可克隆值）→ 抛 `DataCloneError`。
- **入站队列统一**：一个线程安全 FIFO（mutex + condvar），条目带来源标签（host / worker-id）。主 context 的 `onmessage` 收 host 消息；Worker 实例的 `onmessage` 收对应 worker 消息。
- **出站**：JS `postMessage` → polyfill → bridge 序列化 → host 消息走 `message_cb`（主运行时），worker 消息走父运行时入站队列（子运行时）。

## 6. 多上下文、Worker、挂起/恢复

### 6.1 多上下文（机制保留，下沉为内部）

一个 qwrt_t 持一个 JSRuntime + 最多 `QWRT_MAX_CONTEXTS`(64) 个 JSContext + 一个共享 loop。spawn/suspend/resume/destroy 的 C 机制（context.c）保留但**从公共 API 移除**，由 polyfill 的 `qwrtContext` JS API 经 bridge 驱动（宿主无感知，见 6.3）。per-context PAL 指针随 PAL 删除。非主 context 不再有独立权限配置。

### 6.2 Worker（真线程，W3C 合规）

- JS 层 `new Worker(url)`：polyfill 按 url 加载脚本（`file://` 走 fs，`http(s)://` 走 fetch），文本到手后经 bridge 调 C 层 `qwrt_worker_create(source, opts)`。
- 每个 Worker = 独立 OS 线程上的独立 `qwrt_t`：独立 JSRuntime + 独立 libuv loop + 独立 polyfill。真实并行。
- 通信：`postMessage`/`onmessage`/`close` 按 W3C worker 语义；结构化克隆（复制语义）跨线程。
- 子运行时无宿主 `message_cb`；其出站消息经入站队列回父运行时 JS（派发到对应 Worker 实例的 `onmessage`）。
- C 层 worker 句柄：`qwrt_worker_t`（opaque），负责线程/运行时生命周期与消息路由。worker 由 JS 创建（`new Worker`），C 层不向宿主暴露独立 worker 入口（宿主只见主 context）。
- `close()`：worker 自身优雅退出（循环结束 → join 线程 → 释放）。
- **多上下文与 Worker 是两种不同机制**：前者同一 runtime 内多 JSContext（共享 loop，JS 内部管理）；后者独立线程独立 runtime（W3C 语义）。

### 6.3 软挂起 / 恢复（到文件系统）

QuickJS-ng 不支持 live JSContext 快照 → 用**软挂起**（用户已拍板）：

- **挂起（suspend）**：① 序列化应用级 JS 状态（可克隆的全局属性，结构化克隆字节）② 连同脚本/字节码引用、context_id 写入文件系统清单 ③ 销毁该 JSContext，释放内存。
- **恢复（resume）**：读清单 → 新建 JSContext → 重新注入 polyfill → 求值应用脚本 → 反序列化状态回 globals → 激活。
- 挂起是 JS 内部机制：polyfill 提供 qwrt 扩展 API（`qwrtContext`，spawn/suspend/resume/destroy 上下文），宿主无感知。多上下文 = 多个挂起/恢复实例；后台挂起 = 释放内存，快速恢复 = 按需唤醒。
- **明确限制**：挂起不保留进行中的异步操作（timer/socket 等句柄不跨挂起存活）。状态必须位于可克隆的全局属性中。挂起面向可重启/幂等的后台上下文，不是「网络会话续传」。
- 状态文件格式：结构化克隆字节 + 清单（context_id、脚本引用、状态文件路径、时间戳）。

## 7. 删除清单

| 项 | 说明 |
|----|------|
| `include/qwrt/qwrt_pal.h` | PAL v2 spec（25 原语 + 13 URI scheme）整文件删除 |
| `qwrt.h` 中 `qwrt_pal_t`、`qwrt_pal_cb_t`、`qwrt_pal_stream_ops_t`、`qwrt_pal_err_t`、`qwrt_config_t.pal`、`qwrt_tick` | v1 契约全部删除 |
| `platform/` 整树 | `uv/` `mock/` `freertos/` `wasm/` + `platform/pal_common.{c,h}` 删除；`pal_uv.c` 逻辑吸收进 `src/uv_io.c`；`pal_mock` 换成测试用 `mock_libuv` |
| `src/qwrt_internal.h` 中 `pal_cb_data_t`、per-context `pal` 指针、deferred 队列字段、`qwrt_defer_callback` | 随模型 A 删除；libuv 回调数据另行定义 |
| `QWRT_PAL_*` CMake 选项、`libqwrt_pal_common`、`libqwrt_full`（uv+mock+ext 捆绑） | 删除/重组；libuv 变硬依赖（始终构建 `uv_a`） |
| `qwrt.h` 公共 API：`qwrt_eval` / `qwrt_eval_bytecode` / `qwrt_call` / `qwrt_tick` / `qwrt_reset` / `qwrt_spawn` / `qwrt_suspend` / `qwrt_resume` / `qwrt_destroy_ctx` / `qwrt_get_active_ctx_id` / `qwrt_get_jsctx` / `qwrt_compile` / `qwrt_compile_module` | 模型 A 单线程下宿主只能消息交互；预编译用 `qjsc` CLI |
| 文档 | `docs/pal-design.md`、`docs/esp32s3-design.md`、`docs/pal/`、CLAUDE.md 的 PAL 章节删除/更新；`docs/qwrt-architecture-design.md` 更新为 libuv-native |

## 8. 修改与新增清单

**新增文件：**
- `src/uv_io.c` — libuv 集成层（吸收 `pal_uv.c` 全部逻辑：uv 句柄管理、loop 建立、HTTP/timer/fs/random 的 uv 实现）。直接 libuv 类型与调用，**不是接口/抽象层**（无函数指针契约），纯实现拆分以保持 `bridge.c` 薄。
- `src/thread.c` — 内部线程主循环（uv_run + 消息派发 + 微任务冲刷 + 就绪/停机握手）。
- `src/msgq.c` — 线程安全 FIFO（入站：host + worker 带来源标签；加锁/条件变量）。
- `src/worker.c` — Worker 运行时：线程 spawn/join、结构化克隆字节传输、跨运行时消息路由、`close`。
- `test/mock_libuv.c` + `test/mock_libuv.h` — 假 `uv_*` API，确定性离线行为（替代 `pal_mock`）。
- polyfill 新模块：`polyfill/src/worker.js`（Worker/postMessage/onmessage/close）、`polyfill/src/structured-clone.js`（W3C 结构化克隆）、`polyfill/src/context.js`（qwrtContext：上下文管理 + 软挂起/恢复）。

**修改文件：**
- `src/bridge.c` — `js_pal_*` 包装从调 `qwrt_pal_t` 函数指针改为**直接调 libuv**（`src/uv_io.c` 函数）；新增 onmessage 派发、host JSON 编解码、worker 结构化克隆传输的 C 侧薄层。仍只做三件事：JS↔C 转换、调用后端、promise 兑现（桥纪律不变；deferred 队列环节消失）。
- `src/qwrt.c` — 生命周期改造：create 拆分（宿主侧建骨架 + 起线程 + 就绪等待；线程侧完整初始化）；destroy 优雅停机 + join；消息入队 API。
- `src/context.c` — 多上下文保留；补软挂起/恢复的序列化/重建辅助。
- `src/qwrt_internal.h` — 删 PAL/deferred 相关字段；新增线程/队列/loop 字段。
- `CMakeLists.txt` / `test/CMakeLists.txt` — 删 `QWRT_PAL_*`；libuv 硬依赖；测试默认链 `qwrt + mock_libuv`（offline），网络测试链真 libuv（`LIBUV_FOUND`/labels）。
- polyfill 现有模块 — `pal` JS 对象保留、底层改直连 libuv，不换 JS 调用面。

## 9. 测试策略

- **mock libuv**：假 `uv_*` 实现（`uv_loop_new`/`uv_timer_start`/`uv_run` 等），确定性：如 `uv_run` 同步触发到期 timer 后返回。qwrt 内部线程照常跑真 pthread；测试用条件变量同步。
- **测试形态**：config 初始脚本（注册 onmessage + 回显）→ 宿主 `qwrt_post_message` → 等 `message_cb`（mutex + condvar + 超时护栏）→ 断言。确定性、离线。
- **测试命名**（按 writing-plans 惯例）：
  - `TEST(qwrt_create_*)` — 生命周期/就绪握手
  - `TEST(qwrt_post_message_*)` — 入站 → onmessage
  - `TEST(host_*)` — 宿主侧消息/线程行为：`host_message_roundtrip`、`host_wait_thread_exit`、`host_message_thread_safety`
  - `TEST(worker_*)` — 跨运行时消息（时序敏感，标 racy）：`worker_message_roundtrip`、`worker_worker_terminate`
  - `TEST(qwrt_suspend_*)` / `TEST(qwrt_resume_*)` — 软挂起/恢复
- 标签：`offline`（mock libuv，CI 默认）、`network`（真 libuv 出网）、`benchmark`、`dap` 保留。
- 原 `pal_mock` 的确定性测试语义由 mock_libuv 继承；`pal_uv` 的 HTTP/timer/fs 行为改对 `src/uv_io.c` 直测。

## 10. 实现顺序（草稿，writing-plans 细化）

1. **消息边界 + 线程模型**（风险最高，先行）：qwrt 自建线程/自驱 loop、就绪握手、`qwrt_post_message`/`message_cb`、mock_libuv 骨架。立即可测。
2. **bridge 直连 libuv**：`js_pal_*` 迁移到 `src/uv_io.c` 直接调用；删除 deferred 队列与 per-context PAL 指针。HTTP/timer/fs 行为对拍。
3. **Worker**：`src/worker.c` + polyfill `worker.js` + `structured-clone.js`。
4. **软挂起/恢复**：`context.c` + polyfill `context.js`。
5. **清理删除 + 文档**：删 `qwrt_pal.h`/`platform/`/`QWRT_PAL_*`/旧文档，CLAUDE.md 更新。

## 11. 已排除方案（记录，避免重复）

- 复用宿主 loop 注入方案 → 推翻：宿主与 qwrt 纯消息通信，qwrt 自建自驱 loop。
- 协作式 worker（复用 loop，同线程交替）→ 砍：非 W3C 语义，只保留真线程。
- 引擎级快照挂起 → 不可行：QuickJS-ng 不支持 live JSContext 快照；改用软挂起。
- PAL v2（25 原语 + 13 URI scheme + epoll wait）→ 整个作废，回 libuv 原生回调。
- esp32/FreeRTOS/WASM PAL 后端 → 删除，Linux + libuv 唯一。
- 结构化克隆 transferables → v1 不做（复制语义起步），后续按需补。
- 宿主直见多 context → 改为仅主 context 暴露，非主 context JS 内部管理。

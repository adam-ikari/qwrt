# 控制面设计 — 控制命令与 qwrt 自我控制

> 状态：设计文档（架构已拍板，本文落定机制细节与验证门）。
> 日期：2026-09-04
> 决策原文（用户拍板）：**「取消直接控制面的设计」「仍然采用控制命令和 qwrt 自我控制的方式」**——无进程内阻塞式 C API（无 `qwrt_eval_direct`）；控制面 = 控制命令消息（入队），qwrt 线程在自己事件循环的安全点自主执行。外部只发「求」，运行时自主完成——与挂起设计 safepoint 求模型同哲学。
> 引用源码：`include/qwrt/qwrt.h`、`src/qwrt_internal.h`（msgq 节点）、`src/thread.c`（主循环/安全点）、`src/msgq.c`、`src/ipc_envelope.{c,h}`（M-P0 已落地）、`deps/quickjs-ng/quickjs.{c,h}`（中断 API）、`docs/plans/2026-09-04-suspend-restore-design.md`、`docs/plans/2026-09-04-multi-process-model.md`。

---

## TL;DR（决策摘要）

1. **自我控制原则**：唯一执行者 = runtime 自己的线程；外部只投递控制命令；命令在事件循环安全点执行（事件间、微任务排空后——与 `2026-09-04-suspend-restore-design.md` 同一 safepoint 求模型，复用同一状态机）。
2. **命令通道三层**：进程内复用 msgq（新增 `qwrt_control(rt, bytes, len)` 入队，`flags=CONTROL`）→ 跨进程 FB 信封 `kind=CONTROL`（=3，M-P0 已落地 `ipc_envelope.c`，payload=命令 JSON，树路由按 target）→ 可选本地 uv_pipe 端点（外部控制器/CLI，config 开关默认关）。
3. **命令集**：eval / inspect / metrics / ctx.suspend / ctx.resume / ctx.destroy / worker.spawn / worker.terminate / interrupt / events.subscribe。
4. **安全**：`config.control_plane` 三档 OFF（默认）/ IN_PROC / LOCAL；eval = 最高权限；LOCAL 下 unix pipe 权限即认证，首版无 token。
5. **分阶段 CTL-0/1/2 + parity 验证门**（THREAD/PROCESS 双后端回执逐字节一致，复用多进程文档 §1.5 方法）。

---

## 1. 架构总则

### 1.1 自我控制原则（否决「直接控制面」）

直接控制面 = 阻塞式 C API（宿主线程直调 `qwrt_eval_direct(rt, script)` 一类），否决理由：

1. **线程侵入 JSRuntime，违反单线程所有权**。qwrt 的无锁叙事靠「JSRuntime 任意时刻恰有一个所有者线程」（内存所有权原则推论，多进程文档 §10.2 同款）。宿主线程直入 eval = 两个线程同时摸 JSRuntime——要么全量加锁（违背无锁设计），要么数据竞争。唯一例外路径是 interrupt 的原子标志（§3.9，单方向、引擎只读，不破坏所有权）。
2. **死循环场景阻塞式调用卡死宿主线程**。JS `while(true)` 时阻塞式 eval 永不返回，宿主线程连「放弃」的机会都要靠超时硬拆；命令模式天然异步——宿主投递后即可继续，可再投 interrupt、可超时放弃，qwrt 线程独立自决。
3. **与挂起 safepoint 模型同构，一套机制两个用途**。挂起设计已确立「异步求 → 安全点执行」状态机；控制命令只是第二类「求」。两个入口、一个执行点，不新增机制。

### 1.2 命令生命周期

```
投递（宿主任意线程 / 外部进程）
  → 入队（msgq 节点 flags=CONTROL；跨进程则为信封 kind=CONTROL 树路由入站）
  → qwrt 事件循环 safepoint 分流执行
      （thread.c wake 回调排空队列时：flags=CONTROL 的节点交 qwrt_control_dispatch，
        普通节点走 __qwrt_dispatch__ → onmessage；分流点即 thread.c:77-91 排空循环）
  → 结果回执（correl 关联 id 异步回传：进程内经 message_cb，跨进程沿信封回程）
```

- **correl**：命令自带关联 id（发起方生成），回执原样透传——异步语义下宿主/外部控制器凭 correl 配对请求与结果。
- 控制命令**不进 JS 层**：`onmessage` 只收普通消息；控制回执由控制面回执表消费后再决定是否以普通消息形式通知 JS（如事件订阅，§3.10）。

### 1.3 safepoint 执行点

挂起设计 §2 已定义判据：**微任务空 / loop 空闲 / 无在途流式 / 扩展排空**。控制命令在该点执行；实现锚点 = `qwrt_loop_idle`（thread.c:51-72：msgq 空 → flush 微任务 → `JS_IsJobPending` 判残留 → uv_walk 无活跃句柄），与挂起/`wait_idle` 共用同一判据，不另造第二套。

**不可达 safepoint**（JS 死循环占住线程）：命令在队列中等待，`timeout_ms` 到期 → 发起方收 `TIMEOUT` 回执，fail-closed（命令作废，不排队永不丢应答）。

**唯一例外：interrupt**（§3.9）——它是唯一无安全点命令，否则死循环无解。

### 1.4 多进程文档 CONTROL 三处草案的收拢

多进程模型 v6 已有三处 `kind=CONTROL` 使用草案，本文将其收拢为**统一控制通道**，既有草案全部保留、不推翻：

| 既有草案 | 位置 | payload | 执行层 | 收拢后地位 |
|---|---|---|---|---|
| 握手 Handshake/HandshakeAck | 多进程 §3.3 | `{proto_version}` | C 层（通道级，无需 safepoint） | 系统控制消息，通道 BUILD→RUN 门 |
| idle 汇总/应答 | 多进程 §6.1 | `{epoch=W}` | C 层（通道级） | 系统控制消息，`qwrt_wait_idle` 协议 |
| shutdown 链 | 多进程 §9.2 | 空 | C 层（通道级） | 系统控制消息，优雅关闭 |

- 收拢规则：`kind=CONTROL` 是唯一控制入口；**系统级生命周期消息**（上表三处）在 C 层直接消费，不经 JS、无 safepoint 要求——它们本来就是通道/进程协议；**命令集消息**（§3）payload=命令 JSON，入 runtime safepoint 由控制器执行。
- 枚举不新增：`IPC_ENV_KIND_CONTROL=3` 已冻结落地（ipc_envelope.h:50-54）；`kind=STORAGE(4)` 是多进程 §10.2 的另案规划，与本文正交。
- 回执统一走 `kind=CONTROL` 回程信封（correl 配对），与三处草案的 ack 风格一致。

---

## 2. 命令通道

### 2.1 进程内：`qwrt_control` + msgq 复用

```c
/* include/qwrt/qwrt.h —— 线程安全控制命令入队（任何线程可调）。
 * bytes 为命令 JSON，内部拷贝。control_plane=OFF 时恒返回 -1。
 * 返回 0 成功，-1 失败。 */
int qwrt_control(qwrt_t *rt, const char *bytes, size_t len);
```

- msgq 节点复用：`qwrt_msg_push` 同一条无锁 MPSC 队列，零新队列、零新锁。
- **普通/控制区分：`qwrt_msg_t` 加 `uint8_t flags` 字段**（qwrt_internal.h:141-146，现状 `{q, data, len, source}` 无区分位——此为核实结论）。推荐 flags 而非 payload 前缀魔数：① C99 兼容改动最小（qwrt_msg_t 是纯内部结构，无 ABI 面、无外部使用者）；② 分流在入队/出队时一个整数比较完成，魔数要等解析 payload；③ 魔数污染 payload 字节、破坏零拷贝切片（前 4 字节要剥）。
- `flags` 取值：`0`=普通消息，`1`=CONTROL。

### 2.2 跨进程：信封 kind=CONTROL 树路由

- 信封：`Envelope{source, target, kind=3(CONTROL), payload=命令 JSON 字节}`（ipc_envelope.h schema id 0-3，M-P0 编解码已落地并过 Python 交叉验证）。
- `target` = 多进程 §4.3 逐跳相对寻址（0=宿主、1=父、>1=子槽位）；命中本地槽位即入本地 msgq（flags=CONTROL），否则改写 source/target 转发，payload 字节零拷贝透传（§7.2 同款，路由器不解 payload）。
- **worker 上的命令**：target 指向 worker 槽位 → 沿树下发 → worker 的 runtime 在自己 safepoint 执行 → 回执沿树回（correl 原样透传，每跳只改信封头）。
- 既有「CONTROL 的 target 恒为父」约束（多进程 §4.3）**收窄为系统级生命周期消息**；命令集消息 target 可以是任意树路径——控制命令本来就是要寻址到远端 runtime 的。

### 2.3 本地端点：uv_pipe + qwrt-ctl

- `config.control_plane=LOCAL` 时 runtime 监听一个 `uv_pipe`（Linux AF_UNIX），路径 config 显式指定（缺省 `/tmp/qwrt-<pid>-<n>.ctl`，0600）。
- 端点读循环 = 一条连接一条命令流（换行分帧，JSON per line）；收到的字节走 `qwrt_control` 同一入口——**端点只是生产者，不引入第二执行路径**。
- `qwrt-ctl` 小工具（CTL-2 交付）：argv/stdin 拼 JSON 命令 → 连 pipe → 打印 correl 回执。约 150 行，复用现有 cli 骨架。
- **与 DAP 并存规则**：DAP = stdio 调试专用通道（多进程 §13.2 单实例约束）；控制面 = msgq/信封/pipe 管理专用。端点分离，可同时启用，互不抢占——调试器不承担管理命令，控制面不做断点单步。

---

## 3. 命令集

payload 均为 JSON 对象；`op` 命令名、`correl` 关联 id、`timeout_ms`（缺省 5000）为通用字段。

| 命令 | payload 专有字段 | 安全点要求 | 回执 | 错误语义 |
|---|---|---|---|---|
| `eval` | `ctx_id, script` | 需要（ctx 所在 runtime） | `{ok:true, result}` / `{ok:false, error, code}` | 挂起中→INVALID_STATE；无此 ctx→NOT_FOUND；JS 异常→error 带堆栈文本 |
| `inspect` | `ctx_id, expr` | 需要 | `{ok:true, json}` | 同 eval；expr 求值非 JSON 可序列化→INVALID_ARG |
| `metrics` | 无 | **即时返回，无安全点要求**（只读原子计数） | `{ok:true, heap_bytes, handle_count, pending_jobs, ctx_count, worker_count}` | — |
| `ctx.suspend` | `ctx_id` | 复用挂起状态机（§5） | `{ok:true}` + `on_suspended` 事件 | NOT_FOUND |
| `ctx.resume` | `ctx_id, path` | 复用挂起状态机 | `{ok:true}` | NOT_FOUND / CORRUPT |
| `ctx.destroy` | `ctx_id` | 需要（含 G2 pending job drain） | `{ok:true}` | NOT_FOUND |
| `worker.spawn` | `script, backend?` | 目标 runtime safepoint | `{ok:true, worker_id}` | SPAWN_FAILED |
| `worker.terminate` | `worker_id` | 目标 runtime safepoint | `{ok:true}` | NOT_FOUND |
| `interrupt` | `ctx_id` | **无（唯一无安全点命令）** | `{ok:true, interrupted}` | NOT_FOUND（ctx_id 不存在） |
| `events.subscribe` | `types:[...]` | 即时登记，无安全点要求 | `{ok:true}` | UNKNOWN_TYPE |

通用错误：未知 `op` → `UNKNOWN_CMD`；JSON 解析失败 → `BAD_REQUEST`；`timeout_ms` 到期未执行 → `TIMEOUT`（fail-closed，命令作废）。

### 3.9 interrupt — 死循环唯一出口

- **机制**：`qwrt_runtime_init` 时恒装一个 runtime 级 QuickJS 中断处理器（`JS_SetInterruptHandler`，**quickjs 现成公开 API**，deps/quickjs-ng/quickjs.h:1139-1141）；处理器体只读一个原子标志 `rt->ctl_interrupt`（qwrt_internal.h 新增，C11 atomic），置位则返回非零。
- **投递即生效**：`qwrt_control` 收到 interrupt 命令时**在生产者线程直接置原子标志**（单方向写、引擎线程只读，不破坏所有权叙事，§1.1-① 例外），命令消息照常入队只为 correl 回执。JS 指令计数归零时引擎轮询处理器（quickjs.c:529-530 `interrupt_counter`、8259-8263 `js_poll_interrupts`），返回非零 → 引擎抛 `JS_ThrowInterrupted`（quickjs.c:8247-8253）——**引擎在 JS 指令边界自行中断，无需 safepoint**。
- **在库先例**：quickjs 自测与 quickjs-libc 已用同一 API 做执行超时/信号中断（api-test.c:194 `timeout_interrupt_handler`、quickjs-libc.c:1150）——集成约 30 行（handler + 原子标志 + 命令接线），无补丁。
- **中断是普通 JS 异常**：可被 JS `try/catch` 捕获（CTL-0 用例断言可达 catch）；中断后 ctx 状态完好，可继续 eval。
- **ctx_id 语义**：interrupt handler 是 runtime 级（QuickJS 无 per-ctx handler）；`ctx_id` 仅做存在性校验（NOT_FOUND）。命中对象 = 当前正在执行的 JS——qwrt 线程同一时刻至多一个 JS 在跑，且死循环必然占着它，故不存在误伤。

---

## 4. 安全模型

### 4.1 三档开关

`qwrt_config_t` 新增 `control_plane` 字段：

| 档 | 语义 | 攻击面 |
|---|---|---|
| `QWRT_CONTROL_OFF`（**默认**） | `qwrt_control` 恒 -1；信封 CONTROL 命令类入站即丢弃（握手/idle/shutdown 等系统级消息不受影响——进程模型必需） | 零 |
| `QWRT_CONTROL_IN_PROC` | 仅进程内宿主线程命令（msgq 路径） | 宿主进程内 |
| `QWRT_CONTROL_LOCAL` | IN_PROC + uv_pipe 本地端点 | 本机同 uid 进程 |

### 4.2 eval = 最高权限

能 `eval` = 能在任意 ctx 跑任意脚本 = **全权控制 runtime**（可再 spawn worker、读写 storage、开网络、改全局）。因此：

- 默认 OFF 是硬缺省——控制面是显式 opt-in 能力，不是基础设施默认件。
- LOCAL 档认证 = **unix pipe 文件权限即认证**：socket 0600 + `SO_PEERCRED` 校验 peer uid 与 owner 一致，异 uid connect 直接断。同 uid 即可信（本机同 uid 已可 ptrace/读内存，pipe 不降低安全水位——不造虚假安全）。
- **首版无 token、无 per-command ACL**（YAGNI）：eval 全权之下，细粒度授权无真实防御价值。后续可选：token 挑战-应答（防同 uid 误连）、按命令白名单（只读档：仅 metrics/inspect）。列入「不做」而非本期。

### 4.3 与多进程树路由

worker 进程的控制命令**一律经父逐跳路由**——扁平直连不做（树形拓扑已定，多进程 §1.2/§4.3）。收益：父节点天然审计/拦截点（未来 ACL 挂点）、无 O(N) 连接管理；代价：一跳转发（C 层裸逻辑，纳秒级）。

---

## 5. 与挂起/多进程协同

### 5.1 ctx.suspend 命令 = 挂起状态机的第二入口

- C API `qwrt_ctx_serialize`：宿主线程**同步直调**（阻塞至 capture/drain/destroy 完成）。
- `ctx.suspend` 命令：**异步求**（投递 → runtime safepoint 执行）。
- 同一底层状态机（capture → per-ctx job drain[G2] → destroy → 槽位 NULL），两个入口。命令回执在状态机完成后发出，附带 `on_suspended` 事件（事件订阅者可见）。

### 5.2 ctx 状态 × 命令语义表（G1 后：挂起 = destroy = 槽位 NULL）

| ctx 状态 | eval / inspect | ctx.resume | ctx.destroy |
|---|---|---|---|
| 运行中 | 执行 | INVALID_STATE（已在运行） | 执行（含 drain） |
| 挂起中（槽位空） | INVALID_STATE | **合法**（rebuild 回同 id） | NOT_FOUND |
| 从未存在 | NOT_FOUND | NOT_FOUND | NOT_FOUND |

### 5.3 树挂起时的控制命令缓冲（前瞻声明）

未来 S-R3（整树挂起）落地后：树冻结期间到达的控制命令**缓冲不丢弃**，`timeout_ms` 计时暂停，树恢复后按序执行。首版树挂起未落地，本条仅锁定语义，不实现。

---

## 6. 验证门（CTL-0 / CTL-1 / CTL-2 + parity）

### CTL-0 — 进程内基线（THREAD 即可）

- `qwrt_control` + flags 分流 + eval/inspect/metrics/interrupt 四命令 + correl 回执，gtest 覆盖。
- **关键用例 `interrupt_breaks_infinite_loop`**：宿主线程投 `eval{while(true)}`（异步、不等待）→ 投 interrupt → 断言：① 引擎中断异常**可达 JS catch**（脚本写成 `try{while(true);}catch(e){postMessage('caught')}`，收 caught 消息）；② qwrt 线程未卡死（后续 eval 正常返回）；③ 回执 correl 配对正确。
- 超时用例：长 sleep JS + `timeout_ms=100` → TIMEOUT 回执。
- metrics 并发用例：eval 进行中投 metrics → 即时回执（验证无安全点依赖）。
- OFF 档回归：`qwrt_control` 恒 -1。

### CTL-1 — 信封 CONTROL 树路由

- mock_ipc 单进程 gtest（多进程 §10.1 桩）：三层树下发 target=孙槽位命令，逐跳改写断言、correl 透传断言。
- e2e：**外部进程控运行中 worker**——测试宿主 THREAD 模式跑 worker，另一进程经 CTL-1 测试通道对 worker 发 eval/inspect，往返回执断言。

### CTL-2 — 本地端点 + CLI

- uv_pipe 端点 + `qwrt-ctl`（eval/inspect/metrics/interrupt 四命令够用）。
- DAP 并存回归：DAP stdio attach 与控制面 pipe 同时启用，互不干扰（多进程 §13.2 约束不回退）。
- LOCAL 认证回归：异 uid connect 被拒（root 测试环境跑，非 root 跳过）。

### parity — 双后端一致性

同一命令序列在 `worker_backend=THREAD` 与 `=PROCESS` 下各跑一遍，**回执 JSON 逐字节一致**（stdout diff 为空），复用多进程 §1.5 方法；性能差异豁免同款。

---

## 7. 明确不做

- **远程网络控制面**（TCP/WebSocket、跨机）——LOCAL 只到本机同 uid。
- **多控制器仲裁 / HA**——首版单控制器假设；多端连同一 pipe 后到先得，不承诺公平性。
- **命令事务 / 批量原子性**——每命令独立执行、独立回执。
- **token / 细粒度 ACL**——§4.2 已论证（eval 全权之下无防御价值）；后续按需立项。
- **广播命令**——无 `-1` target（多进程 §4.3 同款裁决）。
- **Windows named pipe 端点**——与多进程 §12.1 同批后置，首版 Linux-only。

---

## 证据索引

| 断言 | 证据位置 |
|---|---|
| `JS_SetInterruptHandler` 为 quickjs 现成公开 API | deps/quickjs-ng/quickjs.h:1139-1141（`return != 0 if the JS code needs to be interrupted`） |
| 引擎指令计数轮询中断处理器 | quickjs.c:529-530（interrupt_counter 注释）、8259-8263（js_poll_interrupts） |
| 处理器返回非零 → 抛中断异常 | quickjs.c:8247-8253（__js_poll_interrupts → JS_ThrowInterrupted） |
| 在库使用先例（超时/信号中断） | deps/quickjs-ng/api-test.c:194、quickjs-libc.c:1150 |
| `IPC_ENV_KIND_CONTROL=3` 已落地 | src/ipc_envelope.h:50-54；schema kind:int8=id 2（:10） |
| `qwrt_msg_t` 现状无 flags 字段 | src/qwrt_internal.h:141-146（`{q, data, len, source}`） |
| msgq 为无锁 MPSC、qwrt 线程独占消费 | src/qwrt_internal.h:200-206、src/msgq.c |
| wake 回调排空循环（分流点） | src/thread.c:77-91 |
| 安全点判据实现锚点 | src/thread.c:51-72（qwrt_loop_idle） |
| `thread_ready`/`wait_idle`/`shutting_down` 原子语义 | src/thread.c:122、130-138 |
| CONTROL 三处既有草案（握手/idle/shutdown） | 多进程文档 §3.3、§6.1、§9.2 |
| 逐跳路由/零拷贝透传 | 多进程文档 §4.3、§7.2 |
| 挂起状态机与 G1/G2 语义 | 挂起文档 §3（挂起=destroy=槽位空）、§6（状态机） |
| 双后端 parity 方法 | 多进程文档 §1.5 |

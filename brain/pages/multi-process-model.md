---
id: multi-process-model
title: "多进程模型 M-P0..M-P5 + CTL + M-R2（宿主⇄主RT 进程模型与 path 链路由）"
category: decision
status: active
created: "2026-09-15T23:20:12"
updated: "2026-09-17T04:05:45"
---

<!-- compiled_truth -->
<current best understanding — replace this with the real content>

## Timeline

- time: 2026-09-15T23:20:12
  kind: decision
  summary: "Created this page: 多进程模型 M-P0..M-P5 + CTL + M-R2（宿主⇄主RT 进程模型与 path 链路由）"
  source: "2026-09-15 多进程轨道落地会话"
  affects: [multi-process-model]

- time: 2026-09-15T23:20:22
  kind: decision
  summary: "M-P0..M-P5 + CTL + M-R2 全部落地（commit 范围 2295e402..b92709c7），多进程轨道完整落地。"
  source: "2026-09-15 多进程轨道落地会话"
  affects: [multi-process-model]

- time: 2026-09-15T23:20:23
  kind: decision
  summary: "M-P2 缺省 ISOLATED：amoib 缺省宿主⇄主RT 独立进程（-DAM_PROCESS_MODEL=THREAD 回退）——用户裁决，最终态。"
  source: "2026-09-15 多进程轨道落地会话"
  affects: [multi-process-model]

- time: 2026-09-15T23:20:23
  kind: decision
  summary: "M-P5 嵌套 spawn + §8.2 path 链路由：worker 再 spawn worker（孙）；端点身份 int32→path 链（u16[]，根=[]，子=父path++槽位）；LCA 前缀比较路由（上/本地/下投），零路由表；PORT_TRANSFER 头 16B→变长；信封 source/target 冻结 schema 零改动（wire 兼容）。"
  source: "2026-09-15 多进程轨道落地会话"
  affects: [multi-process-model]

- time: 2026-09-15T23:20:30
  kind: decision
  summary: "Q1-Q3 裁决：① path 放 payload（PORT 头变长 + CTL target_path），信封 int32 只放当前跳；② (owner,id)→path，扁平退化 worker.path=[slot]/mainRT.path=[]，单元素退化为旧语义；③ path 元素 u16（>65535 升 u32）。"
  source: "2026-09-15 多进程轨道落地会话"
  affects: [multi-process-model]

- time: 2026-09-15T23:20:31
  kind: decision
  summary: "CTL-1/CTL-2：控制面树路由 + amoib-ctl CLI（AF_UNIX 端点 + SO_PEERCRED uid 校验）。"
  source: "2026-09-15 多进程轨道落地会话"
  affects: [multi-process-model]

- time: 2026-09-15T23:20:31
  kind: decision
  summary: "M-R2：多 RT 组合 gtest（contexts × workers 正交）。"
  source: "2026-09-15 多进程轨道落地会话"
  affects: [multi-process-model]

- time: 2026-09-15T23:20:36
  kind: decision
  summary: "已知延后：tier-2 超时异步化（§9.2/I5）、§10.2 所有者死亡降级、STORAGE 中继并发关联 id、path u32 升级。"
  source: "2026-09-15 多进程轨道落地会话"
  affects: [multi-process-model]

- time: 2026-09-15T23:20:36
  kind: decision
  summary: "watch-item：mp4 e2e 本地挂（worker localStorage→主RT 同步 RPC）待查。"
  source: "2026-09-15 多进程轨道落地会话"
  affects: [multi-process-model]

- time: 2026-09-17T04:05:45
  kind: decision
  summary: "watch-item 关闭：mp4 storage 同步 RPC「本地挂」经 2026-09-16 量化验证 = 非真 bug，环境/设计误判。ISOLATED 进程模型 test_mp4_storage_crash_e2e.sh 3/3 通过（6.8–7.7s/次，脚本 timeout 30s，CI e2e 无 timeout-minutes 默认 360min，风险低）；THREAD 构建无 process 后端，脚本 probe.js 检测 PROC-ERR → SKIP 退出 0（设计如此），「挂」系绕过探针直接跑 fixture 所致；worker_storage.js 6MB setItem 超 5MiB quota → QuotaExceededError（设计预期），payload 整帧 RPC 到 owner 后才检查，单次 ~7.5s = 同步 RPC 大 payload 线性成本（~1.1ms/KB，src/ipc_process.c emit_sync + src/rt_main.c pipe_read_cb），非死锁。证据：test/test_mp4_storage_crash_e2e.sh、test/mp4-e2e/*.js。"
  source: "2026-09-16 量化验证会话"
  affects: [multi-process-model]

- time: 2026-09-17
  kind: decision
  summary: "延后项复查：tier-2 超时异步化（§9.2/I5）→ 维持 DEFERRED（非真问题，不实施）。am_proc_terminate（src/ipc_process.c:453）同步三级终止，最坏 2s/挂死 worker，但①冻结只在已损坏（无视 shutdown）子进程兑现，正常退出 ~1ms；②嵌入宿主自身从不阻塞——宿主侧 terminate 仅 destroy 路径（rt_host.c:160，专用 loop 线程，非宿主主线程），JS 侧 processTerminate（bridge.c:1927）冻结的是主RT 自身 loop（ISOLATED 缺省为独立进程）；③异步化须迁移 pid 属权与 proc 生命周期跨 loop tick，4 个调用点均紧跟 am_proc_free（假设 terminate 返回即已收尸）→ UAF/双释放风险大于收益。详见 CHANGELOG。"
  source: "tier-2 超时异步化调查会话"
  affects: [multi-process-model]

- time: 2026-09-18
  kind: decision
  summary: "延后项复查：path 元素 u16→u32 → 维持 u16（YAGNI，不实施）。证据：AM_MAX_PROC_HANDLES=64 并发进程句柄/rt（am_internal.h:135），path 深度上限 AM_SELF_PATH_MAX=8（:138）；path 元素 = 各 runtime 本地单调计数器 procWorkerSeq（polyfill/src/worker.js:66，1000 起），溢出须单 runtime 累积 >6.45 万次 PROCESS spawn（id 每 runtime 本地分配，非全局），现实不可达；rt_main.c:600 已有 v<=0xFFFF 解析守卫。非 path 字段 key_port 本为 u32，无其他溢出点。升 u32 须改 PORT_TRANSFER 头变长公式 8+2*(dl+kl)→8+4*(dl+kl)，破坏 §4.1 冻结 schema / §7.2 字节不动 wire 兼容 → 超出低成本，维持延后。详见 CHANGELOG。"
  source: "path u16 升级调查会话"
  affects: [multi-process-model]

- time: 2026-09-18
  kind: decision
  summary: "延后项复查：§10.2 所有者死亡降级 → 不实施，孤儿自杀即设计终点（§9.4 优先）。owner 死亡时孤儿自杀而非存活降级：主RT 死 → parent-fd EOF → shutting_down → teardown → exit（rt_main.c:348-353）；storage 代理同步 RPC 收 EOF → storageSync 抛 InternalError → 连锁自杀（bridge.c:2053、local-storage.js:27-28）。判定不实施：① §9.4 连锁死亡是预期行为，§6.4 通道不重连，孤儿存活即成不可达死进程；② 降级态结构上不可达——owner 恒为树根主RT（§10.2+N-P4），owner 死 ⇒ 祖先全死 ⇒ 任何孤儿必经 §9.4 自杀；③ 计划 §10.2 降级兜底（快照只读+LWW）破坏单所有者不变量且与现有 e2e 级联断言冲突。详见 CHANGELOG。"
  source: "§10.2 所有者死亡降级调查会话"
  affects: [multi-process-model]

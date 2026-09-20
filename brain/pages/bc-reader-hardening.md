---
id: bc-reader-hardening
title: "字节码读取器加固（untrusted stream 防御）"
category: decision
status: active
tags: [fuzz, quickjs, bytecode, security]
created: "2026-09-19T14:33:30"
updated: "2026-09-19T14:36:02"
---

<!-- compiled_truth -->
## 字节码读取器加固（untrusted stream 防御）

**结论**：quickjs-ng 字节码读取器（JS_ReadFunctionTag 等）按 untrusted 输入防御，加固以 `deps/quickjs-ng-bc-reader-hardening.patch` 分层注入（c99-atomics → drain-jobs → hardening 三补丁链）。

**硬性不变量**（2026-09-19 定稿，CI run 35448802113 全绿验证）：
1. `local_count` 必须**严格等于** `arg_count + var_count`，且校验在 `function_size` 计算/`js_mallocz` **之前**。free 路径按 arg+var 走 vardefs，任何上界/钳制版本都会留下"分配 < 遍历量"的 OOB 窗口（fuzzer 实证两次）。
2. `byte_code_len` 安全前置：`b->byte_code_len = 0` 先写，流值只在字节确实读入后才赋给 b；否则 fail 路径 free 走流值长度越界。
3. 各 count 校验必须在分配之前；`opcode < OP_COUNT` 前置；`source_len`/`pc2line_len` 必须真在剩余流字节内（字节级界，不是乘积界）。
4. hardening patch 文件导出时必须剔除 drain hunk（三补丁链会重复定义 `JS_DrainPendingJobsForContext`）——patch 文件按"对上游 git 基线的 diff"生成时天然包含前序补丁内容，需过滤。

**corpus gate**：`test/fuzz-corpus/` 19 seeds + CI 重放门。seed `crash-73a0` 曾暴露 cpool 嵌套函数失败路径，随 byte_code_len 前置修复后可回归 gate。
**教训**：每轮"放宽/收紧"校验必须本地 CI-Debug 等效构建（/tmp/ci_dbg）验证全 corpus + polyfill/workerboot 加载，避免 Release-only 绿假象。


## Timeline

- time: 2026-09-19T14:33:30
  kind: decision
  summary: "Created this page: 字节码读取器加固（untrusted stream 防御）"
  source: created via brain create-page
  affects: [bc-reader-hardening]

- time: 2026-09-19T14:36:02
  kind: decision
  summary: Rewrote compiled_truth to the new best understanding
  source: brain update-truth
  affects: [bc-reader-hardening]

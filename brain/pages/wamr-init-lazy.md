---
id: wamr-init-lazy
title: "WAMR runtime init 懒加载与启动优化裁决"
category: decision
status: active
tags: [wamr, startup, lazy-init]
created: "2026-09-11T10:02:01"
updated: "2026-09-11T10:02:11"
---

<!-- compiled_truth -->
## 决策

wasm_runtime_init + init_thread_env 从 wamr_ext_init 下沉为 wamr_ensure_runtime()，由三个触碰 WAMR API 的 JS 入口（WebAssembly.validate / Module 构造器 / Instance 构造器）首次调用懒初始化；compile/instantiate/streaming 经 CallConstructor 透传自动覆盖；Memory/Table/Global 构造器纯 js_mallocz 不需要守卫。CAS 进程单例（M-R1 §13.2）与 thread_env 幂等性原样保留。commit 3a8c26e9。

## 实测

- Release 端到端 `qwrt -e` median 10.33 → 4.82ms（Δ5.5ms，为探针 init 死重 7.0ms 的 79%，差值归因真实进程主线程栈已部分映射）。
- 不碰 wasm 路径 wasm_runtime_init 调用数 0（--wrap 计数法）。

## 三原则裁决

- #2 instantiate 默认 8MB app-heap mmap（首次 new Instance ~5ms）延后独立处理：改默认堆有 wasm 侧 OOM 风险，需先验证 WAMR 按需增长路径。
- #3 `-DWASM_DISABLE_STACK_HW_BOUND_CHECK` 否决——以沙箱硬件越界保护换 worker spawn 4.5ms，违背运行时定位。
- libuv/mbedtls lazy 否决（uv_loop_init 0.14ms 可忽略；TLS 不在首启路径）。

## 已知敞口

无测试锁"不碰 wasm 则不 init"契约，靠 wamr_ensure_runtime 函数体 design 注释防护。

## 认知更新

启动主战场"WAMR init ~5.5ms"已解决，R1 残余大头为 polyfill ReadObject/Eval ~1.3ms + 进程 fork/exec 固定开销，后续启动优化需重新实测再定。


## Timeline

- time: 2026-09-11T10:02:01
  kind: decision
  summary: "Created this page: WAMR runtime init 懒加载与启动优化裁决"
  source: created via brain create-page
  affects: [wamr-init-lazy]

- time: 2026-09-11T10:02:11
  kind: decision
  summary: "wasm_runtime_init+init_thread_env 下沉为 wamr_ensure_runtime()，三个 WAMR API 触碰入口懒初始化，启动 median 10.33→4.82ms（3a8c26e9）"
  source: brain update-truth
  affects: [wamr-init-lazy]

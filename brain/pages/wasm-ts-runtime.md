---
id: wasm-ts-runtime
title: "Perry 式 wasm 化路线（TS 编译 wasm + amoib 运行时 wasm 化 + WAMR）"
category: decision
status: active
tags: [wasm, ts, wamr, roadmap]
created: "2026-09-18T06:53:31"
updated: "2026-09-18T06:58:45"
---

<!-- compiled_truth -->
<current best understanding — replace this with the real content>

## Timeline

- time: 2026-09-18T06:53:31
  kind: decision
  summary: "Created this page: Perry 式 wasm 化路线（TS 编译 wasm + amoib 运行时 wasm 化 + WAMR）"
  source: "2026-09-18 用户拍板"
  affects: [wasm-ts-runtime]

- time: 2026-09-18T06:53:31
  kind: decision
  summary: "Perry 式 wasm 化路线（用户 2026-09-18 拍板，远期）：①原生支持 TS 源码编译成 wasm 模块；②把 amoib 的 JS 运行时改造成类 perry-js-runtime 的 wasm 模块（JS 运行时本身编译成 wasm）；③用 WAMR 运行编译后的 wasm 模块；④TS 写的模块同样支持 WinterTC（ECMA-429）和当前 amoib 全部 API。执行约束：独立分支、合适时机才开始实验、**amoib 成熟之前不要尝试**（延期触发器 = amoib 主轨道成熟）。现有基础：WAMR 已集成（wasm-engine-integration brain 页，threading 已知坑）、WinterTC API 覆盖已落地。此决策为方向性记录，不改变当前工作排期。"
  source: "2026-09-18 用户拍板"
  affects: [wasm-ts-runtime]

- time: 2026-09-18T06:58:45
  kind: decision
  summary: "补充约束（用户 2026-09-18）：①Perry 已有的成熟工具尽量复用（不重复造轮子——Perry 的 TS→wasm 编译链、wasm runtime 封装等成熟组件直接采纳/适配）；②实验开始时的验收标准 = 以尽可能小的改动跑起来（最小可行切片：先打通一条 TS→wasm→WAMR→跑通 amoib API 的路径，验证链路成立后再渐进扩展，不一次性重写运行时）。"
  source: "2026-09-18 用户拍板"
  affects: [wasm-ts-runtime]

---
id: polyfill-bundling-policy
title: "Polyfill 内嵌强绑定：放弃拆 bundle/外置，优化内存占用为唯一方向"
category: decision
status: active
tags: [polyfill, bundling, memory]
created: "2026-09-10T01:38:27"
updated: "2026-09-11T00:12:24"
---

<!-- compiled_truth -->
## 启动加速决策链（2026-09-10 用户拍板）

- 动机：加速 qwrt_create（R1 7.5ms，polyfill 占 ~2ms）。
- 已否决路径：① 跨 context 复用反序列化字节码（QuickJS realm 绑定，实测同对象两 context 执行均写入第一个 context 的 global——b->realm 在反序列化时定死，无法摊薄）② context 预热池（复杂、驻留内存、跨 realm 不可共享，奥卡姆否决）③ 字节码级分片（esbuild 拆多入口按需 ReadObject——收益 <1ms 且每块仍要反序列化）。
- **最终形态（保留）**：**属性级懒加载**（已实现 6c27d6a9 + 10bff38e）：14 eager + 17 lazy 单元，eval 压至 0.02ms；ReadObject（~2ms）为 QuickJS 一次性反序列化硬成本，无法 lazy。
- 首启剩余 2ms 属引擎固有；最大头为 WAMR/libuv init（~5.5ms，73%）——未来若继续压启动，那是主战场。

## 启动时间构成（实测）

- JS_NewContext 0.4ms；JS_ReadObject ~2ms（不可省）；eval 0.02ms（lazy 已省）；WAMR/libuv init ~5.5ms。
- 模式：rodata 150KB（lz4 压缩 99KB，模式可切换）；minify 已启用。


## Timeline

- time: 2026-09-10T01:38:27
  kind: decision
  summary: "Created this page: Polyfill 内嵌强绑定：放弃拆 bundle/外置，优化内存占用为唯一方向"
  source: "2026-09-10 用户决策"
  affects: [polyfill-bundling-policy]

- time: 2026-09-10T01:38:27
  kind: decision
  summary: Rewrote compiled_truth to the new best understanding
  source: brain update-truth
  affects: [polyfill-bundling-policy]

- time: 2026-09-11T00:12:24
  kind: decision
  summary: "决策补充（2026-09-10）：放弃 context 缓存/预热池/字节码分片——属性级懒加载为最终形态"
  source: brain update-truth
  affects: [polyfill-bundling-policy]

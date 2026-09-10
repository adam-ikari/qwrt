---
id: polyfill-bundling-policy
title: "Polyfill 内嵌强绑定：放弃拆 bundle/外置，优化内存占用为唯一方向"
category: decision
status: active
tags: [polyfill, bundling, memory]
created: "2026-09-10T01:38:27"
updated: "2026-09-10T01:38:27"
---

<!-- compiled_truth -->
# Polyfill 打包策略（用户拍板 2026-09-10）

- **放弃** polyfill 拆独立 bundle / 外置文件分发（模式 B 的"分发用途"不再推进）。polyfill 与 qwrt 引擎**强绑定**：qjsc 字节码绑 quickjs-ng 版本 + pal 契约内部化，拆包的"独立升级"卖点不成立。
- polyfill 与 libqwrt 同步发版、原子升级（模式 C 内嵌 .rodata 为唯一分发形态；模式 B/D 保留为宿主调试/定制后门，非产品路径）。
- **唯一优化方向：polyfill 内存占用**（字节码体积 + 运行时驻留）。
  - 已落地：lazy 初始化（未用 API 零启动/内存成本）、QWRT_WITH_GRPC 门控（OFF 零字节）
  - 模式 A（zlib 压缩内嵌）已存在，字节码 154KB 压缩收益可量化
  - 可选后续：qjsc strip 再核对、按模块砍死代码、bytecode 去重


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

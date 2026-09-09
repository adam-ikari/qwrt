---
id: c-js-layering
title: "C/JS 分层原则与标准"
category: decision
status: active
tags: [arch, layering, policy]
created: "2026-09-09T02:29:21"
updated: "2026-09-09T02:30:15"
---

<!-- compiled_truth -->
# C/JS 分层原则与标准（判据摘要）

用户指令：C 与 JS 分层需要有原则和标准。裁决依据文档：`docs/architecture/c-js-layering.md`（从第一性原理 + 现有实现归纳，逐条判据 + 灰区四问 + 全量模块归类 + 边界裁决记录 + uvhttp 警示）。

**C 层 = 能力原语层**（四条判据，满足任意即 C）：
1. C1 无状态单调用语义（tcpWrite/tcpClose、fsRead、nativeCompress）
2. C2 字节级传输/加密/压缩，不含协议语义（tcp_io/uv_io/mbedtls/miniz）
3. C3 性能关键（实测）且接口长期稳定——唯一"下沉 C"的门，下沉后 JS 接口不变（msgq lock-free、ipc_envelope FlatBuffers 信封）
4. C4 引擎/运行时必需系统能力（libuv/quickjs-ng/wasm 引擎）

**JS 层 = 可编程逻辑层**（四条判据，满足任意即 JS）：
1. JS1 协议状态机（http/1.1、ws RFC6455、h2 RFC7540、HPACK RFC7541、grpc、proto3 全 JS）
2. JS2 策略/路由/应用逻辑（serve 路由、SW 拦截、压缩策略选择）
3. JS3 标准 API 面（WinterTC：fetch/Cache/streams/SW/Worker 等）
4. JS4 需可审计/可热替换/可随 polyfill 升级（bundle 是单一替换单元）

**灰区默认归 JS**：新模块走"边界四问"（Q1 协议/策略语义→JS；Q2 字节级单调用→C；Q3 实测性能瓶颈+接口稳定→才下沉 C；Q4 跨实现互操作/标准跟随→JS），归 C 举证责任在提议方。

**已裁决**：HPACK 首版不下沉（D-HPACK，服务端场景观察后决策，触发=实测拖累）；FlatBuffers 信封 C-only JS 零感知；spawn C 原语通用化+JS 封装 Worker（commit 606acb81）；nghttp2 否决（桥接成本+架构例外+牛刀杀鸡）；msgq 归 C（性能+接口稳定）。

**红线**：不复活 uvhttp C 服务器（协议进 C 的代价教科书：vendored 补丁负债+桥接 bug 面+功能被 C 束缚，2026-08 引入 08-24 移除，纯 JS serve() 取代）。


## Timeline

- time: 2026-09-09T02:29:21
  kind: decision
  summary: "Created this page: C/JS 分层原则与标准"
  source: "2026-09-09 用户指令：C 与 JS 分层需要有原则和标准"
  affects: [c-js-layering]

- time: 2026-09-09T02:29:52
  kind: decision
  summary: Rewrote compiled_truth to the new best understanding
  source: "2026-09-09 架构文档 docs/architecture/c-js-layering.md 落地"
  affects: [c-js-layering]

- time: 2026-09-09T02:30:15
  kind: decision
  summary: "动机：用户要求 C 与 JS 分层显式化原则和标准（2026-09-09）——ROADMAP §二.1 单句表述展开为可逐条引用判据+灰区四问+全量归类+裁决记录，落地 docs/architecture/c-js-layering.md，挂钩 grpc-http2 设计 §2.4 D-HPACK"
  source: "2026-09-09 用户指令"
  affects: [c-js-layering, oss-library-policy, qwrt-positioning]

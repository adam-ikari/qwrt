---
id: qwrt-positioning
title: "qwrt 定位：IoT 连接性中枢（gRPC/HTTP2 双端），非硬件抽象层"
category: decision
status: active
created: "2026-09-09T01:24:36"
updated: "2026-09-09T01:24:45"
---

<!-- compiled_truth -->
# qwrt 定位（用户拍板 2026-09-09）

- 目标市场 = **IoT 设备（gRPC/HTTP2 客户端连云）+ 云端边缘节点（gRPC/HTTP2 服务端）**
- gRPC/HTTP2 **客户端与服务端都是必需**：H1-H3（客户端）与 H4（服务端）均属正确定位，H4 应推进
- **设备原语（GPIO/BLE/serial 类）不是 qwrt 原生追求**——硬件抽象是宿主/其他层职责，qwrt 聚焦 JS 运行时 + 连接性能力
- 与嵌入式定位一致：小内存/快启动/确定性是护城河，连接性（gRPC/HTTP2/TLS/WS）是核心能力面
- WinterTC 子集对齐保持（标准可移植承诺）；能力面受此边界约束


## Timeline

- time: 2026-09-09T01:24:36
  kind: decision
  summary: "Created this page: qwrt 定位：IoT 连接性中枢（gRPC/HTTP2 双端），非硬件抽象层"
  source: "2026-09-09 用户定位拍板"
  affects: [qwrt-positioning]

- time: 2026-09-09T01:24:45
  kind: decision
  summary: Rewrote compiled_truth to the new best understanding
  source: brain update-truth
  affects: [qwrt-positioning]

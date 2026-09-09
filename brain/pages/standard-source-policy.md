---
id: standard-source-policy
title: "qwrt 能力面标准来源：WinterTC（ECMA-429 核心）∪ W3C Web API（补充）"
category: decision
status: active
tags: [standard, policy, wintertc, webrtc, w3c]
created: "2026-09-09T03:37:10"
updated: "2026-09-09T03:37:45"
---

<!-- compiled_truth -->
# qwrt 标准来源策略（用户拍板 2026-09-09）

- **能力面标准来源** = **WinterTC（ECMA-429 Minimum common web API，核心）∪ W3C Web API 标准（补充）**。
- WinterTC 已覆盖且已实现的能力：保持现状（ECMA-429 接口矩阵 + gtest harness，ROADMAP §二.7）。
- WinterTC **缺失**的能力：从对应 W3C 规范**补充实现**——API 面对齐该规范（接口/方法/属性/事件模型一致），**不是私有子集命名**。
- 先例归位（WinterTC 覆盖外能力按本策略定性）：
  - Service Worker / CacheStorage → W3C service-worker 规范（[[service-worker-stack]]）；
  - WebRTC → W3C webrtc-pc + webrtc-datachannel（RTCPeerConnection/RTCDataChannel/RTCSessionDescription/RTCIceCandidate/RTCConfiguration 对标本标准；实现分阶段）。
- **边界**：
  1. 标准对齐 = 接口面与语义（事件模型/方法/属性/回调）对齐规范文本；
  2. 互操作价值来自标准线格式（SDP/ICE/DTLS-SCTP/DTLS-SRTP）——与 Chrome/Firefox 对端互通；
  3. 实现完整性分阶段管理：接口面先行、内部能力分期填充（如 getUserMedia 无设备 → 正确 reject，不虚报）；
  4. 超出本策略的取舍（如媒体编解码引库）仍需走 [[oss-library-policy]] 三条件。
- **适用**：本策略是 WinterTC 之外能力面扩展的总则；与 WinterTC 冲突时 WinterTC 优先（核心），W3C 补充不覆盖已有实现。


## Timeline

- time: 2026-09-09T03:37:10
  kind: decision
  summary: "Created this page: qwrt 能力面标准来源：WinterTC（ECMA-429 核心）∪ W3C Web API（补充）"
  source: "2026-09-09 用户拍板（WebRTC API 设计会话追加指令）"
  affects: [standard-source-policy]

- time: 2026-09-09T03:37:45
  kind: decision
  summary: "2026-09-09 用户拍板：qwrt 能力面标准来源 = WinterTC（ECMA-429 最小集，核心）∪ W3C Web API 标准（补充）；WinterTC 缺失能力按对应 W3C 规范补充、API 面对齐该规范；先例归位 SW/CacheStorage→service-worker 规范、WebRTC→webrtc-pc+datachannel"
  source: "2026-09-09 用户追加指令（WebRTC API 设计会话）"
  affects: [standard-source-policy]

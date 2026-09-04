---
id: oss-library-policy
title: "开源库引入与替换原则（项目级政策）"
category: decision
status: active
tags: [deps, policy, oss]
created: "2026-09-04T13:27:08"
updated: "2026-09-04T13:27:55"
---

<!-- compiled_truth -->
# 开源库引入与替换原则

**默认自制。** 引入或替换开源库是例外动作，须同时满足三条件，缺一不动：

1. **正收益实证**：自制代码本身是风险源（已识别的正确性缺口、重复实现、维护负债），而非仅仅行数超阈值或"不够标准"。收益必须落到可删除的负债与可修复的缺口上，不接受"换上更规范"的抽象收益。
2. **原位可换**：能在不违反架构铁律（C 只给 pal 原语、协议策略在 JS）与不破坏跨层接口（PAL 原语面、bridge 字节协议、polyfill 内部耦合）的前提下原位替换。需要重写消费者或跨层接口的候选直接否决。
3. **vendor 成本可控**：C 库须 C99 兼容、零或近零传递依赖，按 deps/ 现行机制 vendor（本地快照 submodule + 随 repo 提交的补丁，见 [[quickjs-upstream-merge-strategy]]）；JS 库经 esbuild bundle 进 polyfill，首次引入 npm 供应链（license 审计、版本锁定、传递依赖）门槛高于 C 库。许可须与 MIT 兼容。

## 2026-09 全量审计基线（27 个自制模块）

- **唯一建议替换**：`src/uv_io.c` 手写 HTTP 客户端解析 → **llhttp**。约 700 行可删（parse_http_response ×2、双份 chunked 状态机、CONNECT 解析、URL 解析），消 ~290 行重复，修复 obs-fold / 多值 Transfer-Encoding / 双 Content-Length 冲突等 5 类健壮性缺口。它是全项目唯一"自制代码本身是风险源"的形状：既不在 JS 层（拿不到规范测试），也不是薄绑定（自造状态机）。
- **保留 + 观察**（触发条件写死，不许"以后再说"）：
  - `polyfill/src/url.js`（499 行，缺 IDNA）：实测解析 bug 出现 → 换 whatwg-url。
  - `polyfill/src/url-pattern.js`（247 行，子集实现）：需要 Service Worker scope 级匹配语义 → 换 GoogleChromeLabs/urlpattern-polyfill。
- **保留（替换负收益）**，共同形态是"正确的薄层"：C 层 tcp_io.c / ext_crypto.c / ext_compress.c / debugger_dap.c / wasm 引擎绑定 / msgq.c（绑定与胶水，算法全在 mbedTLS/miniz/wasm 引擎）；JS 层 streams.js / structured-clone.js / fetch.js / protobuf.js / http2.js / hpack.js / websocket.js（协议逻辑有 WHATWG/RFC 可对照且有 harness 测试，开源候选均需垫 Node API 或破坏内部耦合）与其余 ~20 个杂项胶水。
- 候选库已否决：libwebsockets（自带事件循环，架空 pal.tcp*，违架构铁律）、picohttpparser（只吃头解析，收益不足）、nghttp2（vendor + C 绑定层成本远超 660 行现实现）、protobufjs（~200KB 且假设 Node 生态）、whatwg-fetch/undici/ws polyfill（需 XHR/Node API）。
- 顺手清理项：`polyfill/src/index.js` 的 queueMicrotask 守卫 polyfill 是死代码（quickjs-ng 内置），可删。

## 后续决策的用法

新模块默认自制 + harness 测试；要引库时先对三条件逐条举证，并核对上面的基线裁决；观察对象只在触发条件成立时换，触发条件变更须走本页 update-truth。


## Timeline

- time: 2026-09-04T13:27:08
  kind: decision
  summary: "Created this page: 开源库引入与替换原则（项目级政策）"
  source: "开源库替换审计会话（oss-replace-audit）"
  affects: [oss-library-policy]

- time: 2026-09-04T13:27:55
  kind: decision
  summary: "固化开源库审计+评审结论为项目级引入与替换原则：默认自制+三条件门槛+2026-09 全量审计基线（uv_io.c→llhttp 唯一建议替换；url.js/url-pattern.js 观察对象带触发条件；其余保留）"
  source: "开源库替换审计会话（oss-replace-audit，5 scout 并行审计）"
  affects: [oss-library-policy]

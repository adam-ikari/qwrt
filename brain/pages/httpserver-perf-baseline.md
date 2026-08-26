---
id: httpserver-perf-baseline
title: HTTPServer pure-JS performance baseline
category: decision
status: active
tags: [httpserver, perf, serve]
created: "2026-08-24T15:29:31"
updated: "2026-08-26T06:02:44"
---

<!-- compiled_truth -->
M2-D3 连接生命周期完成：
- serve({idleTimeout})：空闲超时关闭连接（默认30s，0禁用）
- resetIdle() 每请求重置；WS 升级 + onclose 时清除 idle timer
- sendResponse 对 keep-alive 响应后重置 idle timer
- onclose 清理 conns 数组（防泄漏）

关键修复（存量 bug，D3 暴露）：
- polyfill timers.js setTimeout 未存 currentPalHandle，handle==0（首个 pal.timerStart）时
  clearTimeout 的 'handle > 0' guard 为 false → pal.timerStop 永不调用 → 底层 libuv timer
  保持活跃 → serve().close() 后事件循环不退出 → test_websocket_client 超时
- 修复：setTimeout 存 currentPalHandle=handle，clearTimeout 总能停止
- 影响面：任何 handle==0 的 setTimeout 被 clearTimeout 取消时都会泄漏 timer

验证：e2e 12/12（ws client 不再超时）；idle-close 实测通过；ctest offline 13/13


## Timeline

- time: 2026-08-24T15:29:31
  kind: decision
  summary: "Created this page: HTTPServer pure-JS performance baseline"
  source: created via brain create-page
  affects: [httpserver-perf-baseline]

- time: 2026-08-24T15:39:03
  kind: decision
  summary: "纯JS serve() 性能基线与CI回归守卫"
  source: "test/bench_httpserver.py + ci.yml e1d6e2fa"
  affects: [httpserver-perf-baseline]

- time: 2026-08-25T03:27:40
  kind: decision
  summary: "fsReadBinary + HTML文件支持（同步路径绕过uv_io_fs_read bug）"
  source: 2aa39130
  affects: [httpserver-perf-baseline]

- time: 2026-08-25T05:12:12
  kind: decision
  summary: "开发路线图 ROADMAP.md (M0-M5) 确立"
  source: 39b5980b
  affects: [httpserver-perf-baseline]

- time: 2026-08-25T05:17:32
  kind: decision
  summary: "ROADMAP.md 重写为整体项目路线图（7 领域 A-G）"
  source: 0289991b
  affects: [httpserver-perf-baseline]

- time: 2026-08-25T08:16:29
  kind: decision
  summary: "M1-A1 文件IO异步化回归完成 + 网络IO异步确认"
  source: "638810fe + 并发验证"
  affects: [httpserver-perf-baseline]

- time: 2026-08-25T08:50:05
  kind: decision
  summary: "M1 完成：gzip e2e恢复 + fs全路径测试"
  source: "4a95fc0b + ROADMAP"
  affects: [httpserver-perf-baseline]

- time: 2026-08-25T08:59:57
  kind: decision
  summary: "M1 里程碑正式收尾"
  source: "e2e 10/10 + ctest 13/13 + ROADMAP"
  affects: [httpserver-perf-baseline]

- time: 2026-08-26T04:03:35
  kind: decision
  summary: "D1 HTTP/1.1 协议细节完成"
  source: ddbd396b
  affects: [httpserver-perf-baseline]

- time: 2026-08-26T06:02:44
  kind: decision
  summary: "D3 连接生命周期完成 + timers 存量 bug 修复"
  source: fd743442
  affects: [httpserver-perf-baseline]

---
id: fetch-proxy-support
title: "fetch 出站代理（HTTP_PROXY / CONNECT 隧道）"
category: decision
status: active
tags: [fetch, proxy, connect, tls, uv_io]
created: "2026-08-29T00:20:00"
updated: "2026-08-29T00:20:00"
---

<!-- compiled_truth -->
- **背景**：B2「fetch 完善」剩余项「代理」—— 出站 fetch 需要能走公司/环境 HTTP 代理。此前 C 层 `uv_io.c` 只直连源站，TLS 由 mbedTLS 在 C 层端到端完成，JS 层无法自己搭 CONNECT 隧道（无裸 socket、TLS 在 C）。
- **决策**（2026-08-29）：**在 C 层 `uv_io.c` 透明实现，JS/polyfill 零改动**。读环境变量 `HTTP_PROXY`/`http_proxy`（http 源）、`HTTPS_PROXY`/`https_proxy`（https 源）、`NO_PROXY`/`no_proxy`（逗号分隔，`*` 全匹配，后缀按点边界匹配）。
  - http 源走代理：请求行改**绝对式**（RFC 7230 5.3.2 `GET http://host[:port]/path HTTP/1.1`），Host 头保持源站。
  - https 源走代理：TCP 连代理后发 `CONNECT host:port HTTP/1.1`，等 `HTTP/1.1 2xx` 后**在同一 socket 上启动 mbedTLS 握手**（主机名校验仍对源站，TLS 端到端）；CONNECT 响应后残留字节喂给 TLS 读缓冲。
  - 代理 URL 只支持 `http://host[:port]`（默认 80）；**非法/不支持 scheme（如 socks5://、https 代理）→ 请求立即失败（fail closed），不静默回退直连**（避免流量绕过代理的意外）。NO_PROXY 命中（或环境变量为空）→ 直连。
  - 流式与非流式 fetch 共用 `uv_io_http_connect_cb`/`send_request`，天然同时生效。
- **实现**：`uv_io_http_apply_proxy`（DNS/连接目标 `uv_io_http_connect_host/port`）、`uv_io_http_proxy_request_target`、`uv_io_http_send_connect`、`uv_io_http_proxy_connect_read_cb`、`uv_io_http_start_tls`（从 connect_cb 抽出，CONNECT 续用）。全部 `#if QWRT_WITH_TLS` 守卫（无 TLS 构建零改动）。代理主机经 DNS 解析，DNS 回调无感知。
- **测试**：新增 `test/test_fetch_proxy_e2e.py`（stdlib socket 实现的绝对式转发 + CONNECT 隧道代理）7/7 通过 —— http 经代理、流式经代理、NO_PROXY 精确 IP、NO_PROXY `*`、CONNECT 隧道（200 后 TLS 续接，用垃圾 TLS 响应的本地 sink 验证握手失败路径）、CONNECT 403 拒绝、非法 scheme fail-closed。既有 ctest offline 15/15、httpserver e2e 17/17 无回归。
- **未做（明确范围外）**：SOCKS 代理、TLS-to-proxy（https:// 代理 URL，需在 C 层对代理额外 TLS）、代理 URL userinfo Basic 认证（解析时显式拒绝 `@`）。G2「代理 example」（JS 反向代理服务示例）是独立任务，与此页无关。


## Timeline

- time: 2026-08-29T00:20:00
  kind: decision
  summary: "fetch 出站代理：C 层 HTTP_PROXY/HTTPS_PROXY/NO_PROXY 环境变量，http 绝对式请求行 + https CONNECT 隧道，TLS 端到端；fail closed 不静默直连"
  source: brain update-truth
  affects: [fetch-proxy-support]

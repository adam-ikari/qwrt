---
slug: flow
title: Key flows
role: key flows
updated: "2026-09-01T09:15:17"
---

# Key flows

## End-to-end path of a typical request（宿主→JS 消息）

```mermaid
sequenceDiagram
  participant H as Host C
  participant Q as qzjs thread
  participant L as libuv
  participant J as QuickJS runtime
  H->>Q: qz_create(cfg) — initial_script
  Q->>L: uv_run loop
  Q->>J: eval initial_script
  J->>J: postMessage({ok:true})
  J->>L: register pending timers/io
  Q-->>H: message_cb fired
  H->>Q: qz_post_message({\"cmd\":\"eval\",\"code\":\"...\"})
  Q->>J: __qz_dispatch__ → onmessage eval
  J->>J: run code, postMessage result
  Q-->>H: message_cb returns result
  H->>Q: qz_destroy() — request stop
  Q->>L: uv_stop + join
```

## Other important flows

- Worker 生命周期：qzjs 内部新建线程 → QuickJS 独立 JSRuntime → __qz_dispatch__ 消息通道
- MessagePort 跨线程：pal.portCreate() 分配全局 port id 对 → 序列化到字节通 → 接收侧 __qz_port_from_ref__ 重建代理
- HTTPServer 请求：serve() raw TCP accept → JS 解析 HTTP/1.1 → JS handler（fetch 风格）→ JS 构造 Response → 经 tcp/TLS 原语回写

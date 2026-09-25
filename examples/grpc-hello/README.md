# grpc-hello — gRPC 四形态自连

单个进程里 `serve()` 起 gRPC 服务端（h2c 明文，免证书），再用客户端连自己，
把四种 RPC 形态各跑一遍并核对结果：

| 形态 | RPC | 形状 |
|---|---|---|
| unary | `Echo` | 一个请求 → 一个响应 |
| 服务端流式 | `CountUp` | 一个请求 → 连续 N 个响应 |
| 客户端流式 | `Collect` | 连续 N 个请求 → 一个响应 |
| 双向流式 | `Chat` | 连续 N 个请求 → 连续 N 个响应 |

## 运行

**需要 `QZ_WITH_GRPC=ON` 构建**（默认 OFF 时 gRPC 栈完全不入 bundle）：

```bash
cmake -B build_grpc -DCMAKE_BUILD_TYPE=Release -DQZ_WITH_GRPC=ON
cmake --build build_grpc --target qz_cli qz_rt --parallel
./build_grpc/qzjs examples/grpc-hello/grpc-hello.js
```

## 期望输出

```
[grpc] ok   unary Echo  →  {"text":"echo: hi qzjs"}
[grpc] ok   server-stream CountUp  →  [1,2,3]
[grpc] ok   client-stream Collect  →  {"total":6}
[grpc] ok   bidi Chat  →  [{"text":"reply-a","seq":1},...]
[grpc] 四形态完成：4 通过 / 0 失败
```

## 要点

- `grpc.loadProto(text)` 解析最小 proto3，返回方法注册表；`reg.service(...)`
  / `svc.method(...)` 取到方法句柄。
- **服务端 handler 契约按形态区分**：
  - unary → 普通函数，收 `call.request`，返回响应对象
  - 服务端流式 → `async function*`，`yield` 的每项是一个响应消息
  - 客户端流式 → 收全请求数组 `call.request`，返回单个响应
  - 双向流式 → 收全请求数组，返回整个响应数组
- **客户端**：`invoke` / `invokeStream` / `invokeClientStream` / `invokeBidi`。
  后三者都是 Promise 风格「发完整请求流 → 收完整响应流」。
- `createInsecureChannel()` 是明文 h2c，服务端 `serve({port, grpc})` 嗅探
  24 字节 h2 前导自动切换，无需 TLS。TLS 变体见测试
  `test/grpc_server_tls_e2e.mjs`（ALPN h2）。
- 端口在文件顶部 `var PORT = 18090`。

## 相关文档

- [JS API: grpc](/js-api/grpc) — 客户端与服务端完整 API
- [JS API: serve](/js-api/serve) — `serve({grpc})` 分发

# HTTP/2 + gRPC 可行性调研与设计 — 纯 JS 最小 h2 栈 + 动态 protobuf + 分阶段落地

> 状态：设计决策（只读探查产出，未改 src/，未 git add/commit，未验证运行）
> 日期：2026-09-03
> 范围：qwrt 运行时（QuickJS-ng 嵌入式/边缘）的 HTTP/2 与 gRPC 能力。目标是**云原生边缘节点调用上游 gRPC 服务**（客户端 unary 先行），最终覆盖流式客户端与服务端 gRPC。
> 背景：用户已确认"需要 gRPC"——gRPC 是 h2 硬必须场景（gRPC 线协议基于 HTTP/2），故本设计为 h2 选型 + gRPC 分层落地。

**核心结论（TL;DR）**
1. **传输缺口**：`pal.tcpConnect` 目前**无 TLS 客户端**（只有 `tcpListen` 服务端 TLS）；mbedTLS 已编入 `MBEDTLS_SSL_ALPN` 但 qwrt **从未调用 ALPN API**。h2 over TLS 需要 ALPN `h2`，这是唯一必须先补的 C 缺口。
2. **h2 栈选型**：**方案 A（纯 JS 最小 h2 子集）为推荐主路径**，符合"C 只给原语、协议策略在 JS"铁律；方案 C（HPACK 下沉 C）作为 Phase 3 的**增量回退**决策点；方案 B（vendored nghttp2）否决。
3. **protobuf**：推荐**纯 JS 动态 proto3 子集**（运行时解析 .proto），无工具链步骤；预编译（protoc codegen）否决。
4. **分阶段**：Phase 0 = C 补 TLS 客户端 + ALPN h2（~1-2 人日）；Phase 1 = 客户端 unary + 动态 protobuf（~10-15 人日）；Phase 2 = 流式客户端（~3-5 人日）；Phase 3 = 服务端 gRPC（~1.5-3 周）。验证对端可用 Node `@grpc/grpc-js` / `grpcurl`，Phase 1 可先用明文 h2c 全 JS 跑通（不碰 C）。

---

# 1. 现状（只读探查，2026-09-03）

## 1.1 `pal.tcp*` API 面（确切清单）

`src/tcp_io.c:1061-1067` 注册的全部 TCP 原语：

| PAL 函数 | 签名 | 说明 |
|---|---|---|
| `tcpConnect` | `(host, port, callbacks)` | 客户端连接。callbacks = `{onconnect, ondata(ArrayBuffer), onerror(msg), onclose}`。**读是 ondata 推式回调，无 `tcpRead`**。`src/tcp_io.c:523` |
| `tcpWrite` | `(handle, data)` | data = string \| ArrayBuffer \| Uint8Array。`src/tcp_io.c:633` |
| `tcpClose` | `(handle)` | `src/tcp_io.c:718` |
| `tcpListen` | `(port, hostname, backlog, onconnection[, tls])` | 服务端监听。`tls = {cert, key, sni:{host:{cert,key}}}`（服务端 TLS 已支持，含 SNI 多证书 + 热重载）。**无 `tcpAccept`**——`onconnection(conn_handle)` 回调直接交付新连接句柄（连接句柄复用同一套 ondata/onerror/onclose）。`src/tcp_io.c:907, 890` |
| `tcpCloseListener` | `(handle)` | `src/tcp_io.c:1029` |
| `tcpReloadTls` | `(listener, tlsObj)` | TLS 证书热重载。`src/tcp_io.c:998` |

**关键事实：`tcpConnect` 无 TLS 选项。** 客户端路径 `js_pal_tcp_connect`（`src/tcp_io.c:523-630`）只解析 host/port/callbacks，`c->use_tls` 恒为 0（calloc 零初始化）。TLS 仅出现在服务端 accept 路径（`src/tcp_io.c:855-871`：`mbedtls_ssl_setup + set_bio`）。佐证：`polyfill/src/websocket.js:113` 明写 `wss:// not supported yet`。

## 1.2 mbedTLS：ALPN 可用但未接线

- 版本 **mbedTLS 3.6.6**（`deps/mbedtls/include/mbedtls/build_info.h`）。
- 配置（`deps/mbedtls/include/mbedtls/mbedtls_config.h`）：`MBEDTLS_SSL_ALPN` ✓、`MBEDTLS_SSL_CLI_C` ✓、`MBEDTLS_SSL_SRV_C` ✓、`MBEDTLS_SSL_PROTO_TLS1_2` ✓、TLS1.3（EPHEMERAL + COMPATIBILITY_MODE）✓。
- ALPN API 存在：`mbedtls_ssl_conf_alpn_protocols(conf, protos)` 与 `mbedtls_ssl_get_alpn_protocol(ssl)`（`ssl.h:4309, 4320`）。
- **全仓 grep `alpn` 仅命中 mbedtls 头文件**——`src/tcp_io.c`、`src/uv_io.c` 均未调用。结论：**ALPN 编译进来了，但 qwrt 一次都没用**。

## 1.3 fetch/https 客户端：有完整 TLS 客户端，但无 ALPN

`src/uv_io.c` 为 `pal.httpRequestStream` 实现了一套完整的 TLS 客户端（`tls_init_op`，`uv_io.c:232-286`）：
- `MBEDTLS_SSL_IS_CLIENT`，系统 CA bundle 加载（Debian/RHEL/OpenSUSE/FreeBSD 路径），`mbedtls_ssl_conf_authmode(VERIFY_REQUIRED)`（注释明言：**恒为 VERIFY_REQUIRED，绝不静默降级**），`mbedtls_ssl_set_hostname`（SNI + 主机名校验），握手后二次校验 `mbedtls_ssl_get_verify_result`（纵深防御）。
- 同样**无 ALPN 配置**。

这条 TLS 客户端代码是 Phase 0 的直接可复用模板（照搬到 `tcp_io.c` 的 `tcpConnect` 即可）。

## 1.4 纯 JS 协议栈先例（架构铁律的活证据）

- `polyfill/src/websocket.js`：RFC 6455（握手、帧解析、掩码、close/ping/pong）**全 JS**，传输只用 `pal.tcpConnect/tcpWrite/tcpClose`。
- `polyfill/src/http-server.js`：`serve()` 的 HTTP/1.1 解析/路由/响应序列化 + WS 服务端**全 JS**，只用 `pal.tcpListen/tcpWrite/tcpClose`；TLS 服务端经 `tcpListen` 第 5 参 `{cert,key}`。
- 模式固定：**C = bind/connect/read/write/close/加密传输；协议状态机 = JS**。gRPC/h2 应沿用同一模式。

## 1.5 `deps/`：无 h2/grpc/protobuf 库；miniz 可复用于 gzip

- `deps/` = googletest / libuv / mbedtls / miniz / quickjs-ng / wamr / wasm3。**无任何 h2、gRPC、protobuf 库**（全仓 grep `grpc|http2|protobuf|HPACK` 仅命中 googletest 与 libuv 文档的无关引用）。
- `polyfill/src/fetch.js:5` 用 `pal.httpRequestStream`（C 层 HTTP/1.1）做字节 body + ReadableStream 流式响应——**但那是 HTTP/1.1，帧层/头压缩全在 C，与 h2 无关，gRPC 不可复用**。gRPC 客户端必须走 `pal.tcpConnect` 原始字节流。
- `src/ext_compress.c`：`pal.nativeCompress/nativeDecompress(data, 'gzip'|'deflate'|'deflate-raw')` + 流式 `deflateCreate/deflatePush/inflateCreate/inflatePush`（miniz）。**gRPC 消息级 gzip 压缩可直接复用 `nativeCompress('gzip')`**（HPACK 不是 zlib，miniz 对 HPACK 无帮助——见 §2.4）。

## 1.6 关键差距汇总

| # | 差距 | 影响 | 处理 |
|---|---|---|---|
| G1 | `pal.tcpConnect` 无 TLS 客户端 | 无法对远程 gRPC（TLS 443） | **Phase 0 必修**（C，照搬 uv_io 模板） |
| G2 | 全仓无 ALPN | h2 over TLS 无法协商（RFC 7540 §3.4 强制 ALPN `h2`） | **Phase 0 必修**（C，`mbedtls_ssl_conf_alpn_protocols`） |
| G3 | 无 h2 帧层/HPACK/protobuf 代码 | 无现成栈 | 纯 JS 新增（Phase 1+） |
| G4 | 服务端 `tcpListen` TLS 无 ALPN（未接 `h2`） | 服务端 gRPC（Phase 3）需 `serve()` 感知协商协议 | Phase 3（小改 C + JS 嗅探） |

> 注意：Phase 1 **可以**先用**明文 h2c**（prior knowledge，无 TLS）跑通全 JS 栈——gRPC 明文只发连接前导 `PRI * HTTP/2.0...` 即可，`tcpConnect` 现状就够。G1/G2 只在需要 TLS 生产端点时才成为硬阻塞。

---

# 2. HTTP/2 栈选型（核心决策）

## 2.1 方案 A：纯 JS 自造最小 h2 子集

**gRPC 实际需要的 h2 能力**（对应"能否显著裁剪"）：

| h2 特性 | gRPC 需要？ | 说明 |
|---|---|---|
| 连接前导 + SETTINGS 握手 | ✅ | 客户端发 `PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n` + SETTINGS；收服务端 SETTINGS 并 ACK，处理 peer ACK |
| HEADERS / CONTINUATION | ✅ | 请求头 + 响应头 + **trailers**（gRPC 状态在 trailers HEADERS 帧）。需处理 >16KB 头块拆分/合并 |
| DATA | ✅ | 消息体；>MAX_FRAME_SIZE(16384) 分片，END_STREAM 在末帧 |
| SETTINGS | ✅ | 含 `INITIAL_WINDOW_SIZE`（可调大省流控往返） |
| PING | ✅ | 服务端 keepalive ping → 必回 ACK |
| RST_STREAM | ✅ | 取消流/错误 |
| GOAWAY | ✅ | 优雅关闭处理（停止新流、排空在途流） |
| WINDOW_UPDATE | ✅ | 流控必须（响应 >64KB 需补充连接+流窗口） |
| 服务端推送（PUSH_PROMISE） | ❌ | gRPC 不用，**客户端可整帧忽略**（收到即 GOAWAY/PROTOCOL_ERROR 或丢弃） |
| 优先级/依赖（PRIORITY） | ❌ | 可忽略（收到优先级帧可丢弃） |
| 帧填充（PADDED） | ❌ | 服务端不填；解码时**必须**跳过 padding 字节（零成本） |
| HPACK 动态表（编码侧） | 可省 | 编码用"字面量不索引"即可，零动态表维护 |
| HPACK 动态表（解码侧） | ✅ | 服务端可能索引动态表，**必须**完整解码（含表大小更新） |
| HPACK Huffman（编码侧） | 可省 | 明文头字段合法；省一张编码表 |
| HPACK Huffman（解码侧） | ✅ | 服务端响应头几乎必用 Huffman，**必须**解码（257 项表，机械） |

**工程量估算（纯 JS）**：
- h2 客户端引擎：帧解析（9 字节头 + 载荷）、前导/SETTINGS、流状态机（idle→open→half-closed→closed，客户端流 ID 奇数递增）、连接+流双窗口流控、PING/RST/GOAWAY/CONTINUATION、多流复用（并发 RPC 共享连接）——约 **500-700 行**。
- HPACK：静态表（61 项）+ 动态表（FIFO + 大小记账）+ Huffman 解码表（257 项）+ 整数/字面量编解码——约 **300-400 行**（含表数据）。
- gRPC unary 层：5 字节消息前缀（1 压缩标志 + 4 大端长度）、请求头/响应头/trailers、grpc-status 映射、deadline、metadata、错误模型——约 **200-300 行**。
- 合计 Phase 1 JS 约 **1000-1400 行**，是 WS（~500 行）的 2-3 倍，仍处手写可控范围。

**正确性风险（诚实量化）**：
- HPACK 与真实服务端（grpc-go / nghttp2）互操作是最大风险点：Huffman 边界、动态表状态同步、表大小更新顺序。缓解：RFC 7541 附录 C 官方向量 + 与 nghttp2/grpc-go 对测（§6）。
- 流控漏发 WINDOW_UPDATE → 大响应卡死。缓解：每收 DATA 按量补发连接+流窗口（模式简单恒正确）。
- SETTINGS/ACK 时序、帧类型非法处理（RFC 7540 §5.1 连接错误 vs 流错误分级）。需对照规范，但 gRPC 对端（grpc-go）行为规整。

## 2.2 方案 B：vendored nghttp2（否决）

- 优点：h2 帧层 + HPACK + 流控 + 流管理全成熟，省正确性风险。
- 否决理由：
  1. **破坏架构铁律**：协议策略进 C，与 WS/serve() 的"纯 JS 协议栈"模式割裂，h2 成为唯一例外。
  2. **集成成本 ≈ 自建代价**：nghttp2 是 C 回调式 session API（`nghttp2_session_mem_recv/send` + `on_frame_recv` 等回调），喂 mbedtls 解密的字节流、把帧事件桥回 JS——**这个 JS↔C 桥本身就是"再造一个 h2 事件接口"**，桥接层的工程量和 bug 面接近纯 JS 方案。
  3. **体积/全源码成本**：nghttp2 库本体 ~2 万行 C，CMake 可 `add_subdirectory`（与 mbedtls/miniz 同模式）——技术可行，但为一个"gRPC 需要的 h2 子集"引整个引擎，属于杀鸡用牛刀。
  4. gRPC 层（帧语义、trailers、状态）无论方案 A/B **都还得自己写**——nghttp2 只省帧/HPACK，不省 gRPC。

## 2.3 方案 C：混合（C 帧原语 + JS 状态机）

两种形态：
- **C 层做"字节→帧"解析原语**（`pal.http2Feed(handle, bytes)` 吐出帧对象）——但帧只在流上下文中才有意义，把"帧解析"和"流状态机"劈成 C/JS 两半会两头不讨好，不推荐作为默认形态。
- **务实形态**：C 只补**缺失的传输原语**（G1/G2：TLS 客户端 + ALPN），必要时再加**HPACK 编解码原语**（`pal.hpackEncode/Decode`）；帧层 + 流状态机留在 JS。这实质是"A + 把 HPACK 这一字节级重活下沉 C"。

## 2.4 推荐：A 主路径 + C 增量回退点

**选择：方案 A（纯 JS h2 子集）为 Phase 1/2 主路径；Phase 3 前做一次决策——若 HPACK 编码/解码在服务端场景下正确性或性能成为拖累，仅把 HPACK 下沉为 C 原语（方案 C 务实形态），帧/流状态机始终留在 JS。**

理由：
1. 架构一致性：WS（RFC 6455）与 serve()（HTTP/1.1）都是纯 JS over raw TCP；h2 虽大两倍但性质相同，走同一模式，无架构例外。
2. 风险可控：gRPC 需要的 h2 是**明确子集**（§2.1 表），无服务端推送/优先级依赖；风险集中在 HPACK 解码，而该风险**与选型无关**（方案 B 也要把 HPACK 结果桥回 JS）。
3. 增量可退：选型不是一锤子买卖——Phase 3 前若 HPACK 下沉 C（复用 miniz 的扩展模式，约 150-250 行 C），JS 侧接口不变，收益集中在服务端并发/大消息场景。
4. nghttp2 的桥接成本使其不划算（§2.2）。

**明确不做：gRPC-Web。** gRPC-Web 是 HTTP/1.1 兼容子集，但要求服务端有 gRPC-Web 网关/支持（Envoy、grpc-web proxy）。用户场景是**直接调用原生 gRPC 服务**（grpc-go / grpc-java 等标准实现），它们默认不提供 gRPC-Web 端点。gRPC-Web 无法满足需求，排除。

---

# 3. protobuf 方案

## 3.1 动态解析 vs 预编译

| 维度 | 动态（运行时解析 .proto / FileDescriptorSet） | 预编译（protoc codegen） |
|---|---|---|
| 工具链依赖 | 无（.proto 文本或 .proto.bin 作为资源携带） | **需要 protoc + 生成插件**，违反"无工具链步骤"嵌入式原则 |
| 新增 message | 运行时加载新 .proto 即可 | 需重新生成 + 重编 |
| 运行时开销 | 首次解析一次性；wire 编解码与预编译等价 | — |
| 工作量 | .proto 子集解析器 ~400-600 行 JS | 生成器本身不可行（需 protoc），生成物是外部步骤 |

**选择：动态。** 具体为**纯 JS proto3 子集解析器**（运行时直接解析 `.proto` 文本，不自带 protoc 产物），qyrt 的 fetch/WS 同风格（手写、无依赖、源码内嵌）。备选：若 schema 覆盖爆表，可退化为"运行时加载 protoc 预编译的 `FileDescriptorSet` 二进制"（应用方在开发期跑一次 protoc，qyrt 只解析二进制 descriptor，解析器更小更稳）——作为开放问题 O1 保留。

**proto3 子集范围**（首版）：message、enum、嵌套 message、标量字段（double/float/int32/int64/uint32/uint64/sint32/sint64/fixed32/fixed64/sfixed32/sfixed64/bool/string/bytes）、repeated（含 packed）、map、oneof、`optional`（proto3 显式）、import（同包合并）、注释忽略、`service`/`rpc` 声明（供注册表暴露方法签名，不走解析 wire）。**well-known types**：`google.protobuf.Empty`（空 message，零成本）原生支持；`Timestamp`/`Duration`/`Struct`/`Any` 等做成内置 descriptor（约 100-200 行），首版可只含 Empty + Timestamp（边缘节点常见）。

**工作量**：wire 编解码（varint/zigzag/length-delimited/packed/fixed32/64、tag 解析）~150-250 行；schema 模型 + .proto 解析器 ~400-600 行；well-known ~100-200 行。合计 **~700-1000 行 JS**，对标 protobufjs 的浏览器子集但**无依赖、无 Buffer/process shim**。

**备选提及**：protobufjs 是成熟的纯 JS 实现，但依赖树大（自带 .proto 解析 + Long + Buffer shim），与 qwrt 极简手写风格不合；仅在 proto3 覆盖需求失控时作为 vendored 回退（O2）。

## 3.2 64 位整数策略

QuickJS-ng 原生支持 BigInt。策略：**int64/uint64 默认返回 BigInt；提供 `{int64AsString: true}` 选项返回十进制字符串**（与 protobuf JSON 映射一致，兼容只懂字符串的下游）。编码侧接受 number（安全范围）/BigInt/字符串。放开放问题 O3 确认默认值。

---

# 4. gRPC 范围与分阶段

## Phase 0 — 传输缺口（C，必须先做）

**目标**：让 `tcpConnect` 具备 TLS 客户端 + ALPN h2（G1/G2），并为服务端预留 ALPN（G4）。

- `src/tcp_io.c`：给 `tcpConnect` 加可选第 4 参 `opts = {tls: {ca?, servername?, alpn:['h2'], verify?}}`（**向后兼容**，旧三参调用不变；WS 的 wss:// 未来同路复用）。
- C 实现**照搬 `uv_io.c` 的 `tls_init_op` 模板**（`uv_io.c:232-286`）：`MBEDTLS_SSL_IS_CLIENT`、系统 CA 或显式 `ca`、`VERIFY_REQUIRED`（保持"绝不静默降级"纪律，`verify:false` 不提供，测试走明文 h2c）、`mbedtls_ssl_set_hostname`（SNI + 主机名校验）、握手后 `mbedtls_ssl_get_verify_result` 二次校验 + **新增** `mbedtls_ssl_conf_alpn_protocols({.., "h2", NULL})`，握手后 `mbedtls_ssl_get_alpn_protocol` 非 "h2" → `onerror("h2 not negotiated")` 并关闭。
- 握手状态机沿用现有 TLS 路径（`tcp_io.c:400-442` 的 handshake/read 循环模式）。
- 服务端（Phase 3 预留，可同期做或后置）：`tcpListen` 的 tls 对象加 `alpn:['h2','http/1.1']`，经 `mbedtls_ssl_conf_alpn_protocols` 配置；连接句柄暴露 `negotiatedProtocol` 供 JS 嗅探。

**量级**：~150-250 行 C。**1-2 人日**。

## Phase 1 — 客户端 unary + 动态 protobuf（最小可用）

**目标**：`grpc.createChannel(url).invoke(method, req) → Promise<resp>` 调通远程 gRPC 一元 RPC。

- 纯 JS 新增 `polyfill/src/`：
  - `h2.js` — 客户端 h2 引擎（§2.1 帧子集 + 流控 + 多流复用）。
  - `hpack.js` — HPACK 编解码（编码走字面量不索引，解码全量）。
  - `grpc.js` — gRPC unary 语义：请求头（`:method POST`、`:path /pkg.Svc/M`、`:authority`、`content-type: application/grpc`、`te: trailers`、`grpc-accept-encoding`）、5 字节消息前缀、响应初始头 + DATA + **trailers**（grpc-status/grpc-message/grpc-status-details-bin）、deadline（`grpc-timeout` 头 + 本地定时器 → RST_STREAM + DEADLINE_EXCEEDED）、metadata（头键小写，`-bin` 值 base64）、`grpc-encoding: identity`（Phase 1 不启用 gzip，`nativeCompress` 留给 Phase 2 顺手加）。
  - `protobuf.js` — §3 动态 proto3 子集（解析器 + wire 编解码 + 注册表）。
- 接线：`polyfill/src/index.js` + `build.js` 注册新模块；新增全局 `grpc` 对象（或随 polyfill 导出）。
- 传输：`pal.tcpConnect`（明文 h2c 直通；TLS 端点走 Phase 0）。
- 错误模型：grpc-status 码（OK=0/Cancelled=1/Unknown=2/InvalidArgument=3/DeadlineExceeded=4/NotFound=5/…/Internal=13/Unavailable=14/Unauthenticated=16）映射为 `grpc.StatusError`（含 code、message、details、metadata）。

**量级**：JS ~1700-2400 行（h2 500-700 + HPACK 300-400 + gRPC 200-300 + proto3 700-1000）+ 测试向量。**10-15 人日**（2-3 周单人）。**验收**：对真实 grpc-go / `@grpc/grpc-js` 测试服务成功调用 unary，状态/trailers/错误/deadline/TLS(若启用) 全对（§6）。

## Phase 2 — 流式客户端

**目标**：三种流式 RPC 形态（server-streaming / client-streaming / bidi）在 Phase 1 引擎上复用。

- h2 引擎本就多流复用，流式 = gRPC 层暴露异步迭代接口 + 流控健壮性（大负载、多消息、WINDOW_UPDATE 连续补发）。
- API：server-streaming 返回 `AsyncIterable`（`for await`），client-streaming 接收 `AsyncIterable` 请求 → Promise<resp>，bidi 双向迭代。
- 顺手：`grpc-encoding: gzip` 消息压缩（复用 `pal.nativeCompress/nativeDecompress('gzip')`，miniz 已在）。
- **量级**：~300-500 行 JS。**3-5 人日**。**验收**：大消息（>64KB、>16MB）流式往返 + 压缩位真伪校验。

## Phase 3 — 服务端 gRPC

**目标**：`serve()` 支持被调用（边缘节点对外提供 gRPC 服务）。

- C 小改：`tcpListen` TLS 加 ALPN（G4）；明文路径由 JS 嗅探连接前导 `PRI * HTTP/2.0` 自动切 h2。
- JS 新增服务端 h2 引擎（编码侧 HPACK 现在需要动态表/可索引优化）+ gRPC 服务端语义（解析请求头/消息、回响应 + trailers、并发流、流控）。
- **决策点 D-HPACK**：此阶段前评估是否把 HPACK 下沉 C（`pal.hpackEncode/hpackDecode` 原语，复用 miniz 扩展模式）——服务端要编解码大量头且多路并发，C 更稳更快；JS 接口不变。
- **量级**：~800-1200 行 JS + 可选 ~150-250 行 C。**1.5-3 周**。

---

# 5. 接口设计（草案，实施时迭代）

## 5.1 gRPC 客户端 API

参照 **gRPC-Web 的 promise 语义 + 原生 gRPC 线协议**（推荐：unary 用 promise，简单直接，贴合边缘节点"调上游"主场景；不做 grpc-go 的 `ClientStream` 显式对象形态，直到 Phase 2 流式才引入迭代器）。

```js
// 注册表（动态 .proto，Phase 1 核心）
const reg = grpc.loadProto(`
  syntax = "proto3";
  package helloworld;
  message HelloRequest { string name = 1; }
  message HelloReply  { string message = 1; }
  service Greeter { rpc SayHello(HelloRequest) returns (HelloReply); }
`);

// TLS 通道（生产；Phase 0 后可用）
const ch = grpc.createChannel('https://upstream.example.com:443', {
  tls: { ca: '/etc/qwrt/ca.pem' },   // 缺省用系统 CA；verify 恒为严格
});

// 明文 h2c 通道（本地测试 / 内网，Phase 1 即可全 JS 跑）
const ch = grpc.createInsecureChannel('10.0.0.5:50051');

// 一元 RPC（Phase 1 主形态）
const reply = await ch.invoke('/helloworld.Greeter/SayHello', { name: 'world' }, {
  headers: { authorization: 'Bearer ...' },  // gRPC metadata
  timeoutMs: 5000,
});
// reply = { message: 'Hello world' }

// 预绑定方法（省 type 解析；类型由注册表方法签名推导）
const sayHello = reg.service('helloworld.Greeter').method('SayHello');
const reply = await ch.invoke(sayHello, { name: 'world' });

// 流式（Phase 2）
for await (const item of ch.serverStream(sayHello.List, req, {})) { ... }
const total = await ch.clientStream(sayHello.Collect, [r1, r2, r3], {});
await ch.bidi(sayHello.Chat, outIterable, { onMessage });
```

要点：
- method 载体为**注册表推导的方法对象**（自带 req/resp schema），或 `/pkg.Svc/M` 字符串 + 显式 types。推荐前者（零重复、类型一致）。
- `invoke` 返回 Promise<resp>；失败 reject `grpc.StatusError`（code/message/details/metadata，对应 §4 错误模型）。
- 连接级选项：`maxRecvMsgSize`（防 OOM，默认如 4MB）、`keepaliveMs`（PING 周期）、`connectTimeoutMs`。
- 通道内多流复用；GOAWAY 后停止新流、排空在途、重连新连接。

## 5.2 TLS ALPN 接线（客户端）

```
createChannel('https://host:443', {tls})
  → pal.tcpConnect(host, 443, {onconnect/ondata/onerror/onclose},
                    {tls:{servername:host, alpn:['h2'], ca?}})   // Phase 0
  → mbedtls 握手，ALPN 协商 'h2'，否则 onerror('h2 not negotiated')
  → JS 收到 onconnect 后发 h2 连接前导 + SETTINGS
```

`ondata` 在 TLS 路径已由 C 解密后推送明文字节（现有 `tcp_io.c:413-426` 模式），JS 侧**无感知**——h2 引擎只面对明文字节流，TLS 完全透明。

## 5.3 服务端接线（Phase 3 预留）

- `serve(options, handler)`：options.tls 加 `alpn:['h2','http/1.1']`（C 配置）；连接到达后 JS 读 `conn.negotiatedProtocol`：
  - `'h2'` → 走 h2/gRPC 解析路径（按 `:path` 路由 `/pkg.Svc/M` 到 handler）；
  - `'http/1.1'` → 现 HTTP/1.1 路径；
  - 明文连接：嗅探 24 字节前导 `PRI * HTTP/2.0` → h2c 路径。
- handler 形态：`(call) => { /* call = {request, metadata, sendResponse, onCancel} */ }` 或 async 返回响应对象；Phase 3 细化。

## 5.4 错误模型

`grpc-status` 码 + `grpc-message`（trailers 里）→ `StatusError`；`grpc-status-details-bin`（base64 的 `google.rpc.Status`，带 details）解析出结构化 details。网络层失败（连接失败/GOAWAY/超时）映射 `UNAVAILABLE`/`DEADLINE_EXCEEDED`/`CANCELLED`。

---

# 6. 验证方案

## 6.1 测试对端

| 对端 | 用途 | 备注 |
|---|---|---|
| **Node `@grpc/grpc-js`**（本地起真实 gRPC 服务） | 主验证对端：unary + 三种流式 + 错误注入 + metadata + deadline | `npx` 拉起即可，Node http2 同源，互操作可信 |
| **grpcurl**（`grpcurl -plaintext host:port list` / `invoke`） | 反向验证：用 grpcurl 调 qwrt 起的服务（Phase 3） | 也作 qwrt 客户端对 grpc-go 反射服务的验证 |
| **nghttp2 工具**（`nghttp`/`h2load`） | h2 帧级互操作（SETTINGS/流控/多流） | `nghttp -v` 能看到 qwrt 客户端发出的原始帧 |
| **grpc-go 测试服务**（hello/echo 样例） | 跨语言真实服务端互操作 | 有网络环境时可选 |

Phase 1 明确路径：**先明文 h2c 对 `@grpc/grpc-js` 不启用 TLS 的服务跑通**（100% JS，不碰 C），再做 Phase 0 TLS + ALPN 验证 `grpcurl -servername` 类握手。

## 6.2 protobuf 正确性向量

- **wire 编解码**：对照 protoc 输出的字节（开发期一次性生成 fixture，非运行期依赖）：varint 边界（2^63）、zigzag 负数、packed repeated、nested message、bytes/UTF-8 字符串、enum、oneof、map、fixed32/64 大小端。
- **64 位**：BigInt 往返、字符串选项往返、截断边界。
- **.proto 解析**：语法错误注入（缺字段号/重复字段号/未知类型 → 明确报错）、import 同包合并、注释剥离。
- 提供 `nativeBytesEqual`（`ext_compress.c:648`）做字节精确断言。

## 6.3 h2 / HPACK 互操作向量

- RFC 7540 握手时序（前导、SETTINGS 双 ACK）。
- RFC 7541 附录 C 官方 HPACK 解码向量（静态表引用、字面量、Huffman、动态表、表大小更新）。
- 与 nghttp2/grpc-go 对测：动态表引用响应头、CONTINUATION 拆分头块、>64KB 响应触发流控/WINDOW_UPDATE、GOAWAY 优雅关闭、RST_STREAM 取消。

## 6.4 边界用例

- 空消息（Empty）、超长字段值、超大响应（>16MB，流控 + maxRecvMsgSize 拒绝路径）。
- 服务端主动 GOAWAY / RST_STREAM / 立即关闭 → 客户端错误映射正确。
- 并发多 unary（多流复用同连接，流 ID 奇偶正确、独立取消互不影响）。
- deadline 触发与 grpc-timeout 头传播。
- TLS 握手失败（自签 CA、主机名不匹配、非 h2 ALPN）→ onerror 明确。

---

# 7. 验收标准

1. **Phase 0**：`tcpConnect(host, 443, cb, {tls:{alpn:['h2']}})` 对真实 TLS gRPC 服务握手成功、`onconnect` 触发、ALPN 协商 "h2"；对不支持 h2 的 TLS 服务（如普通 https）`onerror('h2 not negotiated')`；明文 h2c 不受影响。
2. **Phase 1**：`grpc.createInsecureChannel(...).invoke('/helloworld.Greeter/SayHello', {name:'world'})` 对 `@grpc/grpc-js` 返回正确 `{message:'Hello world'}`；非 OK 状态正确 reject 为 `StatusError`（code/message/details）；metadata 往返一致；deadline 生效。
3. **Phase 2**：三种流式形态对 `@grpc/grpc-js` 往返正确；>16MB 流式不卡死（流控）；gzip 压缩消息解码正确。
4. **Phase 3**：grpcurl（`-plaintext` 与 TLS 各一）能 `list` 并 `invoke` qwrt 起的 gRPC 服务；HTTP/1.1 与 h2 在同一 `serve()` 端口共存（ALPN/前导自动分流）。
5. 全链路不新增系统依赖、不引外部库（miniz/mbedtls 等现有 deps 复用），构建保持 C99 全源码。

---

# 8. 开放问题

- **O1（protobuf schema 来源）**：运行时直接解析 `.proto` 文本（推荐，自包含）vs 应用方 protoc 预编译 `FileDescriptorSet` 二进制、qwrt 只解析 descriptor（解析器更小但引入外部工具步骤）。首版倾向前者，若 oneof/map/well-known 覆盖失控再退化为后者。
- **O2（protobufjs 回退）**：若手写 proto3 子集覆盖需求爆表，vendored protobufjs 作为纯 JS 备选（需解决其依赖/shim 问题）。仅作风险预案，不默认采用。
- **O3（64 位整数默认形态）**：BigInt 默认 vs `int64AsString` 默认——边缘节点对接方（遥测/ID/时间戳）常要字符串，需确认默认值。
- **O4（Phase 0 是否同期做服务端 ALPN）**：G4 与客户端 G1/G2 改动点相邻（都改 `tcp_io.c` TLS 面），可顺手做，避免 Phase 3 再动 C。
- **O5（gzip 默认开关）**：`grpc-accept-encoding` 默认 `identity` 还是 `identity,gzip`；miniz 已有，成本低，但默认启压缩需评估 CPU。
- **O6（keepalive/连接池策略）**：边缘节点"长连复用 vs 短连"；grpc-go 默认 PING 2h，边缘抖动网络可能要更短 + 重连退避。
- **O7（服务反射）**：`grpc.reflection.v1alpha.ServerReflection` 支持（免 .proto 发现方法）——Phase 3+ 可选 nice-to-have，需评估工作量。
- **O8（h2 底层 API 是否暴露）**：除 gRPC 外是否提供裸 `h2` channel/session（如未来 h2 fetch、gRPC-Web 服务端兼容）。首版**不暴露**，接口最小化。

---

# 9. 工作量预估（每阶段，单人）

| 阶段 | 内容 | 量级 | 工期 |
|---|---|---|---|
| Phase 0 | tcpConnect TLS 客户端 + ALPN h2（C，照搬 uv_io 模板） | ~150-250 行 C | 1-2 人日 |
| Phase 1 | h2 客户端引擎 + HPACK + gRPC unary + 动态 proto3 + 测试向量 | ~1700-2400 行 JS | 10-15 人日（2-3 周） |
| Phase 2 | 流式客户端 + gzip 压缩 | ~300-500 行 JS | 3-5 人日 |
| Phase 3 | 服务端 h2 引擎 + gRPC 服务端语义 + ALPN/前导分流（+可选 HPACK 下沉 C） | ~800-1200 行 JS + 可选 ~150-250 行 C | 1.5-3 周 |

合计：客户端全量（Phase 0-2）约 **2-4 周**；含服务端（Phase 3）约 **4-7 周**。

**风险排序**：① HPACK 与真实服务端互操作（缓解：RFC 向量 + grpc-go/nghttp2 对测）＞ ② 流控/多流正确性（缓解：§6.3/6.4 用例）＞ ③ proto3 覆盖需求膨胀（缓解：O1/O2 回退）＞ ④ 手写规模（Phase 1 是 WS 2-3 倍，仍可控）。

---

# 附：探查依据（文件 + 行号）

- `src/tcp_io.c:1061-1067` — pal TCP 原语注册（tcpConnect/tcpWrite/tcpClose/tcpListen/tcpCloseListener/tcpReloadTls）
- `src/tcp_io.c:523-630` — `tcpConnect(host, port, callbacks)` 无 TLS 选项（`use_tls` 恒 0）
- `src/tcp_io.c:855-871, 936-956` — 服务端 TLS（accept 路径 `mbedtls_ssl_setup + set_bio`；`tcpListen` 第 5 参 `{cert,key,sni}`）
- `src/tcp_io.c:400-442` — TLS 握手 + 解密读循环（onconnect/ondata 推式回调）
- `deps/mbedtls/include/mbedtls/mbedtls_config.h:1991` — `#define MBEDTLS_SSL_ALPN`；`:3698,3712` — CLI_C/SRV_C；`:1855` — TLS1.2；`:1907,1937` — TLS1.3
- `deps/mbedtls/include/mbedtls/ssl.h:4309,4320` — `mbedtls_ssl_conf_alpn_protocols` / `mbedtls_ssl_get_alpn_protocol`
- `deps/mbedtls/include/mbedtls/build_info.h` — mbedTLS 3.6.6
- `src/uv_io.c:232-286` — fetch TLS 客户端模板（IS_CLIENT、系统 CA、VERIFY_REQUIRED、set_hostname/SNI、无 ALPN）
- `polyfill/src/websocket.js:113` — `wss:// not supported yet`（tcp 无 TLS 客户端佐证）
- `polyfill/src/fetch.js:5` — `pal.httpRequestStream`（C 层 HTTP/1.1，与 h2 无关）
- `polyfill/src/http-server.js:717-721` — `serve()` 走 `pal.tcpListen(..., {cert,key})` 纯 JS HTTP/1.1
- `src/ext_compress.c:988-1005, 377-632` — `nativeCompress/nativeDecompress('gzip')` 与流式 deflate/inflate（miniz）——gRPC gzip 复用点
- `CMakeLists.txt:65,91,584` — mbedtls/miniz/libuv 均 `add_subdirectory` 全源码构建（nghttp2 若引入可同模式，但本设计不采用）
- `polyfill/src/pal.js` — `pal = globalThis.__native_inject__`（PAL 注入机制）
- `deps/` — 无 h2/grpc/protobuf 库；全仓 grep 确认无历史 gRPC/h2 代码（greenfield）

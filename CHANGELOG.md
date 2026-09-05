# Changelog

All notable changes to Qwrt.js.

## [Unreleased]

### Removed
- JS flatbuffers 退役：flatbuffers 重新定位为纯 C 层内部格式（当前无 C 消费者，不实现 C 代码）。JS 层退役理由：① JS 急切 decode 无性能优势（encode 两趟 vtable 回填+对齐，decode 遍历整表物化对象），zero-copy 随机字段访问优势仅在 C 层成立；② Worker 间为同进程共享内存通信，非 IPC 场景，无需跨进程序列化协议，flatbuffers 的跨界序列化价值在 qwrt 中不存在。gRPC 序列化改为 protobuf-only（标准互操作：grpc-go/grpc-js/grpcurl/Envoy）。删除 flatbuffers.js（1219 行）、loadFlatbuffers API、flatbuffers harness、`application/grpc+flatbuffers` content-type。纯 C 层定位：未来出现真实 C 层序列化需求时按 `docs/plans/2026-09-03-flatbuffers-runtime-builtin.md` 方案 B（惰性访问器）实施。
### Added
- 多进程模型 M-P0 信封编解码器：`src/ipc_envelope.c/.h` —— 手写 FlatBuffers wire-format 编解码（固定 `Envelope{source:int32, target:int32, kind:int8, payload:[ubyte]}` schema，零依赖零生成步骤，~130 行 C99）。encode 产出规范布局（vtable 12B + 表 + 向量，40+payload 字节）；decode 布局无关（读任意合法 fb Envelope：短 vtable 缺字段回退默认值、恶意长度全部边界检查拒绝），payload 零拷贝片引用。配套：`test/test_ipc_envelope_gtest.cpp`（8 用例：字节布局冻结/往返/短 vtable 缺省/全字节变异/截断前缀）、`test/ipc_envelope_cli.c` + `test/ipc_envelope_fbcheck.py`（与 Python `flatbuffers` 官方库双向交叉验证 5 用例——C 编码→Python 解码、Python 编码→C 解码，字节级 wire 兼容硬证据）、ASan+UBSan 20 万随机 buffer 模糊通过；ctest 注册 `test_ipc_envelope_gtest` + `ipc_envelope_fbcheck`（offline 标签，flatbuffers 包缺失时自动跳过后者）。设计依据 `docs/plans/2026-09-04-multi-process-model.md` §4/§11。
- gRPC/HTTP2 Phase 3a（C 服务端 ALPN）：`tcpListen` 第5参 `tlsOpts` 新增可选 `alpn` 字段（字符串数组，如 `['h2','http/1.1']`），经 `mbedtls_ssl_conf_alpn_protocols` 在 TLS 握手时协商协议；缺省 `['http/1.1']`（向后兼容，现有 `serve()` HTTP/1.1 行为不变）。`serve()` 的 `tls` 选项现在透传 `alpn`/`sni` 等全部字段到 `tcpListen`（此前只取 `cert`+`key`，丢弃其余）。TLS 握手完成后，服务端连接句柄暴露 `conn.alpn` 属性（协商的协议字符串，如 `"h2"` 或 `"http/1.1"`；未协商时为 `null`），供 JS 层嗅探协议（服务端 gRPC 路由依据）。新增 e2e 测试 3 用例（`test_tcplisten_tls_alpn_h2` / `test_tcplisten_tls_alpn_http11` / `test_tcplisten_tls_default_no_alpn`），Python `ssl.set_alpn_protocols` 为对端验证协商结果。
- CI: HTTP/2 客户端栈性能基准 h2-client-perf job（Node http2 对端 + 纯 JS 栈，阈值=基线50%）
- gRPC/HTTP2 Phase 2（polyfill 客户端）：`polyfill/src/grpc.js` 实现 unary gRPC 客户端——5 字节帧头编解码、content-type 区隔（`application/grpc+proto` / `application/grpc+flatbuffers`）、`te:trailers`、trailers 解析（`grpc-status` / `grpc-message` percent-decoded / `grpc-status-details-bin` base64）、`StatusError`（携带 code + metadata + details）、`FrameSplitter`（`maxRecvMsgSize` 防 OOM）、连接级透明重试一次、方法解析（字符串路径 + registry 或预绑定方法对象）、序列化类型推断（`kind==='message'` → protobuf，`'table'` → flatbuffers）。`QWRT_WITH_GRPC` 编译开关：esbuild alias 门控（`@qwrt/grpc-stack` → stub 模块），OFF 态 tree-shake 消除全部 gRPC/HTTP2/HPACK 模块（polyfill.js 268KB→OFF，411KB→ON；bytecode 145KB→OFF，238KB→ON）。`flatbuffers.js` 扩展：`parseService()` 解析 RPC 方法签名、`Schema.prototype.service(name)` 返回带 `method(name)` 访问器的 service 对象。`protobuf.js` 扩展：`Registry.prototype.service(name)` + `svc.method(name)`。e2e 测试 24/24（手写 Node http2 对端 + @grpc/grpc-js 标准对端），C 侧 offline 15/15 无回归。flatbuffers 适用场景：qwrt↔qwrt 内部快路径（零拷贝 schema-driven），对外互操作默认 protobuf
- gRPC/HTTP2 Phase 0（C 传输原语）：`pal.tcpConnect(host, port, callbacks[, opts])` 新增可选第 4 参，`opts.tls` truthy 即把该连接变成 mbedTLS **TLS 客户端**（此前 `tcpConnect` 只有裸 TCP，TLS 仅存在于 `tcpListen` 服务端路径）。`opts` 接受扁平形状 `{tls:true, servername, alpn:[…], ca}` 与设计文档形状 `{tls:{…}}`；`servername` 缺省取 host（空串明确拒绝，不给「零长度主机名可能跳过校验」留口子），`ca` 缺省用系统 CA 束（PEM 路径或内联 PEM 均可）。信任策略与 `uv_io.c` 的 HTTPS 客户端同纪律：**恒 `MBEDTLS_SSL_VERIFY_REQUIRED`**（无可用 CA 时握手必失败，绝不静默降级为不验证），`mbedtls_ssl_set_hostname` 提供 SNI + 证书主机名校验，握手后再查一次 `mbedtls_ssl_get_verify_result` 作纵深防御；`tls` 为真但构建时 `QWRT_WITH_TLS=OFF` 直接抛错（fail closed，不假装明文可用）。**ALPN**：`mbedtls_ssl_conf_alpn_protocols` 首次接线（mbedTLS 早已编入 `MBEDTLS_SSL_ALPN` 但全仓从未调用），缺省 `['h2','http/1.1']`，显式 `alpn: []` = 不提供 ALPN，非数组/含非字符串/超 7 项同步抛 TypeError（不静默改用缺省）；协商结果暴露在连接句柄的 `conn.alpn`（未协商成功时为 `null`；裸 TCP 连接不设该属性，零回归）。时序修正：TLS 客户端的 `onconnect` 延迟到握手完成之后才触发，因此 JS 在 `onconnect` 里写出的字节一定是明文（h2 连接前导/SETTINGS 由 mbedTLS 加密），不会出现明文协议前导打到 TLS 端口的静默错误；握手失败走既有 `onerror` + 清理 TLS 上下文 + 关 TCP。这是 h2-over-TLS gRPC 客户端（Phase 1+）的唯一 C 前置。测试：test_httpserver_e2e.py 新增 6 例（ALPN `h2` 协商 + 明文字节往返、对端无 ALPN→`alpn:null`、`alpn:[]` 关闭 ALPN + 非法 alpn 抛错矩阵、自签名证书被系统 CA 拒、servername 不匹配被拒、嵌套 opts 形状等价），对端用 Python stdlib `ssl` 起（`tlsListen` 服务端 ALPN 属 Phase 3/G4，故不用 qwrt 自己当对端）；30/30 e2e 全绿（含全部裸 TCP WebSocket 用例零回归），另 300 次 TLS 握手/关闭同进程压力无崩溃，`QWRT_WITH_TLS=OFF` 构建通过且 `tls:true` 正确抛错
- 打包：polyfill 字节码加载四种可选模式（`QWRT_POLYFILL_MODE`，CMake 选项，默认 `C`）。C（默认）= const 数组直接嵌入 .rodata（现状，进程常驻 ≈137KB）；A（压缩嵌入）= build.js 构建时用 zlib 压缩字节码为更小 const 数组（本仓 145,265B→72,186B，-50%），`qwrt_create` 首次建 context 时用 miniz（tinfl）解压到堆、各 context 共享一份、runtime teardown 释放；B（外部文件）= 字节码不嵌入，构建时输出独立 `dist/polyfill_default.polyfill`，运行时经统一接口读入堆（路径 = `QWRT_POLYFILL_FILE` 环境变量或同名编译宏，文件缺失/打不开给出明确错误并拒绝启动，进程常驻 ≈0 但需随程序分发文件）；D（自定义来源）= 不内嵌数据，暴露弱符号 `qwrt_polyfill_load_custom`/`qwrt_polyfill_unload_custom`（缺省实现返回错误），宿主可覆盖以从任意来源（外部 flash 映射、自定义存储等）提供字节码。统一加载接口 `qwrt_polyfill_load`/`qwrt_polyfill_unload`（qwrt_internal.h 声明，polyfill_load.c 按 mode 各自实现），context 创建改经该接口获取 polyfill（runtime 级缓存、单次加载多 context 共享；worker/多 runtime 各自加载一份），A/B/D 堆缓冲在 runtime teardown 释放（ASan 无泄漏）。权衡：A 省 .rodata、启动多一次解压；B 常驻内存最低、需分发文件；D 最灵活、由使用者自担来源与生命周期
- WAMR AOT Tier 0/1：`WebAssembly.compile(bytes, options?)` / `instantiate(bytes, importObject?, options?)` / `compileStreaming(src, options?)` / `instantiateStreaming(src, importObject?, options?)` 接受非标准可选 options `{aot: aotBytes, aotFail?: fn}`——提供 `.aot` 字节时 AOT 优先（`wasm_runtime_load` 按 `\0aot` 魔数自动分派到 AOT 加载器，Tier 0 直传 `.aot` 字节本就可用）；AOT 加载失败（版本/arch/ABI/损坏）静默回退解释器（fail-open，用可移植 `.wasm` 字节）+ 可选 `aotFail` 回调接收失败原因，不抛 JS 异常；仅提供 `.aot` 字节（无 `.wasm` 保底）时失败照抛 TypeError。实现为 ext_wamr.c 内嵌 JS shim（捕获并复用既有 C compile/instantiate 入口），streaming 两条 C 入口透传 options，无新 pal 原语、C 构造器零改动。注：真实 `.aot` 端到端（wamrc+LLVM 生成、AOT 模块 + imports 双加载）需 LLVM 主机验证；本机 gtest（test_wasm_aot_gtest 7/7）用伪造 `\0aot`（版本不符）确定性覆盖 fail-open 回退、aotFail、纯 .aot 抛错、module-arg 忽略 aot、streaming 透传与无 options 回归。
- WASM imports（importObject）：`WebAssembly.Instance(module, importObject)` / `WebAssembly.instantiate(buf|module, importObject)` 现支持函数导入——按 importObject 逐项解析（`importObject[module][field]` 取 JS 函数，缺模块/字段/非函数抛清晰 TypeError），按导入模块名分组注册为 WAMR raw native（`wasm_runtime_register_natives_raw`），WAMR 在加载期解析 import，故 Module 构造在注册后重载字节码使导入链接生效；native 调用时 JS ↔ WASM 双向类型转换（i32→Number、i64→BigInt、f32/f64→Number，导出 i64 结果用 `JS_ToInt64Ext` 兼容 BigInt），导出函数可回调宿主函数，宿主函数抛出的 JS 异常原样传播回导出调用方（pending-exception 槽挂在实例 wrap 上经 custom_data 关联）。限制（文档标注）：内存/全局/表导入不支持（实例化时明确 TypeError）、单导入单结果、>16 参数拒绝、同 import 模块名+字段跨模块以最近注册的签名为准（WAMR 全局 native 注册语义）。wasm3 同步对齐：缺失导入按 LinkError 计数报错、非对象 importObject 抛 TypeError、导入字段非函数计入缺失。新增 gtest test_wasm_imports_gtest 10/10（宿主回调 i32/i64/f64/void、instantiate 路径、缺导入/非对象/内存导入报错、宿主异常传播），ctest -L offline 14/14 全绿。
- fetch 出站代理：支持 `HTTP_PROXY`/`HTTPS_PROXY`/`NO_PROXY`（含小写、`*`/后缀匹配）环境变量。C 层透明实现：http 走绝对式请求行（RFC 7230 5.3.2），https 走 CONNECT 隧道后 TLS 端到端（主机名校验仍对源站）；非法/不支持 scheme 的代理 URL 立即失败（fail closed），不静默回退直连。流式与非流式 fetch 均生效，新增 e2e test_fetch_proxy_e2e.py 7/7。
- HTTPServer perf：修复 TextEncoder 大字符串编码走 JS 慢路径的存量 bug（nativeEncodeUtf8 由 C extension 在 polyfill 求值后注册，顶层一次性 `typeof` 检测恒 false），改为惰性探测；C 侧改用 `JS_ToCStringLen`（顺带修复含 NUL 字符串 encode 截断）。wrk -t2 -c64 /medium(16KB) 240→5100 rps（+21x），/small 2897→11174（+3.9x），/tiny +31%，/post +32%，错误归零
- HTTPServer perf：缓存 handler + Request 构造器引用，Headers._map 直写（绕过正则 normalize），wrk -t4 -c100 /hello +37% /gzip +49% /big +13%
- HTTPServer WebSocket：服务端消息分片重组（FIN=0 + Continuation 帧拼为完整消息）+ 子协议协商（ws 路由支持 `{handler, protocols}` 对象形式，回显首个双方支持的 `Sec-WebSocket-Protocol`）
- HTTPServer 请求体流式：`req.body` 变 ReadableStream（header 一到即调 handler，body 增量 enqueue，二进制安全），新增 `req.text()`/`req.arrayBuffer()`；连接缓冲改字节级（修复二进制 body 经字符串往返膨胀的存量 bug）
- HTTPServer WebSocket：permessage-deflate（RFC 7692）——协商、RSV1 收发、上下文 takeover；C 层新增流式 deflate/inflate 原语（`pal.deflateCreate/Push/Free`、`inflateCreate/Push/Free`，miniz 流式上下文）
- fetch 客户端请求体字节化：`fetch(url, {body})` 的 body 统一序列化为 `Uint8Array` 传给 `pal.httpRequestStream`（支持 string / Uint8Array / ArrayBuffer / ReadableStream，流式 body 异步读取全部 chunk 后再发请求，读取失败 reject）；C 桥接层 `http_request_stream` 直接接受字节 body（不再强制 C string）
- SSE（EventSource）与 WebSocket 补齐 WHATWG 完整语义：EventSource 继承 EventTarget（addEventListener/removeEventListener/dispatchEvent + onopen/onmessage/onerror 处理器属性桥接）；text/event-stream 解析规范完整化（BOM 剥离、LF/CRLF/CR 行尾归一、多 `data:` 行 LF 连接、空 `data:` 分发空串消息、注释行、`event:`/`id:`/`retry:` 字段、跨 chunk 字段缓冲、流式 UTF-8 TextDecoder）；重连语义（网络错误/正文结束按 `retry:` 延时重连并回传 `Last-Event-ID`，非 200 按规范 fail 不重连，`close()` 停止）；WebSocket 继承 EventTarget、binaryType（`blob`/`arraybuffer` 校验）、`_fail` 触发 onerror + onclose(1006, '')、`close(code,reason)` 校验与 code 传播、send 类型映射（string→TEXT、ArrayBuffer/Blob/TypedArray→BINARY）、客户端发送强制 mask、收到服务端 masked 帧/RSV 置位/非法控制帧按协议错误 fail

- WASM 引擎：wasm3 补齐 `WebAssembly.compileStreaming`/`instantiateStreaming`（与 WAMR 语义等价：取完整字节后交 compile/instantiate）；修复 `QWRT_DEFAULT_EXTENSIONS` 从未注册 wasm3 的根因（wasm3 构建下 `WebAssembly` 全局缺失），streaming 测试门控改为 WAMR OR WASM3
- WinterTC/ECMA-429 覆盖盘点（B4）：对照 ECMA-429（Minimum common web API）全接口逐项勾选，补齐 9 个缺口 gtest（CustomEvent / PromiseRejectionEvent / 全局 onerror+onunhandledrejection+onrejectionhandled+reportError / TextEncoderStream / ByteLength+CountQueuingStrategy / TransformStream / BroadcastChannel 克隆隔离 / CacheStorage 扩展 / MessageChannel 消息往返），polyfill gtest 60/60
- A2 多上下文/Worker 健壮性（gtest + 压力）：软挂起边界 4 例（destroy 后 resume 同槽字节一致、坏 state 路径报错隔离、不可克隆属性 skipped 语义、suspend→resume 循环 10 轮槽位稳定复用）；Worker 错误路径 6 例（transfer 重复/不可转移/detached buffer 抛 DataCloneError 且无副作用、非法 URL 构造失败不占槽位、terminate 后 postMessage 静默、父侧 handler 抛错 worker 存活、timer 回调异步抛错进 self.onerror 且 worker 存活）；压力 2 例（4 worker×20 消息洪泛内容校验、30 轮 ArrayBuffer transfer 往返链）
- HTTPServer 示例 C3：静态文件 gzip 结果缓存进 LRU entry（`entry.gz`），重复请求不再每请求重跑 CompressionStream/miniz——wrk 16KB gzip 响应吞吐与未压缩持平（压缩开销从每请求摊销为首次）
- 打包：polyfill 字节码剥离源码——build.js 改用 `qjsc -s`（单 `-s` 仅剥 source、保留 debug 文件名/行号；`-ss` 才连 debug 一起剥）。此前 90.5% 的字节码字节为字符串、含完整 polyfill 源码明文（可反编译还原全部实现），剥离后源码归零；`dist/polyfill.bytecode` 1,078,047B→137,886B（-87.2%），`dist/worker-boot.bytecode` 5,910B→1,522B，内嵌二进制（src/polyfill_default.c）与启动内存同步下降
- localStorage（Web Storage `Storage` 接口，同步 + 跨重启持久化）：`getItem`/`setItem`/`removeItem`/`clear`/`key(n)`/`length`，键/值强制 String，5 MiB 配额超限抛 `DOMException('QuotaExceededError')`。持久化路径 = 环境变量 `QWRT_LOCALSTORAGE_FILE`，否则默认 `~/.qwrt/localstorage.json`（HOME 缺失回退当前目录 `.qwrt-localstorage.json`）；每次变更后原子写回（临时文件 + rename，C 侧新增 `pal.fsWriteSync`，父目录自动 mkdir），启动时同步加载（文件缺失/损坏 → 空）。仅在父 runtime 挂载 `globalThis.localStorage`，Worker 内不挂（无 DOM 场景）；与异步 `qwrt.storage` 共存
- crypto.subtle RSA：RSA-OAEP 加解密与 RSASSA-PKCS1-v1_5 签名/验证完整接线（C 层 mbedTLS 原语 `nativeRsaGenerateKey`/`nativeRsaOaepEncrypt`/`Decrypt`/`nativeRsaSign`/`Verify` + polyfill SubtleCrypto 方法分支）。generateKey 支持 2048/3072/4096 位（publicExponent 默认 65537）、importKey/exportKey 支持 spki/pkcs8（公钥 SPKI DER、私钥 PKCS#8 DER——PKCS#1 内层按 RFC 5208 包装，mbedTLS 无 PKCS#8 writer 故 C 层手写）、RSA-OAEP 支持 label 与 MGF1=hash（SHA-1/256/384/512）、RSASSA 支持 SHA-1/256/384/512；key.usages 按 WebCrypto 校验（OAEP→encrypt/decrypt/wrapKey/unwrapKey；RSASSA→sign/verify）。测试：OAEP encrypt→decrypt 字节往返、RSASSA sign→verify 与篡改拒绝、spki/pkcs8 import/export 往返、1024 位拒绝、3072 位往返（test_crypto_subtle_gtest 30/30，ctest -L offline 12/12 全绿）。jwk 格式未实现（spki/pkcs8 之外抛 NotSupportedError，文档标注）
### Fixed
- fetch NO_PROXY（fix(core)）：NO_PROXY 条目为方括号 IPv6 字面量（如 `[::1]`、`[::1]:8080`）时不生效——`uv_io_parse_url` 产出的请求 host 为裸形式（`::1`，不带方括号），后缀比较带着方括号恒不匹配 → 本应直连的 origin 被错误送经 proxy。修复：`uv_io_no_proxy_entry_match` 在拆出可选 `:port` 后剥离一对包裹方括号再比较。新增 e2e 2 用例（方括号条目 bypass 直连、未命中条目仍走 proxy），`test_fetch_proxy_e2e.py` 9/9。
- C核心（fix(core)）：CLI/HTTP 请求缓冲越界写（json_escape 信封按转义后实际长度分配 + snprintf 截断检查、ftell 失败/SIZE_MAX 缓冲）、chunked 解码 realloc UAF、fs_read OOM 双回调（先置 cb=NULL 再 close）、libuv handle 关闭引用计数误计数（mid-life close 独立计数，防 teardown 期间回调提前释放 op）、wait_idle 先 flush 微任务再判 JS 残留 job、teardown abort in-flight 流式 HTTP op、JS_Call 后取走并释放 pending exception；`_Atomic` 统一改 `__atomic_*`（C99 一致性）
- C扩展（fix(ext)）：raw_inflate 截断/空 DEFLATE 无限循环 DoS（补 MZ_BUF_ERROR 分支 + 10 万次迭代上限）、WAMR 栈数组越界（>16 参数/4 结果抛 RangeError，参数按真实 wasm 类型转换，不再一律 I32）、WAMR/wasm3 导出 Memory/Global/Module/Function 实例 UAF（instance_ref 持有实例引用 + finalizer 释放）、TLS 读循环回调后未查关闭（回调内关闭连接 → 继续读已释放 TLS 上下文）、JS TCP 句柄裸指针 UAF（finalizer 包装 + 关闭后置 NULL）、EC RNG 进程级静态 CTR_DRBG 竞争 → per-runtime DRBG（随 runtime 释放）、btoa/atob 二进制与内嵌 NUL 字节安全（JS_ToCStringLen + 高字节 UTF-8 重编码）
- polyfill（fix(polyfill)）：fetch 契约（Headers.get 空值语义——命中空串返回 `''`、Request.arrayBuffer() 返回真实 ArrayBuffer、Request/Response.blob() 返回 Blob、method 全 token/URL 绝对化/status 200-599 校验、abort reason 透传）、WebSocket 二进制帧/分片重组/掩码与 binaryType、streams 语义（ReadableStream 异步 pull 背压与 enqueue 后 close 抛错、WritableStream 串行写 + close 排队在写之后、pipeTo options.signal 中止、pipeThrough 错误传播到 readable）、crypto 校验（key.usages 白名单/tagLength 合法值/deriveBits 长度）、URL IPv6/host/port 校验、FormData 迭代与 Blob.slice 规范化、structuredClone 自定义类实例抛 DataCloneError
- CLI：test 构建（`QWRT_BUILD_TESTS=ON`，mock libuv）下 qwrt_cli 链接 `qwrt_full`（真实 libuv）→ 核心按 mock 布局、libuv 按真实布局操作同一结构体，`uv_async_init` 越过 mock 大小的 wake 写坏紧随其后的队列字段，任何脚本（连 `console.log(1+1)`）都 `free(): invalid pointer`。修复：test 构建下 CLI 改链 `qwrt mock_libuv`（结构体一致，mock 的 poll 循环驱动 timers/asyncs/threads，基础脚本可正常求值；真实网络需 `QWRT_BUILD_TESTS=OFF`）；并把 CLI 目标定义移到 `add_subdirectory(test)` 之后。
- 编译：`QWRT_WITH_TLS=OFF` 时 `tcp_io.c` 的 `js_pal_tcp_reload_tls` 在 `#if QWRT_WITH_TLS` 之外引用 `l->tls_ctx`（该字段仅在 TLS=ON 时存在）→ 生产 minimal 构建（`QWRT_BUILD_TESTS=OFF` + 全扩展 OFF）编译失败。修复：把 `!l->tls_ctx` 检查移入 `#if QWRT_WITH_TLS` 分支。
- 运行时事件循环：`qwrt_wake_cb` 一次 `uv_run` 派发多条入站消息，但微任务冲刷在 `uv_run` 返回后才统一进行——宿主在消息回调（dispatch 内联的 message_cb）里立即回发下一条消息时，下一条消息的 JS 会在本条消息的 promise 副作用落定之前被派发（读到旧状态）。ASan 构建下 `test_compress_gtest` 因此稳定失败/卡死（DeflateRaw 等 roundtrip 的 `_rok` 永不置位、部分测试直接挂起、GzipMagicBytes 即时读 `_gch` 落空）。修复：每条消息派发后立即 `qwrt_flush_microtasks`，保持"每条消息 = 一个任务，任务后微任务先跑完"的事件循环语义。ASan 该套件 5×18/18 全绿。
- 测试桩 flaky（`test_compress_gtest`）：Debug/非 WAMR 构建下 Gzip1MB / GzipMagicBytes / DeflateRaw 等 roundtrip 用例偶发 5s 轮询超时或整进程卡死。根因在 `test_host.h` 的 poll 语义：`host_poll_until_value`/`host_poll_until` 把整个 poll 的总预算（5s）当作单次 eval 的超时——qwrt 线程在 Debug 高负载下处理 1MB 压缩/解压消息队列（含 JS 层 1MB 比较循环，~1-2s）时，一次慢的 eval 会直接耗尽预算令 poll 一击即败；且该次 eval 超时后其响应稍后到达残留在 inbox，污染后续 host_eval 的"发一条/等一条"配对（错位雪崩，表现为后续测试连环失败/挂起）。修复：单次 eval 改用短超时（3000ms，min 剩余预算）+ 超时重试，poll 持续轮询直到条件满足或总预算耗尽（不再单次一击）；`host_wait_msg` 超时后丢弃堆积的 eval 响应残留（保留 worker/异步回调等非 eval 消息，不误删其他测试依赖），防止残留消息错位。
- HTTPServer：`sendResponse` 对象分支末尾缺 `return`，每个对象（Response）响应后在原连接上额外发一个 `500`——pipelining 客户端收到 `200+500` 双响应，wrk 全部计入 Non-2xx；补 return（合法响应只发一次）
- CLI/TCP 服务：写已关闭的对端连接触发 `SIGPIPE` 直接杀进程（wrk 压测客户端中断连即崩，exit 141）——`main()` 入口 `signal(SIGPIPE, SIG_IGN)`
- timers：setTimeout/setInterval 回调抛错只 `console.error`，不进错误事件流（worker 内 `self.onerror`/全局 `error` 监听收不到异步异常）——catch 后调 `reportError(err)`（存在性守卫，navigator.js 晚于 timers 挂载）再打日志，runtime 不中断
- structuredClone/Worker.postMessage：已 detach 的 ArrayBuffer 再进 transfer 列表不报错、静默无副作用（Web 规范应抛 DataCloneError）——两处 transfer 校验补 `detached` 检查，抛 `DOMException('ArrayBuffer has already been detached', 'DataCloneError')`
- TransformStream：存在 `flush` 时 close 后不调用 `terminate()`（readable 侧永不关闭，队列 drain 后第三次 `read()` 永久挂起）——await flush 结果后再 terminate，兼容异步 flush
- Blob：blobParts 非迭代对象抛 TypeError、type 规范化只去 0x09/0x0A/0x0D、字符串元素 UTF-8 编码
- Headers：new Headers(null) 与 3+ 元素 pair 抛 TypeError
- TextEncoder.encodeInto：代理对感知增量编码 + read/written 精确语义 + 非法目标类型抛错
- CLI/运行时内存泄漏：run_code 的 `json_escape` 返回值未 free；`qwrt_free` 未释放 `config.initial_script`（ASan 回归触发，e2e 16/16 + ASan 0 泄漏）
- Streams：pipeTo 收尾释放 dest writer 锁（正常/出错路径，之后 `dest.getWriter()` 可用）；`reader.releaseLock` reject 未决 pending read 与未 settle 的 closed promise、释放后 `read()` 抛 TypeError（BYOB 同）；tee 单分支 cancel 关闭该分支流（pending read 以 done 结束，另一分支继续）；pipeThrough 校验 `{readable, writable}` 结构与三流 locked 状态
- 运行时 teardown UAF：`qwrt_wait_idle`/`qwrt_destroy` 在 libuv 线程池 request（fs / getaddrinfo / queue_work）在途时进入 teardown，其 done 回调（`bridge_io_done`）在 JSRuntime 释放后 JS_Call → 崩溃（QuickJS `gc_obj_list` 断言 / UAF）。修复：`qwrt_loop_idle` 将 `loop.active_reqs != 0` 判为忙（wait_idle 等全部线程池 work 完成后才退出）；teardown 在销毁 contexts / 释放 runtime 前限轮排空已排队的 work_done 并 flush 微任务（兜底 destroy 瞬时窗口）。ASan 复现：无修复崩溃，有修复干净退出
- 打包（M5-G3）：`cmake --install` 现在同时安装全部 vendored 依赖归档（libuv/libqjs/libiwasm/libminiz/libmbedtls*/libmbedx509/libmbedcrypto）；`libqwrt.pc` 更名 `qwrt.pc` 并补全 Libs（按 `QWRT_WITH_*` 列出所有扩展归档），`pkg-config --cflags --libs qwrt` 即可编译链接消费方（此前 `-lqwrt` 链接缺 qjs/wasm/扩展符号）。WAMR fast-jit/lazy-jit 关闭（asmjit 为 C++，会把 C++ 运行时依赖带进 libiwasm.a；qwrt 仅需 Fast Interpreter + AOT，纯 C 可链接）
- DAP 调试器（A4）：运行中（非暂停）DAP 请求得不到服务——调试会话开着时事件循环一旦空闲，`uv_run(UV_RUN_ONCE)` 无限阻塞在 poll，而 DAP 消息走 stdin（非 libuv 事件源），pause/setBreakpoints/disconnect 永不生效（如 VS Code 点击暂停后调试器卡死）。修复：attach 时注册 50ms 周期轮询 timer（`qwrt_dap_service` 非阻塞排空 stdin：pause 武装下个 dispatch 检查点、setBreakpoints 立即替换断点表、disconnect 停轮询），使 uv_run 有界且每次醒来服务 DAP；`qwrt_loop_idle` 排除该 timer（wait_idle 照常退出）。worker 运行时不再 auto-attach DAP（stdio 单通道无法同时服务两个 runtime，原 QWRT_DEBUG=1 下 worker 会与父竞争读 stdin → 死锁/协议错乱）；断点作用域 = attach 的 runtime，父断点不影响 worker。新增 test_dap_gtest `PauseWhileRunning`（运行中 pause → stopped）2/2 绿。
- 测试桩遗留超时（`test_compress_gtest`）：`CompressTestBase.GzipBinary1MB` 在 Debug 构建（含 ASan/UBSan）下单次 `host_eval` 超 5s 超时。根因是 JS 层 O(n) 循环在 unoptimized QuickJS 解释器下极慢——1MB 二进制数据的逐字节填充循环 ~5.8s、roundtrip 逐字节比较循环 ~2.6s（压缩/解压本身 C 侧仅 ~300ms）。修复：① 大 payload 二进制数据改用 4096 字节模板 + `Uint8Array.set` 块复制生成（原生 memcpy 级，保留原字节序列，5.8s→~30ms）；② 新增 `pal.nativeBytesEqual(a,b)`（C 侧 memcmp）作为 roundtrip 确定性校验（2.6s→~1ms）。`GzipBinary1MB` Debug 下 6249ms→~180ms；Debug `--gtest_repeat=3` 18/18、ASan（含 leak 检测）、Release ctest offline 13/13 全绿。
- 测试桩 poll 预算记账缺陷（`test_compress_gtest` 偶发挂起 >120s）：`host_poll_until`/`host_poll_until_value` 单次 eval 超时最多烧 3s 真实时间却只计 25ms 预算——5s 名义预算在 qwrt 线程真卡住时放大成 ~200 次重试 × 3s ≈ 10 分钟（brain 记录的"单跑偶发挂起 >120s"）。修复：改用 `CLOCK_MONOTONIC` 真实墙钟预算（`mono_ms` 截止时间），单次超时烧多少扣多少，最坏 ≈ timeout_ms + 一次在途 eval（3s），彻底消除挂起。Debug/Release 各 60 次单跑 + 12 核满载 308 次 + 单核饱和 91 次全绿。

## [0.2.0] — 2026-08-19

### Added
- fetch redirect semantics: `Request` stores `redirect`/`keepalive`/`cache`/`mode`/`credentials` options; `fetch()` follows 3xx by default, `redirect:'error'` rejects, `redirect:'manual'` returns status-0 opaque response
- Worker MessagePort multihop transfer: port transferred parent→worker→parent routes through entanglement re-link (pure JS, no C changes)
- HTTPServer gzip LRU cache: per-body content hash (xxhash64+len) LRU cache, `/gzip` +2.25x (4811→10809 rps), module-ized `uvhttp_gzip_cache` (LRU + memory budget + TTL + same-key replace)
- crypto.subtle `wrapKey`/`unwrapKey` (raw + jwk formats, AES-GCM/CBC wrapping) — completes the SubtleCrypto method set; AES-CBC/GCM/CTR encrypt/decrypt round-trips now covered by gtest (algorithm correctness verified)
- ECMA-429 WEBCRYPTO/HR-TIME: `Crypto` / `SubtleCrypto` / `Performance` constructors now exposed on `globalThis` (the existing `crypto` / `performance` instances are unchanged) — completes the ECMA-429 common-interface set
- BYOB (Bring Your Own Buffer) streams per ECMA-429: `ReadableByteStreamController`, `ReadableStreamBYOBReader`, `ReadableStreamBYOBRequest` exposed globally; `ReadableStream({type:'bytes'})` + `getReader({mode:'byob'})`, `read(view)` fills caller-supplied views (partial-fill supported), `controller.byobRequest`/`respond`/`respondWithNewView` pull path
- `structuredClone(value, {transfer})` and `Worker.postMessage(value, transfer)` support transferable objects: ArrayBuffer (detached, `byteLength → 0`) and MessagePort (cross-thread transfer to Workers, bidirectional messaging; same-thread `structuredClone` returns a new usable port, original detached) per the structured-clone spec
- Standalone CLI (`qwrt`): script / `-e` / REPL modes, WinterCG `arguments`/`env` bridge, async-exit (waits for pending timers/fetch/streams); no Node.js APIs
- HTTPServer extension (`serve`): HTTP/1.1 + HTTPS (mbedTLS) + WebSocket + static files + gzip compression, uvhttp-backed
- Worker top-level exceptions now dispatch `ErrorEvent` on the worker (`self.onerror`) and on the parent (`w.onerror`)
- `importScripts('file://...')` in Workers (synchronous extra-script loading)
- `WebAssembly.compileStreaming` / `instantiateStreaming`
- `examples/` tree with runnable samples: `hello` (host↔JS messaging) and `worker` (real-thread Web Worker); built via `QWRT_BUILD_EXAMPLES=ON`
- qwrt_tick encapsulates run_cycle — single unified call with timeout_ms
- qwrt_tick non-blocking design (returns 1/0/-1, no internal loop)
- pal_uv_create requires explicit loop injection (no NULL)
- QWRT_WITH_NONUTF_ENCODINGS compile-time option (Latin-1 support)
- Promise resolution in qwrt_eval (async/await returns resolved value)
- JS exception messages captured in qwrt_eval result
- Runtime-verified npm packages: lodash, dayjs, semver, ms, pako, mitt, clsx, dequal
- WASM playground (qwrt compiled to WebAssembly via Emscripten)
- npm compatibility checker (compat_check tool)
- test262 CI job (prevents QuickJS-ng patch regression)
- ESP32 FreeRTOS PAL timer UAF fix (build-verified with ESP-IDF v5.5.4)
- CONTRIBUTING.md
- Website: Chinese translations, C API reference, examples page, compatible packages

### Changed
- WAMR-2.4.5 as default WASM engine with Fast JIT (wasm3 optional)
- QuickJS-ng ES support: ES2020 (not ES2023 — website corrected)
- uvhttp submodule updated to v2.6.1-59-g449f0ec (PR #348 qwrt embedding fixes + gzip LRU cache, PR #349 router_cache ABI fix)

### Fixed
- streams: `tee()` propagates cancel to underlying source (only when both branches are cancelled)
- streams: `pipeTo()` backpressure serialization + `preventAbort`/`preventClose`/`preventCancel` propagation
- TextDecoder fatal mode: `new TextDecoder('utf-8', {fatal:true})` now throws `TypeError` on invalid UTF-8 (4 branches: lead byte, overlong/out-of-range, truncated, continuation format)
- TextDecoder BOM stripping: `EF BB BF` at start of input is skipped by default; `ignoreBOM:true` preserves `U+FEFF`
- AbortSignal post-abort listener: `signal.addEventListener('abort', cb)` after `ac.abort()` now schedules `cb` via microtask
- URLPattern `:name+` / `:name*` modifiers emitted a literal `:` (never matched multi-segment paths); now emit `(.+)` / `(.*)` per the spec
- META: script= parsing off-by-one (16 chars not 15)
- Blob.slice edge cases (start/end handling, normalizeType spec compliance)
- pal_uv chunk-size cap for non-streaming chunked decode
- pal_uv_destroy in-flight op leak (proper close callbacks)
- Missing self=globalThis injection in WPT runner
- WPT: 0 ERRORs (down from 5), 165 PASS

## [0.1.0] — Initial

- Core runtime: qwrt_create/destroy/tick/eval/call
- Multi-context: spawn/suspend/resume/destroy_ctx
- WinterTC runtime: fetch, console, crypto, streams, timers, URL, encoding, Blob, EventTarget, AbortController, structuredClone
- PAL: libuv (Linux/macOS), mock (testing), FreeRTOS (ESP32-S3)
- Extensions: compress, crypto, textcodec, WAMR, wasm3
- DAP debugger (VS Code)
- WPT runner
- test262 integration
- VitePress documentation (en + zh)

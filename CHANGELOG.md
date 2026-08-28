# Changelog

All notable changes to Qwrt.js.

## [Unreleased]

### Added
- fetch 出站代理：支持 `HTTP_PROXY`/`HTTPS_PROXY`/`NO_PROXY`（含小写、`*`/后缀匹配）环境变量。C 层透明实现：http 走绝对式请求行（RFC 7230 5.3.2），https 走 CONNECT 隧道后 TLS 端到端（主机名校验仍对源站）；非法/不支持 scheme 的代理 URL 立即失败（fail closed），不静默回退直连。流式与非流式 fetch 均生效，新增 e2e test_fetch_proxy_e2e.py 7/7。
- HTTPServer perf：修复 TextEncoder 大字符串编码走 JS 慢路径的存量 bug（nativeEncodeUtf8 由 C extension 在 polyfill 求值后注册，顶层一次性 `typeof` 检测恒 false），改为惰性探测；C 侧改用 `JS_ToCStringLen`（顺带修复含 NUL 字符串 encode 截断）。wrk -t2 -c64 /medium(16KB) 240→5100 rps（+21x），/small 2897→11174（+3.9x），/tiny +31%，/post +32%，错误归零
- HTTPServer perf：缓存 handler + Request 构造器引用，Headers._map 直写（绕过正则 normalize），wrk -t4 -c100 /hello +37% /gzip +49% /big +13%
- HTTPServer WebSocket：服务端消息分片重组（FIN=0 + Continuation 帧拼为完整消息）+ 子协议协商（ws 路由支持 `{handler, protocols}` 对象形式，回显首个双方支持的 `Sec-WebSocket-Protocol`）
- HTTPServer 请求体流式：`req.body` 变 ReadableStream（header 一到即调 handler，body 增量 enqueue，二进制安全），新增 `req.text()`/`req.arrayBuffer()`；连接缓冲改字节级（修复二进制 body 经字符串往返膨胀的存量 bug）
- HTTPServer WebSocket：permessage-deflate（RFC 7692）——协商、RSV1 收发、上下文 takeover；C 层新增流式 deflate/inflate 原语（`pal.deflateCreate/Push/Free`、`inflateCreate/Push/Free`，miniz 流式上下文）
- fetch 客户端请求体字节化：`fetch(url, {body})` 的 body 统一序列化为 `Uint8Array` 传给 `pal.httpRequestStream`（支持 string / Uint8Array / ArrayBuffer / ReadableStream，流式 body 异步读取全部 chunk 后再发请求，读取失败 reject）；C 桥接层 `http_request_stream` 直接接受字节 body（不再强制 C string）

- WASM 引擎：wasm3 补齐 `WebAssembly.compileStreaming`/`instantiateStreaming`（与 WAMR 语义等价：取完整字节后交 compile/instantiate）；修复 `QWRT_DEFAULT_EXTENSIONS` 从未注册 wasm3 的根因（wasm3 构建下 `WebAssembly` 全局缺失），streaming 测试门控改为 WAMR OR WASM3
- WinterTC/ECMA-429 覆盖盘点（B4）：对照 ECMA-429（Minimum common web API）全接口逐项勾选，补齐 9 个缺口 gtest（CustomEvent / PromiseRejectionEvent / 全局 onerror+onunhandledrejection+onrejectionhandled+reportError / TextEncoderStream / ByteLength+CountQueuingStrategy / TransformStream / BroadcastChannel 克隆隔离 / CacheStorage 扩展 / MessageChannel 消息往返），polyfill gtest 60/60
- A2 多上下文/Worker 健壮性（gtest + 压力）：软挂起边界 4 例（destroy 后 resume 同槽字节一致、坏 state 路径报错隔离、不可克隆属性 skipped 语义、suspend→resume 循环 10 轮槽位稳定复用）；Worker 错误路径 6 例（transfer 重复/不可转移/detached buffer 抛 DataCloneError 且无副作用、非法 URL 构造失败不占槽位、terminate 后 postMessage 静默、父侧 handler 抛错 worker 存活、timer 回调异步抛错进 self.onerror 且 worker 存活）；压力 2 例（4 worker×20 消息洪泛内容校验、30 轮 ArrayBuffer transfer 往返链）
### Fixed
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

---
id: wintertc-ecma429-coverage
title: "ECMA-429 (Minimum common web API) 覆盖矩阵盘点 + 缺口测试"
category: decision
status: active
tags: [wintertc, ecma-429, coverage, gtest, streams]
created: "2026-08-27T09:54:25"
updated: "2026-08-27T09:54:42"
---

<!-- compiled_truth -->
## 结论
对 **ECMA-429（ECMA TC55 WinterTC Minimum common web API，2025 snapshot）** 全量接口/方法逐项盘点 qwrt 的**实现**与**gtest 覆盖**。全部必选接口已实现；9 处"已实现但无测试"的规范表面补齐 gtest；并暴露/修复 1 个真实规范违例（TransformStream flush 不关闭 readable，第三次 read() 永久挂起）。

## 盘点范围
基准 = ECMA-429 官方规范（ecma-international.org，2025-12 第 1 版）+ WinterCG proposal 草稿。盘点对象 = polyfill/src/*.js 各模块 + 原生扩展（WASM/compress/crypto）；测试覆盖 = test/*_gtest.cpp + test_httpserver_e2e.py。

## 覆盖矩阵（ECMA-429 5.1 Common interfaces，全实现 ✅）

| 规范出处 | 接口 | 实现 | gtest 覆盖 |
|---|---|---|---|
| DOM | AbortController / AbortSignal / Event / EventTarget | ✅ abort.js / event-target.js | ✅ AbortSignalStatic / EventTargetEdgeCases+Advanced |
| HTML | CustomEvent | ✅ event-target.js | ✅ 新增 Ecma429CustomEvent |
| HTML | ErrorEvent / PromiseRejectionEvent | ✅ error-events.js | ✅ ErrorEventAndEvent / 新增 Ecma429PromiseRejectionEvent |
| HTML | MessageChannel / MessageEvent / MessagePort | ✅ message-channel.js | ✅ 新增 MessageChannelMessaging / worker transfer 13 例 |
| WebIDL | DOMException | ✅ abort.js | ✅ DomException |
| Fetch | Headers / Request / Response | ✅ fetch.js | ✅ HeadersEdgeCases / FetchRequestOptions |
| XHR | FormData | ✅ blob-file-formdata.js | ✅ FormData（append/get/getAll/has/set/delete/forEach/Blob 文件名） |
| File | Blob / File | ✅ blob-file-formdata.js | ✅ BlobEdgeCases |
| Compression | CompressionStream / DecompressionStream | ✅ streams.js（pal.nativeCompress miniz） | ✅ test_compress_gtest + test_compress_consistency_gtest 18 例 |
| Streams | ReadableStream / ReadableStreamBYOBReader / ReadableStreamBYOBRequest / ReadableByteStreamController / ReadableStreamDefaultController / ReadableStreamDefaultReader / WritableStream / WritableStreamDefaultController / WritableStreamDefaultWriter / TransformStream / TransformStreamDefaultController / ByteLengthQueuingStrategy / CountQueuingStrategy | ✅ streams.js（17 个全导出） | ✅ BYOB 5 例 + pipe/tee/releaseLock 8 例 + 新增 TransformStreamApi + QueuingStrategies |
| Encoding | TextDecoder / TextDecoderStream / TextEncoder / TextEncoderStream | ✅ streams.js / text-encoding.js | ✅ TextDecoderEdgeCases / TextDecoderStreamMultibyte / EncodeIntoEdgeCases / 新增 TextEncoderStreamApi |
| URL | URL / URLSearchParams | ✅ url.js | ✅ Url / UrlSearchParamsEdgeCases+Boundary |
| URLPattern | URLPattern | ✅ url-pattern.js | ✅ UrlPatternBasic+Modifiers |
| WebCrypto | Crypto / CryptoKey / SubtleCrypto | ✅ crypto.js / crypto-subtle.js | ✅ CryptoGlobals / test_crypto_subtle_gtest 15 例 |
| HR-Time | Performance | ✅ performance.js | ✅ PerformanceApi / PerformanceGlobals / PerformanceObserver |
| WASM | WebAssembly.{Global,Instance,Memory,Module,Table,Tag,Exception,CompileError,LinkError,RuntimeError} | ✅ 原生 WAMR/wasm3 | ✅ test_wasm_streaming_gtest |

## 覆盖矩阵（ECMA-429 5.2 Common methods/properties，全实现 ✅）

| 方法/属性 | 实现 | gtest 覆盖 |
|---|---|---|
| atob / btoa | ✅ | ✅ BtoaAtobEdgeCases |
| clearTimeout / clearInterval / setTimeout / setInterval | ✅ timers.js | ✅ Timer |
| navigator.userAgent | ✅ navigator.js | ✅ RandomUuidAndNavigator |
| onerror / onunhandledrejection / onrejectionhandled | ✅ navigator.js（defineProperty 接线） | ✅ **新增 GlobalErrorHandlerProperties** |
| queueMicrotask | ✅ index.js | ✅ QueueMicrotask |
| reportError / self | ✅ navigator.js | ✅ **新增 GlobalErrorHandlerProperties** |
| structuredClone | ✅ structured-clone.js | ✅ StructuredCloneTransfer* + DeepTypes |
| fetch / console / crypto / performance | ✅ | ✅ Fetch / Console / CryptoGlobals / PerformanceApi |

## 超出 ECMA-429 的项目扩展（均有 e2e/gtest）
fetch 重定向+流式 body、timers、fs（qwrt.fs）、storage、URLPattern、abort、BroadcastChannel、CacheStorage、EventSource(SSE)、WebSocket（RFC 7692）、serve() HTTP/WS 服务器、Web Workers 多线程、软挂起/多上下文。

## 缺口测试（本盘点新增 9 个，全部通过，polyfill 60/60）
1. **Ecma429CustomEvent** — detail 透传 / 默认 null / 继承 Event
2. **Ecma429PromiseRejectionEvent** — promise/reason / unhandledrejection cancelable=true
3. **GlobalErrorHandlerProperties** — onerror/onunhandledrejection/onrejectionhandled 接线 + reportError 分发 + self===globalThis
4. **TextEncoderStreamApi** — 字符串→UTF-8 字节流往返
5. **QueuingStrategies** — ByteLength/Count highWaterMark + size()
6. **TransformStreamApi** — transform 映射 + flush 回调 + drain 后 done
7. **BroadcastChannelCloneIsolation** — 自收不投递 / postMessage 快照隔离
8. **CacheStorageExtended** — caches.has/keys/delete 生命周期 + matchAll/keys/delete + put 非 Response 拒 TypeError + match 未命中 undefined + 两次 match 独立 clone
9. **MessageChannelMessaging** — onmessage 自动 start / 排队 start() 冲刷 / close 后不投递

## 暴露并修复的真实 bug
**TransformStream（streams.js）**：close 处理器存在 `flush` 时直接返回 `flush(tsController)` 结果，**从不调用 `tsController.terminate()`** —— readable 侧永不关闭，队列 drain 后第三次 read() 永久挂起（规范要求 flush 完成后必须 close readable）。修复：await flush 结果（兼容异步 flush）后 terminate。
验证：新增 TransformStreamApi 测试 + 全量 polyfill 60/60 通过。

## 已知预存问题（与本盘点无关）
- **test_compress_gtest 为预存 flaky**：Gzip1_3KB 在整包 ctest 下偶发失败 / 单跑偶发挂起（>120s）。在纯净 master（stash 全部改动）上同样复现，非本次改动引入；压缩代码路径与本次改动无交集（无 `new TransformStream` 被压缩模块使用）。
- **EventSource 连接无法在 mock_libuv 离线测试**：gtest 仅验证 API 表面（已注明），SSE 解析/重连逻辑待网络 mock 或 e2e 覆盖。
- **build_wasm3/qwrt CLI 存在 `free(): invalid pointer` 崩溃**：疑似该构建配置问题，与本次改动无关。


## Timeline

- time: 2026-08-27T09:54:25
  kind: decision
  summary: "Created this page: ECMA-429 (Minimum common web API) 覆盖矩阵盘点 + 缺口测试"
  source: audit session
  affects: [wintertc-ecma429-coverage]

- time: 2026-08-27T09:54:42
  kind: decision
  summary: "ECMA-429 全接口盘点完成：全实现；新增 9 个缺口 gtest（polyfill 60/60 通过）；修复 TransformStream flush 不关闭 readable 的真实规范违例；记录 test_compress_gtest 预存 flaky"
  source: audit session
  affects: [wintertc-ecma429-coverage]

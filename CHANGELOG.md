# Changelog

All notable changes to Qwrt.js.

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

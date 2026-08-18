# Implementation Plan: qwrt HTTPServer Extension (uvhttp, all features)

**Spec:** docs/superpowers/specs/2026-08-15-qwrt-httpserver-design.md
**Status:** For review

## Phase 0 — Source audit (prereq for CMake wiring)

- **P0.1** Enumerate uvhttp source inter-dependencies: which of the 17 `.c`
  files each needs; confirm minimal set for all features ON (TLS/WS/static/
  compression/LRU/router-cache). Verify `UVHTTP_FEATURE_*` macro effects on
  each file (e.g. `uvhttp_static.c` under `UVHTTP_FEATURE_STATIC_FILES`,
  `uvhttp_tls.c` under TLS, `uvhttp_websocket.c` under WS).
- **P0.2** Check qwrt's mbedtls config (`deps/mbedtls/include/mbedtls/mbedtls_config.h`)
  exposes what uvhttp's TLS/WS need (SHA1, base64, x509, SSLv3/TLS server) —
  note any missing defines.
- **P0.3** Confirm uvhttp compiles cleanly against qwrt's mbedtls 3.6.6 headers
  (trial compile of uvhttp_tls.c / uvhttp_websocket.c with -I pointing at qwrt's
  mbedtls include).
- **Verification:** `gcc -fsyntax-only` on uvhttp TLS/WS sources with qwrt's
  mbedtls/miniz includes; list of sources per feature documented.

## Phase 1 — CMake integration

- **T1.1** Add `option(QWRT_WITH_HTTPSERVER ... ON)` + `set(UVHTTP_DIR ...)`
  next to existing options in root `CMakeLists.txt`; force
  `QWRT_WITH_TLS=ON` when HTTPSERVER=ON (mirror the existing TLS→CRYPTO force).
- **T1.2** When ON: append ext + uvhttp sources + deps (llhttp.c, xxhash.c,
  cJSON.c) to `qwrt` library sources; add include dirs; define
  `QWRT_WITH_HTTPSERVER=1` + `UVHTTP_FEATURE_*`=1; link `uv_a`, mbedtls trio,
  `miniz`.
- **T1.3** `qwrt_ext_registry.h`: add
  `QWRT_EXT_IF_WITH(HTTPSERVER, &qwrt_http_server_ext)`; PUBLIC definitions
  `QWRT_WITH_HTTPSERVER=$<BOOL:...>`.
- **Verification:** `cmake -B build_httpserver -DQWRT_BUILD_TESTS=OFF` configures
  and `cmake --build` links with all features ON; OFF build excludes uvhttp
  symbols (`nm` check).

## Phase 2 — Extension skeleton + `serve` registration

- **T2.1** `include/qwrt/ext_http_server.h` — `extern const qwrt_ext_t
  qwrt_http_server_ext;`.
- **T2.2** `src/ext_http_server.c` — `#if QWRT_WITH_HTTPSERVER` wrapper; init
  callback registers `serve` global (via `JS_NewCFunction`); destroy callback
  stops/closes the uvhttp server; per-extension state (`qwrt_ext_userdata`):
  server handle, JS handler ref, ws refs.
- **T2.3** `serve` argument parsing: options object → port/hostname/tls/static/
  ws + handler function (throws JS TypeError on missing handler).
- **Verification:** CLI `qwrt -e 'console.log(typeof serve)'` prints `function`
  with HTTPSERVER=ON; `undefined` with OFF.

## Phase 3 — Plain HTTP request/response (core path)

- **T3.1** Create `uvhttp_server_new(&rt->loop, ...)` + `uvhttp_server_set_handler`;
  start server (bind port/hostname). C handler stores request; builds JS
  `Request` (method, url, headers, body) via polyfill constructors
  (`JS_GetGlobalObject` → `Request`); calls JS handler; **does not send**.
- **T3.2** Promise resolution path: attach `JS_PromiseThen` continuation; on
  resolve, extract `Response.status/statusText/headers/body` → populate
  `uvhttp_response_t` → `uvhttp_response_send`; free the request/response pair.
- **T3.3** Rejection/exception → `500` text response; bad return type → `500`.
- **T3.4** `server.close()` JS method: `uvhttp_server_stop` + release; no-op on
  second call; destroy callback double-safety.
- **Verification:** integration test — `serve` + curl-style request:
  string body, Response body, async Promise, 500-on-throw, close.

## Phase 4 — HTTPS (mbedTLS)

- **T4.1** `options.tls` → `uvhttp_tls_context_new` +
  `uvhttp_tls_context_load_cert_chain(cert)` + `load_private_key(key)` +
  `uvhttp_server_enable_tls`. Errors → JS throw with uvhttp message.
- **Verification:** integration test with self-signed PEM fixture — HTTPS
  request (curl --insecure equivalent) returns 200.

## Phase 5 — WebSocket

- **T5.1** `options.ws` map → per-path `uvhttp_ws_handler_t`
  (on_open/on_message/on_close/on_error → JS callbacks); JS `ws` wrapper object
  (send/close + assignable onopen/onmessage/onclose/onerror) kept in a registry
  keyed by `uvhttp_ws_connection_t*`.
- **T5.2** Lifetime: wrapper GC-safe (registry holds strong refs; released on
  on_close); close during runtime teardown via `uvhttp_server_ws_close_all`.
- **Verification:** integration test — WS handshake + echo frame round-trip;
  close(code, reason) received server-side.

## Phase 6 — Static files + compression

- **T6.1** `options.static` → `uvhttp_static_context_create(root)` + router
  `static_context` wiring; fallthrough to handler on miss; `index` /
  `maxFileSize` options mapped.
- **T6.2** Compression: when client sends `Accept-Encoding: gzip` and response
  body ≥ threshold (uvhttp default), call `uvhttp_response_set_compress`
  (requires `UVHTTP_FEATURE_COMPRESSION=1` + miniz link).
- **Verification:** integration tests — static file served; missing file
  falls through to handler; gzip response decodes correctly.

## Phase 7 — Lifecycle, errors, tests, docs

- **T7.1** Destroy path: stop server → close ws → release tls ctx/static ctx →
  free uvhttp server, ordered before `uv_loop_close`; no leaks under ASan.
- **T7.2** Error cases: invalid port/hostname/bind failure/PEM → JS throw;
  second `serve` while running → throw.
- **T7.3** Tests: full matrix (plain/HTTPS/WS/static/compression/close/error
  cases + feature-gate OFF build).
- **T7.4** BRAIN.md / knowledge: record uvhttp integration decisions.

**Dependencies:** T3 ← T2 ← T1 ← P0. T4/T5/T6 ← T3. T7 ← T4,T5,T6.
**Acceptance:** all integration tests pass; `QWRT_WITH_HTTPSERVER=OFF` build
clean; no duplicated libuv/mbedtls/miniz in link map.

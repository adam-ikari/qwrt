# qwrt HTTPServer Extension — Design Spec

**Date:** 2026-08-15
**Status:** Draft for review
**Related:** uvhttp (/home/gem/project/uvhttp), qwrt extension registry (qwrt_ext_registry.h)

## 1. Overview & Goals

Provide the **full uvhttp capability set** to qwrt (the QuickJS-ng embeddable
runtime) as a compile-time-gated extension, using the user's **uvhttp** library
as the server foundation. Exposes a WinterCG-style `serve()` API so JS code can
run an HTTP/HTTPS server with WebSocket, static-file and compression support on
the same single-threaded libuv loop qwrt already owns.

- **JS API:** `serve({ port, hostname, tls, static, ws }, handler)`
  (Deno.serve / Bun.serve style, extended with uvhttp's full feature set)
- **Handler model:** fetch-style — `handler(req)` returns a `Response` (or string /
  Promise thereof); the runtime serializes it back to the socket.
- **Features (all of uvhttp):** HTTP/1.1, HTTPS (mbedTLS), WebSocket,
  static-file serving, response compression, LRU cache.
- **Build gate:** `QWRT_WITH_HTTPSERVER` CMake option (default **ON**). When OFF,
  `serve` is absent and zero uvhttp code is compiled in.

## 1.1 Feature mapping (uvhttp → qwrt)

| uvhttp feature | uvhttp macro | qwrt wiring |
|---|---|---|
| HTTP/1.1 core | always | uvhttp core sources (15 .c) |
| HTTPS/TLS | `UVHTTP_FEATURE_TLS=1` | mbedTLS — qwrt's existing deps (3.6.6) |
| WebSocket | `UVHTTP_FEATURE_WEBSOCKET=1` | `uvhttp_websocket.c` + mbedTLS (SHA1/Base64) |
| Static files | `UVHTTP_FEATURE_STATIC_FILES=1` | `uvhttp_static.c` (+ sendfile) |
| Compression | `UVHTTP_FEATURE_COMPRESSION=1` | miniz — qwrt's existing deps |
| LRU cache | `UVHTTP_FEATURE_LRU_CACHE=1` | `uvhttp_lru_cache.c` (internal, transparent) |
| Router cache | `UVHTTP_FEATURE_ROUTER_CACHE=1` | `uvhttp_router_cache.c` (internal, transparent) |

## 2. Architecture

### 2.1 uvhttp integration (no loop ownership conflict)

- uvhttp's `uvhttp_server_new(uv_loop_t*, ...)` accepts an **external loop** — qwrt
  passes `&rt->loop` (the loop owned by the qwrt thread). All accept/read/write/
  handler events run on the qwrt thread, preserving the project's
  single-threaded, lock-free philosophy: **no new threads, no locks**.
- uvhttp is compiled **from source into the qwrt library** (not via uvhttp's own
  CMake, which uses ExternalProject to build a *second* libuv — that would
  collide with qwrt's vendored libuv).
  - Core sources: `src/uvhttp_{server,request,response,router,router_cache,connection,context,config,error,error_helpers,utils,protocol_upgrade,version,lru_cache,static}.c`
  - TLS/WS when enabled: `src/uvhttp_tls.c`, `src/uvhttp_websocket.c`
  - Deps (vendored inside uvhttp): `llhttp` (llhttp.c), `xxhash`, `cJSON`,
    `uthash` (header-only)
- **Third-party libs are unified with qwrt's existing deps** (no duplicates):
  - **libuv** → qwrt's vendored libuv (`uv_a`); uvhttp never builds its own.
  - **mbedTLS** → qwrt's deps/mbedtls (3.6.6). uvhttp's TLS/WS code compiles
    against qwrt's mbedtls headers; **verify API compatibility at build time**
    (risk item — uvhttp's own vendored mbedtls may differ in version; the
    extension build must pass uvhttp's compile tests).
  - **miniz** → qwrt's deps/miniz (already used by QWRT_WITH_COMPRESS). uvhttp's
    compression code uses the miniz API (`mz_*` / `miniz_*`); unify include path.
  - llhttp/xxhash/cJSON/uthash have **no qwrt counterpart** → compiled from
    uvhttp's deps/.
- CMake: `QWRT_WITH_HTTPSERVER` (option, default ON) + `UVHTTP_DIR` (default
  `${CMAKE_CURRENT_SOURCE_DIR}/../uvhttp`, overridable), wiring following the
  existing `QWRT_WITH_*` extension pattern.
- **Risk & verification:** uvhttp's TLS/compression code may assume its own
  mbedtls/miniz versions. The plan must include a build smoke test (tls + ws +
  static + compression all enabled) to catch API drift early.

### 2.2 Extension shape

Follows `ext_compress.c` / `ext_crypto.c`:

- `include/qwrt/ext_http_server.h` — `extern const qwrt_ext_t qwrt_http_server_ext;`
- `src/ext_http_server.c` — gated by `#if QWRT_WITH_HTTPSERVER`; registers
  `serve` (and its companion classes) via the extension init callback.
- Registry: `qwrt_ext_registry.h` adds
  `QWRT_EXT_IF_WITH(HTTPSERVER, &qwrt_http_server_ext)`.

### 2.3 Request/Response reuse

qwrt's fetch polyfill already defines WHATWG `Headers` / `Request` / `Response`
(`polyfill/src/fetch.js`). The extension:
- builds a `Request` object from the uvhttp request (method, url, headers, body),
- calls the JS handler,
- extracts `status` / `statusText` / `headers` / `body` from the returned
  `Response` and writes them back via uvhttp's response API.

## 3. JS API

```js
const server = serve(
  {
    port: 3000,
    hostname: "127.0.0.1",
    // --- HTTPS ---
    tls: { cert: "-----BEGIN CERTIFICATE-----...", key: "-----BEGIN PRIVATE KEY-----..." },
    // --- static files (served before handler; handler sees only non-static paths) ---
    static: { root: "./public", index: "index.html", maxFileSize: 10 * 1024 * 1024 },
    // --- WebSocket handlers by path ---
    ws: {
      "/ws/chat": (ws, req) => {
        ws.onmessage = (e) => ws.send("echo: " + e.data);
        ws.onclose = (code, reason) => console.log("closed", code);
      },
    },
  },
  async (req) => {
    if (req.url === "/api") return Response.json({ ok: true });
    return new Response("Not Found", { status: 404 });
  }
);
server.close(); // stops accepting + releases; safe once
```

### 3.1 `serve(options, handler)`

- `options.port` (number, default 8080); `options.hostname` (string, default
  `"0.0.0.0"`).
- `options.tls` — HTTPS. `{ cert: string, key: string }` (PEM). When present,
  `uvhttp_tls_context_new` + `load_cert_chain` + `load_private_key` +
  `uvhttp_server_enable_tls`. `serve` throws on invalid PEM.
- `options.static` — static file serving. `{ root: string, index?: string,
  maxFileSize?: number }`. Built via `uvhttp_static_context_create` and attached
  to the router; paths not matching a static file fall through to `handler`.
- `options.ws` — WebSocket endpoints. Map of `path → (ws, req) => {}`. Each path
  registers a `uvhttp_ws_handler_t` with on_open/on_message/on_close/on_error
  bridged to JS. WS upgrade requests never reach the HTTP `handler`.
- `handler(req)` — returns `Response` | string | Promise thereof. Handler
  exception/rejection → `500`. Bad return type → `500`.
- Returns `{ close() }`. `close()` stops accepting and releases the uvhttp
  server; further in-flight requests complete per uvhttp semantics.

### 3.2 Server-side WebSocket object (`ws`)

Browser-`WebSocket`-style surface (v1 minimal):

- `ws.send(data)` — string or ArrayBuffer → `uvhttp_server_ws_send`
- `ws.close(code?, reason?)` → `uvhttp_server_ws_close`
- Properties: `ws.url` (request URL), `ws.protocol` (subprotocol, if any)
- Callbacks (assignable): `ws.onopen`, `ws.onmessage` (`{ data }` event),
  `ws.onclose` (`{ code, reason }`), `ws.onerror`
- C-side: each live `uvhttp_ws_connection_t*` maps to a JS wrapper stored in
  the ws-handler `user_data` registry; callbacks invoked from uvhttp on the qwrt
  thread (safe — single thread).

### 3.3 Static files

- `static.root` resolved relative to cwd. Index file support (`index`), optional
  `maxFileSize` guard, sendfile config wired from uvhttp defaults.
- Order: static lookup first (uvhttp router static_context), then `handler`.
- Not found in static → falls through to handler (which may 404).

## 4. Request flow (C side)

1. uvhttp parses the request on the qwrt thread and calls our C handler
   (`uvhttp_request_handler_t`).
2. The C handler builds a JS `Request` (method, url, headers, body from uvhttp
   request) and calls the JS handler, capturing the return value (Promise).
3. The C handler **returns without sending** — the connection stays alive.
   uvhttp's `uvhttp_response_send` is pure (build_data + send_raw) and may be
   called later.
4. On Promise resolution: read `Response.status` / `headers` / `body`,
   populate the `uvhttp_response_t` (set_status / set_header / set_body) and
   send. Compression: if the response body exceeds the threshold and the client
   accepts gzip, `uvhttp_response_set_compress`.
5. On rejection/handler exception: send `500` with a short text body.
6. Keep-alive/connection lifecycle handled by uvhttp.

**Async timing note:** the Promise is resolved by JS microtask flushing; the
qwrt main loop already flushes microtasks between `uv_run` iterations, so the
send happens on the same thread shortly after.

## 5. Lifecycle

- Extension init (`http_server_ext_init`): register `serve` global on the main
  context (same pattern as `compress_ext_init`).
- `serve()` creates the `uvhttp_server_t` + TLS/static/WS wiring; state kept in
  extension `user_data` (server handle, JS refs for handlers).
- Runtime teardown (`qwrt_thread_teardown` → `qwrt_ctx_destroy` → extension
  destroy callback): stop the uvhttp server, close listening socket, close WS
  connections, release uvhttp structures. Must run before `uv_loop_close`.
- `server.close()` (JS): stop + release; second call is a no-op.

## 6. Error handling

- `serve` with invalid port/hostname, bind failure, or bad PEM → throws JS
  `Error` carrying the uvhttp error message.
- Handler exception/rejection → `500` response with short text body.
- WS handler exceptions → log + close the WS connection (code 1011).

## 7. Build integration

```cmake
option(QWRT_WITH_HTTPSERVER "Build the uvhttp-based HTTP server extension" ON)
set(UVHTTP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../uvhttp" CACHE PATH "...")
```

When ON (all uvhttp features):
- Sources appended to the qwrt library: `src/ext_http_server.c` + uvhttp core
  (server/request/response/router/router_cache/connection/context/config/error/
  error_helpers/utils/protocol_upgrade/version/lru_cache/static) + TLS/WS
  (`uvhttp_tls.c`, `uvhttp_websocket.c`) + deps (`llhttp.c`, `xxhash.c`,
  `cJSON.c`).
- Include dirs added: `UVHTTP_DIR/include`, `UVHTTP_DIR/deps/llhttp/include`,
  `UVHTTP_DIR/deps/uthash/src`, `UVHTTP_DIR/deps/xxhash`,
  `UVHTTP_DIR/deps/cjson` — plus qwrt's mbedtls/miniz include dirs (already in
  the build when QWRT_WITH_TLS / QWRT_WITH_COMPRESS are ON).
- Defines: `QWRT_WITH_HTTPSERVER=1`, `UVHTTP_FEATURE_TLS=1`,
  `UVHTTP_FEATURE_WEBSOCKET=1`, `UVHTTP_FEATURE_COMPRESSION=1`,
  `UVHTTP_FEATURE_STATIC_FILES=1`, `UVHTTP_FEATURE_LRU_CACHE=1`,
  `UVHTTP_FEATURE_ROUTER_CACHE=1`.
- Links: qwrt's `uv_a` (libuv), `mbedtls`/`mbedcrypto`/`mbedx509` (qwrt's TLS
  build), `miniz` (qwrt's compress build).
- Registry: `QWRT_EXT_IF_WITH(HTTPSERVER, &qwrt_http_server_ext)` +
  `QWRT_WITH_HTTPSERVER=$<BOOL:...>` in the PUBLIC definitions list.
- **No duplicate libuv/mbedtls/miniz copies.**

## 8. Testing

- **Integration test** (real loop): gtest creates a runtime, runs `serve` via
  the eval channel, issues HTTP requests from the test (uvhttp client or raw
  socket), asserts status/body. Cases:
  - plain HTTP handler (string / Response / async Promise / 500 on throw)
  - HTTPS (self-signed cert via test fixture) — `curl --insecure` style check
  - WebSocket echo (raw WS handshake + frame) 
  - static file serving (fixture dir) + fallthrough to handler
  - compression: `Accept-Encoding: gzip` → gzipped body
  - `server.close()` + re-`serve` after close
- **CLI smoke test:** `qwrt -e 'serve(...)'` + curl; listening handle keeps the
  loop busy so the CLI waits for `server.close()` or signal.
- **Feature-gate test:** build with `QWRT_WITH_HTTPSERVER=OFF` → `serve` is
  undefined, no uvhttp symbols in the binary.

## 9. Out of scope (v1)

- HTTP/2, streaming request bodies (bodies fully buffered in memory), client
  auth (mTLS) in the JS API (uvhttp supports it in C; expose later if asked).
- Multiple servers per runtime: v1 keeps a single active `serve`; a second
  `serve` while one is running throws. (Extend to a list later if needed.)

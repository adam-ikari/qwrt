---
title: serve
description: The HTTP server API in Qwrt.js — serve(), request handling, WebSocket server routes, TLS, and lifecycle.
---

# serve — HTTP Server API

A pure-JS HTTP/1.1 server exposed as the global `serve()`. Transport comes from the `pal.tcp*` layer (bind/listen/accept/read/write); all protocol semantics — request parsing, routing, WebSocket upgrade, response serialization — run in JavaScript. Useful for embedded test servers, local tooling, and device dashboards.

## Global

| Global | Type | Description |
|--------|------|-------------|
| `serve` | `function` | Starts an HTTP server. Only one may be active at a time. |

## serve(options, handler)

Starts a listening server and returns a server handle. `handler` is called once per HTTP request; its return value (or resolved Promise value) is sent as the response.

```js
let server = serve({ port: 8080 }, (req) => {
  if (req.pathname === '/hello') return 'Hello, world!';
  return { status: 404, headers: { 'Content-Type': 'text/plain' }, _body: 'Not found' };
});
```

### Options

| Option | Default | Description |
|--------|---------|-------------|
| `port` | `8080` | TCP port to listen on (0–65535). |
| `hostname` | `'0.0.0.0'` | Address to bind. |
| `idleTimeout` | `30000` | ms of connection inactivity before close; `0` disables. |
| `ws` | `{}` | Route table for WebSocket upgrades, keyed by request path. |
| `tls` | `undefined` | `{ cert, key }` PEM strings to enable HTTPS (requires `QWRT_WITH_TLS`). |

### Request object

`handler` receives a plain object describing the request:

| Field | Type | Description |
|-------|------|-------------|
| `method` | `string` | HTTP method, e.g. `'GET'`. |
| `url` | `string` | Raw request path including query, e.g. `'/a?x=1'`. |
| `pathname` | `string` | Path without query, e.g. `'/a'`. |
| `search` | `string` | Query string including `?`, or `''`. |
| `headers` | `object` | Lower-cased header names → values. |
| `body` | `ReadableStream` \| `null` | Request body when `Content-Length > 0`, otherwise `null`. |
| `keepAlive` | `boolean` | Whether the connection stays open after this response. |

The object also exposes async body readers:

```js
serve({ port: 8080 }, async (req) => {
  let text = await req.text();          // full body as string
  let buf  = await req.arrayBuffer();   // full body as ArrayBuffer
  return { status: 200, headers: { 'Content-Type': 'text/plain' }, _body: text };
});
```

Bodies are fed straight from raw socket bytes into the `ReadableStream`, so binary request bodies are preserved.

### Response values

`handler` may return (or resolve to):

- A **string** — sent as `200 OK` with `Content-Type: text/plain; charset=utf-8`.
- An **object** with `status`, `statusText`, `headers` (a `Headers` instance or plain object), and `_body` (string, `ArrayBuffer`, or `Uint8Array`). Binary bodies go out as raw bytes.
- `null`/`undefined` — `500 Internal Server Error` (also used when the handler throws).

```js
serve({ port: 8080 }, (req) => {
  if (req.method !== 'POST') return { status: 405, _body: 'POST only' };
  let data = new Uint8Array([0xDE, 0xAD, 0xBE, 0xEF]);
  return {
    status: 201,
    headers: { 'Content-Type': 'application/octet-stream' },
    _body: data
  };
});
```

### Server handle

`serve()` returns `{ closed, close() }`:

```js
let server = serve({ port: 8080 }, handler);
server.close();        // stops listening, releases the port
```

Only one server may run at a time. Calling `serve()` again while another is active throws `serve: a server is already running (call srv.close() first)`.

## WebSocket routes

`options.ws` maps paths to upgrade handlers. Each handler receives a connection object with `onopen`, `onmessage`, `onclose`, `onerror`, `send()`, and `close()`.

```js
serve({
  port: 8080,
  ws: {
    '/chat': (conn) => {
      conn.onmessage = (ev) => {
        conn.send('echo: ' + ev.data);       // text messages arrive as strings
      };
      conn.onclose = () => console.log('disconnected');
    }
  }
}, (req) => 'HTTP fallback');
```

A route value may also be an object with `handler` and an optional `protocols` array for subprotocol negotiation:

```js
ws: {
  '/chat': {
    handler: (conn) => { conn.onmessage = (ev) => conn.send('pong'); },
    protocols: ['chat.v1']        // echoed via Sec-WebSocket-Protocol if offered
  }
}
```

Connection object:

| Member | Type | Description |
|--------|------|-------------|
| `send(data)` | `function` | Send a text message (string) or binary (Uint8Array). |
| `close(code, reason)` | `function` | Send a close frame and mark the connection closed. |
| `onopen` | `callback` | Fires when the socket is ready. |
| `onmessage` | `callback` | Receives `{ data }` — string for text frames, `Uint8Array` for binary. |
| `onclose` | `callback` | Receives `{ code, reason, wasClean }`. |
| `onerror` | `callback` | Connection error. |

`permessage-deflate` (RFC 7692) compression is negotiated automatically when the client offers it and the native streaming deflate primitives are available (see [compress](/js-api/compress)).

Requests without a matching `ws` route get a `404`; an upgrade missing `Sec-WebSocket-Key` gets a `400`.

## TLS

Passing `tls: { cert, key }` enables HTTPS on the listener. Requires `QWRT_WITH_TLS=ON` at build time (see [Build Options](/guide/build-options)).

```js
serve({
  port: 8443,
  tls: { cert: pemCert, key: pemKey }
}, (req) => 'secure!');
```

## Notes

- HTTP/1.1 keep-alive is supported; the connection closes after the response when the client requests `close` or uses HTTP/1.0.
- `idleTimeout` resets on each received byte and on each keep-alive response.
- Headers parsed from the wire are lower-cased; response header names are passed through as given.
- The server is synchronous in the sense that each handler runs to completion (or awaits); a slow handler blocks that connection only.

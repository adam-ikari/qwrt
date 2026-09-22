---
title: WebSocket
description: The WebSocket API in Amoib.js — global `WebSocket` client and serve() WebSocket routes, RFC 6455 over raw TCP.
---

# WebSocket

amoib has both sides of WebSocket:

- **Client**: the global `WebSocket` class (RFC 6455 client over raw TCP)
- **Server**: WebSocket routes registered on a `serve()` listener (see
  [serve → WebSocket routes](/js-api/serve#websocket-routes))

## Global

| Global | Type |
|--------|------|
| `WebSocket` | `class` |

## Client

```js
const ws = new WebSocket('ws://127.0.0.1:9000/echo');
ws.onopen = () => ws.send('hello');
ws.onmessage = (ev) => console.log('echo:', ev.data);
ws.onclose = (ev) => console.log('closed', ev.code, ev.reason);
ws.onerror = () => console.log('error');
```

The client connects over raw TCP, with no Node.js `net`/`http` dependency. Frames follow RFC 6455 (masked client frames,
unmasked server frames).

## Server

WebSocket endpoints are declared in the `ws` option of `serve()` and live on
the same port as HTTP:

```js
serve({
  port: 9000,
  ws: {
    '/echo': (ws) => {
      ws.onmessage = (e) => ws.send('echo:' + e.data);
    },
  },
}, () => 'not-ws');
```

Each route receives a connected `ws` object whose `onmessage` / `onclose`
handlers the route registers. See [serve → WebSocket routes](/js-api/serve#websocket-routes)
for the full route-table shape including subprotocol negotiation.

## Notes

- Connection upgrade happens on the same TCP port as HTTP — there is no
  separate ws port.
- `close(code, reason)` performs the closing handshake (code 1000 by
  default); the server echoes back the code/reason.

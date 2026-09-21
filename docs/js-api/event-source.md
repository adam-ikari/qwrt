---
title: EventSource
description: The EventSource API in Qwrt.js — Server-Sent Events (SSE) client with automatic reconnection.
---

# EventSource

Server-Sent Events client. Opens a long-lived HTTP connection to a URL that
streams `text/event-stream` frames, dispatches typed events on the
`EventSource`, and reconnects automatically on disconnect with the last
event ID.

## Global

| Global | Type |
|--------|------|
| `EventSource` | `class` |

## API

```js
const src = new EventSource('https://example.com/events');
src.onmessage = (ev) => console.log('default', ev.data);
src.addEventListener('update', (ev) => console.log('update', ev.data));
src.onerror = () => console.log('reconnecting…');
// src.close();   // stop receiving
```

| Member | Description |
|--------|-------------|
| `new EventSource(url, init?)` | Connect; `init.withCredentials` controls credentials |
| `onmessage` | Default-channel event handler |
| `addEventListener(type, fn)` | Named-event subscription (parsed from `event:` lines) |
| `close()` | Stop receiving and abort reconnection |
| `readyState` | `0` connecting, `1` open, `2` closed |

## Notes

- Reconnection respects the server's `retry:` field if present; otherwise
  a default delay is used.
- The `Last-Event-ID` header carries the last received `id:` so the server
  can resume the stream.

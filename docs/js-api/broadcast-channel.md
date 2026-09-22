---
title: BroadcastChannel
description: The BroadcastChannel API in Qzjs.js — same-origin cross-context messaging for isolated contexts in one runtime.
---

# BroadcastChannel

A named messaging channel that broadcasts to every `BroadcastChannel`
instance created with the same name. Used to coordinate work between
spawned contexts inside one qzjs runtime.

## Global

| Global | Type |
|--------|------|
| `BroadcastChannel` | `class` |

## API

```js
const a = new BroadcastChannel('work');
const b = new BroadcastChannel('work');

a.onmessage = (ev) => console.log('a got', ev.data);
b.postMessage({ task: 'sync' });   // a receives it
```

| Member | Description |
|--------|-------------|
| `new BroadcastChannel(name)` | Subscribe to a named channel |
| `postMessage(data)` | Broadcast to all other subscribers on the same name |
| `onmessage` | Handler called with `{ data }` on each broadcast |
| `close()` | Unsubscribe; no further messages delivered |

## Notes

- Broadcast does not loop back to the sender.
- Messages are JSON-serializable; structured data is fine, but functions
  and proxies are not supported across the channel.

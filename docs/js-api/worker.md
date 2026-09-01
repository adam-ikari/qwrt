---
title: Worker
description: The Web Worker API in Qwrt.js — Worker class, postMessage, terminate, worker-side globals, and message passing.
---

# Worker API

A W3C-style `Worker` class backed by a real qwrt runtime thread. Each worker is its own `qwrt_t` with its own thread, event loop, and JS runtime (execution model A).

## Global

| Global | Type | Description |
|--------|------|-------------|
| `Worker` | class | Spawns a worker thread running a script file. |

## new Worker(url)

Loads a script synchronously and spawns the worker thread. The constructor blocks until the worker is ready, then returns an instance keyed by worker id.

```js
let w = new Worker('file:///app/tasks.js');
w.onmessage = (ev) => console.log('result:', ev.data);
w.postMessage({ op: 'sum', values: [1, 2, 3] });
```

**v1 limitation:** only `file://` URLs are accepted — the script is read via the host filesystem, so workers are local-only in the current release.

Throws an `Error` if the script cannot be loaded or the thread fails to spawn.

## Worker instance

| Member | Type | Description |
|--------|------|-------------|
| `postMessage(value)` | `function` | Send a message to the worker. The value is [structured-cloned](/js-api/structured-clone) into bytes and delivered asynchronously. |
| `terminate()` | `function` | Stop the worker. Stops its event loop and joins the thread (at parent teardown). |
| `onmessage` | `callback` | Fires with a `MessageEvent` whose `data` is deserialized from the worker's reply. |
| `onmessageerror` | `callback` | Fires with a `MessageEvent('messageerror')` when a message from the worker fails to deserialize. |
| `addEventListener(type, cb, options?)` / `removeEventListener(type, cb)` | `function` | Standard event registration for `message`, `error`, and `messageerror` on the Worker instance. |
| `onerror` | `callback` | Fires when the worker script throws at the top level. `event.data` is `{ type: 'error', error: <message> }`. |

```js
let w = new Worker('file:///app/counter.js');

w.onmessage = (ev) => {
  console.log('worker said:', ev.data);
};

w.onerror = (ev) => {
  console.error('worker crashed:', ev.data.error);
};

w.postMessage('increment');
// ...later...
w.terminate();
```

## Worker-side globals

Inside the worker script the following globals are available (the worker runtime has the full [JS API](/js-api) surface plus these):

| Global | Description |
|--------|-------------|
| `postMessage(value, transfer?)` | Send a message back to the parent. Supports structured-clone transfer of `MessagePort`s and `ArrayBuffer`s. |
| `onmessage` / `addEventListener('message', …)` | Receive messages from the parent. Handlers get a `MessageEvent` with `data`. |
| `close()` | Terminate the current worker from within. |
| `importScripts(...urls)` | Synchronously load and run additional scripts. `file://` only in v1. |

```js
// tasks.js
onmessage = (ev) => {
  let { op, values } = ev.data;
  if (op === 'sum') {
    postMessage(values.reduce((a, b) => a + b, 0));
  } else if (op === 'die') {
    close();
  }
};
```

## Message passing

- Messages travel as [structured-clone](/js-api/structured-clone) bytes over each thread's inbound queue.
- Parent → worker delivery dispatches through `__qwrt_dispatch__`; the worker-side boot shim deserializes and fires a `MessageEvent`.
- `MessagePort`s can be transferred alongside a message (both parent→worker and worker→parent), including multi-hop forwarding between threads.

## Error handling

- A top-level exception in the worker script dispatches an `ErrorEvent` (triggering `onerror` in the worker) and then reports to the parent, whose `onerror` fires with `event.data = { type: 'error', error: <message> }`.
- A message from the worker that fails to deserialize dispatches `messageerror` on the parent Worker instance (`w.onmessageerror` or `addEventListener('messageerror', …)`).
- Workers are independent runtimes: an uncaught error does not crash the parent.

## Notes

- Each worker is a separate OS thread with its own event loop — use them for CPU-bound or blocking work without stalling the main runtime.
- Script loading is synchronous at construction; the script path must already exist on the host filesystem.
- No worker pooling, module workers (`type: 'module'`), or `navigator.hardwareConcurrency`-driven scaling in v1.

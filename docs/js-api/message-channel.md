---
title: MessageChannel
description: The MessageChannel API in Qwrt.js — structured clone, port messaging, and inter-context communication.
---

# MessageChannel / MessagePort

WHATWG Channel Messaging API for communication between execution contexts. `MessageChannel` creates two connected ports; `MessagePort` sends and receives messages.

## Globals

| Global | Description |
|--------|-------------|
| `MessageChannel` | Creates a pair of connected MessagePorts |
| `MessagePort` | Port for sending/receiving messages |

## MessageChannel

### Basic Usage

```js
let channel = new MessageChannel();

// Port 1 → Port 2
channel.port1.addEventListener('message', (event) => {
    console.log('Port 1 received:', event.data);
});

channel.port2.postMessage('Hello from port 2!');
```

### Constructor

```js
let channel = new MessageChannel();
// channel.port1 and channel.port2 are connected
```

No options — the constructor always creates two connected ports.

## MessagePort

### postMessage(data)

Send a message to the connected port:

```js
port.postMessage('simple string');
port.postMessage({ key: 'value' });
port.postMessage([1, 2, 3]);
```

Messages are cloned (not shared). The receiver gets a copy.

### Event: 'message'

```js
port.addEventListener('message', (event) => {
    console.log('Received:', event.data);
    // event.data is the cloned message
    // event.ports is an array of transferred MessagePorts (always empty in qwrt)
});
```

### start()

Start the port. In qwrt, ports start automatically when created — explicit `start()` is only needed if `addEventListener` is called after `postMessage`.

```js
port.start();
```

### close()

Close the port, preventing further messages:

```js
port.close();
```

### onmessage

Event handler property alternative:

```js
port.onmessage = (event) => {
    console.log('Got:', event.data);
};
```

### onmessageerror

Called when a message can't be deserialized:

```js
port.onmessageerror = (event) => {
    console.error('Failed to deserialize message');
};
```

## Cross-Context Communication

The primary use case for `MessageChannel` is communication between qwrt contexts (multi-context mode):

```js
// Context A
let channel = new MessageChannel();
contextB.postMessage({ port: channel.port2 }, [channel.port2]);

channel.port1.addEventListener('message', (event) => {
    console.log('Response from context B:', event.data);
});

// Context B (in its own qwrt context)
globalThis.addEventListener('message', (event) => {
    let port = event.ports[0];
    console.log('Request from context A:', event.data);
    port.postMessage('Response from B');
});
```

## Transferable Objects

When using `postMessage` with the second argument (transfer list), the ownership of `MessagePort` objects is transferred:

```js
// Transfer port2 to the receiver — sender loses access
port1.postMessage('take this port', [port2]);
// port2 is now neutered; can't be used by sender
```

Only `MessagePort` objects can be transferred. `ArrayBuffer` transfer is not supported.

## Notes

- Messages are cloned via structured clone algorithm (see [structuredClone](/js-api/structured-clone))
- No `BroadcastChannel` — use `MessageChannel` for 1:1, or build your own pub/sub
- Ports are garbage collected when unreferenced
- There's no concept of "origin" or "source" on received messages
- `event.ports` is always populated (empty array if no ports transferred)

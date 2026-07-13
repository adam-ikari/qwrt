# ReadableStream / WritableStream

WHATWG Streams API for incremental data processing. `ReadableStream` is used internally by `fetch()` for streaming HTTP responses. `WritableStream` and `TransformStream` provide stream pipeline primitives.

## Globals

| Global | Type | Description |
|--------|------|-------------|
| `ReadableStream` | class | Source of data chunks |
| `ReadableStreamDefaultReader` | class | Reader for consuming a ReadableStream |
| `WritableStream` | class | Sink for data chunks |
| `TransformStream` | class | Pipe-through transform |

## ReadableStream

### Creating a ReadableStream

```js
let stream = new ReadableStream({
    start(controller) {
        // Called immediately. Queue initial data or set up producer.
        controller.enqueue(new Uint8Array([1, 2, 3]));
    },
    pull(controller) {
        // Called when the consumer wants more data.
        // Not implemented in qwrt's WinterCG modules.
    },
    cancel(reason) {
        // Called when the consumer cancels the stream.
        console.log('Stream cancelled:', reason);
    }
});
```

### Consuming a ReadableStream

```js
let reader = stream.getReader();

while (true) {
    let { done, value } = await reader.read();
    if (done) break;
    console.log('Chunk:', value.length, 'bytes');
}

reader.releaseLock();
```

### Controller API

```js
new ReadableStream({
    start(controller) {
        controller.enqueue(chunk);    // queue a chunk (Uint8Array or ArrayBuffer)
        controller.close();           // signal end of stream
        controller.error(new Error()); // signal stream error
    }
});
```

### Stream States

```mermaid
flowchart TB
    A["Created → start() called"] --> B{"controller action"}
    B -->|controller.enqueue()| C["Readable (chunks queued)"]
    B -->|controller.close()| D["Closed (done: true on next read)"]
    B -->|controller.error(e)| E["Errored (rejects pending reads)"]
```

## Usage with fetch()

```js
let response = await fetch('https://example.com/large-data');
let reader = response.body.getReader();

let chunks = [];
while (true) {
    let { done, value } = await reader.read();
    if (done) break;
    chunks.push(value);
}

// Concatenate chunks
let totalLength = chunks.reduce((sum, c) => sum + c.length, 0);
let combined = new Uint8Array(totalLength);
let offset = 0;
for (let chunk of chunks) {
    combined.set(chunk, offset);
    offset += chunk.length;
}
```

## WritableStream

### Creating a WritableStream

```js
let writable = new WritableStream({
    start(controller) {
        // Called immediately
    },
    write(chunk, controller) {
        // Process each chunk
        console.log('Writing:', chunk.length, 'bytes');
        // Return a promise to apply backpressure
    },
    close() {
        // All chunks written
        console.log('Stream closed');
    },
    abort(reason) {
        // Stream aborted
        console.log('Aborted:', reason);
    }
});
```

### Writing to a WritableStream

```js
let writer = writable.getWriter();

await writer.write(new Uint8Array([1, 2, 3]));
await writer.write(new Uint8Array([4, 5, 6]));
await writer.close();

// Or abort
// await writer.abort('Cancelled');
```

## TransformStream

Pipe-through transformation:

```js
let transform = new TransformStream({
    start(controller) {
        // Called immediately
    },
    transform(chunk, controller) {
        // Transform each chunk
        let transformed = chunk.map(b => b * 2);
        controller.enqueue(transformed);
    },
    flush(controller) {
        // All chunks processed
    }
});

// Pipe: readable → transform → writable
readable.pipeThrough(transform).pipeTo(writable);
```

## Backpressure

`WritableStream` supports backpressure through `write()` returning a promise:

```js
let writable = new WritableStream({
    async write(chunk) {
        await slowProcess(chunk);  // applies backpressure
    }
});

let writer = writable.getWriter();

// This will pause after the first chunk until write completes
await writer.write(hugeChunk1);
await writer.write(hugeChunk2);
```

## Notes

- `ReadableStream` is fully implemented (used by `fetch`)
- `WritableStream` and `TransformStream` have basic implementations
- `pipeTo` and `pipeThrough` are available but with limited error recovery
- `ByteStreamController` is not implemented (use default controller)
- `ReadableByteStreamController` / `tee()` are not yet supported
- Chunks should be `Uint8Array` or `ArrayBuffer` for interoperability

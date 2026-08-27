---
title: compress
description: Compression in Qwrt.js — CompressionStream and DecompressionStream for gzip, deflate, and deflate-raw via miniz.
---

# Compression API

The WHATWG `CompressionStream` / `DecompressionStream` interfaces, backed by a native miniz extension. `CompressionStream` compresses a stream of chunks; `DecompressionStream` reverses the operation.

## Globals

| Global | Type | Description |
|--------|------|-------------|
| `CompressionStream` | class | Transform stream that compresses written chunks. |
| `DecompressionStream` | class | Transform stream that decompresses written chunks. |

Both are standard [Streams](/js-api/streams) — they expose `.readable` and `.writable` and can be piped through a pipeline.

## new CompressionStream(format)

Creates a compression transform. `format` defaults to `'gzip'`.

| Format | Wire format |
|--------|-------------|
| `'gzip'` | gzip wrapper (10-byte header + DEFLATE + CRC32 + ISIZE) |
| `'deflate'` | zlib wrapper (2-byte header + DEFLATE + Adler-32 trailer) |
| `'deflate-raw'` | raw DEFLATE, no wrapper |

```js
let cs = new CompressionStream('gzip');
let rs = new ReadableStream({ start(c) { c.enqueue(new TextEncoder().encode('hello world')); c.close(); } });
let chunks = [];

await rs
  .pipeThrough(cs)
  .pipeTo(new WritableStream({ write(c) { chunks.push(c); } }));

let gzipBytes = chunks[0]; // Uint8Array of gzip-compressed data
```

## new DecompressionStream(format)

Creates a decompression transform. `format` defaults to `'gzip'` and must match the wrapper used to produce the data (`'gzip'`, `'deflate'`, or `'deflate-raw'`).

```js
let ds = new DecompressionStream('gzip');
let rs = new ReadableStream({ start(c) { c.enqueue(gzipBytes); c.close(); } });
let out = [];

await rs
  .pipeThrough(ds)
  .pipeTo(new WritableStream({ write(c) { out.push(c); } }));

let text = new TextDecoder().decode(out[0]); // "hello world"
```

An unsupported format name throws `Error: CompressionStream: unsupported format: <fmt>` (or `DecompressionStream:`).

## Piping with HTTP

A typical use is compressing a fetch response body:

```js
let res = await fetch('https://example.com/big.json');
let decompressed = res.body.pipeThrough(new DecompressionStream('gzip'));
let text = await new Response(decompressed).text();
```

## Implementation

- Chunks written to `.writable` are buffered; compression/decompression happens once on stream close (all-at-once, not incremental per chunk).
- The polyfill delegates to `pal.nativeCompress` / `pal.nativeDecompress` (C, via miniz). Raw DEFLATE comes from miniz's stream API; zlib and gzip headers/trailers are written by qwrt (gzip uses a slice-by-4 CRC32).
- The same extension also exposes `pal.deflateCreate`/`deflatePush`/`deflateFree` and `pal.inflateCreate`/`inflatePush`/`inflateFree` — the streaming primitives used for `permessage-deflate` WebSocket compression in [serve](/js-api/serve).

## Build option

Compression requires `QWRT_WITH_COMPRESS=ON` (default) at build time. When disabled, the classes exist but error the readable side with `TypeError: Native compression extension not available`. See [Build Options](/guide/build-options).

## Notes

- Input chunks should be `Uint8Array` or `ArrayBuffer` (other chunk types are coerced via `new Uint8Array(...)`).
- All-at-once processing: the full input is held in memory until the stream closes, so extremely large streams are not memory-bounded.
- Decompression of malformed input errors the readable side rather than producing partial output.

---
title: compress
description: Amoib.js 的压缩 —— CompressionStream 与 DecompressionStream，经 miniz 支持 gzip、deflate、deflate-raw。
---

# 压缩 API

WHATWG 的 `CompressionStream` / `DecompressionStream` 接口，由原生 miniz
扩展支撑。`CompressionStream` 压缩写入的块；`DecompressionStream` 反向操作。

## 全局

| Global | 类型 | 说明 |
|--------|------|------|
| `CompressionStream` | class | 压缩写入块的变换流。 |
| `DecompressionStream` | class | 解压写入块的变换流。 |

两者都是标准 [Streams](/zh/js-api/streams)——暴露 `.readable` 与 `.writable`，
可被 pipe 进管道。

## new CompressionStream(format)

创建压缩变换。`format` 默认 `'gzip'`。

| 格式 | 线上格式 |
|--------|-------------|
| `'gzip'` | gzip 包装（10 字节头 + DEFLATE + CRC32 + ISIZE） |
| `'deflate'` | zlib 包装（2 字节头 + DEFLATE + Adler-32 尾） |
| `'deflate-raw'` | 裸 DEFLATE，无包装 |

```js
let cs = new CompressionStream('gzip');
let rs = new ReadableStream({ start(c) { c.enqueue(new TextEncoder().encode('hello world')); c.close(); } });
let chunks = [];

await rs
  .pipeThrough(cs)
  .pipeTo(new WritableStream({ write(c) { chunks.push(c); } }));

let gzipBytes = chunks[0]; // gzip 压缩数据的 Uint8Array
```

## new DecompressionStream(format)

创建解压变换。`format` 默认 `'gzip'`，必须与产生数据的包装匹配
（`'gzip'`、`'deflate'` 或 `'deflate-raw'`）。

```js
let ds = new DecompressionStream('gzip');
let rs = new ReadableStream({ start(c) { c.enqueue(gzipBytes); c.close(); } });
let out = [];

await rs
  .pipeThrough(ds)
  .pipeTo(new WritableStream({ write(c) { out.push(c); } }));

let text = new TextDecoder().decode(out[0]); // "hello world"
```

不支持的格式名抛 `Error: CompressionStream: unsupported format`。

## 说明

- 原生实现是 `miniz`；构建时需 `AM_WITH_COMPRESS=ON`（默认开）。
- `serve()` 的 HTTP handler 可用 `CompressionStream` 生成 `Content-Encoding: gzip`
  响应（见 [serve](/zh/js-api/serve) 与
  [httpserver 示例](https://github.com/adam-ikari/amoib/tree/master/examples/httpserver)）。

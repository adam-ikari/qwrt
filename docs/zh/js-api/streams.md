---
title: 流
description: Qwrt.js 中的 Streams API —— ReadableStream、WritableStream、TransformStream、管道传输与背压。
---

# ReadableStream / WritableStream

WHATWG Streams API，用于增量数据处理。`ReadableStream` 被 `fetch()` 内部用于流式 HTTP 响应。`WritableStream` 和 `TransformStream` 提供流管道原语。

## 全局对象

| 全局对象 | 类型 | 描述 |
|--------|------|-------------|
| `ReadableStream` | class | 数据块源 |
| `ReadableStreamDefaultReader` | class | 用于消费 ReadableStream 的读取器 |
| `WritableStream` | class | 数据块接收器 |
| `TransformStream` | class | 管道传输转换 |

## ReadableStream

### 创建 ReadableStream

```js
let stream = new ReadableStream({
    start(controller) {
        // 立即调用。入队初始数据或设置生产者。
        controller.enqueue(new Uint8Array([1, 2, 3]));
    },
    pull(controller) {
        // 当消费者需要更多数据时调用。
        // 在 qwrt 的 WinterTC 模块中尚未实现。
    },
    cancel(reason) {
        // 当消费者取消流时调用。
        console.log('流已取消:', reason);
    }
});
```

### 消费 ReadableStream

```js
let reader = stream.getReader();

while (true) {
    let { done, value } = await reader.read();
    if (done) break;
    console.log('数据块:', value.length, '字节');
}

reader.releaseLock();
```

### Controller API

```js
new ReadableStream({
    start(controller) {
        controller.enqueue(chunk);    // 入队一个数据块（Uint8Array 或 ArrayBuffer）
        controller.close();           // 发出流结束信号
        controller.error(new Error()); // 发出流错误信号
    }
});
```

### 流状态

```mermaid
flowchart TB
    A["已创建 → start() 被调用"] --> B{"controller 操作"}
    B -->|controller.enqueue()| C["可读（数据块已入队）"]
    B -->|controller.close()| D["已关闭（下次读取时 done: true）"]
    B -->|controller.error(e)| E["已出错（拒绝待处理的读取）"]
```

## 与 fetch() 配合使用

```js
let response = await fetch('https://example.com/large-data');
let reader = response.body.getReader();

let chunks = [];
while (true) {
    let { done, value } = await reader.read();
    if (done) break;
    chunks.push(value);
}

// 拼接数据块
let totalLength = chunks.reduce((sum, c) => sum + c.length, 0);
let combined = new Uint8Array(totalLength);
let offset = 0;
for (let chunk of chunks) {
    combined.set(chunk, offset);
    offset += chunk.length;
}
```

## WritableStream

### 创建 WritableStream

```js
let writable = new WritableStream({
    start(controller) {
        // 立即调用
    },
    write(chunk, controller) {
        // 处理每个数据块
        console.log('写入:', chunk.length, '字节');
        // 返回一个 promise 以施加背压
    },
    close() {
        // 所有数据块已写入
        console.log('流已关闭');
    },
    abort(reason) {
        // 流已中止
        console.log('已中止:', reason);
    }
});
```

### 写入 WritableStream

```js
let writer = writable.getWriter();

await writer.write(new Uint8Array([1, 2, 3]));
await writer.write(new Uint8Array([4, 5, 6]));
await writer.close();

// 或者中止
// await writer.abort('已取消');
```

## TransformStream

管道传输转换：

```js
let transform = new TransformStream({
    start(controller) {
        // 立即调用
    },
    transform(chunk, controller) {
        // 转换每个数据块
        let transformed = chunk.map(b => b * 2);
        controller.enqueue(transformed);
    },
    flush(controller) {
        // 所有数据块已处理
    }
});

// 管道传输：readable → transform → writable
readable.pipeThrough(transform).pipeTo(writable);
```

## 背压

`WritableStream` 通过 `write()` 返回 promise 来支持背压：

```js
let writable = new WritableStream({
    async write(chunk) {
        await slowProcess(chunk);  // 施加背压
    }
});

let writer = writable.getWriter();

// 这将在第一个数据块写入完成后才暂停
await writer.write(hugeChunk1);
await writer.write(hugeChunk2);
```

## 注意事项

- `ReadableStream` 已完全实现（被 `fetch` 使用）
- `WritableStream` 和 `TransformStream` 具有基本实现
- `pipeTo` 和 `pipeThrough` 可用，但错误恢复能力有限
- `ByteStreamController` 未实现（使用默认 controller）
- `ReadableByteStreamController` / `tee()` 尚不支持
- 为保持互操作性，数据块应为 `Uint8Array` 或 `ArrayBuffer`
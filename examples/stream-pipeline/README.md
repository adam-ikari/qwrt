# stream-pipeline — Web Streams 管道

用标准 Web Streams 把数据处理串成一条管道：

```
源 → 变换A → 变换B → 消费
ReadableStream → TransformStream → TransformStream → for await
```

## 运行

```bash
./build/qzjs examples/stream-pipeline/pipeline.js
```

## 期望输出

```
[pipe] 30
[pipe] 60
[pipe] 90
[pipe] 120
[pipe] 共 4 个值，总和 300
```

源产生 1..12，×10 后只放行 3 的倍数 → 30/60/90/120，共 4 个。

## 要点

- `ReadableStream` 的 `start(c)` 里 `c.enqueue(x)` 推数据、`c.close()` 收尾。
- `TransformStream` 的 `transform(chunk, c)` 逐块处理；`flush(c)` 收尾。
- `pipeThrough()` 串联节点并返回下游 readable，可继续链式接。
- `for await (const v of stream)` 用 async iterator 消费最终流。
- 这套 API 与浏览器/Node 一致，管道代码可移植。

## 相关文档

- [JS API: streams](/js-api/streams)
- [JS API: compress](/js-api/compress) — `CompressionStream` 可直接插进管道

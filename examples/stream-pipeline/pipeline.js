/* qwrt example: 流式管道（Web Streams）
 *
 * 演示用标准 Web Streams 把数据处理串成一条管道：
 *
 *   源 → 变换A → 变换B → 消费
 *   ReadableStream → TransformStream → TransformStream → for await
 *
 * 知识点：
 *   1. ReadableStream   — 生产者：start() 里 enqueue 数据、close() 收尾
 *   2. TransformStream  — 变换节点：transform() 逐块处理、flush() 收尾
 *   3. pipeThrough()    — 串联节点，返回下游 readable（可继续链式接管道）
 *   4. for await        — 用 async iterator 消费最终流
 *
 * 运行（仓库根）：
 *   ./build-ws/qwrt examples/stream-pipeline/pipeline.js
 *
 * 依赖的能力（qwrt 内置）：
 *   ReadableStream / TransformStream / pipeThrough / Symbol.asyncIterator
 */

/* 1) 源：产生 1..12 的整数流 */
var source = new ReadableStream({
  start: function (c) {
    for (var i = 1; i <= 12; i++) c.enqueue(i);
    c.close();
  },
});

/* 2) 变换 A：每个数 ×10 */
var timesTen = new TransformStream({
  transform: function (chunk, c) {
    c.enqueue(chunk * 10);
  },
});

/* 3) 变换 B：只放行 3 的倍数（过滤器） */
var multipleOfThree = new TransformStream({
  transform: function (chunk, c) {
    if (chunk % 3 === 0) c.enqueue(chunk);
  },
});

/* 4) 消费：把管道接起来，for await 读取并求和 */
var out = source.pipeThrough(timesTen).pipeThrough(multipleOfThree);

(async function () {
  var sum = 0;
  var count = 0;
  for await (var v of out) {
    console.log('[pipe] ' + v);
    sum += v;
    count++;
  }
  console.log('[pipe] 共 ' + count + ' 个值，总和 ' + sum);
})();

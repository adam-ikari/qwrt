---
title: performance
description: Qwrt.js 中的 performance API —— performance.now() 用于高精度时间戳和性能测量。
---

# performance

高精度时间测量 API。提供 `now()`、`timeOrigin` 以及 `mark()`/`measure()` 用于性能分析。

## 全局对象

| 全局对象 | 类型 | 描述 |
|--------|------|-------------|
| `performance` | `Performance` | 时间测量接口 |

## performance.now()

返回相对于运行时启动的高精度时间戳，单位为毫秒（微秒精度）。

```js
let start = performance.now();

// ... 执行工作 ...

let elapsed = performance.now() - start;
console.log(`操作耗时 ${elapsed.toFixed(3)}ms`);
```

返回值是 `DOMHighResTimeStamp`（双精度浮点数）。它相对于 `performance.timeOrigin`。

## performance.timeOrigin

qwrt 运行时创建时的 Unix 时间戳（毫秒）。

```js
let now = performance.timeOrigin + performance.now();
// 等价于 Date.now()
```

## performance.mark(name)

记录一个命名时间戳：

```js
performance.mark('start-work');

// ... 执行工作 ...

performance.mark('end-work');
```

## performance.measure(name, startMark?, endMark?)

测量两个标记之间的持续时间：

```js
performance.mark('a');
// ... 工作 ...
performance.mark('b');

let measure = performance.measure('work', 'a', 'b');
console.log('持续时间:', measure.duration, 'ms');

// 不带标记：测量自 timeOrigin 以来的时间
let fullTime = performance.measure('total');
```

### measure() 返回值

```js
{
    name: 'work',
    entryType: 'measure',
    startTime: 1234.567,
    duration: 456.789
}
```

## 性能测量模式

```js
function benchmark(fn, iterations = 1000) {
    let start = performance.now();

    for (let i = 0; i < iterations; i++) {
        fn();
    }

    let total = performance.now() - start;
    return {
        total: total,
        avg: total / iterations,
        opsPerSec: iterations / (total / 1000)
    };
}

let result = benchmark(() => {
    crypto.getRandomValues(new Uint8Array(32));
});
console.log(`${result.opsPerSec.toFixed(0)} 次/秒`);
```

## 注意事项

- 如果可用，使用 `pal.hrtime()` 获取高精度时间，否则回退到 `pal.timeNow()`
- 分辨率取决于 PAL：Linux 上为纳秒级（CLOCK_MONOTONIC），ESP32 上为毫秒级
- `performance.now()` 保证单调递增（永不倒退）
- 不支持 `PerformanceObserver` API
- 不支持 `performance.getEntries()`、`performance.getEntriesByName()`、`performance.clearMarks()`
- 不支持 `performance.memory` 或 `performance.timing`（浏览器专用）
---
title: AbortController
description: Qwrt.js 中的 AbortController API —— AbortController、AbortSignal、fetch 取消以及超时模式。
---

# AbortController / AbortSignal / DOMException

WHATWG AbortController API，用于取消异步操作。配合 `fetch()` 使用以中止正在进行的请求。

## 全局对象

| 全局对象 | 描述 |
|--------|-------------|
| `AbortController` | 创建中止信号 |
| `AbortSignal` | 传递给可中止操作的信号 |
| `DOMException` | 用于中止/超时/网络错误的错误类型 |

## AbortController

### 基本用法

```js
let controller = new AbortController();

// 将信号传递给 fetch
fetch('https://example.com', { signal: controller.signal });

// 稍后：中止请求
controller.abort();
```

### 带超时的用法

```js
let controller = new AbortController();

let timeoutId = setTimeout(() => {
    controller.abort();
}, 5000);  // 5 秒后中止

try {
    let response = await fetch('https://slow-server.com', {
        signal: controller.signal
    });
    clearTimeout(timeoutId);
} catch (err) {
    if (err.name === 'AbortError') {
        console.log('请求超时');
    }
}
```

### Controller 属性

```js
controller.signal;   // AbortSignal 实例
controller.aborted;  // false（abort() 之后为 true）
```

### abort(reason?)

```js
controller.abort();              // reason 默认为 DOMException('signal aborted')
controller.abort('timeout');     // 自定义原因
controller.abort(new Error('用户取消'));
```

多次调用 `abort()` 是安全的——只有第一次调用的原因会被使用。

## AbortSignal

### 属性

```js
signal.aborted;  // 布尔值 — 如果 controller 已被中止则为 true
signal.reason;   // 传递给 abort() 的原因，或 AbortError DOMException
```

### 事件：'abort'

```js
signal.addEventListener('abort', (event) => {
    console.log('已中止，原因:', signal.reason);
    // 清理资源
});

signal.removeEventListener('abort', handler);
```

### throwIfAborted()

```js
function doWork(signal) {
    signal.throwIfAborted();  // 如果已中止则抛出
    // ... 执行工作 ...
    signal.throwIfAborted();  // 再次检查
}
```

### AbortSignal.timeout(ms)

静态方法，创建一个在 `ms` 毫秒后自动中止的信号：

```js
let signal = AbortSignal.timeout(5000);

try {
    let response = await fetch('https://example.com', { signal });
} catch (err) {
    if (err.name === 'TimeoutError') {
        console.log('5 秒后超时');
    }
}
```

### AbortSignal.any(signals)

组合多个信号——当其中任何一个中止时触发：

```js
let timeoutSignal = AbortSignal.timeout(5000);
let userCancelSignal = new AbortController().signal;

let combinedSignal = AbortSignal.any([timeoutSignal, userCancelSignal]);

// 在超时或用户取消时中止
let response = await fetch('https://example.com', { signal: combinedSignal });
```

## DOMException

用于中止、超时和网络错误：

```js
try {
    await fetch('https://invalid', { signal: controller.signal });
} catch (err) {
    if (err instanceof DOMException) {
        switch (err.name) {
        case 'AbortError':
            console.log('请求已被中止');
            break;
        case 'TimeoutError':
            console.log('请求超时');
            break;
        case 'NetworkError':
            console.log('网络故障');
            break;
        }
    }
}
```

### 创建 DOMException

```js
let err = new DOMException('自定义消息', 'AbortError');
err.name;    // "AbortError"
err.message; // "自定义消息"
err.code;    // 20（AbortError）、23（TimeoutError）、19（NetworkError）
```

## 错误代码

| name | code | 用途 |
|------|------|-------|
| `AbortError` | 20 | 通过 AbortController 中止 fetch |
| `TimeoutError` | 23 | `AbortSignal.timeout()` 触发 |
| `NetworkError` | 19 | fetch 期间网络故障 |
| `InvalidStateError` | 11 | 当前状态下的无效操作 |
| `QuotaExceededError` | 22 | 超出资源限制 |
| `NotSupportedError` | 9 | 操作不可用 |

## 与 fetch() 集成

当中止信号在 fetch 期间触发时：
1. PAL 的 `http_abort()` 被调用（如果已实现）
2. 响应体流出错，错误为 `AbortError`
3. fetch promise 以 `AbortError` 拒绝

```js
let controller = new AbortController();
controller.signal.addEventListener('abort', () => {
    console.log('清理：关闭连接');
});

try {
    let response = await fetch('https://example.com', {
        signal: controller.signal
    });
    let data = await response.text();  // 如果在流中途中止也会拒绝
} catch (err) {
    console.log('已中止:', err.name);  // "AbortError"
}
```

## 注意事项

- 中止状态不可逆——一旦 `abort()` 被调用，信号将保持中止状态
- 多个 `abort` 事件监听器按注册顺序调用
- `signal.reason` 默认为 `new DOMException('signal is aborted', 'AbortError')`
- `AbortSignal.abort()` 静态方法创建一个预中止的信号（类似于 `AbortSignal.timeout(0)`）
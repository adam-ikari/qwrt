---
title: 定时器
description: Qwrt.js 中的定时器 API —— setTimeout、clearTimeout、setInterval、clearInterval 以及微任务调度。
---

# 定时器 API

标准的 `setTimeout` / `setInterval`，支持毫秒级精度。由 PAL 的 `timer_start` / `timer_stop` 支持。

## 全局对象

| 全局对象 | 描述 |
|--------|-------------|
| `setTimeout(callback, delay, ...args)` | 在 `delay` 毫秒后运行 `callback` |
| `clearTimeout(id)` | 取消一个 timeout |
| `setInterval(callback, delay, ...args)` | 每隔 `delay` 毫秒运行 `callback` |
| `clearInterval(id)` | 取消一个 interval |

## setTimeout

```js
// 1 秒后运行
let id = setTimeout(() => {
    console.log('已过 1 秒');
}, 1000);

// 带参数
setTimeout((name, age) => {
    console.log(name, age);
}, 500, 'Alice', 30);

// 在触发前取消
clearTimeout(id);
```

### 最小延迟

嵌套 timeout 的延迟被限制为最少 **4ms**（浏览器约定）。小于 0 的值被视为 0（尽快执行）。

### 返回值

`setTimeout` 和 `setInterval` 返回数字句柄（而非对象）。这些句柄是每个上下文独立的，在清除后会被重用。

## setInterval

```js
// 每 500ms 运行一次
let counter = 0;
let id = setInterval(() => {
    counter++;
    console.log('滴答:', counter);
    if (counter >= 10) clearInterval(id);
}, 500);
```

## clearTimeout / clearInterval

两个函数可以互换使用——`clearTimeout` 可以取消 interval，反之亦然。

```js
let id = setInterval(() => console.log('滴答'), 1000);
clearTimeout(id);  // 同样有效
```

传入无效句柄（已清除、垃圾值）会被静默忽略。

## 定时器生命周期

```
setTimeout(cb, 1000)
    │
    ▼
pal.timerStart(1000, false) → 句柄 + Promise
    │
    ▼  （1000ms 后）
    │
pal 回调触发 → JS Promise 解析 → cb() 被调用
```

对于 `setInterval`，PAL 的 `repeat=true` 参数创建一个重复定时器。每次触发解析 JS promise，后者重新调度回调。

## 上下文生命周期

- 所有定时器是**每个上下文独立的**（一个 `qwrt_ctx_t` 拥有自己的定时器表）
- 当上下文被**挂起**或**销毁**时，定时器会自动清除
- 在 `qwrt_reset()` 之后，上一个会话中的任何定时器都不会保留

## 最大定时器数量

每个上下文在所有句柄类型（定时器 + 文件系统 + HTTP）中共支持最多 `QWRT_MAX_HANDLES`（256）个句柄。在表满时创建定时器返回 `0`（无效句柄），且回调永远不会被调用。

## 注意事项

- 定时器精度取决于 PAL——`pal_mock` 同步触发，`pal_uv` 使用 libuv 定时器（~1ms 精度），`pal_freertos` 使用 FreeRTOS 软件定时器（基于 tick）
- 不需要 `queueMicrotask` 包装器——它作为 `globalThis.queueMicrotask` 可用
- 嵌套深度超过 5 层的 `setTimeout` 调用被限制为最少 4ms 延迟
- `setTimeout(cb, 0)` 在下一个事件循环 tick 时执行，而非立即执行
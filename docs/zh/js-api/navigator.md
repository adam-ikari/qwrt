---
title: navigator
description: Qwrt.js 中的 navigator API —— 平台与运行时信息、用户代理以及硬件并发数。
---

# navigator

WinterTC `navigator` 全局对象，提供运行时标识和硬件并发数。相比浏览器的 `navigator` 非常精简——没有 DOM，没有用户代理嗅探。

## 全局对象

| 全局对象 | 类型 | 描述 |
|--------|------|-------------|
| `navigator` | `Navigator` | 运行时信息 |

## 属性

### `navigator.hardwareConcurrency`

可用的逻辑 CPU 核心数。

```js
console.log('CPU 核心数:', navigator.hardwareConcurrency);
// 在 Linux/macOS 上通常为 4、8、16 等
// 在 ESP32 上返回 1
```

用于确定工作线程池的大小：

```js
let parallelism = Math.min(8, navigator.hardwareConcurrency);
let results = await Promise.all(
    Array(parallelism).fill(0).map((_, i) => processBatch(i))
);
```

### `navigator.userAgent`

运行时标识字符串。

```js
console.log(navigator.userAgent);
// "qwrt/1.0" 或类似内容
```

这是有意精简的——不是类似浏览器的 user agent 字符串。确切的格式可能在不同 qwrt 版本之间变化。

## 方法

### `navigator.reportError(error)`

向 `globalThis` 分发一个 `ErrorEvent`。用于手动报告错误。

```js
try {
    throw new Error('出了点问题');
} catch (err) {
    navigator.reportError(err);
}
```

这会触发 `globalThis` 上的任何 `error` 事件监听器。

## 不包含的内容

以下浏览器特定的 `navigator` 属性**不可用**：

- `navigator.language` / `navigator.languages`
- `navigator.onLine`
- `navigator.cookieEnabled`
- `navigator.platform`
- `navigator.geolocation`
- `navigator.clipboard`
- `navigator.mediaDevices`
- `navigator.serviceWorker`
- `navigator.storage`

## 注意事项

- `hardwareConcurrency` 在运行时从操作系统读取（不是编译时常量）
- 在 ESP32 上，`hardwareConcurrency` 返回 `1`（单核 FreeRTOS）或 `2`（双核）
- 不支持 `navigator.sendBeacon()` —— 请手动使用 `fetch()` 实现 `keepalive` 语义
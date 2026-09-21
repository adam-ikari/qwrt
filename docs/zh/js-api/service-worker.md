---
title: Service Worker
description: Qwrt.js 的 Service Worker 子集 —— 注册、带 Cache API 的请求拦截、更新机制。暴露为 navigator.serviceWorker。
---

# Service Worker

qwrt 实现的 Service Worker 子集：worker 跑在自己的线程上，拦截运行时的
`fetch`，从 `CacheStorage` 返回缓存响应，超时则回退到网络（有上限）。

## 注册

```js
await navigator.serviceWorker.register('/sw.js');
```

注册将 `/sw.js` 作为真实 `Worker`（独立线程）加载，引导 SW 生命周期，
并把 `__qwrt_sw_intercept__` 装为 fetch 钩子。

## 生命周期状态

`parsed` → `installing` → `installed` → `activating` → `activated`

- **安装**：SW 运行 `install` handler；完成后即可激活。
- **激活**：安装完成后 SW 成为 controller；`skipWaiting` 缩短等待
  （qwrt 立即安装并激活——单运行时嵌入无需旧 controller 交接）。
- **请求拦截**：激活后运行时每次 `fetch()` 都经 `__qwrt_sw_intercept__`；
  SW 的 `fetch` handler 决定从 `caches` 提供、命中网络还是合成响应。

## Fetch handler

```js
self.addEventListener('fetch', (event) => {
  event.respondWith(
    caches.match(event.request).then((cached) =>
      cached || fetch(event.request)
    )
  );
});
```

若 handler 未在 30 秒内响应，回退到网络，防止运行时因异常 SW 挂起。

## 更新机制

`navigator.serviceWorker.update()` 重新读取 SW 脚本并逐字节比对；若未变则
完全跳过安装。新 SW 仅在激活完成后接管为 controller（install/activate
期间旧 SW 保持拦截——无控制真空）。

## 说明

- 全局注册：每个运行时恰好一个 SW；`scope` 接受但忽略（qwrt 将运行时视为
  单一 origin）。
- 离线优先模式中，SW 与 [CacheStorage](/zh/js-api/cache-storage) 成对工作：
  SW 路由到 caches，caches 存储响应。

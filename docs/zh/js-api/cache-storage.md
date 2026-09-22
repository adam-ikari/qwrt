---
title: CacheStorage
description: qzjs 的 CacheStorage API —— 支撑 Service Worker 请求拦截层的全局 `caches` / `CacheStorage` / `Cache`。
---

# CacheStorage

Cache API（`caches`、`CacheStorage`、`Cache`）是 Service Worker 请求拦截的
存储层。它以 URL 为键存 `Response` 对象，支持跨缓存 `match`；SW 靠它离线
返回缓存响应。

## 全局

| Global | 类型 |
|--------|------|
| `caches` | `CacheStorage` 实例 |
| `CacheStorage` | `class` |
| `Cache` | `class` |

## CacheStorage

```js
const cache = await caches.open('v1');
await cache.add('/api/data');             // fetch + put
await cache.addAll(['/a', '/b', '/c']);
const res = await cache.match('/api/data');
const any = await caches.match('/api/data');  // 跨所有缓存搜索
```

| 方法 | 说明 |
|--------|------|
| `caches.open(name)` | 打开（或创建）命名缓存 |
| `caches.delete(name)` | 删除命名缓存 |
| `caches.match(req)` | 在全部缓存中搜索 `req`，返回首个命中 |
| `caches.has(name)` | 检查命名缓存是否存在 |
| `caches.keys()` | 列出所有命名缓存 |

## Cache

| 方法 | 说明 |
|--------|------|
| `match(req)` | 按请求查找已存 `Response` |
| `matchAll(req?)` | 返回所有匹配的已存响应 |
| `add(req)` | fetch 并存储（单个） |
| `addAll(reqs[])` | fetch 并存储多个 |
| `put(req, res)` | 存储显式 (Request, Response) 对 |
| `delete(req)` | 删除已存条目 |
| `keys()` | 列出所有已存请求 |

## 说明

- 存入缓存的 `Response` 会被克隆（原对象仍可用）。`Response.clone()` 背后
  的 tee 语义见 [streams](/zh/js-api/streams)。
- CacheStorage 支撑 Service Worker 的离线层；通用键值存储用
  [`qzjs.storage`](/zh/js-api/storage)。

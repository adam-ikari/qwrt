---
title: CacheStorage
description: The CacheStorage API in Qwrt.js — the global `caches` / `CacheStorage` / `Cache` objects backing the Service Worker fetch interception layer.
---

# CacheStorage

The Cache API (`caches`, `CacheStorage`, `Cache`) is the storage layer for
the Service Worker fetch-interception path. It stores `Response` objects
keyed by `Request` (URL), supports `match` across all caches, and is used
by the SW implementation to serve cached responses offline.

## Globals

| Global | Type |
|--------|------|
| `caches` | `CacheStorage` instance |
| `CacheStorage` | `class` |
| `Cache` | `class` |

## CacheStorage

```js
const cache = await caches.open('v1');
await cache.add('/api/data');             // fetch + put
await cache.addAll(['/a', '/b', '/c']);
const res = await cache.match('/api/data');
const any = await caches.match('/api/data');  // search across all caches
```

| Method | Description |
|--------|-------------|
| `caches.open(name)` | Open (or create) a named cache |
| `caches.delete(name)` | Delete a named cache |
| `caches.match(req)` | Search all caches for `req`, return first hit |
| `caches.has(name)` | Check existence of a named cache |
| `caches.keys()` | List all named caches |

## Cache

| Method | Description |
|--------|-------------|
| `match(req)` | Look up a stored `Response` by request |
| `matchAll(req?)` | Return all matching stored responses |
| `add(req)` | Fetch and store (single request) |
| `addAll(reqs[])` | Fetch and store multiple |
| `put(req, res)` | Store an explicit (Request, Response) pair |
| `delete(req)` | Remove a stored entry |
| `keys()` | List all stored requests |

## Notes

- `Response` objects stored in a cache are cloned (the original remains
  usable). See [streams](/js-api/streams) for the tee semantics behind
  `Response.clone()`.
- CacheStorage backs the Service Worker's offline layer; for general
  key-value storage use [`qwrt.storage`](/js-api/storage).

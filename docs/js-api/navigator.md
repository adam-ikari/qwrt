---
title: navigator
description: The navigator API in Qwrt.js — platform and runtime information, user agent, and hardware concurrency.
---

# navigator

WinterTC `navigator` global providing runtime identification and hardware concurrency. Minimal compared to browser `navigator` — no DOM, no user agent sniffing.

## Global

| Global | Type | Description |
|--------|------|-------------|
| `navigator` | `Navigator` | Runtime information |

## Properties

### `navigator.hardwareConcurrency`

Number of logical CPU cores available.

```js
console.log('CPU cores:', navigator.hardwareConcurrency);
// Typically 4, 8, 16, etc. on Linux/macOS
// Returns 1 on ESP32
```

Use this for sizing worker pools:

```js
let parallelism = Math.min(8, navigator.hardwareConcurrency);
let results = await Promise.all(
    Array(parallelism).fill(0).map((_, i) => processBatch(i))
);
```

### `navigator.userAgent`

Runtime identifier string.

```js
console.log(navigator.userAgent);
// "qwrt/1.0" or similar
```

This is intentionally minimal — not a browser-like user agent string. The exact format may change between qwrt versions.

## Methods

### `navigator.reportError(error)`

Dispatch an `ErrorEvent` to `globalThis`. Useful for manual error reporting.

```js
try {
    throw new Error('Something went wrong');
} catch (err) {
    navigator.reportError(err);
}
```

This triggers any `error` event listeners on `globalThis`.

## Not Included

The following browser-specific `navigator` properties are **not available**:

- `navigator.language` / `navigator.languages`
- `navigator.onLine`
- `navigator.cookieEnabled`
- `navigator.platform`
- `navigator.geolocation`
- `navigator.clipboard`
- `navigator.mediaDevices`
- `navigator.serviceWorker`
- `navigator.storage`

## Notes

- `hardwareConcurrency` is read from the OS at runtime (not a compile-time constant)
- On ESP32, `hardwareConcurrency` returns `1` (single-core FreeRTOS) or `2` (dual-core)
- There is no `navigator.sendBeacon()` — use `fetch()` with `keepalive` semantics manually

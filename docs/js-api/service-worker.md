---
title: Service Worker
description: The Service Worker subset in Qzjs.js — registration, fetch interception with Cache API, and update mechanism. Exposed as navigator.serviceWorker.
---

# Service Worker

A qzjs subset of the Service Worker platform: a worker that runs in its own
thread, intercepts `fetch` requests from the runtime, serves cached
responses from `CacheStorage`, and falls back to the network with a bounded
timeout.

## Registration

```js
await navigator.serviceWorker.register('/sw.js');
```

Registration loads `/sw.js` as a real `Worker` (its own thread), bootstraps
the SW lifecycle, and installs `__qz_sw_intercept__` as the fetch hook.

## Lifecycle states

`parsed` → `installing` → `installed` → `activating` → `activated`

- **Install**: the SW runs its `install` handler; if it completes, the SW
  is ready to activate.
- **Activate**: after install, the SW becomes the controller; `skipWaiting`
  shortens the wait (qzjs installs and activates immediately — there is no
  old-controller handover needed in a single-runtime embed).
- **Fetch interception**: once activated, every `fetch()` in the runtime
  goes through `__qz_sw_intercept__`; the SW's `fetch` handler decides
  whether to serve from `caches`, hit the network, or synthesize a
  response.

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

A 30-second timeout falls back to the network if the handler does not
respond, preventing the runtime from hanging on a misbehaving SW.

## Update mechanism

`navigator.serviceWorker.update()` reads the SW script fresh and compares
byte-for-byte; if unchanged, it skips installation entirely. The new SW
takes over as controller only after activation completes (the old SW
keeps intercepting during install/activate — no control vacuum).

## Notes

- Global registration: there is exactly one SW per runtime; `scope` is
  accepted but ignored (qzjs treats the runtime as one origin).
- For offline-first patterns, the SW and [CacheStorage](/js-api/cache-storage)
  work as a pair: the SW routes to caches, caches store the responses.

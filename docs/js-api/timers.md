---
title: timers
description: Timer APIs in Qwrt.js — setTimeout, clearTimeout, setInterval, clearInterval, and microtask scheduling.
---

# Timers API

Standard `setTimeout` / `setInterval` with millisecond resolution. Backed by PAL `timer_start` / `timer_stop`.

## Globals

| Global | Description |
|--------|-------------|
| `setTimeout(callback, delay, ...args)` | Run `callback` after `delay` ms |
| `clearTimeout(id)` | Cancel a timeout |
| `setInterval(callback, delay, ...args)` | Run `callback` every `delay` ms |
| `clearInterval(id)` | Cancel an interval |

## setTimeout

```js
// Run after 1 second
let id = setTimeout(() => {
    console.log('1 second passed');
}, 1000);

// With arguments
setTimeout((name, age) => {
    console.log(name, age);
}, 500, 'Alice', 30);

// Cancel before it fires
clearTimeout(id);
```

### Minimum Delay

Delays are clamped to a minimum of **4ms** for nested timeouts (browser convention). Values less than 0 are treated as 0 (execute ASAP).

### Return Value

`setTimeout` and `setInterval` return numeric handles (not objects). These handles are per-context and reused after clearing.

## setInterval

```js
// Run every 500ms
let counter = 0;
let id = setInterval(() => {
    counter++;
    console.log('Tick:', counter);
    if (counter >= 10) clearInterval(id);
}, 500);
```

## clearTimeout / clearInterval

Both functions are interchangeable — `clearTimeout` can cancel an interval and vice versa.

```js
let id = setInterval(() => console.log('tick'), 1000);
clearTimeout(id);  // also works
```

Passing an invalid handle (already cleared, garbage value) is silently ignored.

## Timer Lifecycle

```
setTimeout(cb, 1000)
    │
    ▼
pal.timerStart(1000, false) → handle + Promise
    │
    ▼  (1000ms passes)
    │
pal callback fires → JS Promise resolves → cb() called
```

For `setInterval`, the PAL's `repeat=true` parameter creates a recurring timer. Each fire resolves the JS promise, which re-schedules the callback.

## Context Lifecycle

- All timers are **per-context** (one `qwrt_ctx_t` owns its timer table)
- Timers are automatically cleared when a context is **suspended** or **destroyed**
- After `qwrt_reset()`, no timers from the previous session survive

## Max Timers

Each context supports up to `QWRT_MAX_HANDLES` (256) total handles across all handle types (timers + filesystem + HTTP). Creating a timer when the table is full returns `0` (invalid handle), and the callback is never called.

## Notes

- Timer precision depends on the PAL — `pal_mock` fires synchronously, `pal_uv` uses libuv timers (~1ms precision), `pal_freertos` uses FreeRTOS software timers (tick-based)
- There is no `queueMicrotask` wrapper needed — it's available as `globalThis.queueMicrotask`
- Nested `setTimeout` calls deeper than 5 levels are clamped to 4ms minimum delay
- `setTimeout(cb, 0)` executes on the next event loop tick, not immediately

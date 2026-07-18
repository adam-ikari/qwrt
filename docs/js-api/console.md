---
title: console
description: The console API in Qwrt.js — console.log, console.error, console.warn, and structured logging.
---

# console API

The WHATWG Console standard for logging and debugging.

## Global

| Global | Type | Description |
|--------|------|-------------|
| `console` | object | Logging and debugging interface |

## Methods

### `console.log(...args)`

Log informational messages. Arguments are space-separated in output.

```js
console.log('Hello, world!');
console.log('Value:', 42, 'Status:', true);
console.log({ key: 'value' });  // objects serialized
```

### `console.info(...args)`

Alias for `console.log`.

```js
console.info('Server started on port 8080');
```

### `console.warn(...args)`

Log a warning message. Prefixed with `[WARN]` in output.

```js
console.warn('Deprecated API called');
console.warn('Memory usage:', process.memoryUsage());
```

### `console.error(...args)`

Log an error message. Prefixed with `[ERROR]` in output.

```js
console.error('Failed to connect to database');
console.error('Error details:', err.message, err.stack);
```

### `console.debug(...args)`

Log a debug message. Prefixed with `[DEBUG]` in output.

```js
console.debug('Request headers:', JSON.stringify(headers));
```

## Log Levels

Each method maps to a PAL log level:

| Method | PAL Level | Value | Typical Behavior |
|--------|-----------|-------|-----------------|
| `console.debug()` | DEBUG | 0 | Hidden in production |
| `console.log()` / `.info()` | INFO | 1 | Default output |
| `console.warn()` | WARN | 2 | Highlighted output |
| `console.error()` | ERROR | 3 | Error stream |

## Implementation

`console.*` functions call `pal.log(level, message)` under the hood:

```c
// In PAL implementation
static void mypal_log(qwrt_pal_t *pal, int level, const char *msg) {
    switch (level) {
    case 0: /* debug — suppress in production */ break;
    case 1: printf("[INFO] %s\n", msg); break;
    case 2: fprintf(stderr, "[WARN] %s\n", msg); break;
    case 3: fprintf(stderr, "[ERROR] %s\n", msg); break;
    }
}
```

If a PAL sets `log = NULL`, console output is silently discarded.

## Formatting

Arguments are converted to strings via `String()`. Objects serialized with `JSON.stringify` may truncate circular references. Complex objects are best logged one property at a time.

## Notes

- **No** `console.table()`, `console.group()`, `console.time()`, `console.trace()`
- **No** `console.assert()` — use an `if` guard with `console.error`
- Stack traces from `console.error` are not automatically included — pass `err.stack` explicitly
- The log level is determined by the PAL, not configurable from JS

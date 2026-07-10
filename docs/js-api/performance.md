# performance

High-resolution time measurement API. Provides `now()`, `timeOrigin`, and `mark()`/`measure()` for performance profiling.

## Global

| Global | Type | Description |
|--------|------|-------------|
| `performance` | `Performance` | Time measurement interface |

## performance.now()

Returns a high-resolution timestamp in milliseconds (with microsecond precision) relative to the runtime start.

```js
let start = performance.now();

// ... do work ...

let elapsed = performance.now() - start;
console.log(`Operation took ${elapsed.toFixed(3)}ms`);
```

The returned value is a `DOMHighResTimeStamp` (double-precision float). It's relative to `performance.timeOrigin`.

## performance.timeOrigin

The Unix timestamp (ms) when the qwrt runtime was created.

```js
let now = performance.timeOrigin + performance.now();
// Equivalent to Date.now()
```

## performance.mark(name)

Record a named timestamp:

```js
performance.mark('start-work');

// ... do work ...

performance.mark('end-work');
```

## performance.measure(name, startMark?, endMark?)

Measure the duration between two marks:

```js
performance.mark('a');
// ... work ...
performance.mark('b');

let measure = performance.measure('work', 'a', 'b');
console.log('Duration:', measure.duration, 'ms');

// Without marks: measures since timeOrigin
let fullTime = performance.measure('total');
```

### measure() Return

```js
{
    name: 'work',
    entryType: 'measure',
    startTime: 1234.567,
    duration: 456.789
}
```

## Performance Measurement Pattern

```js
function benchmark(fn, iterations = 1000) {
    let start = performance.now();

    for (let i = 0; i < iterations; i++) {
        fn();
    }

    let total = performance.now() - start;
    return {
        total: total,
        avg: total / iterations,
        opsPerSec: iterations / (total / 1000)
    };
}

let result = benchmark(() => {
    crypto.getRandomValues(new Uint8Array(32));
});
console.log(`${result.opsPerSec.toFixed(0)} ops/sec`);
```

## Notes

- Uses `pal.hrtime()` for high-resolution time if available, falls back to `pal.timeNow()`
- Resolution depends on the PAL: nanoseconds on Linux (CLOCK_MONOTONIC), milliseconds on ESP32
- `performance.now()` is guaranteed to be monotonically increasing (never goes backwards)
- No `PerformanceObserver` API
- No `performance.getEntries()`, `performance.getEntriesByName()`, `performance.clearMarks()`
- No `performance.memory` or `performance.timing` (browser-only)

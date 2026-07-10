# AbortController / AbortSignal / DOMException

The WHATWG AbortController API for cancelling async operations. Used with `fetch()` to abort in-flight requests.

## Globals

| Global | Description |
|--------|-------------|
| `AbortController` | Creates abort signals |
| `AbortSignal` | Signal passed to abortable operations |
| `DOMException` | Error type for abort/timeout/network errors |

## AbortController

### Basic Usage

```js
let controller = new AbortController();

// Pass the signal to fetch
fetch('https://example.com', { signal: controller.signal });

// Later: abort the request
controller.abort();
```

### With Timeout

```js
let controller = new AbortController();

let timeoutId = setTimeout(() => {
    controller.abort();
}, 5000);  // abort after 5 seconds

try {
    let response = await fetch('https://slow-server.com', {
        signal: controller.signal
    });
    clearTimeout(timeoutId);
} catch (err) {
    if (err.name === 'AbortError') {
        console.log('Request timed out');
    }
}
```

### Controller Properties

```js
controller.signal;   // AbortSignal instance
controller.aborted;  // false (true after abort())
```

### abort(reason?)

```js
controller.abort();              // reason defaults to DOMException('signal aborted')
controller.abort('timeout');     // custom reason
controller.abort(new Error('Cancelled by user'));
```

Calling `abort()` multiple times is safe — only the first call's reason is used.

## AbortSignal

### Properties

```js
signal.aborted;  // boolean — true if controller has been aborted
signal.reason;   // the reason passed to abort(), or AbortError DOMException
```

### Event: 'abort'

```js
signal.addEventListener('abort', (event) => {
    console.log('Aborted with reason:', signal.reason);
    // Clean up resources
});

signal.removeEventListener('abort', handler);
```

### throwIfAborted()

```js
function doWork(signal) {
    signal.throwIfAborted();  // throws if already aborted
    // ... do work ...
    signal.throwIfAborted();  // check again
}
```

### AbortSignal.timeout(ms)

Static method that creates a signal which auto-aborts after `ms` milliseconds:

```js
let signal = AbortSignal.timeout(5000);

try {
    let response = await fetch('https://example.com', { signal });
} catch (err) {
    if (err.name === 'TimeoutError') {
        console.log('Timed out after 5 seconds');
    }
}
```

### AbortSignal.any(signals)

Combine multiple signals — aborts when any of them aborts:

```js
let timeoutSignal = AbortSignal.timeout(5000);
let userCancelSignal = new AbortController().signal;

let combinedSignal = AbortSignal.any([timeoutSignal, userCancelSignal]);

// Aborts on timeout OR user cancellation
let response = await fetch('https://example.com', { signal: combinedSignal });
```

## DOMException

Used for abort, timeout, and network errors:

```js
try {
    await fetch('https://invalid', { signal: controller.signal });
} catch (err) {
    if (err instanceof DOMException) {
        switch (err.name) {
        case 'AbortError':
            console.log('Request was aborted');
            break;
        case 'TimeoutError':
            console.log('Request timed out');
            break;
        case 'NetworkError':
            console.log('Network failure');
            break;
        }
    }
}
```

### Creating DOMException

```js
let err = new DOMException('Custom message', 'AbortError');
err.name;    // "AbortError"
err.message; // "Custom message"
err.code;    // 20 (AbortError), 23 (TimeoutError), 19 (NetworkError)
```

## Error Codes

| name | code | Usage |
|------|------|-------|
| `AbortError` | 20 | fetch aborted via AbortController |
| `TimeoutError` | 23 | `AbortSignal.timeout()` fired |
| `NetworkError` | 19 | Network failure during fetch |
| `InvalidStateError` | 11 | Invalid operation in current state |
| `QuotaExceededError` | 22 | Resource limit exceeded |
| `NotSupportedError` | 9 | Operation not available |

## Integration with fetch()

When an abort signal fires during a fetch:
1. The PAL's `http_abort()` is called (if implemented)
2. The response body stream is errored with `AbortError`
3. The fetch promise rejects with `AbortError`

```js
let controller = new AbortController();
controller.signal.addEventListener('abort', () => {
    console.log('Cleanup: closing connections');
});

try {
    let response = await fetch('https://example.com', {
        signal: controller.signal
    });
    let data = await response.text();  // also rejects if aborted mid-stream
} catch (err) {
    console.log('Aborted:', err.name);  // "AbortError"
}
```

## Notes

- Aborted state is irreversible — once `abort()` is called, the signal stays aborted
- Multiple `abort` event listeners are called in registration order
- `signal.reason` defaults to `new DOMException('signal is aborted', 'AbortError')`
- `AbortSignal.abort()` static method creates a pre-aborted signal (like `AbortSignal.timeout(0)`)

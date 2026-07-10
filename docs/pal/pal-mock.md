# pal_mock — Mock Backend

The mock PAL is a pure-C, dependency-free implementation used for testing and as a reference for PAL authors. All operations are synchronous and in-memory.

## Overview

```c
#include <qwrt/qwrt.h>
#include <pal_mock.h>

int main(void) {
    qwrt_pal_t *pal = pal_mock_create();

    qwrt_t *rt = qwrt_create(&(qwrt_config_t){ .pal = pal });

    // ... use runtime ...

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
    return 0;
}
```

## Capabilities

| Feature | Support | Notes |
|---------|---------|-------|
| HTTP | ✅ | In-memory URL routing, no network |
| HTTPS | ❌ | Not needed for testing |
| Streaming HTTP | ❌ | — |
| Abort | ❌ | — |
| Filesystem | ✅ | In-memory virtual filesystem |
| Storage | ✅ | In-memory key-value store |
| Timers | ✅ | Simulated, fire synchronously |
| Child processes | ❌ | — |
| Event loop | ❌ | run_cycle is NULL |

## HTTP Mocking

The mock PAL supports pre-registered HTTP routes:

```c
pal_mock_t *mp = pal_mock_create();

// Register a route
pal_mock_register_route(mp, "/api/data", "GET", 200,
    "{\"Content-Type\":\"application/json\"}",
    "{\"ok\":true}", 9);

// Register with custom response
pal_mock_register_route(mp, "/api/error", "POST", 500,
    "{}", "Internal Error", 14);
```

Routes match on both URL path and HTTP method. Unmatched requests return 404.

## Filesystem

The mock PAL provides an in-memory virtual filesystem:

```c
pal_mock_t *mp = pal_mock_create();

// Set up filesystem content before creating the runtime
pal_mock_set_fs(mp, "/app/main.js", "console.log('hello');");
pal_mock_set_fs(mp, "/data/config.json", "{\"version\":1}");

// fs_write stores in memory, fs_read retrieves it
// fs_exists checks if a path exists
// fs_list returns directory entries
```

## Storage

In-memory key-value storage that persists for the PAL's lifetime:

```c
pal_mock_set_storage(mp, "token", "abc123");
// storage_get("token") returns "abc123"
// storage_set / storage_remove update the map
```

## Timers

Timers fire synchronously when `pal_mock_advance_time` is called:

```c
// Start a 1000ms timer
pal->timer_start(pal, 1000, 0, on_timeout, &data);

// Advance time by 1000ms — fires the callback
pal_mock_advance_time(mp, 1000);
```

This makes timer testing deterministic and fast.

## Use in Tests

The mock PAL is used throughout qwrt's test suite:

```c
void test_my_feature(void) {
    pal_mock_t *mp = pal_mock_create();
    qwrt_pal_t *pal = pal_mock_get_pal(mp);

    // Pre-seed files
    pal_mock_set_fs(mp, "/test.js", "export default 42;");

    qwrt_t *rt = qwrt_create(&(qwrt_config_t){ .pal = pal });

    // Run test logic...

    qwrt_destroy(rt);
    pal_mock_destroy(mp);
}
```

## Reference Implementation

The mock PAL is the **best starting point for writing your own PAL**. It's under 500 lines of pure C with no external dependencies. Study the source at `platform/mock/pal_mock.c`.

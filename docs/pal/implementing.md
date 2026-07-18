---
title: Implementing a PAL
description: Step-by-step guide to implementing a custom Qwrt.js PAL backend — required functions, optional hooks, async patterns, and testing.
---

# Implementing a PAL

Step-by-step guide to implementing your own Platform Abstraction Layer backend.

## Overview

A PAL implementation is a C file that fills in all required `qwrt_pal_t` function pointers and provides a constructor/destructor. This guide walks through creating a minimal PAL, then adding async I/O, streaming, and lifecycle hooks.

## Step 1: Skeleton

Create `platform/mypal/pal_mypal.c` and `pal_mypal.h`:

```c
// pal_mypal.h
#ifndef QWRT_PAL_MYPAL_H
#define QWRT_PAL_MYPAL_H

#include "qwrt/qwrt.h"

typedef struct pal_mypal_t pal_mypal_t;

pal_mypal_t *pal_mypal_create(void);
void pal_mypal_destroy(pal_mypal_t *mp);
qwrt_pal_t *pal_mypal_get_pal(pal_mypal_t *mp);

#endif /* QWRT_PAL_MYPAL_H */
```

```c
// pal_mypal.c
#include "pal_mypal.h"
#include <stdlib.h>
#include <time.h>

struct pal_mypal_t {
    qwrt_pal_t pal;  // MUST be first or near-first member
    // Your private state here
};

// Forward declarations
static uint64_t mypal_time_now(qwrt_pal_t *pal);
static uint64_t mypal_hrtime(qwrt_pal_t *pal);
// ... all other required methods

pal_mypal_t *pal_mypal_create(void) {
    pal_mypal_t *mp = calloc(1, sizeof(*mp));
    if (!mp) return NULL;

    qwrt_pal_t *pal = &mp->pal;

    // Identity
    pal->user_data = mp;
    pal->version   = 1;
    pal->name      = "mypal";

    // Required methods (fill these in)
    pal->http_request = mypal_http_request;
    pal->fs_read      = mypal_fs_read;
    // ... all required methods

    // Optional methods (set to NULL if not implemented)
    pal->http_request_stream = NULL;
    pal->http_abort          = NULL;
    pal->run_cycle           = NULL;
    pal->spawn               = NULL;
    // ... all optional methods

    // Lifecycle (NULL = no init/destroy needed)
    pal->init    = NULL;
    pal->destroy = NULL;

    // Reserved (must be NULL)
    memset(pal->reserved, 0, sizeof(pal->reserved));

    return mp;
}

void pal_mypal_destroy(pal_mypal_t *mp) {
    if (!mp) return;
    free(mp);
}

qwrt_pal_t *pal_mypal_get_pal(pal_mypal_t *mp) {
    return &mp->pal;
}
```

## Step 2: Required Methods

Every PAL must implement these synchronous methods at minimum:

| Method | Implementation |
|--------|---------------|
| `time_now` | `return (uint64_t)time(NULL) * 1000;` |
| `hrtime` | Platform-specific monotonic clock |
| `log` | `fprintf(stderr, ...)` or NULL |
| `random_bytes` | `/dev/urandom` or hardware RNG |

And at least stub implementations of the async methods (call callback with `QWRT_ERR_NOT_SUPPORTED` if the operation isn't available):

| Method | Minimal Stub |
|--------|-------------|
| `http_request` | `cb(cb_data, QWRT_ERR_NOT_SUPPORTED, NULL, 0);` |
| `fs_read` | `cb(cb_data, QWRT_ERR_NOT_SUPPORTED, NULL, 0);` |
| `fs_write` | `cb(cb_data, QWRT_ERR_NOT_SUPPORTED, NULL, 0);` |
| `storage_get` | `cb(cb_data, QWRT_ERR_NOT_SUPPORTED, NULL, 0);` |
| `timer_start` | Return NULL |
| ... | ... |

## Step 3: Async HTTP

See [Async Operations](/pal/async) for the full pattern.

## Step 4: Lifecycle Hooks

Use `init` and `destroy` to manage resources:

```c
static int mypal_init(qwrt_pal_t *pal) {
    pal_mypal_t *mp = (pal_mypal_t *)pal->user_data;

    // Initialize networking stack, open file handles, etc.
    mp->socket_pool = create_socket_pool(16);
    if (!mp->socket_pool) return QWRT_ERR_NO_MEMORY;

    return QWRT_OK;
}

static void mypal_destroy(qwrt_pal_t *pal) {
    pal_mypal_t *mp = (pal_mypal_t *)pal->user_data;

    // Cancel all pending operations
    cancel_all_requests(mp);
    destroy_socket_pool(mp->socket_pool);
}
```

## Step 5: Register in CMake

Add to `CMakeLists.txt`:

```cmake
option(QWRT_PAL_MYPAL "My custom PAL backend" OFF)

if(QWRT_PAL_MYPAL)
    add_library(qwrt_mypal STATIC
        platform/mypal/pal_mypal.c)
    target_link_libraries(qwrt_mypal PRIVATE qwrt_pal_common)
    target_include_directories(qwrt_mypal PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/platform)
    install(TARGETS qwrt_mypal ARCHIVE DESTINATION lib)
endif()
```

## Step 6: Testing

Use the mock PAL as a reference for testing. Write unit tests that:

1. Create your PAL
2. Call each method with valid and invalid inputs
3. Verify callback status codes
4. Check edge cases (NULL pointers, empty strings, large payloads)
5. Run with Valgrind to check for leaks

```c
void test_mypal_http_ok(void) {
    pal_mypal_t *mp = pal_mypal_create();
    qwrt_pal_t *pal = pal_mypal_get_pal(mp);

    // Test valid request
    // ...

    pal_mypal_destroy(mp);
}
```

## Reference Implementations

Study the three built-in PALs for real-world patterns:

- **[pal_mock](https://github.com/adam-ikari/qwrt/blob/master/platform/mock/pal_mock.c)** — simplest, pure C, no external dependencies. Best starting point.
- **[pal_uv](https://github.com/adam-ikari/qwrt/blob/master/platform/uv/pal_uv.c)** — full-featured, libuv event loop, mbedTLS for HTTPS.
- **[pal_freertos](https://github.com/adam-ikari/qwrt/blob/master/platform/freertos/pal_freertos.c)** — embedded, FreeRTOS primitives, lwIP sockets.

## Checklist

- [ ] All required function pointers are non-NULL
- [ ] Optional function pointers are NULL (not left uninitialized)
- [ ] `version` = 1, `name` is set, `reserved[4]` is zeroed
- [ ] `user_data` points to your private state
- [ ] All async methods call their callback exactly once
- [ ] Callbacks fire on the event loop thread (or synchronously before returning)
- [ ] No calls to `qwrt_eval`/`qwrt_call` from PAL callbacks (use `qwrt_defer_callback`)
- [ ] Error codes are from `qwrt_pal_err_t` enum, not raw integers
- [ ] Constructor returns NULL on allocation failure
- [ ] Destructor is safe to call with NULL
- [ ] Valgrind-clean (no leaks, no use-after-free)

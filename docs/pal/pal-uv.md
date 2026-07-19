---
title: pal_uv (libuv)
description: The libuv-based PAL backend for Qwrt.js on Linux and macOS — event loop integration, HTTP/HTTPS, timers, and filesystem.
---

# pal_uv — libuv Backend

The production PAL for Linux and macOS. Uses libuv for the event loop and TCP, mbedTLS for HTTPS, and POSIX APIs for filesystem operations.

## Overview

```c
#include <qwrt/qwrt.h>
#include <pal_uv.h>

int main(void) {
    // Create with default libuv loop
    qwrt_pal_t *pal = pal_uv_create(uv_default_loop());

    // Or pass an existing uv_loop_t*
    // uv_loop_t *loop = uv_default_loop();
    // qwrt_pal_t *pal = pal_uv_create(loop);

    qwrt_t *rt = qwrt_create(&(qwrt_config_t){ .pal = pal });

    // ... use runtime ...

    // Drive the event loop
    pal->run_cycle(pal, 100); qwrt_tick(rt);

    qwrt_destroy(rt);
    pal_uv_destroy(pal);  // frees the uv_loop_t
    return 0;
}
```

## Capabilities

| Feature | Support | Notes |
|---------|---------|-------|
| HTTP | ✅ | libuv TCP + custom HTTP/1.1 implementation |
| HTTPS | ✅ | mbedTLS, certificate verification |
| Streaming HTTP | ✅ | Chunked transfer decoding |
| Abort | ✅ | Closes TCP connection |
| Filesystem | ✅ | POSIX open/read/write/stat/readdir/unlink |
| Storage | ✅ | In-memory key-value store |
| Timers | ✅ | libuv `uv_timer_t` |
| Child processes | ✅ | fork/exec with pipe IPC |
| Event loop | ✅ | `uv_run(UV_RUN_NOWAIT)` |

## Event Loop

`pal_uv` wraps libuv's event loop. `run_cycle` calls `uv_run(UV_RUN_NOWAIT)` which processes ready I/O and timers without blocking. The typical loop:

```c
while (pal->run_cycle(pal, 100) > 0) {
    qwrt_tick(rt);
}
```

- `timeout_ms` is passed to `uv_run` — 0 means non-blocking, >0 blocks up to that duration
- Returns the number of events processed (or 0 on timeout, <0 on error)

## HTTP/HTTPS

HTTP is implemented on top of libuv TCP with custom HTTP/1.1 parsing. HTTPS uses mbedTLS for the TLS layer:

- Certificate verification is enabled by default
- Supports SNI (Server Name Indication)
- Chunked transfer encoding for streaming responses
- Connection reuse (Keep-Alive)

## Filesystem

Uses POSIX APIs:
- `fs_read` — `open()` + `read()` + `close()`
- `fs_write` — `open(O_CREAT|O_WRONLY|O_TRUNC)` + `write()` + `close()`
- `fs_exists` — `stat()` (returns `QWRT_OK` or `QWRT_ERR_NOT_FOUND`)
- `fs_remove` — `unlink()`
- `fs_list` — `opendir()` + `readdir()` + `closedir()`

All filesystem operations are **synchronous** — the callback fires before the function returns.

## Build Requirements

- libuv (git submodule at `deps/libuv/`)
- mbedTLS (git submodule at `deps/mbedtls/`, when `QWRT_WITH_TLS=ON`)
- CMake option: `QWRT_PAL_UV=ON` (default)

```bash
cmake -B build -DQWRT_PAL_UV=ON
```

## Thread Safety

All libuv callbacks fire on the thread that calls `uv_run` (which is the thread calling `run_cycle`). The pal_uv implementation defers these callbacks to the JS thread via `qwrt_defer_callback`.

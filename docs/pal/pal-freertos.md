---
title: pal_freertos (ESP32)
description: The FreeRTOS PAL backend for Qwrt.js on ESP32-S3 — WiFi, TCP/TLS, filesystem, and low-power operation.
---

# pal_freertos — FreeRTOS Backend

The FreeRTOS PAL targets ESP32-S3 and similar microcontrollers. Uses lwIP sockets for networking and FreeRTOS primitives for timers and synchronization.

## Overview

```c
#include <qwrt/qwrt.h>
#include <pal_freertos.h>

void app_main(void) {
    qwrt_pal_t *pal = pal_freertos_create();

    qwrt_t *rt = qwrt_create(&(qwrt_config_t){ .pal = pal });

    // ... use runtime ...

    // Drive the event loop
    while (pal->run_cycle(pal, 100) > 0) qwrt_tick(rt);

    qwrt_destroy(rt);
    pal_freertos_destroy(pal);
}
```

## Capabilities

| Feature | Support | Notes |
|---------|---------|-------|
| HTTP | ✅ | lwIP sockets, custom HTTP/1.1 |
| HTTPS | ❌ | mbedTLS integration pending |
| Streaming HTTP | ✅ | Chunked transfer decoding |
| Abort | ✅ | Closes socket |
| Filesystem | ✅ | SPIFFS/LittleFS via POSIX-like API |
| Storage | ❌ | Use filesystem instead |
| Timers | ✅ | FreeRTOS software timers |
| Child processes | ❌ | Not available on MCU |
| Event loop | ✅ | lwIP socket select + timer polling |

## Event Loop

The FreeRTOS PAL implements `run_cycle` using lwIP socket polling and FreeRTOS timer checks:

```c
while (pal->run_cycle(pal, 100) > 0) {
    qwrt_tick(rt);
}
```

- `timeout_ms` is the maximum time to wait for socket activity
- Returns the number of events processed
- Non-blocking when `timeout_ms` is 0

## HTTP

HTTP uses lwIP's socket API (`lwip_socket`, `lwip_connect`, `lwip_send`, `lwip_recv`). The implementation includes:

- Custom HTTP/1.1 request building and response parsing
- Chunked transfer encoding for streaming
- Connection timeout handling
- DNS resolution via lwIP's `netconn_gethostbyname`

## Filesystem

Uses the ESP-IDF filesystem layer, which supports SPIFFS and LittleFS:

```
/mnt/app/main.js
/mnt/data/config.json
```

- `fs_read` — reads files from flash
- `fs_write` — writes files to flash
- `fs_exists` — checks file existence via `stat()`
- `fs_remove` — deletes files via `unlink()`
- `fs_list` — lists directory contents

All filesystem operations are synchronous. Flash wear is a concern for frequent writes — consider using the in-memory storage for volatile data.

## Timers

Uses FreeRTOS software timers (`xTimerCreate`):

```c
// Timers run on the timer task, callbacks are deferred to JS thread
void *handle = pal->timer_start(pal, 1000, 1, on_tick, &data);
// handle is a TimerHandle_t cast to void*
pal->timer_stop(pal, handle);
```

## Memory Constraints

The FreeRTOS PAL is designed for memory-constrained environments:

- Stack sizes are carefully tuned (see `platform/freertos/pal_freertos.c`)
- HTTP buffers are statically allocated where possible
- No heap fragmentation from frequent malloc/free (uses fixed-size pools)
- JSON parsing uses a streaming approach rather than building full DOM

## Build Requirements

This PAL only builds within the ESP-IDF environment. It is not included in the standard CMake build:

```cmake
# In your ESP-IDF project's CMakeLists.txt:
set(QWRT_PAL_FREERTOS ON)
add_subdirectory(qwrt)
```

Requirements:
- ESP-IDF v5.x or later
- lwIP with socket API enabled
- FreeRTOS with software timer support
- WiFi configured and connected

## Platform-Specific Notes

- **WiFi**: Must be connected before creating the runtime. The PAL does not manage WiFi — use ESP-IDF's WiFi API.
- **OTA**: Not handled by the PAL. Use ESP-IDF's OTA APIs alongside qwrt.
- **Power**: The PAL does not manage sleep modes. Use `esp_light_sleep_start()` between `run_cycle` calls.
- **SPIFFS**: Must be initialized and mounted before `pal_freertos_create()`. Mount point is typically `/mnt`.

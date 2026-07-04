# qwrt ESP32-S3 Porting Design

> **Status:** Approved
> **Date:** 2026-06-29
> **Scope:** Port qwrt runtime (QuickJS + PAL) to ESP32-S3. ace-core porting is a separate follow-on spec.

## 1. Architecture Overview

```
┌─────────────────────────────────────────────────┐
│ qwrt (platform-agnostic)                        │
│   qwrt.c, context.c, bridge.c, extension.c      │
│   polyfill JS bytecode (flash-resident)         │
├─────────────────────────────────────────────────┤
│ qwrt_pal_t interface (qwrt/qwrt.h)               │
│   http_request, timer_start, fs_*, storage_*,   │
│   time_now, hrtime, log, mem_alloc, random_bytes│
├─────────────────────────────────────────────────┤
│ pal_freertos.c  ← NEW                          │
│   lwIP sockets for HTTP                         │
│   FreeRTOS xTimer for timers                    │
│   FreeRTOS xQueue for deferred callbacks        │
│   esp_random() for random bytes                 │
│   NVS for storage (non-volatile)                │
│   SPIFFS/LittleFS for filesystem                │
├─────────────────────────────────────────────────┤
│ ESP-IDF (FreeRTOS + lwIP + hardware)            │
└─────────────────────────────────────────────────┘
```

**Key insight:** qwrt.c has zero libuv dependencies. The only libuv coupling is in:
1. `pal_uv.c` — the libuv PAL implementation (replaced by `pal_freertos.c`)
2. `ace.c` — ace-core's poll loop (not in scope for this spec)

qwrt itself only needs `qwrt_pal_t` — a table of function pointers. Porting qwrt = implementing those function pointers on FreeRTOS.

## 2. PAL Function Pointers: FreeRTOS Implementation Strategy

### 2.1 HTTP (`http_request`, `http_request_stream`)

**Implementation:** lwIP sockets API (BSD-compatible subset)

```
pal_freertos_http_request:
  1. Parse URL → host, port, path
  2. lwip_getaddrinfo() → resolve DNS
  3. lwip_socket() + lwip_connect() → TCP connection
  4. Build HTTP/1.1 request string
  5. lwip_send() → send request
  6. lwip_recv() loop → accumulate response
  7. Parse HTTP response (status, headers, body)
  8. Build JSON result → invoke callback via xQueue
```

**Streaming variant:** Same connection flow, but deliver `on_headers`/`on_data`/`on_end` as data arrives instead of buffering the full response.

**TLS:** ESP-IDF includes mbedTLS. The existing `pal_uv.c` mbedTLS code can be largely reused. `#ifdef QWRT_WITH_TLS` paths in pal_uv.c are ~600 lines of mbedTLS integration that can be adapted.

**Constraints:**
- lwIP `send`/`recv` are blocking by default. Use non-blocking sockets + `lwip_select()` for timeout support.
- lwIP has limited socket count (configurable in `sdkconfig`, default ~16).
- DNS resolution is synchronous in basic lwIP; async DNS requires `LWIP_DNS` + callback API.

### 2.2 Timers (`timer_start`, `timer_stop`)

**Implementation:** FreeRTOS software timers (`xTimerCreate`)

```
pal_freertos_timer_start(delay_ms, repeat, cb, cb_data):
  1. Allocate timer_op_t {cb, cb_data, TimerHandle_t}
  2. xTimerCreate(name, pdMS_TO_TICKS(delay_ms), repeat, timer_callback)
  3. xTimerStart()
  4. Return opaque handle

timer_callback (FreeRTOS timer context):
  → Send cb + cb_data to deferred queue via xQueueSend
  → qwrt_tick drains the queue and calls cb
```

**Critical detail:** FreeRTOS timer callbacks run in the timer task context, NOT the qwrt task context. Calling JS functions from a timer callback would crash (no JS context). Solution: timer callbacks enqueue a message via `xQueueSend`; the main task drains this queue in the event loop before calling `qwrt_tick`.

### 2.3 Deferred Callback Queue

This is the key architectural difference from libuv PAL.

**libuv PAL:** `uv_run()` processes I/O and timer events. Callbacks fire inside `uv_run()`. They call `qwrt_defer_callback()` which enqueues work for `qwrt_tick()` to drain. This works because `uv_run()` and `qwrt_tick()` alternate on the same thread.

**FreeRTOS PAL:** There is no `uv_run()`. Instead, the main task runs an explicit event loop:

```c
// Main task / qwrt event loop
while (running) {
    // 1. Block until something happens
    EventBits_t bits = xEventGroupWaitBits(event_group, ...);
    
    // 2. Drain FreeRTOS→qwrt deferred callbacks
    drain_deferred_queue();
    
    // 3. Process JS jobs
    qwrt_tick(rt);
    
    // 4. Check done condition
}
```

The deferred queue bridges the gap between FreeRTOS async callbacks (timers, lwIP callbacks) and the qwrt JS context:

```
FreeRTOS timer/lwIP callback
  → pal_cb_data_t (has resolve/reject JSValues)
  → xQueueSend(deferred_queue, &cb_data)
  
Main loop
  → xQueueReceive(deferred_queue, &cb_data)
  → qwrt_defer_callback(rt, bridge_callback_fn, cb_data)
  → qwrt_tick(rt)
  → bridge_callback_fn calls JS resolve/reject
```

### 2.4 Time (`time_now`, `hrtime`)

```c
uint64_t pal_freertos_time_now(qwrt_pal_t *pal) {
    return xTaskGetTickCount() * portTICK_PERIOD_MS;
}

uint64_t pal_freertos_hrtime(qwrt_pal_t *pal) {
    return esp_timer_get_time() * 1000;  // microseconds → nanoseconds
}
```

`esp_timer_get_time()` provides microsecond resolution since boot — sufficient for `hrtime`.

### 2.5 Memory (`mem_alloc`, `mem_free`)

```c
void *pal_freertos_mem_alloc(qwrt_pal_t *pal, size_t size) {
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void pal_freertos_mem_free(qwrt_pal_t *pal, void *ptr) {
    heap_caps_free(ptr);
}
```

**Strategy:** Prefer PSRAM for large allocations (JS heap, HTTP buffers). Use `MALLOC_CAP_SPIRAM` with `MALLOC_CAP_8BIT` fallback — if PSRAM is unavailable, falls back to internal SRAM.

### 2.6 Logging (`log`)

```c
void pal_freertos_log(qwrt_pal_t *pal, int level, const char *msg) {
    const char *prefix = level == 0 ? "I" : level == 1 ? "W" : "E";
    ESP_LOGI("qwrt", "(%s) %s", prefix, msg ? msg : "");
}
```

Uses ESP-IDF logging subsystem. Output goes to UART by default, configurable via `sdkconfig`.

### 2.7 Random Bytes (`random_bytes`)

```c
void pal_freertos_random_bytes(qwrt_pal_t *pal, uint8_t *buf, size_t len) {
    esp_fill_random(buf, len);
}
```

ESP-IDF provides hardware RNG via `esp_fill_random()`.

### 2.8 Storage (`storage_get`, `storage_set`, `storage_del`)

**Implementation:** NVS (Non-Volatile Storage) — ESP-IDF's built-in key-value storage in flash.

```
pal_freertos_storage_set:
  nvs_open("qwrt", NVS_READWRITE, &handle)
  nvs_set_str(handle, key, value)
  nvs_commit(handle)
  nvs_close(handle)
  → callback with "ok"

pal_freertos_storage_get:
  nvs_open("qwrt", NVS_READONLY, &handle)
  nvs_get_str(handle, key, buf, &len)
  nvs_close(handle)
  → callback with value or "not found"
```

**Constraints:**
- NVS keys are 15 chars max
- NVS values are 4000 bytes max (configurable)
- NVS has limited write cycles (~100K per sector). Storage is for config/settings, not frequent writes.

### 2.9 Filesystem (`fs_read`, `fs_write`, `fs_exists`, `fs_remove`, `fs_list`)

**Implementation:** LittleFS via ESP-IDF's VFS layer. ESP-IDF provides `esp_vfs_littlefs_register()` which mounts a LittleFS partition at a path prefix (e.g. `/littlefs`). After mounting, standard POSIX file operations work.

```
pal_freertos_fs_read(path):
  fopen(path, "r") → fread → fclose
  → callback with content

pal_freertos_fs_write(path, data, len):
  fopen(path, "w") → fwrite → fclose
  → callback with "ok"

pal_freertos_fs_exists(path):
  stat(path, &st) == 0 ? "true" : "false"

pal_freertos_fs_remove(path):
  unlink(path) == 0 ? "ok" : error

pal_freertos_fs_list(path):
  opendir → readdir loop → closedir
  → build JSON array of names
```

**Partition table** must include a `littlefs` partition:

```
# partitions.csv
littlefs, data, spiffs, 0x110000, 0x100000
```

**Mounting** happens at boot in `main.c` before `qwrt_create()`:

```c
esp_vfs_littlefs_conf_t conf = {
    .base_path = "/littlefs",
    .partition_label = "littlefs",
    .format_if_mount_failed = true,
};
esp_vfs_littlefs_register(&conf);
```

**Constraints:**
- Partition size fixed at build time
- No subdirectory support limitation depends on LittleFS version (current versions support directories)
- Flash wear-leveling is handled by LittleFS internally

### 2.10 Process Management (`spawn`, `join`, etc.)

**All set to NULL.** ESP32-S3 has no process model (no `fork`, no `exec`). Multi-agent isolation is not supported on MCU. This is already documented in `qwrt.h`:

> "These are OPTIONAL — set to NULL if the platform doesn't support multi-process isolation (e.g. embedded MCU)."

## 3. Event Loop Design

### 3.1 The Core Loop

The qwrt event loop on FreeRTOS must:
1. Block until an event occurs (HTTP data, timer fire, etc.)
2. Dispatch the event (call the PAL callback, which enqueues to `qwrt_defer_callback`)
3. Call `qwrt_tick()` to process JS promise jobs
4. Repeat

```c
// qwrt event loop — runs in a FreeRTOS task
void qwrt_event_loop(qwrt_t *rt) {
    while (1) {
        // Wait for any event source
        EventBits_t bits = xEventGroupWaitBits(
            event_group,
            EVENT_HTTP_RECV | EVENT_TIMER_FIRE | EVENT_STOP,
            pdTRUE,   // clear on exit
            pdFALSE,  // wait for any bit
            portMAX_DELAY
        );
        
        if (bits & EVENT_STOP) break;
        
        // Drain deferred callbacks from FreeRTOS → qwrt
        drain_deferred_queue(rt);
        
        // Process JS jobs
        qwrt_tick(rt);
    }
}
```

### 3.2 Event Sources

| Event Source | Mechanism | Trigger |
|---|---|---|
| HTTP data ready | `lwip_select()` on socket set | Socket readable |
| Timer fire | FreeRTOS xTimer callback → xQueueSend | Timer period elapsed |
| Stop signal | External task notification | Clean shutdown |

### 3.3 PAL Interface: No Changes

The `qwrt_pal_t` interface is unchanged. The FreeRTOS event loop is implemented
as standalone functions (`pal_freertos_run_cycle`, `pal_freertos_drain_deferred`)
in `pal_freertos.h`, not as PAL function pointers. This keeps the PAL interface
minimal — qwrt doesn't need to know about event loops.

### 3.4 Integration with ace-core (future)

When ace-core is ported on top of qwrt, the event loop becomes:

```c
// ace_poll_blocking on FreeRTOS
while (!done) {
    pal_freertos_run_cycle(pf, timeout_ms);
    pal_freertos_drain_deferred(pf, rt);
    qwrt_tick(rt);
    check_done();
}
```

This is the same pattern as the libuv PAL, just with FreeRTOS primitives instead.

## 4. Memory Model

### 4.1 Memory Regions (ESP32-S3)

| Region | Size | Usage |
|---|---|---|
| Internal SRAM | 512 KB | C stack, qwrt_t struct, small buffers |
| PSRAM (SPIRAM) | 2-8 MB | QuickJS heap, HTTP buffers, JS objects |
| Flash | 4-16 MB | Code (.text), bytecode arrays (.rodata), NVS, LittleFS |

### 4.2 QuickJS Memory

QuickJS allocates JS objects, strings, and the runtime itself via `js_malloc`/`js_free`. By default these use `malloc`/`free`. We configure QuickJS to use `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`:

```c
// In qwrt_create, after JS_NewRuntime():
JS_SetMemoryLimit(rt->jsrt, 4 * 1024 * 1024);  // 4MB cap on PSRAM
```

QuickJS's `JS_NewRuntime()` accepts custom allocation functions via `JSMallocFunctions`. We set these to use PSRAM-preferring allocators.

### 4.3 Bytecode in Flash

The polyfill bytecode (`qwrt_default_polyfill`, ~501KB) and ace bundle bytecode (~371KB) are `const uint8_t` arrays. The compiler places them in `.rodata` which lives in flash. They are NOT copied to RAM — QuickJS reads them directly from flash via memory-mapped access (ESP32-S3 supports MMAP for flash).

**This is the key reason no bundle trimming is needed:** bytecode sits in flash, not SRAM or PSRAM.

## 5. Build System Integration

### 5.1 ESP-IDF Component

qwrt is registered as an ESP-IDF component:

```
platform/esp32s3/
  CMakeLists.txt          ← ESP-IDF project
  main/
    CMakeLists.txt
    main.c                ← Example: create qwrt, eval JS, run event loop
  components/
    qwrt/
      CMakeLists.txt      ← idf_component_register()
      include/
        qwrt/
          qwrt.h          ← symlink or copy of qwrt/qwrt.h
      src/
        qwrt.c            ← symlinks to qwrt/src/*.c
        context.c
        bridge.c
        extension.c
        ext_compress.c
        ext_crypto.c
        ext_textcodec.c
        polyfill_default.c
      pal/
        pal_freertos.c    ← NEW: FreeRTOS PAL implementation
        pal_freertos.h
      Kconfig              ← ESP-IDF menuconfig options
```

### 5.2 Component CMakeLists.txt

```cmake
idf_component_register(
    SRCS
        "src/qwrt.c"
        "src/context.c"
        "src/bridge.c"
        "src/extension.c"
        "src/ext_compress.c"
        "src/ext_crypto.c"
        "src/ext_textcodec.c"
        "src/polyfill_default.c"
        "pal/pal_freertos.c"
    INCLUDE_DIRS
        "include"
        "pal"
    REQUIRES
        nvs_flash
        esp_timer
        lwip
        mbedtls
        spi_flash
        vfs
        littlefs
)

# Link QuickJS as a separate component or static library
target_link_libraries(${COMPONENT_LIB} INTERFACE quickjs)
```

### 5.3 Kconfig (menuconfig options)

```
menu "qwrt Configuration"
    config QWRT_PSRAM_HEAP_SIZE
        int "QuickJS heap size (bytes)"
        default 4194304
        help
            Maximum memory for QuickJS heap. Uses PSRAM if available.
    
    config QWRT_HTTP_MAX_CONNECTIONS
        int "Maximum concurrent HTTP connections"
        default 4
        range 1 16
    
    config QWRT_STORAGE_NAMESPACE
        string "NVS namespace for storage"
        default "qwrt"
endmenu
```

## 6. What Changes in Existing Code

### 6.1 New Files

| File | Purpose |
|---|---|
| `qwrt/platform/freertos/pal_freertos.c` | FreeRTOS PAL implementation |
| `qwrt/platform/freertos/pal_freertos.h` | PAL factory function declaration |
| `platform/esp32s3/CMakeLists.txt` | ESP-IDF project |
| `platform/esp32s3/main/main.c` | Example application |
| `platform/esp32s3/components/qwrt/CMakeLists.txt` | Component registration |
| `platform/esp32s3/components/qwrt/Kconfig` | Menuconfig options |

### 6.2 Modified Files

| File | Change |
|---|---|
| `qwrt/esp-idf/CMakeLists.txt` | NEW — ESP-IDF component registration (`idf_component_register`) |
| `qwrt/platform/freertos/pal_freertos.c` | NEW — FreeRTOS PAL implementation |
| `qwrt/platform/freertos/pal_freertos.h` | NEW — FreeRTOS PAL header |
| `platform/esp32s3/` | NEW — ESP-IDF project directory |
| `qwrt/CMakeLists.txt` | No change (desktop build untouched) |

### 6.3 What Does NOT Change

- **qwrt core** — `qwrt.c`, `context.c`, `bridge.c`, `extension.c` are platform-agnostic
- **PAL interface** — `qwrt_pal_t` unchanged; no new function pointers needed
- **JS polyfill** — same bytecode, same WinterCG APIs
- **Extensions** — compress, crypto, textcodec are already platform-agnostic (use miniz, not zlib)

## 7. Testing Strategy

### 7.1 Unit Tests (Linux host)

`pal_mock.c` already provides a synchronous mock PAL. All existing qwrt tests run against `pal_mock.c`. These continue to work unchanged.

### 7.2 Integration Tests (ESP32-S3 hardware / QEMU)

ESP-IDF supports QEMU emulation for basic testing. However, network-dependent tests (HTTP) require real hardware or a network-capable emulator.

**Phase 1:** Build verification — ensure `idf.py build` succeeds with all source files
**Phase 2:** Boot test — `qwrt_create` + `qwrt_eval("1+1")` on hardware
**Phase 3:** PAL tests — timer, storage, filesystem on hardware
**Phase 4:** HTTP tests — fetch from a test endpoint on hardware

### 7.3 CI Considerations

ESP-IDF builds require the Xtensa toolchain (~200MB download). Not suitable for fast CI turnaround. Options:
- **Nightly build** — build ESP32-S3 target on schedule, not per-commit
- **Host-side compile check** — compile `pal_freertos.c` with `-DUNIT_TEST` on Linux, mocking ESP-IDF headers
- **Self-hosted runner** — ESP32-S3 dev board connected to a CI runner

## 8. Design Decisions (Confirmed)

1. **qwrt source organization:** **Plan B** — add `CMakeLists.txt` in qwrt root with `idf_component_register()`. ESP-IDF project uses `EXTRA_COMPONENT_DIRS` to point at `qwrt/`. No file duplication, no symlinks.

2. **TLS:** **Enabled by default.** mbedTLS is bundled with ESP-IDF — zero external cost. `QWRT_WITH_TLS` is on. Reuse existing mbedTLS integration code from `pal_uv.c`.

3. **Filesystem:** **LittleFS, full implementation.** The example partition table includes a `littlefs` partition. All five `fs_*` PAL functions map to LittleFS VFS calls (`fopen`/`fread`/`fwrite`/`stat`/`unlink`/`readdir`). ESP-IDF's VFS layer provides POSIX-compatible wrappers — the implementation is essentially standard C file I/O.

4. **`run_cycle`:** **Standalone function, not a PAL pointer.** `pal_freertos_run_cycle` and `pal_freertos_drain_deferred` are standalone functions in `pal_freertos.h`, not added to `qwrt_pal_t`. This keeps the PAL interface minimal — qwrt doesn't need to know about event loops. ace-core will call these directly when ported.

## 9. Out of Scope

- **ace-core porting** — requires the `run_cycle` function exported by `pal_freertos.h`; separate spec
- **WiFi provisioning** — embedder responsibility; example main.c will show basic WiFi connect
- **OTA updates** — embedder responsibility
- **Power management** — embedder responsibility
- **Multi-agent isolation** — not possible on MCU (no process model)
- **WASM extensions** — `ext_wamr.c` and `ext_wasm3.c` are out of scope; may be added later if WAMR/WASM3 support ESP32-S3

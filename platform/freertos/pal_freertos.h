/*
 * FreeRTOS PAL — Platform Abstraction Layer implementation for ESP32-S3
 *
 * Provides qwrt_pal_t function pointers backed by FreeRTOS + lwIP + ESP-IDF.
 * All async operations use FreeRTOS primitives (xTimer, xQueue, lwIP sockets).
 *
 * Usage:
 *   pal_freertos_t *pf = pal_freertos_create();
 *   qwrt_pal_t *pal = pal_freertos_get_pal(pf);
 *   ...
 *   // Event loop (runs in a FreeRTOS task):
 *   while (running) {
 *       pal_freertos_run_cycle(pf, portMAX_DELAY);
 *       qwrt_tick(rt);
 *   }
 *   ...
 *   pal_freertos_destroy(pf);
 */

#ifndef PAL_FREERTOS_H
#define PAL_FREERTOS_H

#include "qwrt/qwrt.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque PAL state type */
typedef struct pal_freertos_t pal_freertos_t;

/*
 * Create a FreeRTOS-backed PAL instance.
 *
 * Initializes NVS, mounts LittleFS, creates the deferred callback queue,
 * and sets up all qwrt_pal_t function pointers.
 *
 * Returns NULL on failure (NVS init failed, queue creation failed, etc.).
 */
pal_freertos_t *pal_freertos_create(void);

/*
 * Destroy a FreeRTOS PAL instance.
 *
 * Stops all active timers, closes all HTTP connections, unmounts filesystem,
 * and frees all resources. After this call, the pal pointer is invalid.
 */
void pal_freertos_destroy(pal_freertos_t *pf);

/*
 * Get the qwrt_pal_t interface from the FreeRTOS PAL.
 *
 * The returned pointer is valid for the lifetime of pal_freertos_t.
 * This is the pointer you pass to qwrt_create().
 */
qwrt_pal_t *pal_freertos_get_pal(pal_freertos_t *pf);

/*
 * Run one cycle of the FreeRTOS event loop.
 *
 * Blocks waiting for events (HTTP data, timer fires) up to timeout_ms.
 * When events arrive, dispatches them and drains deferred callbacks
 * into the qwrt callback queue (via qwrt_defer_callback).
 *
 * timeout_ms: maximum time to wait, or portMAX_DELAY to block indefinitely.
 *
 * Returns: 0 = timeout with no events, >0 = events processed, <0 = error.
 *
 * The caller should call qwrt_tick() after this to process JS jobs.
 */
int pal_freertos_run_cycle(pal_freertos_t *pf, int timeout_ms);

/*
 * Set the qwrt runtime associated with this PAL instance.
 *
 * The runtime pointer is used by pal_freertos_run_cycle to drain deferred
 * callbacks (timer/HTTP completions) back into the qwrt thread via
 * qwrt_defer_callback. Call this after qwrt_create() (or ace_create(),
 * which creates qwrt internally — use ace_get_qwrt() to obtain it).
 */
void pal_freertos_set_runtime(pal_freertos_t *pf, struct qwrt_t *rt);

#ifdef __cplusplus
}
#endif

#endif /* PAL_FREERTOS_H */

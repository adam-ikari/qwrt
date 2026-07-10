/*
 * libuv PAL — Platform Abstraction Layer implementation using libuv
 *
 * Provides qwrt_pal_t function pointers backed by libuv async operations.
 * Usage:
 *   qwrt_pal_t *pal = pal_uv_create(NULL);   // creates own loop
 *   // or
 *   qwrt_pal_t *pal = pal_uv_create(my_loop); // uses existing loop
 *   ...
 *   pal_uv_destroy(pal);
 */

#ifndef QWRT_PAL_UV_H
#define QWRT_PAL_UV_H

#include "qwrt/qwrt.h"
#include <uv.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Create a libuv-backed PAL instance.
 *
 * If loop is NULL, a new uv_loop_t is created and owned by the PAL.
 * If loop is non-NULL, it is used but NOT owned (caller must keep it alive).
 *
 * Returns an qwrt_pal_t* with all function pointers set, or NULL on failure.
 */
qwrt_pal_t *pal_uv_create(uv_loop_t *loop);

/*
 * Create a libuv-backed PAL instance with a custom storage limit.
 *
 * Same as pal_uv_create but allows configuring the maximum number of
 * in-memory storage entries. pal_uv_create() uses a default of 128.
 *
 * If storage_max <= 0, the default of 128 is used.
 */
qwrt_pal_t *pal_uv_create_with_config(uv_loop_t *loop, int storage_max);

/*
 * Destroy a libuv PAL instance created by pal_uv_create.
 *
 * Closes all uv handles, frees the internal loop (if owned), and frees
 * the PAL struct itself. After this call, the pal pointer is invalid.
 */
void pal_uv_destroy(qwrt_pal_t *pal);

/*
 * Get the uv_loop_t used by this PAL.
 *
 * Useful for driving the event loop from the host:
 *   uv_run(pal_uv_get_loop(pal), UV_RUN_ONCE);
 */
uv_loop_t *pal_uv_get_loop(qwrt_pal_t *pal);

#ifdef __cplusplus
}
#endif

#endif /* QWRT_PAL_UV_H */

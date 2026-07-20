/*
 * pal_wasm.c — WASM/Emscripten PAL implementation
 *
 * Bridges qwrt's C API to browser Web APIs when compiled to WebAssembly.
 * No libuv, no mbedTLS — the browser provides everything natively.
 *
 * Supported:  random_bytes, time_now, hrtime, log, storage, memory
 * Unsupported: HTTP (needs async), FS, spawn (browser sandbox)
 * Timers:      via emscripten_set_timeout (sync callback in qwrt thread)
 *
 * Usage:
 *   cmake -B build-wasm -S . \
 *     -DCMAKE_TOOLCHAIN_FILE=<emsdk>/emscripten/cmake/Modules/Platform/Emscripten.cmake \
 *     -DQWRT_PAL_WASM=ON
 */

#include <emscripten.h>
#include <emscripten/eventloop.h>
#include "pal_wasm.h"
#include <stdlib.h>
#include <string.h>

/* ================================================================
 * Internal state
 * ================================================================ */

typedef struct {
    qwrt_pal_t pal;
    int storage_max;
} pal_wasm_t;

static pal_wasm_t *pal_wasm_self(qwrt_pal_t *pal)
{
    return (pal_wasm_t *)((char *)pal - offsetof(pal_wasm_t, pal));
}

/* ================================================================
 * Synchronous — browser Web APIs via EM_ASM
 * ================================================================ */

static void pal_wasm_random_bytes(qwrt_pal_t *pal, uint8_t *buf, size_t len)
{
    (void)pal;
    /* crypto.getRandomValues fills the buffer synchronously.
     * Use EM_ASM with $0,$1 placeholders (no {} in the JS block) */
    int buf_int = (int)(uintptr_t)buf;
    int len_int = (int)len;
    EM_ASM({
        var arr = new Uint8Array(HEAPU8.buffer, $0, $1);
        crypto.getRandomValues(arr);
    }, buf_int, len_int);
}

static uint64_t pal_wasm_time_now(qwrt_pal_t *pal)
{
    (void)pal;
    return (uint64_t)EM_ASM_DOUBLE({ return Date.now(); });
}

static uint64_t pal_wasm_hrtime(qwrt_pal_t *pal)
{
    (void)pal;
    /* performance.now() in ms → nanoseconds */
    return (uint64_t)(EM_ASM_DOUBLE({ return performance.now(); }) * 1e6);
}

static void pal_wasm_log(qwrt_pal_t *pal, int level, const char *msg)
{
    (void)pal;
    switch (level) {
    case 0: case 1: case 2: case 3:
        EM_ASM({ console.error(UTF8ToString($0)); }, msg);
        break;
    case 4:
        EM_ASM({ console.warn(UTF8ToString($0)); }, msg);
        break;
    default:
        EM_ASM({ console.log(UTF8ToString($0)); }, msg);
        break;
    }
}

/* ================================================================
 * HTTP — simplified (browser fetch is async, not yet bridged)
 * ================================================================ */

static void pal_wasm_http_not_supported(qwrt_pal_t *pal,
    const char *url, const char *method, const char *headers,
    const char *body, size_t body_len, qwrt_pal_cb_t cb, void *cb_data)
{
    (void)pal; (void)url; (void)method; (void)headers; (void)body; (void)body_len;
    if (cb) cb(cb_data, QWRT_ERR_NOT_SUPPORTED, NULL, 0);
}

/* ================================================================
 * Timers — emscripten_set_timeout for synchronous callbacks
 * ================================================================ */

typedef struct {
    int em_id;
    qwrt_pal_cb_t cb;
    void *cb_data;
} pal_wasm_timer_t;

static void pal_wasm_timer_cb(void *user_data)
{
    pal_wasm_timer_t *t = (pal_wasm_timer_t *)user_data;
    if (t->cb) t->cb(t->cb_data, 0, NULL, 0);
}

static void *pal_wasm_timer_start(qwrt_pal_t *pal, uint64_t delay_ms, int repeat,
                                   qwrt_pal_cb_t cb, void *cb_data)
{
    (void)pal;
    pal_wasm_timer_t *t = (pal_wasm_timer_t *)malloc(sizeof(*t));
    if (!t) return NULL;
    t->cb = cb;
    t->cb_data = cb_data;
    /* emscripten_set_timeout fires on the main browser thread.
     * For the WASM PAL, timers fire synchronously on the same thread
     * that called qwrt_tick — no deferred callback needed. */
    t->em_id = emscripten_set_timeout(pal_wasm_timer_cb, (double)delay_ms, t);
    return t;
}

static void pal_wasm_timer_stop(qwrt_pal_t *pal, void *handle)
{
    (void)pal;
    if (!handle) return;
    pal_wasm_timer_t *t = (pal_wasm_timer_t *)handle;
    emscripten_clear_timeout(t->em_id);
    free(t);
}

/* ================================================================
 * Filesystem — not available in browser
 * ================================================================ */

static void pal_wasm_fs_not_supported(qwrt_pal_t *pal, const char *path,
                                       qwrt_pal_cb_t cb, void *cb_data)
{
    (void)pal; (void)path;
    if (cb) cb(cb_data, QWRT_ERR_NOT_SUPPORTED, NULL, 0);
}

/* ================================================================
 * Storage — browser localStorage via EM_ASM
 * ================================================================ */

static void pal_wasm_storage_get(qwrt_pal_t *pal, const char *key,
                                  qwrt_pal_cb_t cb, void *cb_data)
{
    (void)pal;
    /* EM_ASM_INT returns int. For string values we need EM_ASM_PTR.
     * EM_ASM_PTR is available in newer Emscripten. Use a workaround:
     * write the value into a known buffer and return its length. */
    static char val_buf[4096];
    int len = EM_ASM_INT({
        var key = UTF8ToString($0);
        var val = localStorage.getItem(key);
        if (val === null) return -1;
        var bytes = intArrayFromString(val);
        var buf = $1;
        var maxLen = $2;
        var copyLen = Math.min(bytes.length, maxLen - 1);
        for (var i = 0; i < copyLen; i++) HEAPU8[buf + i] = bytes[i];
        HEAPU8[buf + copyLen] = 0;
        return copyLen;
    }, key, val_buf, (int)sizeof(val_buf));

    if (len >= 0) {
        val_buf[len] = '\0';
        if (cb) cb(cb_data, QWRT_OK, val_buf, (size_t)len);
    } else {
        if (cb) cb(cb_data, QWRT_ERR_NOT_FOUND, NULL, 0);
    }
}

static void pal_wasm_storage_set(qwrt_pal_t *pal, const char *key,
                                  const char *value, size_t value_len,
                                  qwrt_pal_cb_t cb, void *cb_data)
{
    (void)pal; (void)value_len;
    EM_ASM({
        var key = UTF8ToString($0);
        var value = UTF8ToString($1);
        localStorage.setItem(key, value);
    }, key, value);
    if (cb) cb(cb_data, QWRT_OK, NULL, 0);
}

static void pal_wasm_storage_del(qwrt_pal_t *pal, const char *key,
                                  qwrt_pal_cb_t cb, void *cb_data)
{
    (void)pal;
    EM_ASM({ localStorage.removeItem(UTF8ToString($0)); }, key);
    if (cb) cb(cb_data, QWRT_OK, NULL, 0);
}

/* ================================================================
 * Spawn — browser sandbox, not available
 * ================================================================ */

static void *pal_wasm_spawn_null(qwrt_pal_t *pal, const char *cmd,
                                  const char *const *args, const char *const *env)
{
    (void)pal; (void)cmd; (void)args; (void)env;
    return NULL;
}

/* ================================================================
 * Lifecycle
 * ================================================================ */

static int pal_wasm_init(qwrt_pal_t *pal)
{
    (void)pal;
    return QWRT_OK;
}

static void pal_wasm_destroy(qwrt_pal_t *pal)
{
    pal_wasm_t *self = pal_wasm_self(pal);
    free(self);
}

/* ================================================================
 * Constructor
 * ================================================================ */

qwrt_pal_t *pal_wasm_create(void)
{
    pal_wasm_t *self = (pal_wasm_t *)calloc(1, sizeof(*self));
    if (!self) return NULL;

    qwrt_pal_t *pal = &self->pal;

    pal->version = 1;
    pal->name = "wasm";
    pal->user_data = self;

    /* HTTP — not bridged (async complexity) */
    pal->http_request        = (void *)pal_wasm_http_not_supported;
    pal->http_request_stream = NULL;
    pal->http_abort          = NULL;

    /* FS — not available */
    pal->fs_read   = (void *)pal_wasm_fs_not_supported;
    pal->fs_write  = (void *)pal_wasm_fs_not_supported;
    pal->fs_exists = (void *)pal_wasm_fs_not_supported;
    pal->fs_remove = (void *)pal_wasm_fs_not_supported;
    pal->fs_list   = (void *)pal_wasm_fs_not_supported;

    /* Storage */
    pal->storage_get = pal_wasm_storage_get;
    pal->storage_set = pal_wasm_storage_set;
    pal->storage_del = pal_wasm_storage_del;

    /* Timers */
    pal->timer_start = pal_wasm_timer_start;
    pal->timer_stop  = pal_wasm_timer_stop;

    /* Time */
    pal->time_now = pal_wasm_time_now;
    pal->hrtime   = pal_wasm_hrtime;

    /* Entropy */
    pal->random_bytes = pal_wasm_random_bytes;

    /* Logging */
    pal->log = pal_wasm_log;

    /* Memory */
    pal->mem_alloc = NULL;
    pal->mem_free  = NULL;

    /* Event loop — browser drives this */
    pal->run_cycle = NULL;

    /* Process */
    pal->spawn            = pal_wasm_spawn_null;
    pal->spawn_get_stdin  = NULL;
    pal->spawn_get_stdout = NULL;
    pal->channel_send     = NULL;
    pal->channel_recv     = NULL;
    pal->channel_close    = NULL;
    pal->join             = NULL;
    pal->terminate        = NULL;

    /* Lifecycle */
    pal->init    = pal_wasm_init;
    pal->destroy = pal_wasm_destroy;

    memset(pal->reserved, 0, sizeof(pal->reserved));

    return pal;
}

void pal_wasm_destroy_pal(qwrt_pal_t *pal)
{
    pal_wasm_destroy(pal);
}
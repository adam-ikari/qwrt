/*
 * pal_mock.c — Mock PAL implementation for testing
 *
 * All async operations complete synchronously via immediate callback,
 * except timers which require explicit triggering via pal_mock_fire_timer().
 */

#define _POSIX_C_SOURCE 200809L

#include "pal_mock.h"
#include "pal_common.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>

/* ================================================================
 * Internal data structures
 * ================================================================ */

#define MOCK_KV_MAX 256
#define MOCK_TIMER_MAX 256
#define MOCK_LOG_MAX 1024

typedef struct {
    char *key;
    char *value;
} mock_kv_entry_t;

typedef struct {
    mock_kv_entry_t entries[MOCK_KV_MAX];
    int count;
} mock_kv_map_t;

typedef struct {
    int active;
    qwrt_pal_cb_t cb;
    void *cb_data;
    int handle_id;
} mock_timer_entry_t;

typedef struct {
    int level;
    char *msg;
} mock_log_entry_t;

typedef struct {
    qwrt_pal_t pal;              /* embedded PAL interface */

    mock_kv_map_t fs_map;       /* filesystem: path -> content */
    mock_kv_map_t store_map;    /* storage: key -> value */

    mock_timer_entry_t timers[MOCK_TIMER_MAX];
    int timer_count;
    int timer_next_id;

    mock_log_entry_t log_entries[MOCK_LOG_MAX];
    int log_count;

    uint64_t time_ms;           /* mock time, defaults to 0 */

    char *http_response;        /* configurable HTTP response JSON */

    int alloc_count;            /* outstanding allocations for leak detection */

    char *log_buf;              /* per-instance log buffer for pal_mock_get_log */
    size_t log_buf_size;

    /* Last HTTP request details for test assertions */
    char *last_http_url;
    char *last_http_method;
    char *last_http_headers;
    char *last_http_body;
} pal_mock_t;

/* ================================================================
 * KV map helpers
 * ================================================================ */

static void mock_kv_init(mock_kv_map_t *map)
{
    memset(map, 0, sizeof(*map));
}

static void mock_kv_clear(mock_kv_map_t *map)
{
    int i;
    for (i = 0; i < map->count; i++) {
        free(map->entries[i].key);
        free(map->entries[i].value);
        map->entries[i].key = NULL;
        map->entries[i].value = NULL;
    }
    map->count = 0;
}

static mock_kv_entry_t *mock_kv_find(mock_kv_map_t *map, const char *key)
{
    int i;
    for (i = 0; i < map->count; i++) {
        if (map->entries[i].key && strcmp(map->entries[i].key, key) == 0) {
            return &map->entries[i];
        }
    }
    return NULL;
}

static int mock_kv_set(mock_kv_map_t *map, const char *key, const char *value)
{
    mock_kv_entry_t *existing;
    char *key_copy;
    char *val_copy;

    existing = mock_kv_find(map, key);
    if (existing) {
        val_copy = strdup(value);
        if (!val_copy) return -1;
        free(existing->value);
        existing->value = val_copy;
        return 0;
    }

    if (map->count >= MOCK_KV_MAX) return -1;

    key_copy = strdup(key);
    if (!key_copy) return -1;

    val_copy = strdup(value);
    if (!val_copy) {
        free(key_copy);
        return -1;
    }

    map->entries[map->count].key = key_copy;
    map->entries[map->count].value = val_copy;
    map->count++;
    return 0;
}

static int mock_kv_del(mock_kv_map_t *map, const char *key)
{
    int i;
    for (i = 0; i < map->count; i++) {
        if (map->entries[i].key && strcmp(map->entries[i].key, key) == 0) {
            free(map->entries[i].key);
            free(map->entries[i].value);
            /* shift remaining entries down */
            if (i < map->count - 1) {
                memmove(&map->entries[i], &map->entries[i + 1],
                        (map->count - i - 1) * sizeof(mock_kv_entry_t));
            }
            map->count--;
            map->entries[map->count].key = NULL;
            map->entries[map->count].value = NULL;
            return 0;
        }
    }
    return -2;
}

/* ================================================================
 * Helper: get pal_mock_t from qwrt_pal_t pointer
 * ================================================================ */

static pal_mock_t *pal_to_mock(qwrt_pal_t *pal)
{
    return (pal_mock_t *)((char *)pal - offsetof(pal_mock_t, pal));
}

/* ================================================================
 * Mock PAL implementations
 * ================================================================ */

/* ---- HTTP ---- */

static void pal_mock_http_request(qwrt_pal_t *pal,
                                   const char *url, const char *method,
                                   const char *headers, const char *body,
                                   size_t body_len,
                                   qwrt_pal_cb_t cb, void *cb_data)
{
    pal_mock_t *m = pal_to_mock(pal);
    const char *resp;

    /* Store request details for test assertions */
    free(m->last_http_url);
    free(m->last_http_method);
    free(m->last_http_headers);
    free(m->last_http_body);

    m->last_http_url = url ? strdup(url) : NULL;
    m->last_http_method = method ? strdup(method) : NULL;
    m->last_http_headers = headers ? strdup(headers) : NULL;
    m->last_http_body = (body && body_len > 0) ? strndup(body, body_len) : NULL;

    resp = m->http_response ? m->http_response
        : "{\"status\":200,\"headers\":{\"Content-Type\":\"application/json\"},\"body\":\"mock response\"}";

    cb(cb_data, 0, resp, strlen(resp));
}

static void pal_mock_http_request_stream(qwrt_pal_t *pal,
                                          const char *url, const char *method,
                                          const char *headers, const char *body,
                                          size_t body_len,
                                          qwrt_pal_stream_ops_t *ops)
{
    pal_mock_t *m = pal_to_mock(pal);
    const char *resp;
    const char *resp_body = "mock response";
    const char *resp_headers = "{\"Content-Type\":\"application/json\"}";
    int resp_status = 200;
    /* Track malloc'd copies so we can free them after delivery. */
    char *owned_headers = NULL;
    char *owned_body = NULL;

    (void)method;
    (void)headers;
    (void)body;
    (void)body_len;

    /* Store request details for test assertions */
    free(m->last_http_url);
    free(m->last_http_method);
    free(m->last_http_headers);
    free(m->last_http_body);

    m->last_http_url = url ? strdup(url) : NULL;
    m->last_http_method = method ? strdup(method) : NULL;
    m->last_http_headers = headers ? strdup(headers) : NULL;
    m->last_http_body = (body && body_len > 0) ? strndup(body, body_len) : NULL;

    /* Parse the configured JSON response to extract status, headers, body */
    resp = m->http_response ? m->http_response
        : "{\"status\":200,\"headers\":{\"Content-Type\":\"application/json\"},\"body\":\"mock response\"}";

    /* Simple JSON parsing for mock response */
    {
        const char *p;
        /* Extract status */
        p = strstr(resp, "\"status\"");
        if (p) {
            p = strchr(p, ':');
            if (p) {
                resp_status = (int)strtol(p + 1, NULL, 10);
            }
        }
        /* Extract headers as JSON string */
        {
            const char *h_start = strstr(resp, "\"headers\"");
            if (h_start) {
                h_start = strchr(h_start, ':');
                if (h_start) {
                    while (*h_start && *h_start != '{') h_start++;
                    if (*h_start == '{') {
                        int depth = 0;
                        const char *h_end = h_start;
                        while (*h_end) {
                            if (*h_end == '{') depth++;
                            if (*h_end == '}') { depth--; if (depth == 0) { h_end++; break; } }
                            h_end++;
                        }
                        {
                            size_t h_len = h_end - h_start;
                            char *h_buf = (char *)malloc(h_len + 1);
                            if (h_buf) {
                                memcpy(h_buf, h_start, h_len);
                                h_buf[h_len] = '\0';
                                resp_headers = h_buf;
                                owned_headers = h_buf;
                            }
                        }
                    }
                }
            }
        }
        /* Extract body as string */
        {
            const char *b_start = strstr(resp, "\"body\"");
            if (b_start) {
                b_start = strchr(b_start, ':');
                if (b_start) {
                    while (*b_start && *b_start != '\"') b_start++;
                    if (*b_start == '\"') {
                        b_start++;
                        {
                            const char *b_end = b_start;
                            while (*b_end && *b_end != '\"') {
                                if (*b_end == '\\') b_end++;
                                b_end++;
                            }
                            {
                                size_t b_len = b_end - b_start;
                                char *b_buf = (char *)malloc(b_len + 1);
                                if (b_buf) {
                                    memcpy(b_buf, b_start, b_len);
                                    b_buf[b_len] = '\0';
                                    resp_body = b_buf;
                                    owned_body = b_buf;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    /* Deliver streaming response: headers, data, end */
    if (ops) {
        if (ops->on_headers) {
            ops->on_headers(ops->user_data, resp_status, resp_headers);
        }
        if (ops->on_data && resp_body && resp_body[0]) {
            ops->on_data(ops->user_data, resp_body, strlen(resp_body));
        }
        if (ops->on_end) {
            ops->on_end(ops->user_data, 0);
        }
    }

    /* Free malloc'd header/body copies (callbacks have consumed them). */
    free(owned_headers);
    free(owned_body);
}

/* The mock PAL delivers stream responses synchronously, so there is never
 * an in-flight request to abort. No-op. */
static void pal_mock_http_abort(qwrt_pal_t *pal)
{
    (void)pal;
}

/* ---- Filesystem ---- */

static void pal_mock_fs_read(qwrt_pal_t *pal, const char *path,
                              qwrt_pal_cb_t cb, void *cb_data)
{
    pal_mock_t *m = pal_to_mock(pal);
    mock_kv_entry_t *entry;

    entry = mock_kv_find(&m->fs_map, path);
    if (entry) {
        cb(cb_data, 0, entry->value, strlen(entry->value));
    } else {
        const char *err = "not found";
        cb(cb_data, QWRT_ERR_NOT_FOUND, err, strlen(err));
    }
}

static void pal_mock_fs_write(qwrt_pal_t *pal, const char *path,
                               const char *data, size_t data_len,
                               qwrt_pal_cb_t cb, void *cb_data)
{
    pal_mock_t *m = pal_to_mock(pal);
    char *tmp;
    const char *err;

    /* null-terminate the data for storage */
    tmp = (char *)malloc(data_len + 1);
    if (!tmp) {
        err = "allocation failed";
        cb(cb_data, QWRT_ERR_GENERIC, err, strlen(err));
        return;
    }
    memcpy(tmp, data, data_len);
    tmp[data_len] = '\0';

    if (mock_kv_set(&m->fs_map, path, tmp) != 0) {
        free(tmp);
        err = "store failed";
        cb(cb_data, QWRT_ERR_GENERIC, err, strlen(err));
        return;
    }
    free(tmp);

    cb(cb_data, 0, "ok", 2);
}

static void pal_mock_fs_exists(qwrt_pal_t *pal, const char *path,
                                qwrt_pal_cb_t cb, void *cb_data)
{
    pal_mock_t *m = pal_to_mock(pal);
    mock_kv_entry_t *entry;

    entry = mock_kv_find(&m->fs_map, path);
    if (entry) {
        cb(cb_data, 0, "true", 4);
    } else {
        cb(cb_data, 0, "false", 5);
    }
}

static void pal_mock_fs_remove(qwrt_pal_t *pal, const char *path,
                                qwrt_pal_cb_t cb, void *cb_data)
{
    pal_mock_t *m = pal_to_mock(pal);
    int result;

    result = mock_kv_del(&m->fs_map, path);
    if (result == 0) {
        cb(cb_data, 0, "ok", 2);
    } else {
        const char *err = "not found";
        cb(cb_data, QWRT_ERR_NOT_FOUND, err, strlen(err));
    }
}

static void pal_mock_fs_list(qwrt_pal_t *pal, const char *path,
                              qwrt_pal_cb_t cb, void *cb_data)
{
    pal_mock_t *m = pal_to_mock(pal);
    int i;
    size_t path_len;
    int first;
    /* Build a JSON array of paths that start with the given prefix.
       Use a dynamic buffer approach. */
    size_t buf_size;
    size_t buf_used;
    char *buf;

    path_len = path ? strlen(path) : 0;

    /* Start with a reasonable buffer */
    buf_size = 1024;
    buf = (char *)malloc(buf_size);
    if (!buf) {
        const char *err = "allocation failed";
        cb(cb_data, QWRT_ERR_GENERIC, err, strlen(err));
        return;
    }

    buf_used = 0;
    buf[buf_used++] = '[';
    first = 1;

    for (i = 0; i < m->fs_map.count; i++) {
        const char *key = m->fs_map.entries[i].key;
        size_t key_len;
        size_t needed;

        if (!key) continue;
        if (path_len > 0 && strncmp(key, path, path_len) != 0) continue;

        key_len = strlen(key);
        /* estimate: comma + quote + key + quote */
        needed = (first ? 0 : 1) + 1 + key_len + 1;

        /* grow buffer if needed */
        if (buf_used + needed + 2 > buf_size) {
            char *new_buf = (char *)realloc(buf, (buf_used + needed + 2) * 2);
            if (!new_buf) {
                const char *err = "allocation failed";
                free(buf);
                cb(cb_data, QWRT_ERR_GENERIC, err, strlen(err));
                return;
            }
            buf = new_buf;
            buf_size = (buf_used + needed + 2) * 2;
        }

        if (!first) {
            buf[buf_used++] = ',';
        }
        first = 0;

        buf[buf_used++] = '"';
        memcpy(buf + buf_used, key, key_len);
        buf_used += key_len;
        buf[buf_used++] = '"';
    }

    buf[buf_used++] = ']';
    buf[buf_used] = '\0';

    cb(cb_data, 0, buf, buf_used);
    free(buf);
}

/* ---- Storage ---- */

static void pal_mock_storage_get(qwrt_pal_t *pal, const char *key,
                                  qwrt_pal_cb_t cb, void *cb_data)
{
    pal_mock_t *m = pal_to_mock(pal);
    mock_kv_entry_t *entry;

    entry = mock_kv_find(&m->store_map, key);
    if (entry) {
        cb(cb_data, 0, entry->value, strlen(entry->value));
    } else {
        const char *err = "not found";
        cb(cb_data, QWRT_ERR_NOT_FOUND, err, strlen(err));
    }
}

static void pal_mock_storage_set(qwrt_pal_t *pal, const char *key,
                                  const char *value, size_t value_len,
                                  qwrt_pal_cb_t cb, void *cb_data)
{
    pal_mock_t *m = pal_to_mock(pal);
    char *tmp;
    const char *err;

    /* null-terminate for storage */
    tmp = (char *)malloc(value_len + 1);
    if (!tmp) {
        err = "allocation failed";
        cb(cb_data, QWRT_ERR_GENERIC, err, strlen(err));
        return;
    }
    memcpy(tmp, value, value_len);
    tmp[value_len] = '\0';

    if (mock_kv_set(&m->store_map, key, tmp) != 0) {
        free(tmp);
        err = "store failed";
        cb(cb_data, QWRT_ERR_GENERIC, err, strlen(err));
        return;
    }
    free(tmp);

    cb(cb_data, 0, "ok", 2);
}

static void pal_mock_storage_del(qwrt_pal_t *pal, const char *key,
                                  qwrt_pal_cb_t cb, void *cb_data)
{
    pal_mock_t *m = pal_to_mock(pal);
    int result;

    result = mock_kv_del(&m->store_map, key);
    if (result == 0) {
        cb(cb_data, 0, "ok", 2);
    } else {
        const char *err = "not found";
        cb(cb_data, QWRT_ERR_NOT_FOUND, err, strlen(err));
    }
}

/* ---- Timer ---- */

static void *pal_mock_timer_start(qwrt_pal_t *pal, uint64_t delay_ms,
                                   int repeat,
                                   qwrt_pal_cb_t cb, void *cb_data)
{
    pal_mock_t *m = pal_to_mock(pal);
    int idx;

    (void)delay_ms;
    (void)repeat;

    if (m->timer_count >= MOCK_TIMER_MAX) {
        return NULL;
    }

    idx = m->timer_count;
    m->timers[idx].active = 1;
    m->timers[idx].cb = cb;
    m->timers[idx].cb_data = cb_data;
    m->timer_next_id++;
    m->timers[idx].handle_id = m->timer_next_id;
    m->timer_count++;

    /* Return handle as (void*)(intptr_t)handle_id.
       The handle_idx is (timer_count - 1), but callers use handle_id.
       We map back via linear scan in fire_timer. */
    return (void *)(intptr_t)m->timers[idx].handle_id;
}

static void pal_mock_timer_stop(qwrt_pal_t *pal, void *handle)
{
    pal_mock_t *m = pal_to_mock(pal);
    int handle_id;
    int i;

    if (!handle) return;

    handle_id = (int)(intptr_t)handle;

    for (i = 0; i < m->timer_count; i++) {
        if (m->timers[i].handle_id == handle_id) {
            m->timers[i].active = 0;
            return;
        }
    }
}

/* ---- Time ---- */

static uint64_t pal_mock_time_now(qwrt_pal_t *pal)
{
    pal_mock_t *m = pal_to_mock(pal);
    return m->time_ms;
}

static uint64_t pal_mock_hrtime(qwrt_pal_t *pal)
{
    pal_mock_t *m = pal_to_mock(pal);
    /* Return mock time in nanoseconds (time_ms is in milliseconds) */
    return m->time_ms * 1000000ULL;
}

/* ---- Log ---- */

static void pal_mock_log(qwrt_pal_t *pal, int level, const char *msg)
{
    pal_mock_t *m = pal_to_mock(pal);

    if (m->log_count >= MOCK_LOG_MAX) return;

    m->log_entries[m->log_count].level = level;
    m->log_entries[m->log_count].msg = msg ? strdup(msg) : NULL;
    m->log_count++;
}

/* ---- Memory ---- */

static void *pal_mock_mem_alloc(qwrt_pal_t *pal, size_t size)
{
    pal_mock_t *m = pal_to_mock(pal);
    void *ptr;

    ptr = malloc(size);
    if (ptr) {
        m->alloc_count++;
    }
    return ptr;
}

static void pal_mock_mem_free(qwrt_pal_t *pal, void *ptr)
{
    pal_mock_t *m = pal_to_mock(pal);

    if (ptr) {
        m->alloc_count--;
        free(ptr);
    }
}

static void pal_mock_random_bytes(qwrt_pal_t *pal, uint8_t *buf, size_t len)
{
    (void)pal;
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
        /* Read may return fewer bytes (rare for /dev/urandom); fill what we
         * can and zero the rest so callers always get deterministic output. */
        size_t got = fread(buf, 1, len, f);
        if (got < len) {
            memset(buf + got, 0, len - got);
        }
        fclose(f);
    } else {
        memset(buf, 0, len);
    }
}

/* ================================================================
 * Denied PAL functions — always return permission error
 * ================================================================ */

static void pal_denied_cb(qwrt_pal_cb_t cb, void *cb_data, const char *capability)
{
    char err[128];
    int len = snprintf(err, sizeof(err), "Permission denied: no %s access", capability);
    cb(cb_data, 1, err, len);
}

static void pal_denied_http_request(qwrt_pal_t *pal, const char *url, const char *method,
                                     const char *headers, const char *body, size_t body_len,
                                     qwrt_pal_cb_t cb, void *cb_data)
{
    (void)pal; (void)url; (void)method; (void)headers; (void)body; (void)body_len;
    pal_denied_cb(cb, cb_data, "HTTP");
}

static void pal_denied_fs_write(qwrt_pal_t *pal, const char *path, const char *data, size_t data_len,
                                 qwrt_pal_cb_t cb, void *cb_data)
{
    (void)pal; (void)path; (void)data; (void)data_len;
    pal_denied_cb(cb, cb_data, "filesystem write");
}

static void pal_denied_fs_remove(qwrt_pal_t *pal, const char *path, qwrt_pal_cb_t cb, void *cb_data)
{
    (void)pal; (void)path;
    pal_denied_cb(cb, cb_data, "filesystem remove");
}

/* ================================================================
 * Public API: create / destroy
 * ================================================================ */

qwrt_pal_t *pal_mock_create(void)
{
    pal_mock_t *m;

    m = (pal_mock_t *)calloc(1, sizeof(pal_mock_t));
    if (!m) return NULL;

    mock_kv_init(&m->fs_map);
    mock_kv_init(&m->store_map);

    m->timer_count = 0;
    m->timer_next_id = 0;
    m->log_count = 0;
    m->time_ms = 0;
    m->alloc_count = 0;
    m->http_response = NULL;

    m->last_http_url = NULL;
    m->last_http_method = NULL;
    m->last_http_headers = NULL;
    m->last_http_body = NULL;

    m->log_buf = NULL;
    m->log_buf_size = 0;

    /* Set up PAL function pointers */
    m->pal.user_data    = NULL;  /* not used; we derive via offsetof */
    m->pal.version      = 1;
    m->pal.name         = "mock";

    m->pal.http_request = pal_mock_http_request;
    m->pal.http_request_stream = pal_mock_http_request_stream;
    m->pal.http_abort = pal_mock_http_abort;

    m->pal.fs_read      = pal_mock_fs_read;
    m->pal.fs_write     = pal_mock_fs_write;
    m->pal.fs_exists    = pal_mock_fs_exists;
    m->pal.fs_remove    = pal_mock_fs_remove;
    m->pal.fs_list      = pal_mock_fs_list;

    m->pal.storage_get  = pal_mock_storage_get;
    m->pal.storage_set  = pal_mock_storage_set;
    m->pal.storage_del  = pal_mock_storage_del;

    m->pal.timer_start  = pal_mock_timer_start;
    m->pal.timer_stop   = pal_mock_timer_stop;

    m->pal.time_now     = pal_mock_time_now;
    m->pal.hrtime       = pal_mock_hrtime;
    m->pal.log          = pal_mock_log;
    m->pal.mem_alloc    = pal_mock_mem_alloc;
    m->pal.mem_free     = pal_mock_mem_free;
    m->pal.random_bytes = pal_mock_random_bytes;

    /* Lifecycle — all setup is done in pal_mock_create(),
     * teardown is done via pal_mock_destroy() by the embedder. */
    m->pal.init    = NULL;
    m->pal.destroy = NULL;
    memset(m->pal.reserved, 0, sizeof(m->pal.reserved));

    return &m->pal;
}

void pal_mock_destroy(qwrt_pal_t *pal)
{
    pal_mock_t *m;
    int i;

    if (!pal) return;
    m = pal_to_mock(pal);

    /* Free KV maps */
    mock_kv_clear(&m->fs_map);
    mock_kv_clear(&m->store_map);

    /* Free log entries */
    for (i = 0; i < m->log_count; i++) {
        free(m->log_entries[i].msg);
    }

    /* Free HTTP response override */
    free(m->http_response);

    /* Free last HTTP request details */
    free(m->last_http_url);
    free(m->last_http_method);
    free(m->last_http_headers);
    free(m->last_http_body);

    /* Free per-instance log buffer */
    free(m->log_buf);

    free(m);
}

/* ================================================================
 * Test helpers
 * ================================================================ */

void pal_mock_set_time(qwrt_pal_t *pal, uint64_t ms)
{
    pal_mock_t *m;

    if (!pal) return;
    m = pal_to_mock(pal);
    m->time_ms = ms;
}

uint64_t pal_mock_get_time(qwrt_pal_t *pal)
{
    pal_mock_t *m;

    if (!pal) return 0;
    m = pal_to_mock(pal);
    return m->time_ms;
}

void pal_mock_fire_timer(qwrt_pal_t *pal, int handle_id)
{
    pal_mock_t *m;
    int i;

    if (!pal) return;
    m = pal_to_mock(pal);

    for (i = 0; i < m->timer_count; i++) {
        if (m->timers[i].handle_id == handle_id && m->timers[i].active && m->timers[i].cb) {
            m->timers[i].cb(m->timers[i].cb_data, 0, NULL, 0);
            return;
        }
    }
}

void pal_mock_fire_all_timers(qwrt_pal_t *pal)
{
    pal_mock_t *m;
    int i;

    if (!pal) return;
    m = pal_to_mock(pal);

    for (i = 0; i < m->timer_count; i++) {
        if (m->timers[i].active && m->timers[i].cb) {
            m->timers[i].cb(m->timers[i].cb_data, 0, NULL, 0);
        }
    }
}

const char *pal_mock_get_log(qwrt_pal_t *pal, int *count)
{
    pal_mock_t *m;
    char *buf;
    size_t buf_size;
    size_t buf_used;
    int i;

    if (!pal) {
        if (count) *count = 0;
        return NULL;
    }
    m = pal_to_mock(pal);

    if (count) *count = m->log_count;

    if (m->log_count == 0) return "";

    /* Build a combined log string: "level:msg\nlevel:msg\n..." */
    buf_size = 4096;
    buf = (char *)realloc(m->log_buf, buf_size);
    if (!buf) return "";
    m->log_buf = buf;
    m->log_buf_size = buf_size;

    buf_used = 0;
    for (i = 0; i < m->log_count; i++) {
        int written;
        const char *msg;
        size_t remaining;
        int level;

        level = m->log_entries[i].level;
        msg = m->log_entries[i].msg ? m->log_entries[i].msg : "";
        remaining = buf_size - buf_used;

        written = snprintf(buf + buf_used, remaining, "%d:%s\n", level, msg);

        if (written < 0) break;

        if ((size_t)written >= remaining) {
            /* grow buffer */
            buf_size = (buf_size + written + 1) * 2;
            buf = (char *)realloc(m->log_buf, buf_size);
            if (!buf) return "";
            m->log_buf = buf;
            m->log_buf_size = buf_size;

            remaining = buf_size - buf_used;
            written = snprintf(buf + buf_used, remaining, "%d:%s\n",
                               level, msg);
            if (written < 0 || (size_t)written >= remaining) break;
        }

        buf_used += (size_t)written;
    }

    if (buf_used > 0 && buf[buf_used - 1] == '\n') {
        buf[buf_used - 1] = '\0';
    } else {
        buf[buf_used] = '\0';
    }

    return buf;
}

void pal_mock_clear_log(qwrt_pal_t *pal)
{
    pal_mock_t *m;
    int i;

    if (!pal) return;
    m = pal_to_mock(pal);

    for (i = 0; i < m->log_count; i++) {
        free(m->log_entries[i].msg);
        m->log_entries[i].msg = NULL;
        m->log_entries[i].level = 0;
    }
    m->log_count = 0;
}

void pal_mock_set_http_response(qwrt_pal_t *pal, const char *json_response)
{
    pal_mock_t *m;

    if (!pal) return;
    m = pal_to_mock(pal);

    free(m->http_response);
    m->http_response = json_response ? strdup(json_response) : NULL;
}

int pal_mock_get_alloc_count(qwrt_pal_t *pal)
{
    pal_mock_t *m;

    if (!pal) return 0;
    m = pal_to_mock(pal);
    return m->alloc_count;
}

/* ================================================================
 * Permission-restricted PAL factories
 * ================================================================ */

qwrt_pal_t *pal_mock_create_readonly_fs(void)
{
    qwrt_pal_t *pal = pal_mock_create();
    if (!pal) return NULL;
    pal->fs_write = pal_denied_fs_write;
    pal->fs_remove = pal_denied_fs_remove;
    return pal;
}

qwrt_pal_t *pal_mock_create_no_http(void)
{
    qwrt_pal_t *pal = pal_mock_create();
    if (!pal) return NULL;
    pal->http_request = pal_denied_http_request;
    return pal;
}

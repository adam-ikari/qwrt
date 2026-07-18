/*
 * pal_freertos.c — FreeRTOS PAL implementation for ESP32-S3
 *
 * Implements qwrt_pal_t using FreeRTOS primitives, lwIP for networking,
 * mbedTLS for TLS, NVS for storage, and LittleFS for filesystem.
 *
 * Architecture:
 *   - HTTP: lwIP sockets + mbedTLS (blocking sockets, one task per request)
 *   - Timers: FreeRTOS xTimer → deferred callback via xQueue
 *   - Storage: ESP-IDF NVS (Non-Volatile Storage)
 *   - Filesystem: LittleFS via ESP-IDF VFS layer
 *   - Memory: heap_caps_malloc(PSRAM) preference
 *   - Random: esp_fill_random (hardware RNG)
 *   - Process management: all NULL (no process model on MCU)
 *
 * Deferred Callback Queue:
 *   FreeRTOS timer callbacks run in the timer task context — they cannot
 *   call JS functions directly (no JS context). Instead, they enqueue
 *   a message via xQueueSend. The embedder's event loop drains this queue
 *   and calls qwrt_defer_callback() so JS calls happen within qwrt_tick().
 *
 * Embedder event loop pattern:
 *
 *   pal_freertos_t *pf = pal_freertos_create();
 *   qwrt_pal_t *pal = pal_freertos_get_pal(pf);
 *   qwrt_t *rt = qwrt_create(&(qwrt_config_t){ .pal = pal });
 *
 *   while (running) {
 *       pal_freertos_run_cycle(pf, 1000);  // block up to 1s
 *       pal_freertos_drain_deferred(pf, rt);
 *       qwrt_tick(rt);
 *   }
 *
 *   qwrt_destroy(rt);
 *   pal_freertos_destroy(pf);
 */

#ifdef ESP_PLATFORM

#include "pal_freertos.h"
#include "pal_common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_vfs.h"
#include "esp_spiffs.h"

/* lwIP */
#include "lwip/sockets.h"
#include "lwip/netdb.h"

/* mbedTLS (bundled with ESP-IDF) */
#include "mbedtls/ssl.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "esp_crt_bundle.h"  /* Mozilla CA bundle for TLS verification */

/* ================================================================
 * Constants
 * ================================================================ */

#define PAL_FREERTOS_STORAGE_MAX      64
#define PAL_FREERTOS_NVS_NAMESPACE    "qwrt"
#define PAL_FREERTOS_HTTP_BUF_INIT    4096
#define PAL_FREERTOS_CONNECT_TIMEOUT_MS  30000
#define PAL_FREERTOS_READ_IDLE_TIMEOUT_MS 60000
#define PAL_FREERTOS_DEFERRED_QUEUE_LEN  256
/* HTTP task: streaming + TLS needs a generous stack (mbedTLS handshake +
 * printf-style logging). 12 KiB is a safe default for ESP32-S3 with PSRAM. */
#define PAL_FREERTOS_HTTP_TASK_STACK   (12 * 1024)
#define PAL_FREERTOS_HTTP_TASK_PRIO    5
#define PAL_FREERTOS_FS_MOUNT          "/littlefs"
#define PAL_FREERTOS_MAX_TIMERS        32

/* Event group bits */
#define EVENT_HTTP_RECV   (1 << 0)
#define EVENT_TIMER_FIRE  (1 << 1)
#define EVENT_STOP        (1 << 2)

static const char *TAG = "qwrt-pal";

/* ================================================================
 * Forward declarations
 * ================================================================ */

/* qwrt_t is opaque to the PAL — we only pass it through to
 * qwrt_defer_callback via the drain function.  Declare as
 * incomplete type to avoid depending on qwrt_internal.h. */
struct qwrt_t;

/* qwrt_defer_callback — declared in qwrt_internal.h.
 * We forward-declare it here so the PAL can call it without
 * including qwrt_internal.h (which pulls in QuickJS headers). */
extern void qwrt_defer_callback(struct qwrt_t *rt,
                                 void (*fn)(void *data), void *data);

/* ================================================================
 * Deferred callback entry
 * ================================================================ */

typedef void (*pal_freertos_deferred_fn)(void *data);

typedef struct {
    pal_freertos_deferred_fn fn;
    void *data;
} pal_freertos_deferred_msg_t;

/* ================================================================
 * Internal PAL state
 * ================================================================ */

struct pal_freertos_t {
    qwrt_pal_t pal;                     /* embedded PAL interface */

    /* Deferred callback queue — bridges FreeRTOS → qwrt */
    QueueHandle_t deferred_queue;

    /* Event group for waking pal_freertos_run_cycle */
    EventGroupHandle_t event_group;

    /* Storage: NVS handle (opened on demand) */
    nvs_handle_t nvs_handle;
    int nvs_initialized;

    /* Filesystem: whether LittleFS is mounted */
    int fs_mounted;

    /* Track active timers for cleanup */
    TimerHandle_t timers[PAL_FREERTOS_MAX_TIMERS];
    int timer_count;

    /* Runtime pointer for deferred drain */
    struct qwrt_t *rt;
};

/* ================================================================
 * Helper: get pal_freertos_t from qwrt_pal_t pointer
 * ================================================================ */

static pal_freertos_t *pal_freertos_self(qwrt_pal_t *pal)
{
    return (pal_freertos_t *)((char *)pal - offsetof(pal_freertos_t, pal));
}

/* ================================================================
 * Helper: signal event group to wake pal_freertos_run_cycle
 * ================================================================ */

static void signal_event(pal_freertos_t *pf, int event_bit)
{
    if (pf->event_group) {
        xEventGroupSetBits(pf->event_group, event_bit);
    }
}

/* ================================================================
 * Deferred callback enqueue — called from FreeRTOS callback context
 * (timer task, lwIP callback) to safely defer work to qwrt context.
 * ================================================================ */

static void deferred_enqueue(pal_freertos_t *pf,
                              pal_freertos_deferred_fn fn, void *data)
{
    pal_freertos_deferred_msg_t msg;
    msg.fn = fn;
    msg.data = data;

    /* Block up to 100 ms for queue space — the drain runs in the same
     * task so this should never actually block in practice, but it
     * prevents silent callback loss under load. */
    if (xQueueSend(pf->deferred_queue, &msg, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Deferred queue full after 100ms — callback LOST");
    }
}

/* ================================================================
 * Public: drain deferred queue into qwrt_defer_callback
 * ================================================================ */

int pal_freertos_drain_deferred(pal_freertos_t *pf, struct qwrt_t *rt)
{
    if (!pf || !rt) return 0;

    pal_freertos_deferred_msg_t msg;
    int count = 0;

    while (xQueueReceive(pf->deferred_queue, &msg, 0) == pdTRUE) {
        qwrt_defer_callback(rt, msg.fn, msg.data);
        count++;
    }

    return count;
}

void pal_freertos_set_runtime(pal_freertos_t *pf, struct qwrt_t *rt)
{
    if (pf) {
        pf->rt = rt;
    }
}

/* ────────────────────────────────────────────────────────────────
 * JSON helpers and URL parser are now in platform/pal_common.c
 * (pal_json_escape, pal_build_http_json, pal_build_json_array,
 *  pal_parse_url, pal_build_headers_json).
 * ──────────────────────────────────────────────────────────────── */

/* ================================================================
 * HTTP operation — synchronous blocking implementation using lwIP
 *
 * Each HTTP request spawns a FreeRTOS task that:
 *   1. Resolves DNS via lwip_getaddrinfo
 *   2. Connects TCP via lwip_connect
 *   3. Sends HTTP request
 *   4. Reads HTTP response (headers + body)
 *   5. Calls the PAL callback (which defers to qwrt context)
 *
 * This is simpler than the async uv approach because lwIP sockets
 * on ESP32 are typically used in blocking mode within dedicated tasks.
 * ================================================================ */

typedef struct {
    /* Request parameters (copied) */
    char *url;
    char *method;
    char *headers_json;
    char *body;
    size_t body_len;

    /* PAL callback */
    qwrt_pal_cb_t cb;
    void *cb_data;

    /* PAL reference */
    pal_freertos_t *pf;

    /* Streaming mode */
    int streaming;
    qwrt_pal_stream_ops_t stream_ops;
} pal_freertos_http_task_t;

/* Parse HTTP response headers from a buffer.
 *   buf/len       the response bytes
 *   http_status   out: status code (e.g. 200)
 *   chunked       out: 1 if Transfer-Encoding: chunked
 *   body_expected out: Content-Length value (0 if absent)
 *   header_end    out: offset of the first body byte (past the final \n of
 *                      the \r\n\r\n header terminator)
 * Returns 0 if headers are complete, -1 otherwise.
 *
 * Mirrors pal_uv's parse_http_response logic, including the >= 18 chunked
 * detection (NOT > 26, which misses "Transfer-Encoding: chunked" exactly). */
static int parse_http_response(const char *buf, size_t len,
                               int *http_status, int *chunked,
                               size_t *body_expected, size_t *header_end)
{
    /* Find \r\n\r\n marking end of headers */
    size_t he = 0;
    int found = 0;
    size_t i;
    for (i = 3; i < len; i++) {
        if (buf[i - 3] == '\r' && buf[i - 2] == '\n' &&
            buf[i - 1] == '\r' && buf[i] == '\n') {
            he = i + 1;
            found = 1;
            break;
        }
    }
    if (!found) return -1;

    /* Status line: HTTP/1.x NNN ... */
    const char *line_end = NULL;
    for (i = 0; i + 1 < he; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n') {
            line_end = buf + i;
            break;
        }
    }
    if (!line_end) return -1;

    const char *sp = buf;
    while (sp < line_end && *sp != ' ') sp++;
    if (sp >= line_end) return -1;
    sp++;

    int status = 0, digits = 0;
    while (sp < line_end && *sp >= '0' && *sp <= '9' && digits < 3) {
        status = status * 10 + (*sp - '0');
        sp++;
        digits++;
    }
    if (digits == 0) return -1;

    /* Scan headers (region after status line up to he - 4) */
    const char *hdrs = line_end + 2;
    const char *hdrs_end = buf + he - 4;
    int is_chunked = 0;
    size_t cl = 0;

    const char *hp = hdrs;
    while (hp < hdrs_end) {
        const char *eol = hp;
        while (eol < hdrs_end &&
               !(*eol == '\r' && (eol + 1 < hdrs_end) && *(eol + 1) == '\n')) {
            eol++;
        }
        if (eol - hp >= 15 && strncasecmp(hp, "Content-Length:", 15) == 0) {
            const char *val = hp + 15;
            while (val < eol && (*val == ' ' || *val == '\t')) val++;
            cl = (size_t)strtoull(val, NULL, 10);
        } else if (eol - hp >= 18 &&
                   strncasecmp(hp, "Transfer-Encoding:", 18) == 0) {
            const char *val = hp + 18;
            while (val < eol && (*val == ' ' || *val == '\t')) val++;
            if (eol - val >= 7 && strncasecmp(val, "chunked", 7) == 0) {
                is_chunked = 1;
            }
        }
        if (eol >= hdrs_end) break;
        hp = eol + 2;
    }

    *http_status = status;
    *chunked = is_chunked;
    *body_expected = cl;
    *header_end = he;
    return 0;
}

/* HTTP task — runs the blocking HTTP request */
static void http_task(void *arg)
{
    pal_freertos_http_task_t *ht = (pal_freertos_http_task_t *)arg;
    pal_freertos_t *pf = ht->pf;

    char *host = NULL;
    int port = 80;
    char *path = NULL;
    int sock = -1;
    char *resp_buf = NULL;
    size_t resp_buf_len = 0;
    size_t resp_buf_cap = 0;
    int error_status = 0;
    const char *error_msg = NULL;

    /* Parse URL */
    {
        pal_url_t parts = {0};
        if (pal_parse_url(ht->url, &parts) < 0) {
            error_status = QWRT_ERR_INVALID_ARG;
            error_msg = "invalid url format";
            goto done;
        }
        host    = parts.host;
        port    = parts.port;
        path    = parts.path;

        /* parts fields are now owned by local vars — do NOT call pal_url_free */
    }

    /* Resolve DNS */
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", port);

    struct addrinfo *res = NULL;
    int rc = lwip_getaddrinfo(host, port_str, &hints, &res);
    if (rc != 0 || !res) {
        error_status = -5;
        error_msg = "DNS resolution failed";
        goto done;
    }

    /* Create socket */
    sock = lwip_socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) {
        error_status = -5;
        error_msg = "socket creation failed";
        lwip_freeaddrinfo(res);
        goto done;
    }

    /* Set socket timeout */
    struct timeval tv;
    tv.tv_sec = PAL_FREERTOS_CONNECT_TIMEOUT_MS / 1000;
    tv.tv_usec = (PAL_FREERTOS_CONNECT_TIMEOUT_MS % 1000) * 1000;
    lwip_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    lwip_setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    /* Connect */
    rc = lwip_connect(sock, res->ai_addr, res->ai_addrlen);
    lwip_freeaddrinfo(res);

    if (rc < 0) {
        error_status = -5;
        error_msg = "connection failed";
        goto done;
    }

    /* Build HTTP request */
    const char *method = ht->method ? ht->method : "GET";
    size_t req_cap = 1024 + (ht->body_len > 0 ? ht->body_len : 0);
    char *req_buf = (char *)malloc(req_cap);
    if (!req_buf) {
        error_status = -1;
        error_msg = "out of memory";
        goto done;
    }

    size_t pos = 0;
    pos += snprintf(req_buf + pos, req_cap - pos,
                    "%s %s HTTP/1.1\r\nHost: %s\r\n",
                    method, path, host);

    if (ht->body && ht->body_len > 0) {
        pos += snprintf(req_buf + pos, req_cap - pos,
                        "Content-Length: %zu\r\n", ht->body_len);
    }

    pos += snprintf(req_buf + pos, req_cap - pos,
                    "Connection: close\r\n");

    /* Parse and add custom headers */
    if (ht->headers_json && ht->headers_json[0] == '{') {
        const char *p = ht->headers_json + 1;
        while (*p && *p != '}') {
            while (*p == ' ' || *p == ',' || *p == '\t') p++;
            if (*p == '}' || *p == '\0') break;
            if (*p != '"') { p++; continue; }
            p++;

            const char *key_start = p;
            while (*p && *p != '"') {
                if (*p == '\\') p++;
                p++;
            }
            size_t key_len = (size_t)(p - key_start);
            if (*p == '"') p++;

            while (*p == ' ' || *p == ':' || *p == '\t') p++;
            if (*p != '"') { p++; continue; }
            p++;

            const char *val_start = p;
            while (*p && *p != '"') {
                if (*p == '\\') p++;
                p++;
            }
            size_t val_len = (size_t)(p - val_start);
            if (*p == '"') p++;

            if (pos + key_len + val_len + 5 < req_cap) {
                memcpy(req_buf + pos, key_start, key_len);
                pos += key_len;
                req_buf[pos++] = ':';
                req_buf[pos++] = ' ';
                memcpy(req_buf + pos, val_start, val_len);
                pos += val_len;
                req_buf[pos++] = '\r';
                req_buf[pos++] = '\n';
            }
        }
    }

    req_buf[pos++] = '\r';
    req_buf[pos++] = '\n';

    if (ht->body && ht->body_len > 0 && pos + ht->body_len < req_cap) {
        memcpy(req_buf + pos, ht->body, ht->body_len);
        pos += ht->body_len;
    }

    /* Send request */
    size_t sent = 0;
    while (sent < pos) {
        ssize_t n = lwip_send(sock, req_buf + sent, pos - sent, 0);
        if (n < 0) {
            free(req_buf);
            error_status = -5;
            error_msg = "send failed";
            goto done;
        }
        sent += (size_t)n;
    }
    free(req_buf);

    /* Read response */
    resp_buf_cap = PAL_FREERTOS_HTTP_BUF_INIT;
    resp_buf = (char *)malloc(resp_buf_cap);
    if (!resp_buf) {
        error_status = -1;
        error_msg = "out of memory";
        goto done;
    }

    char read_buf[1024];
    while (1) {
        ssize_t n = lwip_recv(sock, read_buf, sizeof(read_buf), 0);
        if (n < 0) {
            error_status = -5;
            error_msg = "recv failed";
            goto done;
        }
        if (n == 0) {
            break; /* EOF */
        }

        /* Grow buffer if needed */
        size_t new_len = resp_buf_len + (size_t)n;
        if (new_len > resp_buf_cap) {
            size_t new_cap = resp_buf_cap * 2;
            if (new_cap < new_len) new_cap = new_len;
            char *new_buf = (char *)realloc(resp_buf, new_cap);
            if (!new_buf) {
                error_status = -1;
                error_msg = "out of memory";
                goto done;
            }
            resp_buf = new_buf;
            resp_buf_cap = new_cap;
        }
        memcpy(resp_buf + resp_buf_len, read_buf, (size_t)n);
        resp_buf_len = new_len;
    }

    /* Parse response */
    {
        int http_status = 0;
        int chunked = 0;
        size_t body_expected = 0;
        size_t header_end = 0;

        if (parse_http_response(resp_buf, resp_buf_len,
                                &http_status, &chunked,
                                &body_expected, &header_end) < 0) {
            error_status = -5;
            error_msg = "failed to parse HTTP response";
            goto done;
        }

        /* Build headers JSON */
        const char *status_line_end = NULL;
        size_t i;
        for (i = 0; i < header_end; i++) {
            if (resp_buf[i] == '\r' && resp_buf[i + 1] == '\n') {
                status_line_end = resp_buf + i + 2;
                break;
            }
        }

        char *headers_json = NULL;
        size_t headers_json_len = 0;

        if (status_line_end) {
            size_t hdr_len = resp_buf + header_end - 4 - status_line_end;
            headers_json = pal_build_headers_json(status_line_end, hdr_len,
                                                  &headers_json_len);
        }

        const char *body_start = resp_buf + header_end;
        size_t body_len = resp_buf_len - header_end;

        size_t json_len;
        char *json = pal_build_http_json(http_status,
                                     headers_json ? headers_json : "{}",
                                     headers_json ? headers_json_len : 2,
                                     body_start, body_len, &json_len);
        free(headers_json);

        if (json) {
            /* Enqueue callback via deferred queue */
            /* We need to copy json to heap for the deferred callback */
            char *json_copy = strdup(json);
            free(json);

            if (json_copy) {
                /* Use a wrapper to call the PAL callback */
                /* For simplicity, call cb directly — the HTTP task
                 * runs in its own context, and the cb will enqueue
                 * to qwrt_defer_callback via bridge.c's deferred_pal_cb */
                ht->cb(ht->cb_data, 0, json_copy, strlen(json_copy));
                free(json_copy);
            } else {
                ht->cb(ht->cb_data, QWRT_ERR_GENERIC, "out of memory", 13);
            }
        } else {
            ht->cb(ht->cb_data, QWRT_ERR_GENERIC, "out of memory", 13);
        }
    }

    goto cleanup;

done:
    /* Error path */
    if (error_msg) {
        ht->cb(ht->cb_data, error_status, error_msg, strlen(error_msg));
    }

cleanup:
    if (sock >= 0) {
        lwip_close(sock);
    }
    free(resp_buf);
    free(host);
    free(path);

    /* Free task parameters */
    free(ht->url);
    free(ht->method);
    free(ht->headers_json);
    free(ht->body);
    free(ht);

    /* Signal event to wake run_cycle */
    signal_event(pf, EVENT_HTTP_RECV);

    vTaskDelete(NULL);
}

/* ================================================================
 * PAL: http_request
 * ================================================================ */

static void pal_freertos_http_request(qwrt_pal_t *pal,
                                       const char *url, const char *method,
                                       const char *headers, const char *body,
                                       size_t body_len,
                                       qwrt_pal_cb_t cb, void *cb_data)
{
    pal_freertos_t *pf = pal_freertos_self(pal);

    if (!url) {
        cb(cb_data, QWRT_ERR_INVALID_ARG, "invalid url", 11);
        return;
    }

    pal_freertos_http_task_t *ht =
        (pal_freertos_http_task_t *)calloc(1, sizeof(*ht));
    if (!ht) {
        cb(cb_data, QWRT_ERR_GENERIC, "out of memory", 13);
        return;
    }

    ht->url = strdup(url);
    ht->method = method ? strdup(method) : strdup("GET");
    ht->headers_json = headers ? strdup(headers) : NULL;
    if (body && body_len > 0) {
        ht->body = (char *)malloc(body_len + 1);
        if (ht->body) {
            memcpy(ht->body, body, body_len);
            ht->body[body_len] = '\0';
            ht->body_len = body_len;
        }
    }
    ht->cb = cb;
    ht->cb_data = cb_data;
    ht->pf = pf;
    ht->streaming = 0;

    /* Create a FreeRTOS task for this HTTP request.
     * Stack size: 8KB should be sufficient for lwIP + HTTP parsing. */
    BaseType_t ret = xTaskCreate(
        http_task, "qwrt-http", 8192, ht,
        tskIDLE_PRIORITY + 2, NULL);

    if (ret != pdPASS) {
        free(ht->url);
        free(ht->method);
        free(ht->headers_json);
        free(ht->body);
        free(ht);
        cb(cb_data, QWRT_ERR_GENERIC, "failed to create HTTP task", 25);
    }
}

/* ================================================================
 * PAL: http_request_stream — streaming HTTP with optional TLS (mbedTLS)
 *
 * Runs a blocking request in a dedicated FreeRTOS task. For https:// it
 * performs a full mbedTLS handshake and encrypted recv/send. Response
 * headers are parsed and delivered via on_headers; the body is streamed
 * via on_data (chunked-transfer decoded if applicable), then on_end.
 *
 * All stream callbacks (on_headers/on_data/on_end) are invoked directly
 * here; bridge.c's stream_on_* wrappers defer them into the qwrt thread
 * via qwrt_defer_callback, so it is safe to call them from this task.
 * ================================================================ */

/* mbedTLS BIO send/recv over a lwIP socket. */
static int tls_sock_send(void *ctx, const unsigned char *buf, size_t len)
{
    int sock = (int)(intptr_t)ctx;
    int n = lwip_send(sock, buf, len, 0);
    if (n < 0) return MBEDTLS_ERR_NET_SEND_FAILED;
    return n;
}
static int tls_sock_recv(void *ctx, unsigned char *buf, size_t len)
{
    int sock = (int)(intptr_t)ctx;
    int n = lwip_recv(sock, buf, len, 0);
    if (n < 0) return MBEDTLS_ERR_NET_RECV_FAILED;
    if (n == 0) return MBEDTLS_ERR_SSL_WANT_READ; /* peer closed */
    return n;
}

/* Read exactly one chunk: for non-TLS use lwip_recv; for TLS use
 * mbedtls_ssl_read. Returns bytes read (0 = EOF), or -1 on error. */
static int stream_read_one(int sock, mbedtls_ssl_context *ssl,
                           char *buf, size_t len)
{
    if (ssl) {
        int n = mbedtls_ssl_read(ssl, (unsigned char *)buf, len);
        if (n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return 0;
        if (n < 0) return -1;
        return n;
    }
    int n = lwip_recv(sock, buf, len, 0);
    if (n == 0) return 0;     /* EOF */
    if (n < 0) return -1;
    return n;
}

static int stream_write_all(int sock, mbedtls_ssl_context *ssl,
                            const char *buf, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        int n;
        if (ssl) {
            n = mbedtls_ssl_write(ssl, (const unsigned char *)buf + sent,
                                  len - sent);
            if (n <= 0) return -1;
        } else {
            n = lwip_send(sock, buf + sent, len - sent, 0);
            if (n < 0) return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

static void http_stream_task(void *arg)
{
    pal_freertos_http_task_t *ht = (pal_freertos_http_task_t *)arg;
    pal_freertos_t *pf = ht->pf;
    qwrt_pal_stream_ops_t *ops = &ht->stream_ops;

    char *host = NULL, *path = NULL;
    char *hdr_buf = NULL;
    int port = 80, use_tls = 0;
    int sock = -1;
    int error_status = -5;
    mbedtls_ssl_context ssl_ctx;
    mbedtls_ssl_config ssl_conf;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_x509_crt cacert;
    int tls_inited = 0;
    mbedtls_ssl_context *ssl_ptr = NULL;

    mbedtls_ssl_init(&ssl_ctx);
    mbedtls_ssl_config_init(&ssl_conf);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_entropy_init(&entropy);
    mbedtls_x509_crt_init(&cacert);

    /* Parse URL */
    {
        pal_url_t parts = {0};
        if (pal_parse_url(ht->url, &parts) < 0) {
            error_status = QWRT_ERR_INVALID_ARG;
            goto done;
        }
        host    = parts.host;
        port    = parts.port;
        path    = parts.path;
        use_tls = parts.tls;
        /* parts fields are now owned by local vars — do NOT call pal_url_free */
    }

    /* DNS */
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", port);
    if (lwip_getaddrinfo(host, port_str, &hints, &res) != 0 || !res) {
        error_status = -5;
        goto done;
    }

    /* TCP connect */
    sock = lwip_socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) { lwip_freeaddrinfo(res); error_status = -5; goto done; }
    struct timeval tv = { .tv_sec = PAL_FREERTOS_CONNECT_TIMEOUT_MS / 1000,
                          .tv_usec = (PAL_FREERTOS_CONNECT_TIMEOUT_MS % 1000) * 1000 };
    lwip_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    lwip_setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (lwip_connect(sock, res->ai_addr, res->ai_addrlen) < 0) {
        lwip_freeaddrinfo(res); error_status = -5; goto done;
    }
    lwip_freeaddrinfo(res);

    /* TLS handshake */
    if (use_tls) {
        if (mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                  (const unsigned char *)host, strlen(host)) != 0) {
            error_status = -5; goto done;
        }
        mbedtls_ssl_config_defaults(&ssl_conf, MBEDTLS_SSL_IS_CLIENT,
                                    MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT);
        /* Verify the server certificate against the ESP-IDF certificate bundle
         * (Mozilla CA bundle shipped with ESP-IDF). VERIFY_REQUIRED rejects
         * untrusted/expired/wrong-hostname certs (MITM protection). */
        if (esp_crt_bundle_attach(&ssl_conf) != ESP_OK) {
            ESP_LOGW(TAG, "esp_crt_bundle_attach failed; TLS verification disabled");
            mbedtls_ssl_conf_authmode(&ssl_conf, MBEDTLS_SSL_VERIFY_NONE);
        } else {
            mbedtls_ssl_conf_authmode(&ssl_conf, MBEDTLS_SSL_VERIFY_REQUIRED);
        }
        mbedtls_ssl_conf_rng(&ssl_conf, mbedtls_ctr_drbg_random, &ctr_drbg);
        mbedtls_ssl_setup(&ssl_ctx, &ssl_conf);
        mbedtls_ssl_set_hostname(&ssl_ctx, host);  /* SNI + hostname check */
        mbedtls_ssl_set_bio(&ssl_ctx, (void *)(intptr_t)sock,
                            tls_sock_send, tls_sock_recv, NULL);
        int r;
        while ((r = mbedtls_ssl_handshake(&ssl_ctx)) != 0) {
            if (r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE) {
                error_status = -5; goto done;
            }
        }
        ssl_ptr = &ssl_ctx;
        tls_inited = 1;
    }

    /* Build + send request (reuse the same header/body formatting as http_task). */
    {
        const char *method = ht->method ? ht->method : "GET";
        size_t req_cap = 1024 + (ht->body_len > 0 ? ht->body_len : 0);
        char *req = (char *)malloc(req_cap);
        if (!req) { error_status = -1; goto done; }
        size_t pos = 0;
        pos += snprintf(req + pos, req_cap - pos,
                        "%s %s HTTP/1.1\r\nHost: %s\r\n", method, path, host);
        if (ht->body && ht->body_len > 0) {
            pos += snprintf(req + pos, req_cap - pos,
                            "Content-Length: %zu\r\n", ht->body_len);
        }
        pos += snprintf(req + pos, req_cap - pos, "Connection: close\r\n");
        /* custom headers (same minimal JSON parser as http_task) */
        if (ht->headers_json && ht->headers_json[0] == '{') {
            const char *p = ht->headers_json + 1;
            while (*p && *p != '}') {
                while (*p == ' ' || *p == ',' || *p == '\t') p++;
                if (*p == '}' || *p == '\0') break;
                if (*p != '"') { p++; continue; }
                p++;
                const char *ks = p;
                while (*p && *p != '"') { if (*p == '\\') p++; p++; }
                size_t kl = (size_t)(p - ks);
                if (*p == '"') p++;
                while (*p == ' ' || *p == ':' || *p == '\t') p++;
                if (*p != '"') { p++; continue; }
                p++;
                const char *vs = p;
                while (*p && *p != '"') { if (*p == '\\') p++; p++; }
                size_t vl = (size_t)(p - vs);
                if (*p == '"') p++;
                if (pos + kl + vl + 5 < req_cap) {
                    memcpy(req + pos, ks, kl); pos += kl;
                    req[pos++] = ':'; req[pos++] = ' ';
                    memcpy(req + pos, vs, vl); pos += vl;
                    req[pos++] = '\r'; req[pos++] = '\n';
                }
            }
        }
        req[pos++] = '\r'; req[pos++] = '\n';
        if (ht->body && ht->body_len > 0 && pos + ht->body_len < req_cap) {
            memcpy(req + pos, ht->body, ht->body_len);
            pos += ht->body_len;
        }
        if (stream_write_all(sock, ssl_ptr, req, pos) < 0) {
            free(req); error_status = -5; goto done;
        }
        free(req);
    }

    /* Read headers until \r\n\r\n */
    {
        size_t hdr_cap = 2048;
        size_t hlen = 0;
        int header_end = -1;
        hdr_buf = (char *)malloc(hdr_cap);
        if (!hdr_buf) { error_status = -11; goto done; }

        while (1) {
            /* Grow buffer if nearly full */
            if (hlen + 512 >= hdr_cap) {
                size_t new_cap = hdr_cap * 2;
                char *new_hdr = (char *)realloc(hdr_buf, new_cap);
                if (!new_hdr) { error_status = -11; goto done; }
                hdr_buf = new_hdr;
                hdr_cap = new_cap;
            }
            int n = stream_read_one(sock, ssl_ptr, hdr_buf + hlen, hdr_cap - 1 - hlen);
            if (n < 0) { error_status = -5; goto done; }
            if (n == 0) break;
            hlen += (size_t)n;
            hdr_buf[hlen] = '\0';
            /* search for \r\n\r\n */
            for (size_t i = 3; i <= hlen; i++) {
                if (hdr_buf[i-3] == '\r' && hdr_buf[i-2] == '\n' &&
                    hdr_buf[i-1] == '\r' && hdr_buf[i] == '\n') {
                    header_end = (int)i + 1;
                    break;
                }
            }
            if (header_end >= 0) break;
        }
        if (header_end < 0) { error_status = -5; goto done; }

        int http_status = 0, chunked = 0;
        size_t body_expected = 0, he = 0;
        if (parse_http_response(hdr_buf, hlen, &http_status, &chunked,
                                &body_expected, &he) < 0) {
            error_status = -5; goto done;
        }
        /* Build + deliver headers JSON */
        const char *status_line_end = NULL;
        for (size_t i = 0; i + 1 < (size_t)header_end; i++) {
            if (hdr_buf[i] == '\r' && hdr_buf[i+1] == '\n') {
                status_line_end = hdr_buf + i + 2; break;
            }
        }
        char *headers_json = NULL;
        size_t headers_json_len = 0;
        if (status_line_end) {
            size_t hdr_len = (hdr_buf + he - 4) - status_line_end;
            headers_json = pal_build_headers_json(status_line_end, hdr_len,
                                                  &headers_json_len);
        }
        if (ops->on_headers) {
            ops->on_headers(ops->user_data, http_status,
                            headers_json ? headers_json : "{}");
        }
        if (headers_json) free(headers_json);

        /* Any body bytes already read past the header go to on_data first. */
        char read_buf[1024];
        size_t body_received = 0;
        if ((size_t)header_end < hlen) {
            size_t extra = hlen - (size_t)header_end;
            if (ops->on_data) ops->on_data(ops->user_data, hdr_buf + header_end, extra);
            body_received += extra;
        }

        /* Stream the rest of the body. For chunked, decode chunk framing;
         * for Content-Length, stop at body_expected; otherwise read until EOF. */
        if (chunked) {
            /* Simple chunked decoder: read [hexsize]\r\n[data]\r\n until 0-size. */
            char chunk_hdr[16];
            size_t chunk_hdr_len = 0;
            size_t chunk_remaining = 0;
            int in_size = 1;
            for (;;) {
                int n = stream_read_one(sock, ssl_ptr, read_buf, sizeof(read_buf));
                if (n < 0) { error_status = -5; goto done; }
                if (n == 0) break;
                size_t off = 0;
                while (off < (size_t)n) {
                    if (in_size) {
                        /* accumulate into chunk_hdr until \r\n */
                        while (off < (size_t)n && chunk_hdr_len < sizeof(chunk_hdr) - 1) {
                            chunk_hdr[chunk_hdr_len++] = read_buf[off++];
                            if (chunk_hdr_len >= 2 &&
                                chunk_hdr[chunk_hdr_len-2] == '\r' &&
                                chunk_hdr[chunk_hdr_len-1] == '\n') {
                                chunk_hdr[chunk_hdr_len-2] = '\0';
                                chunk_remaining = strtoul(chunk_hdr, NULL, 16);
                                chunk_hdr_len = 0;
                                in_size = 0;
                                if (chunk_remaining == 0) goto body_done;
                                break;
                            }
                        }
                    } else {
                        size_t take = (size_t)n - off;
                        if (take > chunk_remaining) take = chunk_remaining;
                        if (take > 0 && ops->on_data) {
                            ops->on_data(ops->user_data, read_buf + off, take);
                        }
                        off += take;
                        chunk_remaining -= take;
                        if (chunk_remaining == 0) {
                            /* consume trailing \r\n */
                            if (off < (size_t)n && read_buf[off] == '\r') off++;
                            if (off < (size_t)n && read_buf[off] == '\n') off++;
                            in_size = 1;
                        }
                    }
                }
            }
        } else {
            for (;;) {
                if (body_expected > 0 && body_received >= body_expected) break;
                int n = stream_read_one(sock, ssl_ptr, read_buf, sizeof(read_buf));
                if (n < 0) { error_status = -5; goto done; }
                if (n == 0) break;
                if (ops->on_data) ops->on_data(ops->user_data, read_buf, (size_t)n);
                body_received += (size_t)n;
            }
        }
    body_done:
        error_status = 0;
    }

done:
    if (ops->on_end) ops->on_end(ops->user_data, error_status);

    free(hdr_buf);

    if (tls_inited) {
        mbedtls_ssl_close_notify(&ssl_ctx);
        mbedtls_ssl_free(&ssl_ctx);
        mbedtls_ssl_config_free(&ssl_conf);
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);
        mbedtls_x509_crt_free(&cacert);
    }
    if (sock >= 0) lwip_close(sock);
    free(host);
    free(path);
    free(ht->url);
    free(ht->method);
    free(ht->headers_json);
    free(ht->body);
    free(ht);

    signal_event(pf, EVENT_HTTP_RECV);
    vTaskDelete(NULL);
}

static void pal_freertos_http_request_stream(qwrt_pal_t *pal,
                                              const char *url,
                                              const char *method,
                                              const char *headers,
                                              const char *body,
                                              size_t body_len,
                                              qwrt_pal_stream_ops_t *ops)
{
    pal_freertos_t *pf = pal_freertos_self(pal);

    if (!url || !ops) {
        if (ops && ops->on_end) ops->on_end(ops->user_data, QWRT_ERR_INVALID_ARG);
        return;
    }

    pal_freertos_http_task_t *ht =
        (pal_freertos_http_task_t *)calloc(1, sizeof(*ht));
    if (!ht) {
        if (ops->on_end) ops->on_end(ops->user_data, QWRT_ERR_GENERIC);
        return;
    }
    ht->pf = pf;
    ht->url = strdup(url);
    ht->method = method ? strdup(method) : strdup("GET");
    ht->headers_json = headers ? strdup(headers) : NULL;
    if (body && body_len > 0) {
        ht->body = (char *)malloc(body_len);
        if (ht->body) { memcpy(ht->body, body, body_len); ht->body_len = body_len; }
    }
    ht->streaming = 1;
    ht->stream_ops = *ops;  /* copy (function ptrs + user_data) */

    if (xTaskCreate(http_stream_task, "http_stream",
                    PAL_FREERTOS_HTTP_TASK_STACK, ht,
                    PAL_FREERTOS_HTTP_TASK_PRIO, NULL) != pdPASS) {
        free(ht->url); free(ht->method); free(ht->headers_json);
        free(ht->body); free(ht);
        if (ops->on_end) ops->on_end(ops->user_data, QWRT_ERR_GENERIC);
    }
}

/* ================================================================
 * Filesystem operations — LittleFS via ESP-IDF VFS
 * ================================================================ */

static void pal_freertos_fs_read(qwrt_pal_t *pal, const char *path,
                                  qwrt_pal_cb_t cb, void *cb_data)
{
    if (!path) {
        cb(cb_data, QWRT_ERR_INVALID_ARG, "invalid path", 12);
        return;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        cb(cb_data, QWRT_ERR_NOT_FOUND, "not found", 9);
        return;
    }

    /* Get file size */
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize < 0) {
        fclose(f);
        cb(cb_data, QWRT_ERR_GENERIC, "read error", 10);
        return;
    }

    char *buf = (char *)malloc((size_t)fsize + 1);
    if (!buf) {
        fclose(f);
        cb(cb_data, QWRT_ERR_GENERIC, "out of memory", 13);
        return;
    }

    size_t nread = fread(buf, 1, (size_t)fsize, f);
    fclose(f);

    buf[nread] = '\0';
    cb(cb_data, 0, buf, nread);
    free(buf);
}

static void pal_freertos_fs_write(qwrt_pal_t *pal, const char *path,
                                   const char *data, size_t data_len,
                                   qwrt_pal_cb_t cb, void *cb_data)
{
    if (!path) {
        cb(cb_data, QWRT_ERR_INVALID_ARG, "invalid path", 12);
        return;
    }

    FILE *f = fopen(path, "wb");
    if (!f) {
        cb(cb_data, QWRT_ERR_GENERIC, "cannot open file for writing", 28);
        return;
    }

    size_t written = fwrite(data, 1, data_len, f);
    fclose(f);

    if (written < data_len) {
        cb(cb_data, QWRT_ERR_GENERIC, "write error", 11);
    } else {
        cb(cb_data, 0, "ok", 2);
    }
}

static void pal_freertos_fs_exists(qwrt_pal_t *pal, const char *path,
                                    qwrt_pal_cb_t cb, void *cb_data)
{
    if (!path) {
        cb(cb_data, QWRT_ERR_INVALID_ARG, "invalid path", 12);
        return;
    }

    struct stat st;
    if (stat(path, &st) == 0) {
        cb(cb_data, 0, "true", 4);
    } else {
        cb(cb_data, 0, "false", 5);
    }
}

static void pal_freertos_fs_remove(qwrt_pal_t *pal, const char *path,
                                    qwrt_pal_cb_t cb, void *cb_data)
{
    if (!path) {
        cb(cb_data, QWRT_ERR_INVALID_ARG, "invalid path", 12);
        return;
    }

    if (unlink(path) == 0) {
        cb(cb_data, 0, "ok", 2);
    } else if (errno == ENOENT) {
        cb(cb_data, QWRT_ERR_NOT_FOUND, "not found", 9);
    } else {
        cb(cb_data, QWRT_ERR_GENERIC, "remove error", 12);
    }
}

static void pal_freertos_fs_list(qwrt_pal_t *pal, const char *path,
                                  qwrt_pal_cb_t cb, void *cb_data)
{
    if (!path) {
        cb(cb_data, QWRT_ERR_INVALID_ARG, "invalid path", 12);
        return;
    }

    DIR *dir = opendir(path);
    if (!dir) {
        cb(cb_data, QWRT_ERR_GENERIC, "cannot open directory", 21);
        return;
    }

    /* Collect names */
    int cap = 32;
    int count = 0;
    char **names = (char **)malloc(sizeof(char *) * (size_t)cap);
    if (!names) {
        closedir(dir);
        cb(cb_data, QWRT_ERR_GENERIC, "out of memory", 13);
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        if (count >= cap) {
            cap *= 2;
            char **new_names = (char **)realloc(names,
                                                 sizeof(char *) * (size_t)cap);
            if (!new_names) {
                int j;
                for (j = 0; j < count; j++) free(names[j]);
                free(names);
                closedir(dir);
                cb(cb_data, QWRT_ERR_GENERIC, "out of memory", 13);
                return;
            }
            names = new_names;
        }

        names[count] = strdup(entry->d_name);
        if (!names[count]) {
            int j;
            for (j = 0; j < count; j++) free(names[j]);
            free(names);
            closedir(dir);
            cb(cb_data, QWRT_ERR_GENERIC, "out of memory", 13);
            return;
        }
        count++;
    }
    closedir(dir);

    /* Build JSON array */
    size_t json_len;
    char *json = pal_build_json_array((const char *const *)names, count, &json_len);

    int i;
    for (i = 0; i < count; i++) free(names[i]);
    free(names);

    if (!json) {
        cb(cb_data, QWRT_ERR_GENERIC, "out of memory", 13);
        return;
    }

    cb(cb_data, 0, json, json_len);
    free(json);
}

/* ================================================================
 * Storage operations — NVS (Non-Volatile Storage)
 * ================================================================ */

static int ensure_nvs_open(pal_freertos_t *pf)
{
    if (pf->nvs_initialized) return 0;

    esp_err_t err = nvs_open(PAL_FREERTOS_NVS_NAMESPACE,
                              NVS_READWRITE, &pf->nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return -1;
    }

    pf->nvs_initialized = 1;
    return 0;
}

static void pal_freertos_storage_get(qwrt_pal_t *pal, const char *key,
                                      qwrt_pal_cb_t cb, void *cb_data)
{
    pal_freertos_t *pf = pal_freertos_self(pal);

    if (!key) {
        cb(cb_data, QWRT_ERR_INVALID_ARG, "invalid key", 11);
        return;
    }

    if (ensure_nvs_open(pf) < 0) {
        cb(cb_data, QWRT_ERR_GENERIC, "storage unavailable", 19);
        return;
    }

    /* Get required size first */
    size_t required_size = 0;
    esp_err_t err = nvs_get_str(pf->nvs_handle, key, NULL, &required_size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        cb(cb_data, QWRT_ERR_NOT_FOUND, "not found", 9);
        return;
    }
    if (err != ESP_OK) {
        cb(cb_data, QWRT_ERR_GENERIC, "storage read error", 18);
        return;
    }

    char *value = (char *)malloc(required_size);
    if (!value) {
        cb(cb_data, QWRT_ERR_GENERIC, "out of memory", 13);
        return;
    }

    err = nvs_get_str(pf->nvs_handle, key, value, &required_size);
    if (err != ESP_OK) {
        free(value);
        cb(cb_data, QWRT_ERR_GENERIC, "storage read error", 18);
        return;
    }

    cb(cb_data, 0, value, required_size - 1); /* exclude null terminator */
    free(value);
}

static void pal_freertos_storage_set(qwrt_pal_t *pal, const char *key,
                                      const char *value, size_t value_len,
                                      qwrt_pal_cb_t cb, void *cb_data)
{
    pal_freertos_t *pf = pal_freertos_self(pal);

    if (!key) {
        cb(cb_data, QWRT_ERR_INVALID_ARG, "invalid key", 11);
        return;
    }

    if (ensure_nvs_open(pf) < 0) {
        cb(cb_data, QWRT_ERR_GENERIC, "storage unavailable", 19);
        return;
    }

    /* NVS requires null-terminated strings */
    char *tmp = (char *)malloc(value_len + 1);
    if (!tmp) {
        cb(cb_data, QWRT_ERR_GENERIC, "out of memory", 13);
        return;
    }
    memcpy(tmp, value, value_len);
    tmp[value_len] = '\0';

    esp_err_t err = nvs_set_str(pf->nvs_handle, key, tmp);
    free(tmp);

    if (err != ESP_OK) {
        cb(cb_data, QWRT_ERR_GENERIC, "storage write error", 19);
        return;
    }

    err = nvs_commit(pf->nvs_handle);
    if (err != ESP_OK) {
        cb(cb_data, QWRT_ERR_GENERIC, "storage commit error", 20);
        return;
    }

    cb(cb_data, 0, "ok", 2);
}

static void pal_freertos_storage_del(qwrt_pal_t *pal, const char *key,
                                      qwrt_pal_cb_t cb, void *cb_data)
{
    pal_freertos_t *pf = pal_freertos_self(pal);

    if (!key) {
        cb(cb_data, QWRT_ERR_INVALID_ARG, "invalid key", 11);
        return;
    }

    if (ensure_nvs_open(pf) < 0) {
        cb(cb_data, QWRT_ERR_GENERIC, "storage unavailable", 19);
        return;
    }

    esp_err_t err = nvs_erase_key(pf->nvs_handle, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        cb(cb_data, QWRT_ERR_NOT_FOUND, "not found", 9);
        return;
    }
    if (err != ESP_OK) {
        cb(cb_data, QWRT_ERR_GENERIC, "storage delete error", 20);
        return;
    }

    err = nvs_commit(pf->nvs_handle);
    if (err != ESP_OK) {
        cb(cb_data, QWRT_ERR_GENERIC, "storage commit error", 20);
        return;
    }

    cb(cb_data, 0, "ok", 2);
}

/* ================================================================
 * Timer operations — FreeRTOS software timers
 *
 * Timer callbacks run in the FreeRTOS timer task context.
 * They enqueue a deferred callback via xQueueSend so the
 * actual JS callback happens in qwrt_tick's context.
 * ================================================================ */

typedef struct {
    qwrt_pal_cb_t cb;
    void *cb_data;
    pal_freertos_t *pf;
    int repeat;
    int deleted;       /* set to 1 by timer_stop before free(); callback checks this */
    TimerHandle_t handle;
} pal_freertos_timer_op_t;

/* Trampoline: the deferred queue / qwrt_defer_callback invokes a 1-arg
 * void(*)(void*). The timer's real cb is a qwrt_pal_cb_t (4-arg). Calling a
 * 4-arg function through a 1-arg pointer is UB, so this trampoline is what's
 * enqueued; it calls op->cb with the correct signature (status 0, no data -
 * the timer-fired contract). */
static void freertos_timer_fire(void *data)
{
    pal_freertos_timer_op_t *op = (pal_freertos_timer_op_t *)data;
    if (op && !op->deleted && op->cb) {
        op->cb(op->cb_data, 0, NULL, 0);
    }
}

static void freertos_timer_callback(TimerHandle_t xTimer)
{
    pal_freertos_timer_op_t *op =
        (pal_freertos_timer_op_t *)pvTimerGetTimerID(xTimer);

    if (!op) return;

    /* Enqueue the trampoline for qwrt_tick processing (calls op->cb with the
     * correct qwrt_pal_cb_t signature; casting the 4-arg cb directly was UB). */
    deferred_enqueue(op->pf, freertos_timer_fire, op);

    /* Signal event to wake run_cycle */
    signal_event(op->pf, EVENT_TIMER_FIRE);

    /* If one-shot, stop the timer */
    if (!op->repeat) {
        xTimerStop(xTimer, 0);
    }
}

static void *pal_freertos_timer_start(qwrt_pal_t *pal, uint64_t delay_ms,
                                       int repeat,
                                       qwrt_pal_cb_t cb, void *cb_data)
{
    pal_freertos_t *pf = pal_freertos_self(pal);

    if (pf->timer_count >= PAL_FREERTOS_MAX_TIMERS) {
        return NULL;
    }

    pal_freertos_timer_op_t *op =
        (pal_freertos_timer_op_t *)calloc(1, sizeof(*op));
    if (!op) return NULL;

    op->cb = cb;
    op->cb_data = cb_data;
    op->pf = pf;
    op->repeat = repeat;

    /* Clamp delay to TickType_t range (typically uint32_t).
     * pdMS_TO_TICKS on ESP32: max safe ms ~ 2^32 / portTICK_PERIOD_MS */
    if (delay_ms > 0xFFFFFFFFULL) delay_ms = 0xFFFFFFFFULL;
    TickType_t period_ticks = pdMS_TO_TICKS((TickType_t)delay_ms);
    if (period_ticks < 1) period_ticks = 1;

    TimerHandle_t handle = xTimerCreate(
        "qwrt-tmr",
        period_ticks,
        repeat ? pdTRUE : pdFALSE,
        op,
        freertos_timer_callback);

    if (!handle) {
        free(op);
        return NULL;
    }

    op->handle = handle;

    if (xTimerStart(handle, 0) != pdPASS) {
        xTimerDelete(handle, 0);
        free(op);
        return NULL;
    }

    pf->timers[pf->timer_count++] = handle;
    return op;
}

static void pal_freertos_timer_stop(qwrt_pal_t *pal, void *handle)
{
    pal_freertos_t *pf = pal_freertos_self(pal);

    if (!handle) return;

    pal_freertos_timer_op_t *op = (pal_freertos_timer_op_t *)handle;

    /* Mark as deleted BEFORE stopping — if the callback fires after this
     * point, freertos_timer_fire sees `deleted` and skips the deferred
     * enqueue, avoiding UAF on op. */
    op->deleted = 1;

    /* Stop the timer synchronously (block until the timer command is
     * processed by the FreeRTOS timer task). Block-time portMAX_DELAY
     * ensures the timer daemon has processed the stop before we proceed,
     * so the callback won't fire after this call. */
    if (xTimerIsTimerActive(op->handle)) {
        xTimerStop(op->handle, portMAX_DELAY);
    }
    xTimerDelete(op->handle, portMAX_DELAY);

    /* Remove from tracked timers */
    int i;
    for (i = 0; i < pf->timer_count; i++) {
        if (pf->timers[i] == op->handle) {
            pf->timers[i] = pf->timers[pf->timer_count - 1];
            pf->timer_count--;
            break;
        }
    }

    free(op);
}

/* ================================================================
 * Synchronous operations
 * ================================================================ */

static uint64_t pal_freertos_time_now(qwrt_pal_t *pal)
{
    (void)pal;
    /* Milliseconds since boot, from the 64-bit esp_timer (no 32-bit wrap like
     * xTaskGetTickCount() * portTICK_PERIOD_MS, which wraps ~49 days). Not
     * wall-clock epoch time (no RTC) - sufficient for relative timing. */
    return (uint64_t)esp_timer_get_time() / 1000ULL;
}

static uint64_t pal_freertos_hrtime(qwrt_pal_t *pal)
{
    (void)pal;
    /* esp_timer_get_time returns microseconds, convert to nanoseconds */
    return (uint64_t)esp_timer_get_time() * 1000ULL;
}

static void pal_freertos_log(qwrt_pal_t *pal, int level, const char *msg)
{
    (void)pal;
    switch (level) {
    case 0:  ESP_LOGI(TAG, "%s", msg ? msg : ""); break;
    case 1:  ESP_LOGW(TAG, "%s", msg ? msg : ""); break;
    case 2:  ESP_LOGE(TAG, "%s", msg ? msg : ""); break;
    default: ESP_LOGI(TAG, "%s", msg ? msg : ""); break;
    }
}

static void *pal_freertos_mem_alloc(qwrt_pal_t *pal, size_t size)
{
    (void)pal;
    /* Prefer PSRAM for large allocations (frees scarce internal SRAM); fall
     * back to internal SRAM (the default heap) when PSRAM is absent or full.
     * Previously this only requested PSRAM and returned NULL on boards without
     * it, breaking all qwrt core allocations. */
    void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) {
        p = heap_caps_malloc(size, MALLOC_CAP_8BIT);
    }
    return p;
}

static void pal_freertos_mem_free(qwrt_pal_t *pal, void *ptr)
{
    (void)pal;
    heap_caps_free(ptr);
}

static void pal_freertos_random_bytes(qwrt_pal_t *pal, uint8_t *buf,
                                       size_t len)
{
    (void)pal;
    esp_fill_random(buf, len);
}

/* ================================================================
 * Process management — all NULL (no process model on MCU)
 * ================================================================ */

/* These are intentionally left as NULL — set in pal_freertos_create */

/* ================================================================
 * pal_freertos_run_cycle — block waiting for events, then drain
 * deferred callbacks into qwrt_defer_callback.
 * ================================================================ */

int pal_freertos_run_cycle(pal_freertos_t *pf, int timeout_ms)
{
    if (!pf || !pf->event_group) return -1;

    TickType_t ticks;
    if (timeout_ms < 0) {
        ticks = portMAX_DELAY;
    } else if ((unsigned int)timeout_ms > 0xFFFFFFFFU / portTICK_PERIOD_MS) {
        ticks = portMAX_DELAY;
    } else {
        ticks = pdMS_TO_TICKS((TickType_t)timeout_ms);
    }

    EventBits_t bits = xEventGroupWaitBits(
        pf->event_group,
        EVENT_HTTP_RECV | EVENT_TIMER_FIRE | EVENT_STOP,
        pdTRUE,   /* clear on exit */
        pdFALSE,  /* wait for any bit */
        ticks);

    if (bits & EVENT_STOP) {
        return -1;
    }

    /* Drain deferred queue into qwrt if runtime is set */
    if (pf->rt) {
        pal_freertos_drain_deferred(pf, pf->rt);
    }

    return (bits == 0) ? 0 : __builtin_popcount((unsigned int)bits);
}

/*
 * PAL run_cycle wrapper: recovers the pal_freertos_t from the qwrt_pal_t
 * and delegates to pal_freertos_run_cycle. This lets host applications drive the
 * FreeRTOS event loop through the PAL interface without knowing about
 * pal_freertos_t.
 */
static int pal_freertos_run_cycle_pal(qwrt_pal_t *pal, int timeout_ms)
{
    if (!pal) return -1;
    pal_freertos_t *pf = pal_freertos_self(pal);
    return pal_freertos_run_cycle(pf, timeout_ms);
}

/* ================================================================
 * Create / Destroy
 * ================================================================ */

pal_freertos_t *pal_freertos_create(void)
{
    pal_freertos_t *pf = (pal_freertos_t *)calloc(1, sizeof(*pf));
    if (!pf) return NULL;

    /* Create deferred callback queue */
    pf->deferred_queue = xQueueCreate(PAL_FREERTOS_DEFERRED_QUEUE_LEN,
                                       sizeof(pal_freertos_deferred_msg_t));
    if (!pf->deferred_queue) {
        ESP_LOGE(TAG, "Failed to create deferred queue");
        free(pf);
        return NULL;
    }

    /* Create event group */
    pf->event_group = xEventGroupCreate();
    if (!pf->event_group) {
        ESP_LOGE(TAG, "Failed to create event group");
        vQueueDelete(pf->deferred_queue);
        free(pf);
        return NULL;
    }

    /* Initialize NVS */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* NVS partition was truncated and needs to be erased */
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS init failed: %s — storage unavailable",
                 esp_err_to_name(err));
        /* Non-fatal — storage operations will fail gracefully */
    }

    pf->nvs_initialized = 0;
    pf->fs_mounted = 0;
    pf->timer_count = 0;
    pf->rt = NULL;

    /* Mount LittleFS */
    /* Note: The partition must be defined in partitions.csv:
     *   littlefs, data, spiffs, 0x110000, 0x100000
     * And the esp_littlefs component must be included in the build. */
    esp_vfs_spiffs_conf_t fs_conf = {
        .base_path = PAL_FREERTOS_FS_MOUNT,
        .partition_label = "littlefs",
        .max_files = 5,
        .format_if_mount_failed = true,
    };

    err = esp_vfs_spiffs_register(&fs_conf);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "LittleFS mount failed: %s — filesystem unavailable",
                 esp_err_to_name(err));
        /* Non-fatal — fs operations will fail gracefully */
    } else {
        pf->fs_mounted = 1;
        ESP_LOGI(TAG, "LittleFS mounted at %s", PAL_FREERTOS_FS_MOUNT);
    }

    /* Set up PAL function pointers */
    qwrt_pal_t *pal = &pf->pal;
    pal->user_data = NULL;  /* we derive via offsetof */
    pal->version   = 1;
    pal->name      = "freertos";

    pal->http_request        = pal_freertos_http_request;
    pal->http_request_stream = pal_freertos_http_request_stream;
    pal->fs_read             = pal_freertos_fs_read;
    pal->fs_write            = pal_freertos_fs_write;
    pal->fs_exists           = pal_freertos_fs_exists;
    pal->fs_remove           = pal_freertos_fs_remove;
    pal->fs_list             = pal_freertos_fs_list;
    pal->storage_get         = pal_freertos_storage_get;
    pal->storage_set         = pal_freertos_storage_set;
    pal->storage_del         = pal_freertos_storage_del;
    pal->timer_start         = pal_freertos_timer_start;
    pal->timer_stop          = pal_freertos_timer_stop;
    pal->time_now            = pal_freertos_time_now;
    pal->hrtime              = pal_freertos_hrtime;
    pal->log                 = pal_freertos_log;
    pal->mem_alloc           = pal_freertos_mem_alloc;
    pal->mem_free            = pal_freertos_mem_free;
    pal->random_bytes        = pal_freertos_random_bytes;
    pal->run_cycle           = pal_freertos_run_cycle_pal;

    /* Process management — all NULL */
    pal->spawn            = NULL;
    pal->spawn_get_stdin  = NULL;
    pal->spawn_get_stdout = NULL;
    pal->channel_send     = NULL;
    pal->channel_recv     = NULL;
    pal->channel_close    = NULL;
    pal->join             = NULL;
    pal->terminate        = NULL;

    /* Lifecycle — all setup is done in pal_freertos_create(),
     * teardown is done via pal_freertos_destroy() by the embedder. */
    pal->init    = NULL;
    pal->destroy = NULL;
    memset(pal->reserved, 0, sizeof(pal->reserved));

    return pf;
}

void pal_freertos_destroy(pal_freertos_t *pf)
{
    if (!pf) return;

    /* Stop all timers */
    int i;
    for (i = 0; i < pf->timer_count; i++) {
        if (pf->timers[i]) {
            xTimerStop(pf->timers[i], portMAX_DELAY);
            xTimerDelete(pf->timers[i], portMAX_DELAY);
        }
    }

    /* Close NVS */
    if (pf->nvs_initialized) {
        nvs_close(pf->nvs_handle);
    }

    /* Unmount LittleFS */
    if (pf->fs_mounted) {
        esp_vfs_spiffs_unregister("littlefs");
    }

    /* Delete event group */
    if (pf->event_group) {
        vEventGroupDelete(pf->event_group);
    }

    /* Delete deferred queue */
    if (pf->deferred_queue) {
        vQueueDelete(pf->deferred_queue);
    }

    free(pf);
}

qwrt_pal_t *pal_freertos_get_pal(pal_freertos_t *pf)
{
    if (!pf) return NULL;
    return &pf->pal;
}

#endif /* ESP_PLATFORM */

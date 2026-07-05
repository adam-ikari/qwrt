/*
 * libuv PAL Implementation
 *
 * Implements qwrt_pal_t using libuv async operations.
 * All async PAL operations use uv handles with wrapper structs that
 * carry the PAL callback + callback data alongside the uv request.
 */

#define _GNU_SOURCE  /* putenv, etc. (not exposed by _POSIX_C_SOURCE alone) */

#include "pal_uv.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

#ifdef QWRT_WITH_TLS
#include <mbedtls/ssl.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/error.h>
#endif

/* ================================================================
 * Constants
 * ================================================================ */

#define PAL_UV_STORAGE_DEFAULT 128
#define PAL_UV_FS_BUF_INIT 4096
#define PAL_UV_HTTP_BUF_INIT 4096
#define PAL_UV_CONNECT_TIMEOUT_MS 30000
#define PAL_UV_READ_IDLE_TIMEOUT_MS 60000
#define PAL_UV_MAX_CHUNK_SIZE (16 * 1024 * 1024)  /* 16 MB */

/* Chunked transfer-encoding parsing states */
enum {
    CHUNK_STATE_SIZE = 0,   /* reading chunk-size line */
    CHUNK_STATE_DATA,       /* reading chunk-data */
    CHUNK_STATE_TRAILER,    /* reading \r\n after chunk-data */
    CHUNK_STATE_DONE        /* received 0-length final chunk */
};

/* ================================================================
 * Storage entry (in-memory key-value)
 * ================================================================ */

typedef struct pal_uv_store_entry_t {
    char *key;
    char *value;
    size_t value_len;
} pal_uv_store_entry_t;

/* ================================================================
 * Internal PAL state
 * ================================================================ */

/* Forward decl: pal_uv_t tracks the active streaming HTTP op (see below),
 * which is defined after pal_uv_t. We forward-declare the struct tag only
 * (not a typedef) so the later `typedef struct pal_uv_http_op_t {...}` is
 * not a redefinition under C99. */
struct pal_uv_http_op_t;

typedef struct pal_uv_t {
    uv_loop_t *loop;
    int owns_loop;       /* 1 if we created the loop, 0 if caller provided */
    qwrt_pal_t pal;       /* embedded PAL interface */

    /* In-memory storage */
    pal_uv_store_entry_t *store;
    int storage_max;     /* configurable max storage entries */
    int store_count;

    /* Track active handles for cleanup */
    uv_handle_t *handles[256];
    int handle_count;

    /*
     * Currently-active streaming HTTP operation, or NULL.
     * Set in pal_uv_http_request_stream, cleared in pal_uv_http_stream_cleanup
     * (the teardown shared by normal completion, error, and abort). Used by
     * pal_uv_http_abort to reach the in-flight TCP/timer handles so an
     * host cancel() can tear down an in-flight request promptly.
     */
    struct pal_uv_http_op_t *active_stream;
} pal_uv_t;
/* ================================================================
 * Operation wrapper structs — carry PAL callback alongside uv req
 * ================================================================ */

/* Generic fs operation wrapper */
typedef struct pal_uv_fs_op_t {
    qwrt_pal_cb_t cb;
    void *cb_data;
    uv_fs_t fs_req;
    pal_uv_t *self;
    /* For fs_read: accumulated buffer */
    char *buf;
    size_t buf_len;
    size_t buf_cap;
    /* For fs_read: file descriptor after open */
    uv_file fd;
    /* For fs_list: directory entries */
    uv_dirent_t *dent_buf;
    int dent_count;
    int dent_cap;
} pal_uv_fs_op_t;

/* Timer wrapper */
typedef struct pal_uv_timer_op_t {
    qwrt_pal_cb_t cb;
    void *cb_data;
    uv_timer_t timer;
    pal_uv_t *self;
} pal_uv_timer_op_t;

/* HTTP operation wrapper */
typedef struct pal_uv_http_op_t {
    qwrt_pal_cb_t cb;
    void *cb_data;
    pal_uv_t *self;
    uv_tcp_t tcp;
    uv_connect_t connect_req;
    uv_write_t write_req;
    uv_getaddrinfo_t addr_req;
    uv_timer_t connect_timer;
    uv_timer_t idle_timer;

    /* Parsed URL components (heap-allocated copies) */
    char *host;
    int port;
    char *path;
    char *method;
    char *headers_json;
    char *body;
    size_t body_len;

    /* TLS flag: 1 = https requested */
    int use_tls;

#ifdef QWRT_WITH_TLS
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config ssl_conf;
    mbedtls_x509_crt ca_certs;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    unsigned char *tls_read_buf; /* Encrypted data from network */
    size_t tls_read_buf_len;
    size_t tls_read_consumed;   /* Bytes consumed by tls_recv_cb */
    int tls_handshake_done;
#endif

    /* Request buffer */
    char *req_buf;
    size_t req_buf_len;

    /* Response buffer */
    char *resp_buf;
    size_t resp_buf_len;
    size_t resp_buf_cap;

    /* Response parsing state */
    int parse_state;       /* 0=status, 1=headers, 2=body */
    int http_status;
    int headers_done;
    size_t header_end_offset;

    /* Content-Length / chunked body tracking */
    int chunked;           /* 1 = Transfer-Encoding: chunked */
    size_t body_expected;  /* Content-Length value (0 = unknown) */
    size_t body_received;  /* body bytes received so far */

    /* Chunked encoding parsing state */
    int chunk_state;       /* CHUNK_STATE_* */
    size_t chunk_remaining;/* bytes remaining in current chunk */
    char chunk_size_buf[16];/* partial chunk-size line buffer */
    size_t chunk_size_buf_len;

    /* Streaming mode */
    int streaming;              /* 1 = streaming response via stream_ops */
    qwrt_pal_stream_ops_t stream_ops;

    /* Streaming header parsing state */
    int headers_parsed;         /* 1 = headers already delivered via on_headers */
    char *resp_headers;         /* Accumulated header text for parsing */
    size_t resp_headers_len;

    /* Streaming chunked decoding state */
    size_t chunk_size;          /* parsed chunk size for current chunk */

    /* Handle initialization flags — only close handles that were init'd */
    int tcp_init : 1;
    int timer_init : 1;
    int idle_timer_init : 1;
    int aborted : 1;       /* set by pal_uv_http_abort; callbacks must no-op */
    int teardown_started : 1; /* set at the top of pal_uv_http_stream_cleanup;
                               * makes it idempotent and lets the read cb no-op
                               * on the ECANCELED delivered after a forced close */
} pal_uv_http_op_t;

/* ================================================================
 * Helper: get pal_uv_t from qwrt_pal_t
 * ================================================================ */

static pal_uv_t *pal_uv_self(qwrt_pal_t *pal)
{
    return (pal_uv_t *)pal->user_data;
}

/* ================================================================
 * Helper: track a uv handle for cleanup
 * ================================================================ */

static void pal_uv_track_handle(pal_uv_t *self, uv_handle_t *h)
{
    if (self->handle_count < 256) {
        self->handles[self->handle_count++] = h;
    }
}

static void pal_uv_untrack_handle(pal_uv_t *self, uv_handle_t *h)
{
    int i;
    for (i = 0; i < self->handle_count; i++) {
        if (self->handles[i] == h) {
            self->handles[i] = self->handles[self->handle_count - 1];
            self->handle_count--;
            return;
        }
    }
}

/* ================================================================
 * TLS helpers (mbedTLS)
 * ================================================================ */

#ifdef QWRT_WITH_TLS
static int tls_init_op(pal_uv_http_op_t *op) {
    int ret;
    mbedtls_ssl_init(&op->ssl);
    mbedtls_ssl_config_init(&op->ssl_conf);
    mbedtls_x509_crt_init(&op->ca_certs);
    mbedtls_entropy_init(&op->entropy);
    mbedtls_ctr_drbg_init(&op->ctr_drbg);
    op->tls_read_buf = NULL;
    op->tls_read_buf_len = 0;
    op->tls_read_consumed = 0;

    ret = mbedtls_ctr_drbg_seed(&op->ctr_drbg, mbedtls_entropy_func,
                                 &op->entropy, NULL, 0);
    if (ret != 0) return -1;

    ret = mbedtls_ssl_config_defaults(&op->ssl_conf,
                                       MBEDTLS_SSL_IS_CLIENT,
                                       MBEDTLS_SSL_TRANSPORT_STREAM,
                                       MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) return -1;

    mbedtls_ssl_conf_rng(&op->ssl_conf, mbedtls_ctr_drbg_random, &op->ctr_drbg);

    /* Load system CA certificates */
    const char *ca_paths[] = {
        "/etc/ssl/certs/ca-certificates.crt",                /* Debian/Ubuntu */
        "/etc/pki/tls/certs/ca-bundle.crt",                  /* RHEL/CentOS */
        "/etc/ssl/ca-bundle.pem",                            /* OpenSUSE */
        "/usr/local/share/certs/ca-root-nss.crt",           /* FreeBSD */
        NULL
    };
    int ca_loaded = 0;
    for (const char **p = ca_paths; *p != NULL; p++) {
        ret = mbedtls_x509_crt_parse_file(&op->ca_certs, *p);
        if (ret == 0) {
            ca_loaded = 1;
            break;
        }
    }
    if (ca_loaded) {
        mbedtls_ssl_conf_ca_chain(&op->ssl_conf, &op->ca_certs, NULL);
        mbedtls_ssl_conf_authmode(&op->ssl_conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    } else {
        /* No CA certs available — fall back to optional verification */
        mbedtls_ssl_conf_authmode(&op->ssl_conf, MBEDTLS_SSL_VERIFY_OPTIONAL);
    }

    ret = mbedtls_ssl_setup(&op->ssl, &op->ssl_conf);
    if (ret != 0) return -1;

    mbedtls_ssl_set_hostname(&op->ssl, op->host);
    op->tls_handshake_done = 0;
    return 0;
}

static void tls_free_op(pal_uv_http_op_t *op) {
    mbedtls_ssl_free(&op->ssl);
    mbedtls_ssl_config_free(&op->ssl_conf);
    mbedtls_x509_crt_free(&op->ca_certs);
    mbedtls_entropy_free(&op->entropy);
    mbedtls_ctr_drbg_free(&op->ctr_drbg);
    if (op->tls_read_buf) {
        free(op->tls_read_buf);
        op->tls_read_buf = NULL;
    }
}

static void tls_write_cb(uv_write_t *req, int status) {
    (void)status;  /* write result unused — buffer freed regardless */
    free(req->data); /* free the copied buffer */
    free(req);
}

static int tls_send_cb(void *ctx, const unsigned char *buf, size_t len) {
    pal_uv_http_op_t *op = (pal_uv_http_op_t *)ctx;
    /* Copy buffer — mbedTLS may reuse it, and uv_write is async */
    char *copy = malloc(len);
    if (!copy) return MBEDTLS_ERR_NET_SEND_FAILED;
    memcpy(copy, buf, len);
    uv_write_t *req = malloc(sizeof(uv_write_t));
    if (!req) { free(copy); return MBEDTLS_ERR_NET_SEND_FAILED; }
    req->data = copy;
    uv_buf_t wbuf = uv_buf_init(copy, (unsigned int)len);
    int ret = uv_write(req, (uv_stream_t *)&op->tcp, &wbuf, 1, tls_write_cb);
    if (ret != 0) { free(copy); free(req); return MBEDTLS_ERR_NET_SEND_FAILED; }
    return (int)len;
}

static int tls_recv_cb(void *ctx, unsigned char *buf, size_t len) {
    pal_uv_http_op_t *op = (pal_uv_http_op_t *)ctx;
    size_t avail = op->tls_read_buf_len - op->tls_read_consumed;
    if (avail == 0) {
        return MBEDTLS_ERR_SSL_WANT_READ;
    }
    size_t copy = len < avail ? len : avail;
    memcpy(buf, op->tls_read_buf + op->tls_read_consumed, copy);
    op->tls_read_consumed += copy;
    return (int)copy;
}
#endif

/* ================================================================
 * Helper: JSON-escape a string into a buffer
 * Returns number of bytes written (not including null terminator).
 * ================================================================ */

static size_t json_escape(const char *src, size_t src_len,
                          char *dst, size_t dst_cap)
{
    size_t i, j;
    for (i = 0, j = 0; i < src_len && j + 6 < dst_cap; i++) {
        unsigned char c = (unsigned char)src[i];
        switch (c) {
        case '"':  dst[j++] = '\\'; dst[j++] = '"';  break;
        case '\\': dst[j++] = '\\'; dst[j++] = '\\'; break;
        case '\b': dst[j++] = '\\'; dst[j++] = 'b';  break;
        case '\f': dst[j++] = '\\'; dst[j++] = 'f';  break;
        case '\n': dst[j++] = '\\'; dst[j++] = 'n';  break;
        case '\r': dst[j++] = '\\'; dst[j++] = 'r';  break;
        case '\t': dst[j++] = '\\'; dst[j++] = 't';  break;
        default:
            if (c < 0x20) {
                j += snprintf(dst + j, dst_cap - j, "\\u%04x", c);
            } else {
                dst[j++] = (char)c;
            }
            break;
        }
    }
    if (j < dst_cap) {
        dst[j] = '\0';
    }
    return j;
}

/* ================================================================
 * Helper: build JSON result for HTTP response
 * Caller must free the returned string.
 * ================================================================ */

static char *build_http_json(int status, const char *headers,
                             size_t headers_len,
                             const char *body, size_t body_len,
                             size_t *out_len)
{
    /* Estimate: status digits + headers + body*2 (escaped) + overhead */
    size_t cap = 64 + headers_len + body_len * 2 + 128;
    char *buf = (char *)malloc(cap);
    if (!buf) return NULL;

    size_t pos = 0;
    pos += snprintf(buf + pos, cap - pos, "{\"status\":%d,\"headers\":", status);

    if (headers && headers_len > 0) {
        /* headers is already a JSON object string from parsing */
        memcpy(buf + pos, headers, headers_len);
        pos += headers_len;
    } else {
        memcpy(buf + pos, "{}", 2);
        pos += 2;
    }

    memcpy(buf + pos, ",\"body\":\"", 9);
    pos += 9;

    pos += json_escape(body, body_len, buf + pos, cap - pos - 2);

    memcpy(buf + pos, "\"}", 2);
    pos += 2;

    *out_len = pos;
    return buf;
}

/* ================================================================
 * Helper: build JSON array from string list
 * Caller must free the returned string.
 * ================================================================ */

static char *build_json_array(const char **items, int count, size_t *out_len)
{
    size_t cap = 4 + (size_t)count * 256;
    char *buf = (char *)malloc(cap);
    if (!buf) return NULL;

    size_t pos = 0;
    buf[pos++] = '[';

    int i;
    for (i = 0; i < count; i++) {
        if (i > 0) {
            buf[pos++] = ',';
        }
        buf[pos++] = '"';
        pos += json_escape(items[i], strlen(items[i]), buf + pos, cap - pos - 2);
        buf[pos++] = '"';
    }

    buf[pos++] = ']';
    buf[pos] = '\0';
    *out_len = pos;
    return buf;
}

/* ================================================================
 * Storage operations (in-memory, synchronous callback)
 * ================================================================ */

static void pal_uv_storage_get(qwrt_pal_t *pal, const char *key,
                               qwrt_pal_cb_t cb, void *cb_data)
{
    pal_uv_t *self = pal_uv_self(pal);
    int i;

    if (!key) {
        cb(cb_data, -6, "invalid key", 11);
        return;
    }

    for (i = 0; i < self->store_count; i++) {
        if (strcmp(self->store[i].key, key) == 0) {
            cb(cb_data, 0, self->store[i].value, self->store[i].value_len);
            return;
        }
    }

    cb(cb_data, -2, "not found", 9);
}

static void pal_uv_storage_set(qwrt_pal_t *pal, const char *key,
                               const char *value, size_t value_len,
                               qwrt_pal_cb_t cb, void *cb_data)
{
    pal_uv_t *self = pal_uv_self(pal);
    int i;

    if (!key) {
        cb(cb_data, -6, "invalid key", 11);
        return;
    }

    /* Update existing */
    for (i = 0; i < self->store_count; i++) {
        if (strcmp(self->store[i].key, key) == 0) {
            free(self->store[i].value);
            self->store[i].value = (char *)malloc(value_len + 1);
            if (!self->store[i].value) {
                cb(cb_data, -1, "out of memory", 13);
                return;
            }
            memcpy(self->store[i].value, value, value_len);
            self->store[i].value[value_len] = '\0';
            self->store[i].value_len = value_len;
            cb(cb_data, 0, "ok", 2);
            return;
        }
    }

    /* Insert new */
    if (self->store_count >= self->storage_max) {
        cb(cb_data, -1, "storage full", 12);
        return;
    }

    self->store[self->store_count].key = strdup(key);
    self->store[self->store_count].value = (char *)malloc(value_len + 1);
    if (!self->store[self->store_count].key ||
        !self->store[self->store_count].value) {
        free(self->store[self->store_count].key);
        free(self->store[self->store_count].value);
        cb(cb_data, -1, "out of memory", 13);
        return;
    }
    memcpy(self->store[self->store_count].value, value, value_len);
    self->store[self->store_count].value[value_len] = '\0';
    self->store[self->store_count].value_len = value_len;
    self->store_count++;

    cb(cb_data, 0, "ok", 2);
}

static void pal_uv_storage_del(qwrt_pal_t *pal, const char *key,
                               qwrt_pal_cb_t cb, void *cb_data)
{
    pal_uv_t *self = pal_uv_self(pal);
    int i;

    if (!key) {
        cb(cb_data, -6, "invalid key", 11);
        return;
    }

    for (i = 0; i < self->store_count; i++) {
        if (strcmp(self->store[i].key, key) == 0) {
            free(self->store[i].key);
            free(self->store[i].value);
            /* Shift remaining entries down */
            int j;
            for (j = i; j < self->store_count - 1; j++) {
                self->store[j] = self->store[j + 1];
            }
            self->store_count--;
            cb(cb_data, 0, "ok", 2);
            return;
        }
    }

    cb(cb_data, -2, "not found", 9);
}

/* ================================================================
 * Filesystem operations
 * ================================================================ */

/* --- fs_read: open -> read loop -> close -> callback --- */

static void pal_uv_fs_read_close_cb(uv_fs_t *req)
{
    pal_uv_fs_op_t *op = (pal_uv_fs_op_t *)req->data;
    uv_fs_req_cleanup(req);

    /* Callback with accumulated data */
    if (op->cb) {
        op->cb(op->cb_data, 0, op->buf, op->buf_len);
    }

    free(op->buf);
    free(op);
}

static void pal_uv_fs_read_cb(uv_fs_t *req)
{
    pal_uv_fs_op_t *op = (pal_uv_fs_op_t *)req->data;
    ssize_t result = req->result;
    uv_fs_req_cleanup(req);

    if (result < 0) {
        /* Read error */
        uv_fs_close(op->self->loop, &op->fs_req, op->fd,
                     pal_uv_fs_read_close_cb);
        /* op->buf will be freed in close_cb; set cb to NULL to avoid
         * double-callback, then do error callback from close_cb.
         * Actually, we need to deliver the error. Let's do it here
         * and clean up in close_cb without calling cb again. */
        qwrt_pal_cb_t cb = op->cb;
        void *cb_data = op->cb_data;
        op->cb = NULL;
        cb(cb_data, -1, "read error", 10);
        return;
    }

    if (result == 0) {
        /* EOF — close file and deliver data */
        uv_fs_close(op->self->loop, &op->fs_req, op->fd,
                     pal_uv_fs_read_close_cb);
        return;
    }

    /* Append data to buffer */
    size_t new_len = op->buf_len + (size_t)result;
    if (new_len > op->buf_cap) {
        size_t new_cap = op->buf_cap * 2;
        if (new_cap < new_len) new_cap = new_len;
        char *new_buf = (char *)realloc(op->buf, new_cap);
        if (!new_buf) {
            qwrt_pal_cb_t cb = op->cb;
            void *cb_data = op->cb_data;
            op->cb = NULL;
            uv_fs_close(op->self->loop, &op->fs_req, op->fd,
                         pal_uv_fs_read_close_cb);
            cb(cb_data, -1, "out of memory", 13);
            return;
        }
        op->buf = new_buf;
        op->buf_cap = new_cap;
    }
    memcpy(op->buf + op->buf_len, op->fs_req.bufs[0].base, (size_t)result);
    op->buf_len = new_len;

    /* Read more */
    uv_buf_t iov;
    iov.base = op->buf + op->buf_len;
    iov.len = op->buf_cap - op->buf_len;
    if (iov.len == 0) {
        iov.len = PAL_UV_FS_BUF_INIT;
        /* Grow buffer */
        size_t new_cap = op->buf_cap + PAL_UV_FS_BUF_INIT;
        char *new_buf = (char *)realloc(op->buf, new_cap);
        if (!new_buf) {
            qwrt_pal_cb_t cb = op->cb;
            void *cb_data = op->cb_data;
            op->cb = NULL;
            uv_fs_close(op->self->loop, &op->fs_req, op->fd,
                         pal_uv_fs_read_close_cb);
            cb(cb_data, -1, "out of memory", 13);
            return;
        }
        op->buf = new_buf;
        op->buf_cap = new_cap;
        iov.base = op->buf + op->buf_len;
        iov.len = op->buf_cap - op->buf_len;
    }
    uv_fs_read(op->self->loop, &op->fs_req, op->fd, &iov, 1, -1,
               pal_uv_fs_read_cb);
}

static void pal_uv_fs_read_open_cb(uv_fs_t *req)
{
    pal_uv_fs_op_t *op = (pal_uv_fs_op_t *)req->data;
    ssize_t result = req->result;
    uv_fs_req_cleanup(req);

    if (result < 0) {
        /* Open failed */
        const char *msg = "file not found";
        op->cb(op->cb_data, -2, msg, (size_t)strlen(msg));
        free(op->buf);
        free(op);
        return;
    }

    op->fd = (uv_file)result;

    /* Start reading */
    uv_buf_t iov;
    iov.base = op->buf;
    iov.len = op->buf_cap;
    uv_fs_read(op->self->loop, &op->fs_req, op->fd, &iov, 1, -1,
               pal_uv_fs_read_cb);
}

static void pal_uv_fs_read(qwrt_pal_t *pal, const char *path,
                           qwrt_pal_cb_t cb, void *cb_data)
{
    pal_uv_t *self = pal_uv_self(pal);

    if (!path) {
        cb(cb_data, -6, "invalid path", 12);
        return;
    }

    pal_uv_fs_op_t *op = (pal_uv_fs_op_t *)calloc(1, sizeof(*op));
    if (!op) {
        cb(cb_data, -1, "out of memory", 13);
        return;
    }

    op->cb = cb;
    op->cb_data = cb_data;
    op->self = self;
    op->fs_req.data = op;
    op->buf_cap = PAL_UV_FS_BUF_INIT;
    op->buf = (char *)malloc(op->buf_cap);
    if (!op->buf) {
        free(op);
        cb(cb_data, -1, "out of memory", 13);
        return;
    }

    uv_fs_open(self->loop, &op->fs_req, path, O_RDONLY, 0,
               pal_uv_fs_read_open_cb);
}

/* --- fs_write: open -> write -> close -> callback --- */

static void pal_uv_fs_write_close_cb(uv_fs_t *req)
{
    pal_uv_fs_op_t *op = (pal_uv_fs_op_t *)req->data;
    uv_fs_req_cleanup(req);

    if (op->cb) {
        op->cb(op->cb_data, 0, "ok", 2);
    }

    free(op->buf);
    free(op);
}

static void pal_uv_fs_write_cb(uv_fs_t *req)
{
    pal_uv_fs_op_t *op = (pal_uv_fs_op_t *)req->data;
    ssize_t result = req->result;
    uv_fs_req_cleanup(req);

    if (result < 0) {
        qwrt_pal_cb_t cb = op->cb;
        void *cb_data = op->cb_data;
        op->cb = NULL;
        uv_fs_close(op->self->loop, &op->fs_req, op->fd,
                     pal_uv_fs_write_close_cb);
        cb(cb_data, -1, "write error", 11);
        return;
    }

    /* Check if all data was written */
    if ((size_t)result < op->buf_len) {
        /* Partial write — write remaining */
        op->buf += result;
        op->buf_len -= (size_t)result;
        uv_buf_t iov;
        iov.base = op->buf;
        iov.len = op->buf_len;
        uv_fs_write(op->self->loop, &op->fs_req, op->fd, &iov, 1, -1,
                     pal_uv_fs_write_cb);
        return;
    }

    /* All written — close file */
    uv_fs_close(op->self->loop, &op->fs_req, op->fd,
                 pal_uv_fs_write_close_cb);
}

static void pal_uv_fs_write_open_cb(uv_fs_t *req)
{
    pal_uv_fs_op_t *op = (pal_uv_fs_op_t *)req->data;
    ssize_t result = req->result;
    uv_fs_req_cleanup(req);

    if (result < 0) {
        op->cb(op->cb_data, -1, "cannot open file for writing", 28);
        free(op->buf);
        free(op);
        return;
    }

    op->fd = (uv_file)result;

    uv_buf_t iov;
    iov.base = op->buf;
    iov.len = op->buf_len;
    uv_fs_write(op->self->loop, &op->fs_req, op->fd, &iov, 1, -1,
                 pal_uv_fs_write_cb);
}

static void pal_uv_fs_write(qwrt_pal_t *pal, const char *path,
                            const char *data, size_t data_len,
                            qwrt_pal_cb_t cb, void *cb_data)
{
    pal_uv_t *self = pal_uv_self(pal);

    if (!path) {
        cb(cb_data, -6, "invalid path", 12);
        return;
    }

    pal_uv_fs_op_t *op = (pal_uv_fs_op_t *)calloc(1, sizeof(*op));
    if (!op) {
        cb(cb_data, -1, "out of memory", 13);
        return;
    }

    op->cb = cb;
    op->cb_data = cb_data;
    op->self = self;
    op->fs_req.data = op;
    op->buf_len = data_len;
    op->buf = (char *)malloc(data_len);
    if (!op->buf) {
        free(op);
        cb(cb_data, -1, "out of memory", 13);
        return;
    }
    memcpy(op->buf, data, data_len);

    uv_fs_open(self->loop, &op->fs_req, path,
               O_WRONLY | O_CREAT | O_TRUNC, 0644,
               pal_uv_fs_write_open_cb);
}

/* --- fs_exists: stat -> callback --- */

static void pal_uv_fs_exists_cb(uv_fs_t *req)
{
    pal_uv_fs_op_t *op = (pal_uv_fs_op_t *)req->data;
    uv_fs_req_cleanup(req);

    if (req->result == 0) {
        op->cb(op->cb_data, 0, "true", 4);
    } else {
        op->cb(op->cb_data, 0, "false", 5);
    }

    free(op);
}

static void pal_uv_fs_exists(qwrt_pal_t *pal, const char *path,
                             qwrt_pal_cb_t cb, void *cb_data)
{
    pal_uv_t *self = pal_uv_self(pal);

    if (!path) {
        cb(cb_data, -6, "invalid path", 12);
        return;
    }

    pal_uv_fs_op_t *op = (pal_uv_fs_op_t *)calloc(1, sizeof(*op));
    if (!op) {
        cb(cb_data, -1, "out of memory", 13);
        return;
    }

    op->cb = cb;
    op->cb_data = cb_data;
    op->self = self;
    op->fs_req.data = op;

    uv_fs_stat(self->loop, &op->fs_req, path, pal_uv_fs_exists_cb);
}

/* --- fs_remove: unlink -> callback --- */

static void pal_uv_fs_remove_cb(uv_fs_t *req)
{
    pal_uv_fs_op_t *op = (pal_uv_fs_op_t *)req->data;
    uv_fs_req_cleanup(req);

    if (req->result == 0) {
        op->cb(op->cb_data, 0, "ok", 2);
    } else {
        int err = (req->result == UV_ENOENT) ? -2 : -1;
        const char *msg = (req->result == UV_ENOENT) ? "not found" : "remove error";
        size_t msg_len = (size_t)strlen(msg);
        op->cb(op->cb_data, err, msg, msg_len);
    }

    free(op);
}

static void pal_uv_fs_remove(qwrt_pal_t *pal, const char *path,
                             qwrt_pal_cb_t cb, void *cb_data)
{
    pal_uv_t *self = pal_uv_self(pal);

    if (!path) {
        cb(cb_data, -6, "invalid path", 12);
        return;
    }

    pal_uv_fs_op_t *op = (pal_uv_fs_op_t *)calloc(1, sizeof(*op));
    if (!op) {
        cb(cb_data, -1, "out of memory", 13);
        return;
    }

    op->cb = cb;
    op->cb_data = cb_data;
    op->self = self;
    op->fs_req.data = op;

    uv_fs_unlink(self->loop, &op->fs_req, path, pal_uv_fs_remove_cb);
}

/* --- fs_list: scandir -> collect entries -> callback --- */

static void pal_uv_fs_list_cb(uv_fs_t *req)
{
    pal_uv_fs_op_t *op = (pal_uv_fs_op_t *)req->data;

    if (req->result < 0) {
        uv_fs_req_cleanup(req);
        op->cb(op->cb_data, -1, "scandir error", 13);
        free(op);
        return;
    }

    /* Collect directory entries */
    int count = 0;
    int cap = 32;
    char **names = (char **)malloc(sizeof(char *) * (size_t)cap);
    if (!names) {
        uv_fs_req_cleanup(req);
        op->cb(op->cb_data, -1, "out of memory", 13);
        free(op);
        return;
    }

    uv_dirent_t dent;
    while (uv_fs_scandir_next(req, &dent) != UV_EOF) {
        /* Skip . and .. */
        if (strcmp(dent.name, ".") == 0 || strcmp(dent.name, "..") == 0) {
            continue;
        }

        if (count >= cap) {
            cap *= 2;
            char **new_names = (char **)realloc(names, sizeof(char *) * (size_t)cap);
            if (!new_names) {
                int j;
                for (j = 0; j < count; j++) free(names[j]);
                free(names);
                uv_fs_req_cleanup(req);
                op->cb(op->cb_data, -1, "out of memory", 13);
                free(op);
                return;
            }
            names = new_names;
        }

        names[count] = strdup(dent.name);
        if (!names[count]) {
            int j;
            for (j = 0; j < count; j++) free(names[j]);
            free(names);
            uv_fs_req_cleanup(req);
            op->cb(op->cb_data, -1, "out of memory", 13);
            free(op);
            return;
        }
        count++;
    }

    uv_fs_req_cleanup(req);

    /* Build JSON array */
    size_t json_len;
    char *json = build_json_array((const char **)names, count, &json_len);

    /* Free names */
    int i;
    for (i = 0; i < count; i++) {
        free(names[i]);
    }
    free(names);

    if (!json) {
        op->cb(op->cb_data, -1, "out of memory", 13);
        free(op);
        return;
    }

    op->cb(op->cb_data, 0, json, json_len);
    free(json);
    free(op);
}

static void pal_uv_fs_list(qwrt_pal_t *pal, const char *path,
                           qwrt_pal_cb_t cb, void *cb_data)
{
    pal_uv_t *self = pal_uv_self(pal);

    if (!path) {
        cb(cb_data, -6, "invalid path", 12);
        return;
    }

    pal_uv_fs_op_t *op = (pal_uv_fs_op_t *)calloc(1, sizeof(*op));
    if (!op) {
        cb(cb_data, -1, "out of memory", 13);
        return;
    }

    op->cb = cb;
    op->cb_data = cb_data;
    op->self = self;
    op->fs_req.data = op;

    uv_fs_scandir(self->loop, &op->fs_req, path, 0, pal_uv_fs_list_cb);
}

/* ================================================================
 * HTTP operation (uv_tcp based)
 *
 * Parses URL, connects via TCP, sends HTTP request, reads response.
 * Response is parsed to extract status code, headers, and body.
 * Supports DNS resolution via uv_getaddrinfo.
 * Supports Content-Length and chunked transfer-encoding.
 * HTTPS URLs are detected but require QWRT_WITH_TLS compile flag.
 * Connect timeout of 30 seconds via uv_timer_t.
 * Callback receives JSON: {"status":NNN,"headers":{...},"body":"..."}
 * ================================================================ */

/* Simple URL parser: extracts host, port, path from http:// or https:// URLs */
static int parse_http_url(const char *url, char **out_host, int *out_port,
                          char **out_path, int *out_use_tls)
{
    int use_tls = 0;
    const char *p = NULL;

    if (strncmp(url, "https://", 8) == 0) {
        use_tls = 1;
        p = url + 8;
    } else if (strncmp(url, "http://", 7) == 0) {
        use_tls = 0;
        p = url + 7;
    } else {
        return -1;
    }

    *out_use_tls = use_tls;

    const char *host_start = p;
    const char *host_end = NULL;
    const char *port_start = NULL;
    const char *path_start = NULL;

    /* Find end of host (either : or / or end) */
    while (*p && *p != ':' && *p != '/') {
        p++;
    }
    host_end = p;

    if (*p == ':') {
        p++;
        port_start = p;
        while (*p && *p != '/') {
            p++;
        }
    }

    if (*p == '/') {
        path_start = p;
    } else {
        path_start = "/";
    }

    /* Extract host */
    size_t host_len = (size_t)(host_end - host_start);
    *out_host = (char *)malloc(host_len + 1);
    if (!*out_host) return -1;
    memcpy(*out_host, host_start, host_len);
    (*out_host)[host_len] = '\0';

    /* Extract port */
    if (port_start) {
        *out_port = atoi(port_start);
        if (*out_port <= 0) *out_port = use_tls ? 443 : 80;
    } else {
        *out_port = use_tls ? 443 : 80;
    }

    /* Extract path */
    *out_path = strdup(path_start);
    if (!*out_path) {
        free(*out_host);
        return -1;
    }

    return 0;
}

/* Parse HTTP response headers from the response buffer.
 * Sets http_status, finds header region, finds body start.
 * Also extracts Content-Length and checks for chunked transfer-encoding.
 * Returns 0 if headers are complete, -1 if not yet. */
static int parse_http_response(pal_uv_http_op_t *op)
{
    /* Find \r\n\r\n marking end of headers */
    char *hdr_end = NULL;
    size_t i;
    for (i = 3; i < op->resp_buf_len; i++) {
        if (op->resp_buf[i - 3] == '\r' && op->resp_buf[i - 2] == '\n' &&
            op->resp_buf[i - 1] == '\r' && op->resp_buf[i] == '\n') {
            hdr_end = op->resp_buf + i + 1;
            op->header_end_offset = (size_t)(hdr_end - op->resp_buf);
            break;
        }
    }

    if (!hdr_end) {
        return -1; /* headers not complete yet */
    }

    /* Parse status line: HTTP/1.x NNN ... */
    char *line_start = op->resp_buf;
    char *line_end = NULL;

    /* Find end of status line */
    for (i = 0; i < op->header_end_offset; i++) {
        if (op->resp_buf[i] == '\r' && op->resp_buf[i + 1] == '\n') {
            line_end = op->resp_buf + i;
            break;
        }
    }

    if (!line_end) return -1;

    /* Find the status code after "HTTP/1.x " */
    char *sp = line_start;
    while (sp < line_end && *sp != ' ') sp++;
    if (sp >= line_end) return -1;
    sp++; /* skip the space */

    /* Parse 3-digit status code */
    op->http_status = 0;
    int digits = 0;
    while (sp < line_end && *sp >= '0' && *sp <= '9' && digits < 3) {
        op->http_status = op->http_status * 10 + (*sp - '0');
        sp++;
        digits++;
    }

    if (digits == 0) return -1;

    /* Scan headers for Content-Length and Transfer-Encoding: chunked.
     * Header region starts after the status line (\r\n) and ends at
     * header_end_offset - 4 (the \r\n\r\n). */
    const char *hdrs = op->resp_buf + (line_end - op->resp_buf) + 2;
    const char *hdrs_end = op->resp_buf + op->header_end_offset - 4;

    op->body_expected = 0;
    op->chunked = 0;

    const char *hp = hdrs;
    while (hp < hdrs_end) {
        /* Find end of this header line */
        const char *eol = hp;
        while (eol < hdrs_end && !(*eol == '\r' && (eol + 1 < hdrs_end) && *(eol + 1) == '\n')) {
            eol++;
        }

        /* Check for Content-Length (case-insensitive) */
        if (eol - hp > 15 && strncasecmp(hp, "Content-Length:", 15) == 0) {
            const char *val = hp + 15;
            while (val < eol && (*val == ' ' || *val == '\t')) val++;
            op->body_expected = (size_t)strtoull(val, NULL, 10);
        }

        /* Check for Transfer-Encoding: chunked (case-insensitive) */
        if (eol - hp > 26 && strncasecmp(hp, "Transfer-Encoding:", 18) == 0) {
            const char *val = hp + 18;
            while (val < eol && (*val == ' ' || *val == '\t')) val++;
            if (eol - val >= 7 && strncasecmp(val, "chunked", 7) == 0) {
                op->chunked = 1;
            }
        }

        /* Move to next line */
        hp = eol + 2;
        if (hp > hdrs_end) hp = hdrs_end;
    }

    op->headers_done = 1;
    return 0;
}

/* Build a JSON object string from the raw header lines.
 * Input: header region (between status line and \r\n\r\n)
 * Output: {"Key":"Value","Key2":"Value2"} */
static char *build_headers_json(const char *hdr_start, size_t hdr_len,
                                size_t *out_len)
{
    /* Worst case: each char could become an escape sequence */
    size_t cap = hdr_len * 2 + 4;
    char *buf = (char *)malloc(cap);
    if (!buf) return NULL;

    size_t pos = 0;
    buf[pos++] = '{';

    const char *p = hdr_start;
    const char *end = hdr_start + hdr_len;
    int first = 1;

    while (p < end) {
        /* Find end of line */
        const char *eol = p;
        while (eol < end && !(*eol == '\r' && (eol + 1 < end) && *(eol + 1) == '\n')) {
            eol++;
        }

        /* Find colon separating key and value */
        const char *colon = p;
        while (colon < eol && *colon != ':') colon++;

        if (colon < eol) {
            /* We have a key: value pair */
            size_t key_len = (size_t)(colon - p);
            const char *val_start = colon + 1;
            /* Skip leading whitespace in value */
            while (val_start < eol && (*val_start == ' ' || *val_start == '\t')) {
                val_start++;
            }
            size_t val_len = (size_t)(eol - val_start);

            if (!first) {
                buf[pos++] = ',';
            }
            first = 0;

            buf[pos++] = '"';
            pos += json_escape(p, key_len, buf + pos, cap - pos - 1);
            buf[pos++] = '"';
            buf[pos++] = ':';
            buf[pos++] = '"';
            pos += json_escape(val_start, val_len, buf + pos, cap - pos - 1);
            buf[pos++] = '"';
        }

        /* Move past \r\n */
        p = eol;
        if (p < end && *p == '\r') p++;
        if (p < end && *p == '\n') p++;
    }

    buf[pos++] = '}';
    buf[pos] = '\0';
    *out_len = pos;
    return buf;
}

/* Forward declarations for HTTP operations */
static void pal_uv_http_finish_error(pal_uv_http_op_t *op, int status,
                                     const char *msg);
static void pal_uv_http_finish_success(pal_uv_http_op_t *op);
static void pal_uv_http_stream_close_cb(uv_handle_t *handle);

static void pal_uv_http_cleanup(pal_uv_http_op_t *op)
{
    /* This is the final free for every HTTP op (streaming and non-streaming,
     * normal/error/abort paths all funnel here before free(op)). Clear the
     * PAL's active_stream tracker if it still points at us, so a later
     * pal_uv_http_abort() never dereferences a freed op. */
    if (op->self && op->self->active_stream == op) {
        op->self->active_stream = NULL;
    }

    /* Untrack all handles that belong to this op. The TCP close callback
     * (pal_uv_http_close_cb / pal_uv_http_stream_close_cb) untracks the TCP
     * handle, but the timer close callback (pal_uv_http_timer_close_cb) does
     * NOT — it can't safely access op (may already be freed). So we untrack
     * the timers here, before free(op), to prevent dangling pointers in the
     * handles[] array that would cause uv_close assertions in pal_uv_destroy. */
    if (op->self) {
        if (op->timer_init) {
            pal_uv_untrack_handle(op->self, (uv_handle_t *)&op->connect_timer);
        }
        if (op->idle_timer_init) {
            pal_uv_untrack_handle(op->self, (uv_handle_t *)&op->idle_timer);
        }
        /* TCP may or may not have been untracked by the close callback yet.
         * untrack is safe to call even if already removed (no-op if not found). */
        if (op->tcp_init) {
            pal_uv_untrack_handle(op->self, (uv_handle_t *)&op->tcp);
        }
    }
#ifdef QWRT_WITH_TLS
    if (op->use_tls) {
        tls_free_op(op);
    }
#endif
    if (op->host) free(op->host);
    if (op->path) free(op->path);
    if (op->method) free(op->method);
    if (op->headers_json) free(op->headers_json);
    if (op->body) free(op->body);
    if (op->req_buf) free(op->req_buf);
    if (op->resp_buf) free(op->resp_buf);
    if (op->resp_headers) free(op->resp_headers);
    free(op);
}

static void pal_uv_http_close_cb(uv_handle_t *handle)
{
    pal_uv_http_op_t *op = (pal_uv_http_op_t *)handle->data;
    pal_uv_untrack_handle(op->self, (uv_handle_t *)&op->tcp);
    pal_uv_http_cleanup(op);
}

/* Close callback for the connect_timer and idle_timer — frees nothing extra,
 * the main http_close_cb or stream_cleanup handles the op cleanup.
 * We cannot access op here because it may already be freed by the time
 * this callback runs (tcp close cb fires first, freeing op). */
static void pal_uv_http_timer_close_cb(uv_handle_t *handle)
{
    (void)handle;
}

static void pal_uv_http_finish_error(pal_uv_http_op_t *op, int status,
                                     const char *msg)
{
    size_t msg_len = strlen(msg);

    /* Stop and close the connect timer if initialized and active */
    if (op->timer_init && op->connect_timer.data &&
        !uv_is_closing((uv_handle_t *)&op->connect_timer)) {
        uv_timer_stop(&op->connect_timer);
        uv_close((uv_handle_t *)&op->connect_timer, pal_uv_http_timer_close_cb);
    }

    if (op->streaming) {
        /* Streaming mode: deliver error via on_end callback */
        if (op->stream_ops.on_end) {
            op->stream_ops.on_end(op->stream_ops.user_data, status);
        }
        if (op->tcp_init && !uv_is_closing((uv_handle_t *)&op->tcp)) {
            uv_close((uv_handle_t *)&op->tcp, pal_uv_http_stream_close_cb);
        } else {
            pal_uv_http_cleanup(op);
        }
        return;
    }

    op->cb(op->cb_data, status, msg, msg_len);
    if (op->tcp_init && !uv_is_closing((uv_handle_t *)&op->tcp)) {
        uv_close((uv_handle_t *)&op->tcp, pal_uv_http_close_cb);
    } else {
        /* TCP never init'd (DNS failed) — clean up directly */
        pal_uv_http_cleanup(op);
    }
}

static void pal_uv_http_finish_success(pal_uv_http_op_t *op)
{
    /* Stop and close the connect timer if initialized and active */
    if (op->timer_init && op->connect_timer.data &&
        !uv_is_closing((uv_handle_t *)&op->connect_timer)) {
        uv_timer_stop(&op->connect_timer);
        uv_close((uv_handle_t *)&op->connect_timer, pal_uv_http_timer_close_cb);
    }

    /* Parse headers and build JSON response */
    char *headers_json = NULL;
    size_t headers_json_len = 0;

    /* Find the header region (after status line, before \r\n\r\n) */
    const char *status_line_end = NULL;
    size_t i;
    for (i = 0; i < op->header_end_offset; i++) {
        if (op->resp_buf[i] == '\r' && op->resp_buf[i + 1] == '\n') {
            status_line_end = op->resp_buf + i + 2;
            break;
        }
    }

    if (status_line_end) {
        size_t hdr_len = op->resp_buf + op->header_end_offset - 4 - status_line_end;
        headers_json = build_headers_json(status_line_end, hdr_len,
                                          &headers_json_len);
    }

    const char *body_start = op->resp_buf + op->header_end_offset;
    size_t body_len = op->resp_buf_len - op->header_end_offset;

    size_t json_len;
    char *json = build_http_json(op->http_status,
                                 headers_json ? headers_json : "{}",
                                 headers_json ? headers_json_len : 2,
                                 body_start, body_len, &json_len);
    free(headers_json);

    if (json) {
        op->cb(op->cb_data, 0, json, json_len);
        free(json);
    } else {
        op->cb(op->cb_data, -1, "out of memory", 13);
    }

    if (!uv_is_closing((uv_handle_t *)&op->tcp)) {
        uv_close((uv_handle_t *)&op->tcp, pal_uv_http_close_cb);
    }
}

/* ================================================================
 * Chunked transfer-encoding body processor
 *
 * Processes body data starting at body_start for body_len bytes.
 * The decoded body is written back into op->resp_buf starting at
 * op->header_end_offset, compacting in place.
 *
 * Returns:
 *   1  — chunked body is complete (final 0-length chunk seen)
 *   0  — need more data
 *  -1  — parse error
 * ================================================================ */

static int process_chunked_body(pal_uv_http_op_t *op,
                                const char *body_start, size_t body_len)
{
    /* We write decoded chunks back into resp_buf at header_end_offset */
    size_t write_pos = op->header_end_offset;
    const char *p = body_start;
    const char *end = body_start + body_len;

    /* If we have leftover partial chunk-size from previous read,
     * resume from chunk_state. */
    while (p < end) {
        if (op->chunk_state == CHUNK_STATE_SIZE) {
            /* Reading chunk-size line. Look for \r\n. */
            const char *eol = p;
            while (eol < end && !(*eol == '\r' && (eol + 1 < end) && *(eol + 1) == '\n')) {
                eol++;
            }

            if (eol >= end) {
                /* Partial chunk-size line — save what we have */
                size_t avail = (size_t)(end - p);
                if (op->chunk_size_buf_len + avail < sizeof(op->chunk_size_buf) - 1) {
                    memcpy(op->chunk_size_buf + op->chunk_size_buf_len, p, avail);
                    op->chunk_size_buf_len += avail;
                }
                break; /* need more data */
            }

            /* We have a complete chunk-size line.
             * Combine with any previously buffered partial. */
            char size_buf[32];
            size_t size_buf_len = op->chunk_size_buf_len;
            if (size_buf_len > 0) {
                if (size_buf_len >= sizeof(size_buf)) size_buf_len = sizeof(size_buf) - 1;
                memcpy(size_buf, op->chunk_size_buf, size_buf_len);
                op->chunk_size_buf_len = 0;
            }
            size_t line_len = (size_t)(eol - p);
            if (size_buf_len + line_len >= sizeof(size_buf))
                line_len = sizeof(size_buf) - 1 - size_buf_len;
            memcpy(size_buf + size_buf_len, p, line_len);
            size_buf_len += line_len;
            size_buf[size_buf_len] = '\0';

            /* Parse chunk size (hex, may have extensions after ;) */
            size_t chunk_size = (size_t)strtoul(size_buf, NULL, 16);

            if (chunk_size == 0) {
                /* Final chunk — skip trailing \r\n and we're done */
                p = eol + 2;
                op->chunk_state = CHUNK_STATE_DONE;
                op->resp_buf_len = write_pos;
                return 1;
            }

            op->chunk_remaining = chunk_size;
            op->chunk_state = CHUNK_STATE_DATA;
            p = eol + 2; /* skip \r\n after chunk-size */

        } else if (op->chunk_state == CHUNK_STATE_DATA) {
            /* Read chunk data bytes */
            size_t avail = (size_t)(end - p);
            size_t to_copy = avail < op->chunk_remaining ? avail : op->chunk_remaining;

            /* Ensure resp_buf has space */
            if (write_pos + to_copy > op->resp_buf_cap) {
                size_t new_cap = op->resp_buf_cap * 2;
                if (new_cap < write_pos + to_copy) new_cap = write_pos + to_copy;
                char *new_buf = (char *)realloc(op->resp_buf, new_cap);
                if (!new_buf) return -1;
                op->resp_buf = new_buf;
                op->resp_buf_cap = new_cap;
            }

            memcpy(op->resp_buf + write_pos, p, to_copy);
            write_pos += to_copy;
            p += to_copy;
            op->chunk_remaining -= to_copy;

            if (op->chunk_remaining == 0) {
                op->chunk_state = CHUNK_STATE_TRAILER;
            }

        } else if (op->chunk_state == CHUNK_STATE_TRAILER) {
            /* Expect \r\n after chunk data */
            if (end - p < 2) {
                /* Need more data for the trailer \r\n */
                break;
            }
            if (p[0] != '\r' || p[1] != '\n') {
                return -1; /* malformed */
            }
            p += 2;
            op->chunk_state = CHUNK_STATE_SIZE;

        } else if (op->chunk_state == CHUNK_STATE_DONE) {
            break;
        }
    }

    /* Update body length with decoded bytes so far */
    op->resp_buf_len = write_pos;
    return (op->chunk_state == CHUNK_STATE_DONE) ? 1 : 0;
}

/* Connect timeout callback */
static void pal_uv_http_connect_timer_cb(uv_timer_t *handle)
{
    pal_uv_http_op_t *op = (pal_uv_http_op_t *)handle->data;
    if (op->aborted) return;  /* teardown already in progress */
    pal_uv_http_finish_error(op, -5, "connection timeout");
}

static void pal_uv_http_stream_cleanup(pal_uv_http_op_t *op);

/* Read-idle timeout callback — fires when no data arrives during streaming */
static void pal_uv_http_idle_timer_cb(uv_timer_t *handle)
{
    pal_uv_http_op_t *op = (pal_uv_http_op_t *)handle->data;
    if (op->aborted) return;  /* teardown already in progress */
    if (op->stream_ops.on_end) {
        op->stream_ops.on_end(op->stream_ops.user_data, -5);
    }
    pal_uv_http_stream_cleanup(op);
}

static void pal_uv_http_read_cb(uv_stream_t *stream, ssize_t nread,
                                const uv_buf_t *buf);

static void pal_uv_http_stream_read_cb(uv_stream_t *stream, ssize_t nread,
                                        const uv_buf_t *buf);

static void pal_uv_http_alloc_cb(uv_handle_t *handle, size_t suggested_size,
                                 uv_buf_t *buf)
{
    (void)handle;
    (void)suggested_size;
    buf->base = (char *)malloc(4096);
    buf->len = buf->base ? 4096 : 0;
}

static void pal_uv_http_write_cb(uv_write_t *req, int status);

#ifdef QWRT_WITH_TLS
static void tls_read_cb(uv_stream_t *stream, ssize_t nread,
                         const uv_buf_t *buf);
static void tls_stream_read_cb(uv_stream_t *stream, ssize_t nread,
                                const uv_buf_t *buf);
#endif

/* ================================================================
 * HTTP request builder — builds and sends the HTTP request over TCP.
 * Called from the connect callback (non-TLS) or after TLS handshake.
 * ================================================================ */

static void pal_uv_http_send_request(pal_uv_http_op_t *op)
{
    const char *method = op->method ? op->method : "GET";
    const char *path = op->path ? op->path : "/";
    const char *host = op->host ? op->host : "localhost";

    size_t req_cap = 1024 + (op->body_len > 0 ? op->body_len : 0);
    char *req_buf = (char *)malloc(req_cap);
    if (!req_buf) {
        pal_uv_http_finish_error(op, -1, "out of memory");
        return;
    }

    size_t pos = 0;
    pos += snprintf(req_buf + pos, req_cap - pos,
                    "%s %s HTTP/1.1\r\nHost: %s\r\n",
                    method, path, host);

    /* Add Content-Length if we have a body */
    if (op->body && op->body_len > 0) {
        pos += snprintf(req_buf + pos, req_cap - pos,
                        "Content-Length: %zu\r\n", op->body_len);
    }

    /* Add Connection: close so server closes after response */
    pos += snprintf(req_buf + pos, req_cap - pos, "Connection: close\r\n");

    /* Parse and add custom headers from headers_json.
     * Simple approach: scan for "key":"value" patterns. */
    if (op->headers_json && op->headers_json[0] == '{') {
        const char *p = op->headers_json + 1;
        while (*p && *p != '}') {
            /* Skip whitespace and commas */
            while (*p == ' ' || *p == ',' || *p == '\t') p++;
            if (*p == '}' || *p == '\0') break;

            /* Expect opening quote for key */
            if (*p != '"') { p++; continue; }
            p++;

            /* Read key */
            const char *key_start = p;
            while (*p && *p != '"') {
                if (*p == '\\') p++;
                p++;
            }
            size_t key_len = (size_t)(p - key_start);
            if (*p == '"') p++;

            /* Skip colon */
            while (*p == ' ' || *p == ':' || *p == '\t') p++;

            /* Expect opening quote for value */
            if (*p != '"') { p++; continue; }
            p++;

            /* Read value */
            const char *val_start = p;
            while (*p && *p != '"') {
                if (*p == '\\') p++;
                p++;
            }
            size_t val_len = (size_t)(p - val_start);
            if (*p == '"') p++;

            /* Add header line: key: value */
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

    /* End of headers */
    req_buf[pos++] = '\r';
    req_buf[pos++] = '\n';

    /* Append body if present */
    if (op->body && op->body_len > 0 && pos + op->body_len < req_cap) {
        memcpy(req_buf + pos, op->body, op->body_len);
        pos += op->body_len;
    }

    op->req_buf = req_buf;
    op->req_buf_len = pos;

#ifdef QWRT_WITH_TLS
    if (op->use_tls) {
        /* Encrypt and send through TLS */
        int ret = mbedtls_ssl_write(&op->ssl, (const unsigned char *)req_buf, pos);
        if (ret < 0) {
            free(req_buf);
            pal_uv_http_finish_error(op, -5, "TLS write failed");
            return;
        }
        free(req_buf);
        op->req_buf = NULL;
        /* Start reading response via TLS */
        if (op->streaming) {
            uv_read_start((uv_stream_t *)&op->tcp, pal_uv_http_alloc_cb,
                          tls_stream_read_cb);
            /* Start read-idle timer for TLS streaming */
            if (uv_timer_init(op->self->loop, &op->idle_timer) == 0) {
                op->idle_timer_init = 1;
                op->idle_timer.data = op;
                pal_uv_track_handle(op->self, (uv_handle_t *)&op->idle_timer);
                uv_timer_start(&op->idle_timer, pal_uv_http_idle_timer_cb,
                               PAL_UV_READ_IDLE_TIMEOUT_MS,
                               PAL_UV_READ_IDLE_TIMEOUT_MS);
            }
        } else {
            uv_read_start((uv_stream_t *)&op->tcp, pal_uv_http_alloc_cb,
                          tls_read_cb);
        }
        return;
    }
#endif

    /* Send the request (non-TLS path) */
    uv_buf_t write_buf;
    write_buf.base = op->req_buf;
    write_buf.len = op->req_buf_len;

    op->write_req.data = op;
    uv_write(&op->write_req, (uv_stream_t *)&op->tcp, &write_buf, 1,
             pal_uv_http_write_cb);
}

/* ================================================================
 * TLS handshake read callback — feeds encrypted data to mbedTLS.
 * On handshake completion, proceeds to send the HTTP request.
 * ================================================================ */

#ifdef QWRT_WITH_TLS
static void tls_handshake_read_cb(uv_stream_t *stream, ssize_t nread,
                                   const uv_buf_t *buf)
{
    pal_uv_http_op_t *op = (pal_uv_http_op_t *)stream->data;
    if (op->aborted) {
        if (buf && buf->base) free(buf->base);
        return;
    }
    if (nread < 0) {
        free(buf->base);
        pal_uv_http_finish_error(op, -5, "TLS handshake read error");
        return;
    }

    /* Free previous buffer if partially consumed */
    if (op->tls_read_buf != NULL) {
        free(op->tls_read_buf);
    }
    op->tls_read_buf = (unsigned char *)buf->base;
    op->tls_read_buf_len = (size_t)nread;
    op->tls_read_consumed = 0;

    int ret = mbedtls_ssl_handshake(&op->ssl);
    if (ret == 0) {
        op->tls_handshake_done = 1;
        uv_read_stop((uv_stream_t *)&op->tcp);
        free(op->tls_read_buf);
        op->tls_read_buf = NULL;
        op->tls_read_buf_len = 0;
        op->tls_read_consumed = 0;
        pal_uv_http_send_request(op);
    } else if (ret != MBEDTLS_ERR_SSL_WANT_READ &&
               ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
        char err[128];
        mbedtls_strerror(ret, err, sizeof(err));
        free(op->tls_read_buf);
        op->tls_read_buf = NULL;
        op->tls_read_buf_len = 0;
        pal_uv_http_finish_error(op, -5, err);
    }
    /* On WANT_READ/WANT_WRITE, keep buffer for next recv_cb call */
}
#endif

/* ================================================================
 * HTTP connect callback — called after TCP connection is established.
 * Stops the connect timer, then sends the HTTP request.
 * ================================================================ */

static void pal_uv_http_connect_cb(uv_connect_t *req, int status)
{
    pal_uv_http_op_t *op = (pal_uv_http_op_t *)req->data;

    /* If the op was aborted (pal_uv_http_abort), teardown is already in
     * progress / the op may be freed — do nothing. */
    if (op->aborted) return;

    /* Stop the connect timer on any connect result */
    if (op->connect_timer.data && !uv_is_closing((uv_handle_t *)&op->connect_timer)) {
        uv_timer_stop(&op->connect_timer);
        uv_close((uv_handle_t *)&op->connect_timer, pal_uv_http_timer_close_cb);
    }

    if (status < 0) {
        pal_uv_http_finish_error(op, -5, "connection failed");
        return;
    }

    /* If TLS was requested, initiate handshake */
    if (op->use_tls) {
#ifdef QWRT_WITH_TLS
        if (tls_init_op(op) != 0) {
            pal_uv_http_finish_error(op, -5, "TLS init failed");
            return;
        }
        mbedtls_ssl_set_bio(&op->ssl, op, tls_send_cb, tls_recv_cb, NULL);
        op->tls_handshake_done = 0;
        /* Start reading for handshake data */
        uv_read_start((uv_stream_t *)&op->tcp, pal_uv_http_alloc_cb,
                      tls_handshake_read_cb);
        /* Kick off the handshake — this sends ClientHello via tls_send_cb */
        {
            int ret = mbedtls_ssl_handshake(&op->ssl);
            if (ret == 0) {
                /* Handshake completed synchronously (unlikely) */
                op->tls_handshake_done = 1;
                uv_read_stop((uv_stream_t *)&op->tcp);
                pal_uv_http_send_request(op);
            } else if (ret != MBEDTLS_ERR_SSL_WANT_READ &&
                       ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
                char err[128];
                mbedtls_strerror(ret, err, sizeof(err));
                pal_uv_http_finish_error(op, -5, err);
            }
            /* WANT_READ/WANT_WRITE: wait for tls_handshake_read_cb */
        }
        return;
#else
        pal_uv_http_finish_error(op, -5, "TLS not supported: compile with QWRT_WITH_TLS");
        return;
#endif
    }

    pal_uv_http_send_request(op);
}

static void pal_uv_http_write_cb(uv_write_t *req, int status)
{
    pal_uv_http_op_t *op = (pal_uv_http_op_t *)req->data;

    if (op->aborted) return;  /* teardown in progress — don't start reads */

    if (status < 0) {
        pal_uv_http_finish_error(op, -5, "write error");
        return;
    }

    /* Request sent — start reading response */
    if (op->streaming) {
        uv_read_start((uv_stream_t *)&op->tcp, pal_uv_http_alloc_cb,
                      pal_uv_http_stream_read_cb);
        /* Start read-idle timer for streaming */
        if (uv_timer_init(op->self->loop, &op->idle_timer) == 0) {
            op->idle_timer_init = 1;
            op->idle_timer.data = op;
            pal_uv_track_handle(op->self, (uv_handle_t *)&op->idle_timer);
            uv_timer_start(&op->idle_timer, pal_uv_http_idle_timer_cb,
                           PAL_UV_READ_IDLE_TIMEOUT_MS,
                           PAL_UV_READ_IDLE_TIMEOUT_MS);
        }
    } else {
        uv_read_start((uv_stream_t *)&op->tcp, pal_uv_http_alloc_cb,
                      pal_uv_http_read_cb);
    }
}

/* ================================================================
 * HTTP response data processor — appends data to response buffer
 * and parses headers/body. Called from both TLS and non-TLS paths.
 *
 * Returns 1 if the operation completed (success or error dispatched),
 * 0 if more data is needed.
 * ================================================================ */

static int pal_uv_http_process_data(pal_uv_http_op_t *op, const char *data,
                                     size_t len)
{
    /* Append to response buffer */
    size_t new_len = op->resp_buf_len + len;
    if (new_len > op->resp_buf_cap) {
        size_t new_cap = op->resp_buf_cap * 2;
        if (new_cap < new_len) new_cap = new_len;
        char *new_buf = (char *)realloc(op->resp_buf, new_cap);
        if (!new_buf) {
            pal_uv_http_finish_error(op, -1, "out of memory");
            return 1;
        }
        op->resp_buf = new_buf;
        op->resp_buf_cap = new_cap;
    }
    memcpy(op->resp_buf + op->resp_buf_len, data, len);
    op->resp_buf_len = new_len;

    /* Try to parse headers if not done yet */
    if (!op->headers_done) {
        if (parse_http_response(op) == 0) {
            /* Headers parsed — check body encoding */

            if (op->chunked) {
                /* Process any body data already in the buffer */
                const char *body_start = op->resp_buf + op->header_end_offset;
                size_t body_len = op->resp_buf_len - op->header_end_offset;
                int result = process_chunked_body(op, body_start, body_len);
                if (result == 1) {
                    /* Chunked body complete */
                    pal_uv_http_finish_success(op);
                    return 1;
                } else if (result < 0) {
                    pal_uv_http_finish_error(op, -5, "chunked encoding parse error");
                    return 1;
                }
                /* else: need more data, keep reading */
            } else if (op->body_expected > 0) {
                /* Content-Length: check if we have all body bytes */
                op->body_received = op->resp_buf_len - op->header_end_offset;
                if (op->body_received >= op->body_expected) {
                    /* We have all the body bytes — finish */
                    op->resp_buf_len = op->header_end_offset + op->body_expected;
                    pal_uv_http_finish_success(op);
                    return 1;
                }
            }
            /* else: no Content-Length and not chunked — fall back to EOF */
        }
        return 0;
    }

    /* Headers already parsed — process additional body data */
    if (op->chunked) {
        const char *body_start = op->resp_buf + op->header_end_offset;
        size_t body_len = op->resp_buf_len - op->header_end_offset;
        int result = process_chunked_body(op, body_start, body_len);
        if (result == 1) {
            pal_uv_http_finish_success(op);
            return 1;
        } else if (result < 0) {
            pal_uv_http_finish_error(op, -5, "chunked encoding parse error");
            return 1;
        }
        /* else: need more data, keep reading */
    } else if (op->body_expected > 0) {
        /* Content-Length: check if we have all body bytes */
        op->body_received = op->resp_buf_len - op->header_end_offset;
        if (op->body_received >= op->body_expected) {
            op->resp_buf_len = op->header_end_offset + op->body_expected;
            pal_uv_http_finish_success(op);
            return 1;
        }
    }
    /* else: no Content-Length and not chunked — keep reading until EOF */
    return 0;
}

/* ================================================================
 * TLS-aware read callback — decrypts incoming data via mbedTLS
 * and feeds it to the HTTP response parser.
 * ================================================================ */

#ifdef QWRT_WITH_TLS
static void tls_read_cb(uv_stream_t *stream, ssize_t nread,
                         const uv_buf_t *buf)
{
    pal_uv_http_op_t *op = (pal_uv_http_op_t *)stream->data;

    if (nread < 0) {
        free(buf->base);
        if (nread == UV_EOF) {
            /* Connection closed — finish with what we have */
            if (op->headers_done) {
                if (op->chunked && op->chunk_state != CHUNK_STATE_DONE) {
                    pal_uv_http_finish_error(op, -5,
                        "connection closed before chunked body complete");
                } else {
                    pal_uv_http_finish_success(op);
                }
            } else {
                pal_uv_http_finish_error(op, -5,
                    "connection closed before headers");
            }
        } else {
            pal_uv_http_finish_error(op, -5, "TLS read error");
        }
        return;
    }

    if (nread == 0) {
        free(buf->base);
        return;
    }

    /* Free previous TLS read buffer if any */
    if (op->tls_read_buf) {
        free(op->tls_read_buf);
    }
    op->tls_read_buf = (unsigned char *)buf->base;
    op->tls_read_buf_len = (size_t)nread;
    op->tls_read_consumed = 0;

    /* Decrypt and feed to HTTP parser */
    unsigned char decrypt_buf[8192];
    int ret;
    while ((ret = mbedtls_ssl_read(&op->ssl, decrypt_buf,
                                    sizeof(decrypt_buf))) > 0) {
        /* Feed decrypted data to the HTTP response parser */
        if (pal_uv_http_process_data(op, (const char *)decrypt_buf,
                                      (size_t)ret)) {
            /* Operation completed — clean up TLS buffer */
            free(op->tls_read_buf);
            op->tls_read_buf = NULL;
            op->tls_read_buf_len = 0;
            op->tls_read_consumed = 0;
            return;
        }
    }

    /* Clean up consumed TLS buffer */
    if (op->tls_read_consumed >= op->tls_read_buf_len) {
        free(op->tls_read_buf);
        op->tls_read_buf = NULL;
        op->tls_read_buf_len = 0;
        op->tls_read_consumed = 0;
    }

    if (ret != MBEDTLS_ERR_SSL_WANT_READ) {
        if (ret == 0) {
            /* TLS connection closed cleanly */
            pal_uv_http_finish_success(op);
        } else if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            pal_uv_http_finish_success(op);
        } else {
            pal_uv_http_finish_error(op, -5, "TLS decrypt error");
        }
    }
    /* On WANT_READ, keep reading — more encrypted data needed */
}
#endif

/* ================================================================
 * HTTP read callback — reads response data and handles:
 *  - Content-Length: reads exactly N body bytes then finishes
 *  - Chunked transfer-encoding: decodes chunks until 0-length final chunk
 *  - Fallback: reads until EOF if neither Content-Length nor chunked
 * ================================================================ */

static void pal_uv_http_read_cb(uv_stream_t *stream, ssize_t nread,
                                const uv_buf_t *buf)
{
    pal_uv_http_op_t *op = (pal_uv_http_op_t *)stream->data;

    if (nread < 0) {
        if (nread == UV_EOF) {
            /* Connection closed — if we have headers, deliver what we have */
            if (op->headers_done) {
                /* For chunked, EOF before complete is an error unless done */
                if (op->chunked && op->chunk_state != CHUNK_STATE_DONE) {
                    pal_uv_http_finish_error(op, -5, "connection closed before chunked body complete");
                } else {
                    pal_uv_http_finish_success(op);
                }
            } else {
                pal_uv_http_finish_error(op, -5, "connection closed before headers");
            }
        } else {
            pal_uv_http_finish_error(op, -5, "read error");
        }
        free(buf->base);
        return;
    }

    if (nread == 0) {
        free(buf->base);
        return;
    }

    /* Process the data through the shared processor */
    pal_uv_http_process_data(op, buf->base, (size_t)nread);
    free(buf->base);
}

/* ================================================================
 * Streaming HTTP — cleanup and finish helpers
 * ================================================================ */

static void pal_uv_http_stream_close_cb(uv_handle_t *handle)
{
    pal_uv_http_op_t *op = (pal_uv_http_op_t *)handle->data;
    pal_uv_untrack_handle(op->self, (uv_handle_t *)&op->tcp);
    pal_uv_http_cleanup(op);
}

static void pal_uv_http_stream_cleanup(pal_uv_http_op_t *op)
{
    /*
     * Idempotency guard. The non-abort teardown paths (idle timeout, write
     * error, connect error, chunked/parse error) call on_end + stream_cleanup,
     * which uv_close()s the TCP while reads may still be pending. libuv then
     * delivers a read callback with UV_ECANCELED — without this guard that
     * callback would re-enter the nread<0 path, fire on_end a SECOND time,
     * and call stream_cleanup again, which (seeing tcp already closing) would
     * fall through to free(op) while the first close callback is still
     * pending → double-free / UAF. Setting teardown_started here makes both
     * stream_cleanup and the read callback no-op on re-entry.
     */
    if (op->teardown_started) return;
    op->teardown_started = 1;

    /* Clear the active-stream tracker first. The op itself is freed later
     * (in the TCP close callback or via pal_uv_http_cleanup below), but no
     * other code should reach it via active_stream after teardown begins. */
    if (op->self && op->self->active_stream == op) {
        op->self->active_stream = NULL;
    }

    /* Stop and close the idle timer if initialized and active */
    if (op->idle_timer_init && op->idle_timer.data &&
        !uv_is_closing((uv_handle_t *)&op->idle_timer)) {
        uv_timer_stop(&op->idle_timer);
        uv_close((uv_handle_t *)&op->idle_timer, pal_uv_http_timer_close_cb);
    }

    /* Stop and close the connect timer if initialized and active */
    if (op->timer_init && op->connect_timer.data &&
        !uv_is_closing((uv_handle_t *)&op->connect_timer)) {
        uv_timer_stop(&op->connect_timer);
        uv_close((uv_handle_t *)&op->connect_timer, pal_uv_http_timer_close_cb);
    }

    /* Close TCP handle; cleanup happens in close callback */
    if (op->tcp_init && !uv_is_closing((uv_handle_t *)&op->tcp)) {
        uv_close((uv_handle_t *)&op->tcp, pal_uv_http_stream_close_cb);
    } else if (op->aborted && !op->tcp_init) {
        /*
         * Aborted before TCP was initialized: DNS (uv_getaddrinfo) is likely
         * still in flight and will fire its callback later with this op. We
         * must NOT free the op here — cancel the addrinfo request and let the
         * getaddrinfo callback (which checks op->aborted) free it. Otherwise
         * the callback would touch freed memory.
         */
        uv_cancel((uv_req_t *)&op->addr_req);
    } else {
        pal_uv_http_cleanup(op);
    }
}

/*
 * Abort the currently-active streaming HTTP request (if any).
 * Delivers an on_end error to the stream consumer (so the fetch Promise
 * rejects) and tears down the TCP connection + timers. Must be called on
 * the loop thread (host calls it from the poll loop's cancel branch,
 * which runs on the owner thread).
 */
static void pal_uv_http_abort(qwrt_pal_t *pal)
{
    pal_uv_t *self = pal_uv_self(pal);
    pal_uv_http_op_t *op = self->active_stream;
    if (!op) return;

    /* Mark aborted so any in-flight callbacks (connect, read, timer) that
     * fire after we begin teardown become no-ops instead of touching the op
     * (which may be freed by the TCP close callback). */
    op->aborted = 1;

    /* Deliver a cancellation error to the JS consumer before teardown so
     * the fetch Promise rejects rather than hanging. Use -7 (CANCELLED)
     * to mirror error codes. */
    if (op->stream_ops.on_end) {
        op->stream_ops.on_end(op->stream_ops.user_data, -7);
    }

    /* Tear down handles (clears active_stream, closes TCP/timers, frees op
     * via the TCP close callback). */
    pal_uv_http_stream_cleanup(op);
}

/* ================================================================
 * Streaming chunked transfer-encoding decoder
 *
 * Decodes chunked TE data and calls on_data for decoded chunks.
 * Returns: 1 = final chunk seen, 0 = need more data, -1 = error
 * ================================================================ */

static int stream_decode_chunked(pal_uv_http_op_t *op,
                                 const char *data, size_t len)
{
    const char *p = data;
    const char *end = data + len;

    while (p < end) {
        if (op->chunk_state == CHUNK_STATE_SIZE) {
            /* Reading chunk-size line. Look for \r\n. */
            const char *eol = p;
            while (eol < end && !(*eol == '\r' && (eol + 1 < end) && *(eol + 1) == '\n')) {
                eol++;
            }

            if (eol >= end) {
                /* Partial chunk-size line — save what we have */
                size_t avail = (size_t)(end - p);
                if (op->chunk_size_buf_len + avail < sizeof(op->chunk_size_buf) - 1) {
                    memcpy(op->chunk_size_buf + op->chunk_size_buf_len, p, avail);
                    op->chunk_size_buf_len += avail;
                }
                break; /* need more data */
            }

            /* We have a complete chunk-size line.
             * Combine with any previously buffered partial. */
            char size_buf[32];
            size_t size_buf_len = op->chunk_size_buf_len;
            if (size_buf_len > 0) {
                if (size_buf_len >= sizeof(size_buf)) size_buf_len = sizeof(size_buf) - 1;
                memcpy(size_buf, op->chunk_size_buf, size_buf_len);
                op->chunk_size_buf_len = 0;
            }
            size_t line_len = (size_t)(eol - p);
            if (size_buf_len + line_len >= sizeof(size_buf))
                line_len = sizeof(size_buf) - 1 - size_buf_len;
            memcpy(size_buf + size_buf_len, p, line_len);
            size_buf_len += line_len;
            size_buf[size_buf_len] = '\0';

            /* Parse chunk size (hex, may have extensions after ;) */
            errno = 0;
            op->chunk_size = (size_t)strtoul(size_buf, NULL, 16);

            if (errno == ERANGE || op->chunk_size > PAL_UV_MAX_CHUNK_SIZE) {
                return -1; /* chunk too large or overflow */
            }

            if (op->chunk_size == 0) {
                /* Final chunk */
                op->chunk_state = CHUNK_STATE_DONE;
                return 1;
            }

            op->chunk_remaining = op->chunk_size;
            op->chunk_state = CHUNK_STATE_DATA;
            p = eol + 2; /* skip \r\n after chunk-size */

        } else if (op->chunk_state == CHUNK_STATE_DATA) {
            /* Read chunk data bytes — deliver directly via on_data */
            size_t avail = (size_t)(end - p);
            size_t to_deliver = avail < op->chunk_remaining ? avail : op->chunk_remaining;

            if (op->stream_ops.on_data) {
                op->stream_ops.on_data(op->stream_ops.user_data, p, to_deliver);
            }
            p += to_deliver;
            op->chunk_remaining -= to_deliver;

            if (op->chunk_remaining == 0) {
                op->chunk_state = CHUNK_STATE_TRAILER;
            }

        } else if (op->chunk_state == CHUNK_STATE_TRAILER) {
            /* Expect \r\n after chunk data */
            if (end - p < 2) {
                break; /* need more data */
            }
            if (p[0] != '\r' || p[1] != '\n') {
                return -1; /* malformed */
            }
            p += 2;
            op->chunk_state = CHUNK_STATE_SIZE;

        } else if (op->chunk_state == CHUNK_STATE_DONE) {
            break;
        }
    }

    return (op->chunk_state == CHUNK_STATE_DONE) ? 1 : 0;
}

/* ================================================================
 * Streaming response data processor
 *
 * Accumulates header data until headers are complete, then delivers
 * on_headers callback and switches to body delivery mode via on_data.
 * For chunked responses, decodes chunks before calling on_data.
 *
 * Returns 1 if the operation completed (end callback dispatched),
 * 0 if more data is needed.
 * ================================================================ */

static int pal_uv_http_stream_process_data(pal_uv_http_op_t *op,
                                            const char *data, size_t len)
{
    if (op->headers_parsed) {
        /* Body data — check for chunked encoding */
        if (op->chunked) {
            int result = stream_decode_chunked(op, data, len);
            if (result == 1) {
                /* Final chunk seen */
                if (op->stream_ops.on_end) {
                    op->stream_ops.on_end(op->stream_ops.user_data, 0);
                }
                pal_uv_http_stream_cleanup(op);
                return 1;
            } else if (result < 0) {
                if (op->stream_ops.on_end) {
                    op->stream_ops.on_end(op->stream_ops.user_data, -5);
                }
                pal_uv_http_stream_cleanup(op);
                return 1;
            }
        } else {
            /* Non-chunked body — deliver raw bytes */
            if (op->stream_ops.on_data) {
                op->stream_ops.on_data(op->stream_ops.user_data, data, len);
            }
        }
        return 0;
    }

    /* Headers not yet parsed — accumulate header data */
    size_t new_len = op->resp_headers_len + len;
    char *new_buf = (char *)realloc(op->resp_headers, new_len + 1);
    if (!new_buf) {
        if (op->stream_ops.on_end) {
            op->stream_ops.on_end(op->stream_ops.user_data, -5);
        }
        pal_uv_http_stream_cleanup(op);
        return 1;
    }
    op->resp_headers = new_buf;
    memcpy(op->resp_headers + op->resp_headers_len, data, len);
    op->resp_headers_len = new_len;
    op->resp_headers[new_len] = '\0';

    /* Look for \r\n\r\n marking end of headers */
    char *hdr_end = NULL;
    size_t i;
    for (i = 3; i < op->resp_headers_len; i++) {
        if (op->resp_headers[i - 3] == '\r' && op->resp_headers[i - 2] == '\n' &&
            op->resp_headers[i - 1] == '\r' && op->resp_headers[i] == '\n') {
            hdr_end = op->resp_headers + i + 1;
            break;
        }
    }

    if (!hdr_end) {
        return 0; /* need more header data */
    }

    /* Parse status line: HTTP/1.x NNN ... */
    char *line_end = NULL;
    for (i = 0; i < op->resp_headers_len; i++) {
        if (op->resp_headers[i] == '\r' && op->resp_headers[i + 1] == '\n') {
            line_end = op->resp_headers + i;
            break;
        }
    }

    int http_status = 0;
    if (line_end) {
        char *sp = op->resp_headers;
        while (sp < line_end && *sp != ' ') sp++;
        if (sp < line_end) sp++; /* skip space */
        int digits = 0;
        while (sp < line_end && *sp >= '0' && *sp <= '9' && digits < 3) {
            http_status = http_status * 10 + (*sp - '0');
            sp++;
            digits++;
        }
    }

    /* Scan headers for Transfer-Encoding: chunked */
    op->chunked = 0;
    size_t header_end_offset = (size_t)(hdr_end - op->resp_headers);
    const char *hdrs = op->resp_headers + (line_end - op->resp_headers) + 2;
    const char *hdrs_end = op->resp_headers + header_end_offset - 4;

    const char *hp = hdrs;
    while (hp < hdrs_end) {
        const char *eol = hp;
        while (eol < hdrs_end && !(*eol == '\r' && (eol + 1 < hdrs_end) && *(eol + 1) == '\n')) {
            eol++;
        }

        if (eol - hp > 26 && strncasecmp(hp, "Transfer-Encoding:", 18) == 0) {
            const char *val = hp + 18;
            while (val < eol && (*val == ' ' || *val == '\t')) val++;
            if (eol - val >= 7 && strncasecmp(val, "chunked", 7) == 0) {
                op->chunked = 1;
            }
        }

        hp = eol + 2;
        if (hp > hdrs_end) hp = hdrs_end;
    }

    /* Build headers JSON for on_headers callback */
    char *headers_json = NULL;
    size_t headers_json_len = 0;

    if (line_end) {
        const char *status_line_end = op->resp_headers + (line_end - op->resp_headers) + 2;
        size_t hdr_len = (size_t)(hdrs_end - status_line_end);
        headers_json = build_headers_json(status_line_end, hdr_len, &headers_json_len);
    }

    /* Deliver on_headers callback */
    if (op->stream_ops.on_headers) {
        op->stream_ops.on_headers(op->stream_ops.user_data, http_status,
                                   headers_json ? headers_json : "{}");
    }
    free(headers_json);

    op->headers_parsed = 1;
    op->chunk_state = CHUNK_STATE_SIZE;

    /* Any data after the header boundary is body data */
    size_t body_offset = header_end_offset;
    size_t body_len = op->resp_headers_len - body_offset;

    if (body_len > 0) {
        /* Process remaining body data */
        return pal_uv_http_stream_process_data(op,
                                                op->resp_headers + body_offset,
                                                body_len);
    }

    return 0;
}

/* ================================================================
 * Streaming HTTP read callback — reads response data and feeds it
 * to the streaming processor for header/body delivery.
 * ================================================================ */

static void pal_uv_http_stream_read_cb(uv_stream_t *stream, ssize_t nread,
                                        const uv_buf_t *buf)
{
    pal_uv_http_op_t *op = (pal_uv_http_op_t *)stream->data;

    /* Aborted or teardown-in-progress: a forced close (idle timeout/error/etc.)
     * delivers a final UV_ECANCELED read here; treat as no-op so we don't
     * re-enter on_end / stream_cleanup and double-free. */
    if (op->aborted || op->teardown_started) {
        if (buf && buf->base) free(buf->base);
        return;
    }

    if (nread < 0) {
        free(buf->base);
        if (nread == UV_EOF) {
            if (op->stream_ops.on_end) {
                if (op->headers_parsed) {
                    if (op->chunked && op->chunk_state != CHUNK_STATE_DONE) {
                        op->stream_ops.on_end(op->stream_ops.user_data, -5);
                    } else {
                        op->stream_ops.on_end(op->stream_ops.user_data, 0);
                    }
                } else {
                    op->stream_ops.on_end(op->stream_ops.user_data, -5);
                }
            }
        } else {
            if (op->stream_ops.on_end) {
                op->stream_ops.on_end(op->stream_ops.user_data, -5);
            }
        }
        pal_uv_http_stream_cleanup(op);
        return;
    }

    if (nread == 0) {
        free(buf->base);
        return;
    }

    pal_uv_http_stream_process_data(op, buf->base, (size_t)nread);
    free(buf->base);

    /* Reset idle timer on data received */
    if (op->idle_timer_init && !uv_is_closing((uv_handle_t *)&op->idle_timer)) {
        uv_timer_again(&op->idle_timer);
    }
}

/* ================================================================
 * TLS-aware streaming read callback — decrypts incoming data via
 * mbedTLS and feeds it to the streaming response processor.
 * ================================================================ */

#ifdef QWRT_WITH_TLS
static void tls_stream_read_cb(uv_stream_t *stream, ssize_t nread,
                                const uv_buf_t *buf)
{
    pal_uv_http_op_t *op = (pal_uv_http_op_t *)stream->data;

    /* Aborted or teardown-in-progress: mirror the non-TLS read-cb guard so a
     * post-close UV_ECANCELED doesn't double-fire on_end / double-free. */
    if (op->aborted || op->teardown_started) {
        if (buf && buf->base) free(buf->base);
        return;
    }

    if (nread < 0) {
        free(buf->base);
        if (nread == UV_EOF) {
            if (op->stream_ops.on_end) {
                if (op->headers_parsed) {
                    if (op->chunked && op->chunk_state != CHUNK_STATE_DONE) {
                        op->stream_ops.on_end(op->stream_ops.user_data, -5);
                    } else {
                        op->stream_ops.on_end(op->stream_ops.user_data, 0);
                    }
                } else {
                    op->stream_ops.on_end(op->stream_ops.user_data, -5);
                }
            }
        } else {
            if (op->stream_ops.on_end) {
                op->stream_ops.on_end(op->stream_ops.user_data, -5);
            }
        }
        pal_uv_http_stream_cleanup(op);
        return;
    }

    if (nread == 0) {
        free(buf->base);
        return;
    }

    /* Free previous TLS read buffer if any */
    if (op->tls_read_buf) {
        free(op->tls_read_buf);
    }
    op->tls_read_buf = (unsigned char *)buf->base;
    op->tls_read_buf_len = (size_t)nread;
    op->tls_read_consumed = 0;

    /* Decrypt and feed to streaming processor */
    unsigned char decrypt_buf[8192];
    int ret;
    while ((ret = mbedtls_ssl_read(&op->ssl, decrypt_buf,
                                    sizeof(decrypt_buf))) > 0) {
        if (pal_uv_http_stream_process_data(op, (const char *)decrypt_buf,
                                             (size_t)ret)) {
            /* Operation completed — clean up TLS buffer */
            free(op->tls_read_buf);
            op->tls_read_buf = NULL;
            op->tls_read_buf_len = 0;
            op->tls_read_consumed = 0;
            return;
        }
    }

    /* Clean up consumed TLS buffer */
    if (op->tls_read_consumed >= op->tls_read_buf_len) {
        free(op->tls_read_buf);
        op->tls_read_buf = NULL;
        op->tls_read_buf_len = 0;
        op->tls_read_consumed = 0;
    }

    /* Reset idle timer on data received */
    if (op->idle_timer_init && !uv_is_closing((uv_handle_t *)&op->idle_timer)) {
        uv_timer_again(&op->idle_timer);
    }

    if (ret != MBEDTLS_ERR_SSL_WANT_READ) {
        if (ret == 0 || ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            /* TLS connection closed cleanly */
            if (op->stream_ops.on_end) {
                if (op->headers_parsed) {
                    op->stream_ops.on_end(op->stream_ops.user_data, 0);
                } else {
                    op->stream_ops.on_end(op->stream_ops.user_data, -5);
                }
            }
            pal_uv_http_stream_cleanup(op);
        } else {
            if (op->stream_ops.on_end) {
                op->stream_ops.on_end(op->stream_ops.user_data, -5);
            }
            pal_uv_http_stream_cleanup(op);
        }
    }
    /* On WANT_READ, keep reading — more encrypted data needed */
}
#endif

/* ================================================================
 * DNS resolution callback — called after uv_getaddrinfo completes.
 * Initiates the TCP connection and starts the connect timer.
 * ================================================================ */

static void pal_uv_http_getaddrinfo_cb(uv_getaddrinfo_t *req,
                                        int status, struct addrinfo *res)
{
    pal_uv_http_op_t *op = (pal_uv_http_op_t *)req->data;

    if (status < 0) {
        pal_uv_http_finish_error(op, -5, "DNS resolution failed");
        uv_freeaddrinfo(res);
        return;
    }

    if (!res) {
        pal_uv_http_finish_error(op, -5, "DNS resolution returned no addresses");
        return;
    }

    /* Initialize TCP handle */
    int rc = uv_tcp_init(op->self->loop, &op->tcp);
    if (rc < 0) {
        uv_freeaddrinfo(res);
        pal_uv_http_finish_error(op, -5, "tcp init failed");
        return;
    }
    op->tcp_init = 1;
    op->tcp.data = op;
    pal_uv_track_handle(op->self, (uv_handle_t *)&op->tcp);

    /* Connect using the first resolved address */
    op->connect_req.data = op;
    rc = uv_tcp_connect(&op->connect_req, &op->tcp, res->ai_addr,
                        pal_uv_http_connect_cb);
    uv_freeaddrinfo(res);

    if (rc < 0) {
        pal_uv_http_finish_error(op, -5, "connect failed");
        return;
    }

    /* Start connect timer */
    rc = uv_timer_init(op->self->loop, &op->connect_timer);
    if (rc < 0) {
        /* Timer init failed — connection is already started, let it proceed.
         * We just won't have a timeout. Not fatal. */
    } else {
        op->timer_init = 1;
        op->connect_timer.data = op;
        pal_uv_track_handle(op->self, (uv_handle_t *)&op->connect_timer);
        rc = uv_timer_start(&op->connect_timer, pal_uv_http_connect_timer_cb,
                            PAL_UV_CONNECT_TIMEOUT_MS, 0);
        if (rc < 0) {
            /* Timer start failed — non-fatal, connection proceeds without timeout */
            uv_close((uv_handle_t *)&op->connect_timer, pal_uv_http_timer_close_cb);
        }
    }
}

/* ================================================================
 * HTTP request entry point
 * ================================================================ */

static void pal_uv_http_request(qwrt_pal_t *pal,
                                const char *url, const char *method,
                                const char *headers, const char *body,
                                size_t body_len,
                                qwrt_pal_cb_t cb, void *cb_data)
{
    pal_uv_t *self = pal_uv_self(pal);

    if (!url) {
        cb(cb_data, -6, "invalid url", 11);
        return;
    }

    pal_uv_http_op_t *op = (pal_uv_http_op_t *)calloc(1, sizeof(*op));
    if (!op) {
        cb(cb_data, -1, "out of memory", 13);
        return;
    }

    op->cb = cb;
    op->cb_data = cb_data;
    op->self = self;
    op->chunk_state = CHUNK_STATE_SIZE;

    /* Parse URL */
    if (parse_http_url(url, &op->host, &op->port, &op->path, &op->use_tls) < 0) {
        cb(cb_data, -6, "invalid url format", 18);
        free(op);
        return;
    }

    /* If TLS is requested, check compile-time support */
    if (op->use_tls) {
#ifdef QWRT_WITH_TLS
        /* TLS handshake will be initiated after connect */
#else
        /* Error out early before doing any network I/O */
        pal_uv_http_cleanup(op);
        cb(cb_data, -5, "TLS not supported: compile with QWRT_WITH_TLS", 45);
        return;
#endif
    }

    /* Copy method and headers */
    op->method = method ? strdup(method) : strdup("GET");
    op->headers_json = headers ? strdup(headers) : NULL;

    /* Copy body */
    if (body && body_len > 0) {
        op->body = (char *)malloc(body_len + 1);
        if (op->body) {
            memcpy(op->body, body, body_len);
            op->body[body_len] = '\0';
            op->body_len = body_len;
        }
    }

    /* Initialize response buffer */
    op->resp_buf_cap = PAL_UV_HTTP_BUF_INIT;
    op->resp_buf = (char *)malloc(op->resp_buf_cap);
    if (!op->resp_buf) {
        pal_uv_http_cleanup(op);
        cb(cb_data, -1, "out of memory", 13);
        return;
    }

    /* Resolve hostname via uv_getaddrinfo */
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;      /* IPv4 for now */
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    /* Build port string for getaddrinfo */
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", op->port);

    op->addr_req.data = op;
    int rc = uv_getaddrinfo(self->loop, &op->addr_req,
                            pal_uv_http_getaddrinfo_cb,
                            op->host, port_str, &hints);
    if (rc < 0) {
        pal_uv_http_cleanup(op);
        cb(cb_data, -5, "DNS resolution request failed", 29);
        return;
    }
}

/* ================================================================
 * Streaming HTTP request entry point
 *
 * Similar to pal_uv_http_request but uses streaming callbacks instead
 * of a single completion callback. Reuses the same connection flow
 * (DNS -> TCP connect -> send request -> read response) but delivers
 * headers, body chunks, and end-of-stream via stream_ops callbacks.
 * ================================================================ */

/* Streaming-specific error handler — calls on_end instead of cb */
static void pal_uv_http_stream_finish_error(pal_uv_http_op_t *op, int error_status)
{
    /* Idempotency: a re-entry (e.g. a UV_ECANCELED read cb after this close)
     * must not deliver on_end again or reach the free fall-through. */
    if (op->teardown_started) return;
    op->teardown_started = 1;

    /* Stop and close the connect timer if initialized and active */
    if (op->timer_init && op->connect_timer.data &&
        !uv_is_closing((uv_handle_t *)&op->connect_timer)) {
        uv_timer_stop(&op->connect_timer);
        uv_close((uv_handle_t *)&op->connect_timer, pal_uv_http_timer_close_cb);
    }

    if (op->self && op->self->active_stream == op) {
        op->self->active_stream = NULL;
    }

    if (op->stream_ops.on_end) {
        op->stream_ops.on_end(op->stream_ops.user_data, error_status);
    }

    if (op->tcp_init && !uv_is_closing((uv_handle_t *)&op->tcp)) {
        uv_close((uv_handle_t *)&op->tcp, pal_uv_http_stream_close_cb);
    } else {
        pal_uv_http_cleanup(op);
    }
}

/* Streaming DNS resolution callback */
static void pal_uv_http_stream_getaddrinfo_cb(uv_getaddrinfo_t *req,
                                                int status, struct addrinfo *res)
{
    pal_uv_http_op_t *op = (pal_uv_http_op_t *)req->data;

    /*
     * If the op was aborted before DNS completed, stream_cleanup cancelled
     * this request and deferred freeing the op to us. Free addrinfo + op and
     * return — do not touch any handles (none were init'd yet).
     */
    if (op->aborted) {
        if (res) uv_freeaddrinfo(res);
        pal_uv_http_cleanup(op);
        return;
    }

    if (status < 0) {
        pal_uv_http_stream_finish_error(op, -5);
        uv_freeaddrinfo(res);
        return;
    }

    if (!res) {
        pal_uv_http_stream_finish_error(op, -5);
        return;
    }

    /* Initialize TCP handle */
    int rc = uv_tcp_init(op->self->loop, &op->tcp);
    if (rc < 0) {
        uv_freeaddrinfo(res);
        pal_uv_http_stream_finish_error(op, -5);
        return;
    }
    op->tcp_init = 1;
    op->tcp.data = op;
    pal_uv_track_handle(op->self, (uv_handle_t *)&op->tcp);

    /* Connect using the first resolved address */
    op->connect_req.data = op;
    rc = uv_tcp_connect(&op->connect_req, &op->tcp, res->ai_addr,
                        pal_uv_http_connect_cb);
    uv_freeaddrinfo(res);

    if (rc < 0) {
        pal_uv_http_stream_finish_error(op, -5);
        return;
    }

    /* Start connect timer */
    rc = uv_timer_init(op->self->loop, &op->connect_timer);
    if (rc < 0) {
        /* Timer init failed — non-fatal */
    } else {
        op->timer_init = 1;
        op->connect_timer.data = op;
        pal_uv_track_handle(op->self, (uv_handle_t *)&op->connect_timer);
        rc = uv_timer_start(&op->connect_timer, pal_uv_http_connect_timer_cb,
                            PAL_UV_CONNECT_TIMEOUT_MS, 0);
        if (rc < 0) {
            uv_close((uv_handle_t *)&op->connect_timer, pal_uv_http_timer_close_cb);
        }
    }
}

static void pal_uv_http_request_stream(qwrt_pal_t *pal,
                                        const char *url, const char *method,
                                        const char *headers, const char *body,
                                        size_t body_len,
                                        qwrt_pal_stream_ops_t *ops)
{
    pal_uv_t *self = pal_uv_self(pal);

    if (!url || !ops) {
        if (ops && ops->on_end) {
            ops->on_end(ops->user_data, -6);
        }
        return;
    }

    pal_uv_http_op_t *op = (pal_uv_http_op_t *)calloc(1, sizeof(*op));
    if (!op) {
        if (ops->on_end) {
            ops->on_end(ops->user_data, -1);
        }
        return;
    }

    op->cb = NULL;  /* streaming uses ops callbacks instead */
    op->cb_data = NULL;
    op->self = self;
    op->streaming = 1;
    op->stream_ops = *ops;  /* copy stream ops */
    op->chunk_state = CHUNK_STATE_SIZE;

    /* Parse URL */
    if (parse_http_url(url, &op->host, &op->port, &op->path, &op->use_tls) < 0) {
        if (ops->on_end) {
            ops->on_end(ops->user_data, -6);
        }
        free(op);
        return;
    }

    /* If TLS is requested, check compile-time support */
    if (op->use_tls) {
#ifdef QWRT_WITH_TLS
        /* TLS handshake will be initiated after connect */
#else
        pal_uv_http_cleanup(op);
        if (ops->on_end) {
            ops->on_end(ops->user_data, -5);
        }
        return;
#endif
    }

    /* Copy method and headers */
    op->method = method ? strdup(method) : strdup("GET");
    op->headers_json = headers ? strdup(headers) : NULL;

    /* Copy body */
    if (body && body_len > 0) {
        op->body = (char *)malloc(body_len + 1);
        if (op->body) {
            memcpy(op->body, body, body_len);
            op->body[body_len] = '\0';
            op->body_len = body_len;
        }
    }

    /* Resolve hostname via uv_getaddrinfo */
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;      /* IPv4 for now */
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    /* Build port string for getaddrinfo */
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", op->port);

    op->addr_req.data = op;
    int rc = uv_getaddrinfo(self->loop, &op->addr_req,
                            pal_uv_http_stream_getaddrinfo_cb,
                            op->host, port_str, &hints);
    if (rc < 0) {
        pal_uv_http_cleanup(op);
        if (ops->on_end) {
            ops->on_end(ops->user_data, -5);
        }
        return;
    }

    /*
     * Only now is the op committed to async I/O (getaddrinfo submitted) with
     * a callback that will run later. Track it as the active stream so
     * pal_uv_http_abort can reach it. host is single-run, so at most one
     * stream is active at a time; any prior active_stream should already have
     * been cleared by its own stream_cleanup.
     */
    self->active_stream = op;
}

/* ================================================================
 * Timer operations
 * ================================================================ */

static void pal_uv_timer_cb(uv_timer_t *handle)
{
    pal_uv_timer_op_t *op = (pal_uv_timer_op_t *)handle->data;
    op->cb(op->cb_data, 0, NULL, 0);
}

static void *pal_uv_timer_start(qwrt_pal_t *pal, uint64_t delay_ms, int repeat,
                                qwrt_pal_cb_t cb, void *cb_data)
{
    pal_uv_t *self = pal_uv_self(pal);

    pal_uv_timer_op_t *op = (pal_uv_timer_op_t *)calloc(1, sizeof(*op));
    if (!op) return NULL;

    op->cb = cb;
    op->cb_data = cb_data;
    op->self = self;

    int rc = uv_timer_init(self->loop, &op->timer);
    if (rc < 0) {
        free(op);
        return NULL;
    }
    op->timer.data = op;
    pal_uv_track_handle(self, (uv_handle_t *)&op->timer);

    uint64_t repeat_ms = repeat ? delay_ms : 0;
    rc = uv_timer_start(&op->timer, pal_uv_timer_cb, delay_ms, repeat_ms);
    if (rc < 0) {
        pal_uv_untrack_handle(self, (uv_handle_t *)&op->timer);
        uv_close((uv_handle_t *)&op->timer, NULL);
        free(op);
        return NULL;
    }

    return (void *)op;
}

static void pal_uv_timer_close_cb(uv_handle_t *handle)
{
    pal_uv_timer_op_t *op = (pal_uv_timer_op_t *)handle->data;
    pal_uv_untrack_handle(op->self, handle);
    free(op);
}

static void pal_uv_timer_stop(qwrt_pal_t *pal, void *handle)
{
    (void)pal;
    if (!handle) return;

    pal_uv_timer_op_t *op = (pal_uv_timer_op_t *)handle;

    uv_timer_stop(&op->timer);
    if (!uv_is_closing((uv_handle_t *)&op->timer)) {
        uv_close((uv_handle_t *)&op->timer, pal_uv_timer_close_cb);
    }
}

/* ================================================================
 * Synchronous operations
 * ================================================================ */

static uint64_t pal_uv_time_now(qwrt_pal_t *pal)
{
    (void)pal;
    return (uint64_t)(uv_hrtime() / 1000000);
}

static uint64_t pal_uv_hrtime(qwrt_pal_t *pal)
{
    (void)pal;
    return uv_hrtime();
}

static void pal_uv_log(qwrt_pal_t *pal, int level, const char *msg)
{
    (void)pal;
    const char *prefix;
    switch (level) {
    case 0:  prefix = "[INFO]";  break;
    case 1:  prefix = "[WARN]";  break;
    case 2:  prefix = "[ERROR]"; break;
    default: prefix = "[LOG]";   break;
    }
    fprintf(stderr, "%s %s\n", prefix, msg ? msg : "");
}

static void *pal_uv_mem_alloc(qwrt_pal_t *pal, size_t size)
{
    (void)pal;
    return malloc(size);
}

static void pal_uv_mem_free(qwrt_pal_t *pal, void *ptr)
{
    (void)pal;
    free(ptr);
}

static void pal_uv_random_bytes(qwrt_pal_t *pal, uint8_t *buf, size_t len)
{
    (void)pal;
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
        size_t n = fread(buf, 1, len, f);
        fclose(f);
        if (n < len) memset(buf + n, 0, len - n);
    } else {
        memset(buf, 0, len);
    }
}

/* ── Process management (POSIX) ──────────────────────────────────── */
/*
 * Platform-abstracted process primitives:
 *   spawn  → fork+exec    (POSIX)
 *   channel → pipe        (POSIX)
 *   join   → waitpid      (POSIX)
 *   terminate → kill      (POSIX)
 *
 * Each spawned process has a struct that holds PID and pipe fds.
 */

typedef struct pal_uv_proc_t {
    pid_t pid;
    int stdin_fd;   /* write end → child's stdin */
    int stdout_fd;  /* read end  ← child's stdout */
    /* Channel handles for the two pipe directions */
    void *stdin_ch;
    void *stdout_ch;
} pal_uv_proc_t;

/* Channel handle — just wraps an fd */
typedef struct pal_uv_channel_t {
    int fd;
} pal_uv_channel_t;

static void *pal_uv_spawn(qwrt_pal_t *pal,
                          const char *cmd,
                          const char *const *args,
                          const char *const *env)
{
    (void)pal;

    int stdin_pipe[2], stdout_pipe[2];
    if (pipe(stdin_pipe) < 0 || pipe(stdout_pipe) < 0) {
        if (stdin_pipe[0] >= 0) { close(stdin_pipe[0]); close(stdin_pipe[1]); }
        return NULL;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(stdin_pipe[0]); close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        return NULL;
    }

    if (pid == 0) {
        /* Child: redirect stdin/stdout, exec */
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);

        if (env) {
            for (const char *const *e = env; *e; e++) {
                /* Copy string — putenv requires writable */
                char *entry = strdup(*e);
                if (entry) putenv(entry);
            }
        }

        execvp(cmd, (char *const *)args);
        _exit(127);
    }

    /* Parent */
    close(stdin_pipe[0]);
    close(stdout_pipe[1]);

    pal_uv_proc_t *proc = calloc(1, sizeof(*proc));
    if (!proc) {
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
        return NULL;
    }

    proc->pid = pid;
    proc->stdin_fd = stdin_pipe[1];
    proc->stdout_fd = stdout_pipe[0];

    /* Create channel handles */
    pal_uv_channel_t *in_ch = calloc(1, sizeof(*in_ch));
    pal_uv_channel_t *out_ch = calloc(1, sizeof(*out_ch));
    if (in_ch)  in_ch->fd  = stdin_pipe[1];
    if (out_ch) out_ch->fd = stdout_pipe[0];
    proc->stdin_ch  = in_ch;
    proc->stdout_ch = out_ch;

    return proc;
}

static void *pal_uv_spawn_get_stdin(qwrt_pal_t *pal, void *proc)
{
    (void)pal;
    if (!proc) return NULL;
    return ((pal_uv_proc_t *)proc)->stdin_ch;
}

static void *pal_uv_spawn_get_stdout(qwrt_pal_t *pal, void *proc)
{
    (void)pal;
    if (!proc) return NULL;
    return ((pal_uv_proc_t *)proc)->stdout_ch;
}

static int pal_uv_channel_send(qwrt_pal_t *pal, void *ch,
                               const char *data, size_t len)
{
    (void)pal;
    if (!ch || !data) return -1;
    int fd = ((pal_uv_channel_t *)ch)->fd;
    size_t written = 0;
    while (written < len) {
        ssize_t n = write(fd, data + written, len - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        written += (size_t)n;
    }
    return (int)written;
}

static void pal_uv_channel_recv(qwrt_pal_t *pal, void *ch,
                                qwrt_pal_cb_t cb, void *cb_data)
{
    (void)pal;
    if (!ch || !cb) { cb(cb_data, -1, NULL, 0); return; }
    int fd = ((pal_uv_channel_t *)ch)->fd;
    char buf[65536];
    ssize_t n = read(fd, buf, sizeof(buf));
    if (n < 0) {
        cb(cb_data, -1, NULL, 0);
    } else {
        cb(cb_data, 0, buf, (size_t)n);
    }
}

static void pal_uv_channel_close(qwrt_pal_t *pal, void *ch)
{
    (void)pal;
    if (!ch) return;
    int fd = ((pal_uv_channel_t *)ch)->fd;
    if (fd >= 0) close(fd);
    free(ch);
}

static int pal_uv_join(qwrt_pal_t *pal, void *proc, int timeout_ms)
{
    (void)pal;
    if (!proc) return -1;
    pal_uv_proc_t *p = (pal_uv_proc_t *)proc;

    if (timeout_ms == 0) {
        /* Poll — non-blocking */
        int status;
        pid_t r = waitpid(p->pid, &status, WNOHANG);
        if (r == 0) return -1; /* still running */
        if (r < 0) return -1;
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }

    /* Blocking wait */
    int status;
    pid_t r = waitpid(p->pid, &status, 0);
    if (r < 0) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static void pal_uv_terminate(qwrt_pal_t *pal, void *proc)
{
    (void)pal;
    if (!proc) return;
    pal_uv_proc_t *p = (pal_uv_proc_t *)proc;
    kill(p->pid, SIGTERM);

    /* Close remaining fds */
    if (p->stdin_fd >= 0)  close(p->stdin_fd);
    if (p->stdout_fd >= 0) close(p->stdout_fd);
    free(p->stdin_ch);
    free(p->stdout_ch);
    free(p);
}

/* ================================================================
 * Create / Destroy
 * ================================================================ */

/* Forward decl: assigned to pal->run_cycle in pal_uv_create_with_config,
 * defined below pal_uv_get_loop. */
static int pal_uv_run_cycle(qwrt_pal_t *pal, int timeout_ms);

qwrt_pal_t *pal_uv_create_with_config(uv_loop_t *loop, int storage_max)
{
    pal_uv_t *self = (pal_uv_t *)calloc(1, sizeof(*self));
    if (!self) return NULL;

    if (storage_max <= 0) {
        storage_max = PAL_UV_STORAGE_DEFAULT;
    }
    self->storage_max = storage_max;

    /* Allocate storage array */
    self->store = (pal_uv_store_entry_t *)calloc((size_t)storage_max,
                                                   sizeof(pal_uv_store_entry_t));
    if (!self->store) {
        free(self);
        return NULL;
    }

    if (loop) {
        self->loop = loop;
        self->owns_loop = 0;
    } else {
        self->loop = (uv_loop_t *)malloc(sizeof(uv_loop_t));
        if (!self->loop) {
            free(self->store);
            free(self);
            return NULL;
        }
        int rc = uv_loop_init(self->loop);
        if (rc < 0) {
            free(self->loop);
            free(self->store);
            free(self);
            return NULL;
        }
        self->owns_loop = 1;
    }

    /* Set up PAL function pointers */
    qwrt_pal_t *pal = &self->pal;
    pal->user_data = self;

    pal->http_request = pal_uv_http_request;
    pal->http_request_stream = pal_uv_http_request_stream;
    pal->http_abort = pal_uv_http_abort;
    pal->fs_read      = pal_uv_fs_read;
    pal->fs_write     = pal_uv_fs_write;
    pal->fs_exists    = pal_uv_fs_exists;
    pal->fs_remove    = pal_uv_fs_remove;
    pal->fs_list      = pal_uv_fs_list;
    pal->storage_get  = pal_uv_storage_get;
    pal->storage_set  = pal_uv_storage_set;
    pal->storage_del  = pal_uv_storage_del;
    pal->timer_start  = pal_uv_timer_start;
    pal->timer_stop   = pal_uv_timer_stop;
    pal->time_now     = pal_uv_time_now;
    pal->hrtime       = pal_uv_hrtime;
    pal->log          = pal_uv_log;
    pal->mem_alloc    = pal_uv_mem_alloc;
    pal->mem_free     = pal_uv_mem_free;
    pal->random_bytes = pal_uv_random_bytes;
    pal->run_cycle    = pal_uv_run_cycle;

    /* Process management */
    pal->spawn            = pal_uv_spawn;
    pal->spawn_get_stdin  = pal_uv_spawn_get_stdin;
    pal->spawn_get_stdout = pal_uv_spawn_get_stdout;
    pal->channel_send     = pal_uv_channel_send;
    pal->channel_recv     = pal_uv_channel_recv;
    pal->channel_close    = pal_uv_channel_close;
    pal->join             = pal_uv_join;
    pal->terminate        = pal_uv_terminate;

    return pal;
}

qwrt_pal_t *pal_uv_create(uv_loop_t *loop)
{
    return pal_uv_create_with_config(loop, PAL_UV_STORAGE_DEFAULT);
}

static void pal_uv_walk_close_cb(uv_handle_t *handle, void *arg)
{
    (void)arg;
    if (!uv_is_closing(handle)) {
        uv_close(handle, NULL);
    }
}

void pal_uv_destroy(qwrt_pal_t *pal)
{
    if (!pal) return;

    pal_uv_t *self = pal_uv_self(pal);

    /* Close all tracked handles */
    int i;
    for (i = 0; i < self->handle_count; i++) {
        if (self->handles[i] && !uv_is_closing(self->handles[i])) {
            uv_close(self->handles[i], NULL);
        }
    }

    /* Walk the loop and close any remaining handles */
    if (self->loop) {
        uv_walk(self->loop, pal_uv_walk_close_cb, NULL);

        /* Run the loop once to process close callbacks */
        uv_run(self->loop, UV_RUN_NOWAIT);

        if (self->owns_loop) {
            uv_loop_close(self->loop);
            free(self->loop);
        }
    }

    /* Free storage entries */
    for (i = 0; i < self->store_count; i++) {
        free(self->store[i].key);
        free(self->store[i].value);
    }

    free(self->store);
    free(self);
}

uv_loop_t *pal_uv_get_loop(qwrt_pal_t *pal)
{
    if (!pal) return NULL;
    pal_uv_t *self = pal_uv_self(pal);
    return self->loop;
}

/*
 * PAL run_cycle: drive the libuv loop for one iteration with a timeout.
 *
 *   timeout_ms < 0  → UV_RUN_ONCE  (block until the next I/O event/timer)
 *   timeout_ms == 0 → UV_RUN_NOWAIT (process already-ready work only)
 *   timeout_ms > 0  → UV_RUN_ONCE, but only if the loop actually has a
 *                     pending deadline (uv_backend_timeout > 0); otherwise
 *                     UV_RUN_NOWAIT (nothing to block on). This preserves the
 *                     historical poll_blocking behavior where a pending
 *                     async tool with closing HTTP handles would otherwise
 *                     busy-spin.
 *
 * Returns uv_run's result (non-zero if any callback ran), or -1 never
 * (libuv has no "stop" signal here; the host decides when to stop polling).
 */
static int pal_uv_run_cycle(qwrt_pal_t *pal, int timeout_ms)
{
    if (!pal) return 0;
    pal_uv_t *self = pal_uv_self(pal);
    uv_loop_t *loop = self->loop;
    if (!loop) return 0;

    if (timeout_ms == 0) {
        return uv_run(loop, UV_RUN_NOWAIT);
    }
    if (timeout_ms < 0) {
        return uv_run(loop, UV_RUN_ONCE);
    }
    /* timeout_ms > 0: block only if there is a real deadline pending. */
    int bt = uv_backend_timeout(loop);
    if (bt > 0) {
        return uv_run(loop, UV_RUN_ONCE);
    }
    return uv_run(loop, UV_RUN_NOWAIT);
}

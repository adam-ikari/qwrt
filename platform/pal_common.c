/*
 * pal_common.c — Shared helpers for PAL implementations
 *
 * These functions are used by pal_uv, pal_mock, and pal_freertos to build
 * HTTP response JSON, escape JSON strings, and parse HTTP URLs.  They are
 * compiled once into a static library (qwrt_pal_common) and linked by
 * every PAL target.
 *
 * This file has NO dependency on QuickJS — it only needs the C standard
 * library, so it can be used by any PAL regardless of the engine.
 */

#include "pal_common.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ================================================================
 * pal_json_escape — JSON-escape a string into a buffer
 *
 * Returns number of bytes written (not including null terminator),
 * or -1 if the destination buffer is too small (truncation avoided).
 * ================================================================ */

int pal_json_escape(const char *src, size_t src_len,
                    char *dst, size_t dst_cap)
{
    size_t i, j;
    for (i = 0, j = 0; i < src_len; i++) {
        unsigned char c = (unsigned char)src[i];
        switch (c) {
        case '"':  if (j + 2 > dst_cap) return -1; dst[j++] = '\\'; dst[j++] = '"';  break;
        case '\\': if (j + 2 > dst_cap) return -1; dst[j++] = '\\'; dst[j++] = '\\'; break;
        case '\b': if (j + 2 > dst_cap) return -1; dst[j++] = '\\'; dst[j++] = 'b';  break;
        case '\f': if (j + 2 > dst_cap) return -1; dst[j++] = '\\'; dst[j++] = 'f';  break;
        case '\n': if (j + 2 > dst_cap) return -1; dst[j++] = '\\'; dst[j++] = 'n';  break;
        case '\r': if (j + 2 > dst_cap) return -1; dst[j++] = '\\'; dst[j++] = 'r';  break;
        case '\t': if (j + 2 > dst_cap) return -1; dst[j++] = '\\'; dst[j++] = 't';  break;
        default:
            if (c < 0x20) {
                if (j + 7 > dst_cap) return -1;
                j += (size_t)snprintf(dst + j, dst_cap - j, "\\u%04x", c);
            } else {
                if (j + 1 > dst_cap) return -1;
                dst[j++] = (char)c;
            }
            break;
        }
    }
    if (j < dst_cap) {
        dst[j] = '\0';
    } else if (dst_cap > 0) {
        return -1;
    }
    return (int)j;
}

/* ================================================================
 * pal_build_headers_json — Build a JSON object from raw HTTP headers
 *
 * Input: header region (between status line and \r\n\r\n)
 * Output: {"Key":"Value","Key2":"Value2"}
 * ================================================================ */

char *pal_build_headers_json(const char *hdr_start, size_t hdr_len,
                             size_t *out_len)
{
    /* Worst case: each char could become a \u00XX sequence (6 bytes) */
    size_t cap = hdr_len * 6 + 4;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        *out_len = 0;
        return NULL;
    }

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
            {
                int n = pal_json_escape(p, key_len, buf + pos, cap - pos - 1);
                if (n < 0) { free(buf); *out_len = 0; return NULL; }
                pos += (size_t)n;
            }
            buf[pos++] = '"';
            buf[pos++] = ':';
            buf[pos++] = '"';
            {
                int n = pal_json_escape(val_start, val_len, buf + pos, cap - pos - 1);
                if (n < 0) { free(buf); *out_len = 0; return NULL; }
                pos += (size_t)n;
            }
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

/* ================================================================
 * pal_build_http_json — Build JSON result for HTTP response
 *
 * Format: {"status":NNN,"headers":<json>,"body":"<escaped>"}
 * Caller must free the returned string.
 * ================================================================ */

char *pal_build_http_json(int status, const char *headers,
                          size_t headers_len,
                          const char *body, size_t body_len,
                          size_t *out_len)
{
    /* Worst case: body chars could each become \u00XX (6 bytes) */
    size_t cap = 64 + headers_len + body_len * 6 + 128;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        *out_len = 0;
        return NULL;
    }

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

    {
        int n = pal_json_escape(body, body_len, buf + pos, cap - pos - 2);
        if (n < 0) { free(buf); *out_len = 0; return NULL; }
        pos += (size_t)n;
    }

    memcpy(buf + pos, "\"}", 2);
    pos += 2;

    *out_len = pos;
    return buf;
}

/* ================================================================
 * pal_build_json_array — Build JSON array from string list
 *
 * Output: ["item1","item2",...]
 * Caller must free the returned string.
 * ================================================================ */

char *pal_build_json_array(const char *const *items, int count,
                           size_t *out_len)
{
    size_t cap = 4 + (size_t)count * 256;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        *out_len = 0;
        return NULL;
    }

    size_t pos = 0;
    buf[pos++] = '[';

    int i;
    for (i = 0; i < count; i++) {
        if (i > 0) {
            buf[pos++] = ',';
        }
        buf[pos++] = '"';
    {
        int n = pal_json_escape(items[i], strlen(items[i]),
                               buf + pos, cap - pos - 2);
        if (n < 0) { free(buf); *out_len = 0; return NULL; }
        pos += (size_t)n;
    }
        buf[pos++] = '"';
    }

    buf[pos++] = ']';
    buf[pos] = '\0';
    *out_len = pos;
    return buf;
}

/* ================================================================
 * pal_parse_url — Simple URL parser
 *
 * Extracts host, port, path, and TLS flag from http:// or https://
 * URLs.  Returns 0 on success, -1 on invalid scheme or alloc failure.
 * Caller must call pal_url_free() to release heap fields.
 * ================================================================ */

int pal_parse_url(const char *url, pal_url_t *out)
{
    int use_tls = 0;
    const char *p = NULL;

    if (!url || !out) return -1;

    if (strncmp(url, "https://", 8) == 0) {
        use_tls = 1;
        p = url + 8;
    } else if (strncmp(url, "http://", 7) == 0) {
        use_tls = 0;
        p = url + 7;
    } else {
        return -1;
    }

    out->tls = use_tls;

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
    out->host = (char *)malloc(host_len + 1);
    if (!out->host) return -1;
    memcpy(out->host, host_start, host_len);
    out->host[host_len] = '\0';

    /* Extract port */
    if (port_start) {
        long parsed = strtol(port_start, NULL, 10);
        if (parsed <= 0 || parsed > 65535) {
            out->port = use_tls ? 443 : 80;
        } else {
            out->port = (int)parsed;
        }
    } else {
        out->port = use_tls ? 443 : 80;
    }

    /* Extract path */
    out->path = strdup(path_start);
    if (!out->path) {
        free(out->host);
        out->host = NULL;
        return -1;
    }

    return 0;
}

/* ================================================================
 * pal_url_free — Release heap fields of a pal_url_t
 * ================================================================ */

void pal_url_free(pal_url_t *url)
{
    if (!url) return;
    free(url->host);
    free(url->path);
    url->host = NULL;
    url->path = NULL;
}

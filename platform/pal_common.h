#ifndef QWRT_PAL_COMMON_H
#define QWRT_PAL_COMMON_H

/*
 * pal_common.h — Shared helpers for PAL implementations
 *
 * These utilities are used by pal_uv, pal_mock, and pal_freertos to build
 * HTTP response JSON, escape JSON strings, and parse HTTP URLs.  They live
 * in a separate compilation unit (platform/pal_common.c) so each PAL links
 * them without duplicating the code.
 *
 * All returned heap strings must be freed with free().
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── JSON helpers ──────────────────────────────────────────────── */

/**
 * JSON-escape a source string into a pre-allocated destination buffer.
 *
 * Returns the number of bytes written (not including the null terminator),
 * or -1 if the destination buffer is too small (truncation would occur).
 * Worst case: 6 × src_len for \\u00XX escapes + 1 for NUL.
 */
int pal_json_escape(const char *src, size_t src_len,
                    char *dst, size_t dst_cap);

/**
 * Build a JSON object string from raw HTTP header lines.
 *
 * Input: header region between the status line and the terminating
 * \\r\\n\\r\\n.  Output: a malloc'd string like
 *   {"Key":"Value","Key2":"Value2"}
 *
 * On allocation failure returns NULL and sets *out_len to 0.
 */
char *pal_build_headers_json(const char *hdr_start, size_t hdr_len,
                             size_t *out_len);

/**
 * Build the standard HTTP response JSON delivered to qwrt_pal_cb_t.
 *
 * Format: {"status":NNN,"headers":<headers_json>,"body":"<escaped>"}
 *
 * @param status        HTTP status code
 * @param headers       pre-built headers JSON string (may be NULL → "{}")
 * @param headers_len   length of headers JSON
 * @param body          response body bytes
 * @param body_len      length of body
 * @param out_len       receives total length of the returned string
 * @return              malloc'd JSON string, or NULL on allocation failure
 */
char *pal_build_http_json(int status, const char *headers,
                          size_t headers_len,
                          const char *body, size_t body_len,
                          size_t *out_len);

/**
 * Build a JSON array string from a NULL-terminated list of C strings.
 *
 * Output: ["item1","item2",...]
 *
 * @param items    NULL-terminated array of C strings
 * @param count    number of items
 * @param out_len  receives total length of the returned string
 * @return         malloc'd JSON string, or NULL on allocation failure
 */
char *pal_build_json_array(const char *const *items, int count,
                           size_t *out_len);

/* ── URL parsing ───────────────────────────────────────────────── */

/**
 * Parsed URL components.  Call pal_url_free() to release.
 */
typedef struct {
    char *host;         /* heap-allocated hostname */
    int   port;         /* port number (defaults to 80 or 443) */
    char *path;         /* heap-allocated path (always at least "/") */
    int   tls;          /* 1 if https://, 0 if http:// */
} pal_url_t;

/**
 * Parse an http:// or https:// URL into its components.
 *
 * Returns 0 on success, -1 if the scheme is not recognised or an
 * allocation fails.
 */
int pal_parse_url(const char *url, pal_url_t *out);

/**
 * Free the heap-allocated fields of a pal_url_t.
 * Safe to call on a zero-initialized struct.
 */
void pal_url_free(pal_url_t *url);

#ifdef __cplusplus
}
#endif

#endif /* QWRT_PAL_COMMON_H */

/*
 * PAL Common Unit Tests
 *
 * Tests for shared PAL helper functions (pal_json_escape, pal_build_headers_json,
 * pal_build_http_json, pal_build_json_array, pal_parse_url, pal_url_free).
 */

#define _POSIX_C_SOURCE 200809L

#include "test_qwrt.h"
#include "pal_common.h"
#include <stdlib.h>
#include <string.h>

/* ================================================================
 * Group 1: pal_json_escape
 * ================================================================ */

TEST(json_escape_plain) {
    char buf[256];
    int n = pal_json_escape("hello", 5, buf, sizeof(buf));
    ASSERT(n == 5);
    ASSERT_STR_EQ(buf, "hello");
}

TEST(json_escape_quote) {
    char buf[256];
    int n = pal_json_escape("he\"llo", 6, buf, sizeof(buf));
    ASSERT(n == 7);
    ASSERT_STR_EQ(buf, "he\\\"llo");
}

TEST(json_escape_backslash) {
    char buf[256];
    int n = pal_json_escape("a\\b", 3, buf, sizeof(buf));
    ASSERT(n == 4);
    ASSERT_STR_EQ(buf, "a\\\\b");
}

TEST(json_escape_control_chars) {
    char buf[256];
    /* Test the 5 C0 control chars that have short escapes */
    char src[] = { '\n', '\r', '\t', '\b', '\f' };
    int n = pal_json_escape(src, 5, buf, sizeof(buf));
    ASSERT_EQ(n, 10);
    ASSERT_STR_EQ(buf, "\\n\\r\\t\\b\\f");
}

TEST(json_escape_null_char) {
    char buf[256];
    /* Null byte should escape as \\u0000 */
    int n = pal_json_escape("\x00", 1, buf, sizeof(buf));
    ASSERT_EQ(n, 6);
    ASSERT_STR_EQ(buf, "\\u0000");
}

TEST(json_escape_mixed) {
    char buf[512];
    const char *src = "line1\nline2\t\"quoted\"";
    int n = pal_json_escape(src, strlen(src), buf, sizeof(buf));
    ASSERT(n > 0);
    ASSERT(strstr(buf, "\\n") != NULL);
    ASSERT(strstr(buf, "\\t") != NULL);
    ASSERT(strstr(buf, "\\\"") != NULL);
}

TEST(json_escape_empty) {
    char buf[16];
    int n = pal_json_escape("", 0, buf, sizeof(buf));
    ASSERT_EQ(n, 0);
    ASSERT_STR_EQ(buf, "");
}

TEST(json_escape_buffer_too_small) {
    char buf[4];
    int n = pal_json_escape("hello", 5, buf, sizeof(buf));
    ASSERT_EQ(n, -1);
}

TEST(json_escape_exact_fit) {
    /* "abc" — needs 4 bytes (3 chars + NUL) */
    char buf[4];
    int n = pal_json_escape("abc", 3, buf, sizeof(buf));
    ASSERT_EQ(n, 3);
    ASSERT_STR_EQ(buf, "abc");
}

/* ================================================================
 * Group 2: pal_build_headers_json
 * ================================================================ */

TEST(build_headers_basic) {
    const char *hdr = "Content-Type: text/html\r\nX-Custom: value\r\n";
    size_t out_len = 0;
    char *json = pal_build_headers_json(hdr, strlen(hdr), &out_len);
    ASSERT_NOT_NULL(json);
    ASSERT(out_len > 0);
    ASSERT(json[0] == '{');
    ASSERT(json[out_len - 1] == '}');
    ASSERT(strstr(json, "\"Content-Type\"") != NULL);
    ASSERT(strstr(json, "\"text/html\"") != NULL);
    ASSERT(strstr(json, "\"X-Custom\"") != NULL);
    ASSERT(strstr(json, "\"value\"") != NULL);
    free(json);
}

TEST(build_headers_empty) {
    const char *hdr = "";
    size_t out_len = 0;
    char *json = pal_build_headers_json(hdr, 0, &out_len);
    ASSERT_NOT_NULL(json);
    ASSERT_STR_EQ(json, "{}");
    free(json);
}

TEST(build_headers_no_colon) {
    /* Line without colon should be skipped */
    const char *hdr = "InvalidHeader\r\n";
    size_t out_len = 0;
    char *json = pal_build_headers_json(hdr, strlen(hdr), &out_len);
    ASSERT_NOT_NULL(json);
    ASSERT_STR_EQ(json, "{}");
    free(json);
}

/* ================================================================
 * Group 3: pal_build_http_json
 * ================================================================ */

TEST(build_http_basic) {
    size_t out_len = 0;
    char *json = pal_build_http_json(200, "{}", 2, "Hello", 5, &out_len);
    ASSERT_NOT_NULL(json);
    ASSERT(out_len > 0);
    ASSERT(strstr(json, "\"status\":200") != NULL);
    ASSERT(strstr(json, "\"headers\":{}") != NULL);
    ASSERT(strstr(json, "\"body\":\"Hello\"") != NULL);
    free(json);
}

TEST(build_http_null_headers) {
    size_t out_len = 0;
    char *json = pal_build_http_json(404, NULL, 0, "Not Found", 9, &out_len);
    ASSERT_NOT_NULL(json);
    ASSERT(strstr(json, "\"headers\":{}") != NULL);
    ASSERT(strstr(json, "\"status\":404") != NULL);
    free(json);
}

TEST(build_http_empty_body) {
    size_t out_len = 0;
    char *json = pal_build_http_json(204, "{}", 2, "", 0, &out_len);
    ASSERT_NOT_NULL(json);
    ASSERT(strstr(json, "\"body\":\"") != NULL);
    free(json);
}

TEST(build_http_body_with_specials) {
    size_t out_len = 0;
    const char *body = "line1\nline2\t\"quoted\"";
    char *json = pal_build_http_json(200, "{}", 2, body, strlen(body), &out_len);
    ASSERT_NOT_NULL(json);
    ASSERT(strstr(json, "\\n") != NULL);
    ASSERT(strstr(json, "\\\"") != NULL);
    free(json);
}

/* ================================================================
 * Group 4: pal_build_json_array
 * ================================================================ */

TEST(build_json_array_basic) {
    const char *items[] = { "a", "b", "c", NULL };
    size_t out_len = 0;
    char *json = pal_build_json_array(items, 3, &out_len);
    ASSERT_NOT_NULL(json);
    ASSERT_STR_EQ(json, "[\"a\",\"b\",\"c\"]");
    free(json);
}

TEST(build_json_array_single) {
    const char *items[] = { "only", NULL };
    size_t out_len = 0;
    char *json = pal_build_json_array(items, 1, &out_len);
    ASSERT_NOT_NULL(json);
    ASSERT_STR_EQ(json, "[\"only\"]");
    free(json);
}

TEST(build_json_array_empty) {
    size_t out_len = 0;
    char *json = pal_build_json_array(NULL, 0, &out_len);
    ASSERT_NOT_NULL(json);
    ASSERT_STR_EQ(json, "[]");
    free(json);
}

TEST(build_json_array_special_chars) {
    const char *items[] = { "a\"b", "c\nd", NULL };
    size_t out_len = 0;
    char *json = pal_build_json_array(items, 2, &out_len);
    ASSERT_NOT_NULL(json);
    ASSERT(strstr(json, "a\\\"b") != NULL);
    ASSERT(strstr(json, "c\\nd") != NULL);
    free(json);
}

/* ================================================================
 * Group 5: pal_parse_url
 * ================================================================ */

TEST(parse_url_http) {
    pal_url_t url = {0};
    int rc = pal_parse_url("http://example.com/path", &url);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(url.host, "example.com");
    ASSERT_EQ(url.port, 80);
    ASSERT_STR_EQ(url.path, "/path");
    ASSERT_EQ(url.tls, 0);
    pal_url_free(&url);
}

TEST(parse_url_https) {
    pal_url_t url = {0};
    int rc = pal_parse_url("https://secure.example.com:8443/api/v1", &url);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(url.host, "secure.example.com");
    ASSERT_EQ(url.port, 8443);
    ASSERT_STR_EQ(url.path, "/api/v1");
    ASSERT_EQ(url.tls, 1);
    pal_url_free(&url);
}

TEST(parse_url_no_path) {
    pal_url_t url = {0};
    int rc = pal_parse_url("http://example.com", &url);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(url.host, "example.com");
    ASSERT_STR_EQ(url.path, "/");
    ASSERT_EQ(url.port, 80);
    pal_url_free(&url);
}

TEST(parse_url_default_ports) {
    pal_url_t url_http = {0};
    pal_url_t url_https = {0};
    pal_parse_url("http://a.com", &url_http);
    pal_parse_url("https://b.com", &url_https);
    ASSERT_EQ(url_http.port, 80);
    ASSERT_EQ(url_https.port, 443);
    pal_url_free(&url_http);
    pal_url_free(&url_https);
}

TEST(parse_url_bad_scheme) {
    pal_url_t url = {0};
    int rc = pal_parse_url("ftp://example.com/file", &url);
    ASSERT(rc < 0);
    ASSERT_NULL(url.host);
}

TEST(parse_url_null_input) {
    pal_url_t url = {0};
    int rc = pal_parse_url(NULL, &url);
    ASSERT(rc < 0);
}

TEST(parse_url_invalid_port) {
    pal_url_t url = {0};
    int rc = pal_parse_url("http://example.com:999999/path", &url);
    ASSERT_EQ(rc, 0);
    /* strtol overflow: port defaults to 80 */
    ASSERT_EQ(url.port, 80);
    pal_url_free(&url);
}

TEST(url_free_null) {
    /* pal_url_free on NULL should be safe */
    pal_url_free(NULL);

    /* pal_url_free on zero-initialized should be safe */
    pal_url_t url = {0};
    pal_url_free(&url);
}

/* ================================================================
 * Main
 * ================================================================ */

int main(void)
{
    printf("=== PAL Common Unit Tests ===\n\n");

    printf("--- pal_json_escape ---\n");
    RUN_TEST(json_escape_plain);
    RUN_TEST(json_escape_quote);
    RUN_TEST(json_escape_backslash);
    RUN_TEST(json_escape_control_chars);
    RUN_TEST(json_escape_null_char);
    RUN_TEST(json_escape_mixed);
    RUN_TEST(json_escape_empty);
    RUN_TEST(json_escape_buffer_too_small);
    RUN_TEST(json_escape_exact_fit);

    printf("\n--- pal_build_headers_json ---\n");
    RUN_TEST(build_headers_basic);
    RUN_TEST(build_headers_empty);
    RUN_TEST(build_headers_no_colon);

    printf("\n--- pal_build_http_json ---\n");
    RUN_TEST(build_http_basic);
    RUN_TEST(build_http_null_headers);
    RUN_TEST(build_http_empty_body);
    RUN_TEST(build_http_body_with_specials);

    printf("\n--- pal_build_json_array ---\n");
    RUN_TEST(build_json_array_basic);
    RUN_TEST(build_json_array_single);
    RUN_TEST(build_json_array_empty);
    RUN_TEST(build_json_array_special_chars);

    printf("\n--- pal_parse_url ---\n");
    RUN_TEST(parse_url_http);
    RUN_TEST(parse_url_https);
    RUN_TEST(parse_url_no_path);
    RUN_TEST(parse_url_default_ports);
    RUN_TEST(parse_url_bad_scheme);
    RUN_TEST(parse_url_null_input);
    RUN_TEST(parse_url_invalid_port);
    RUN_TEST(url_free_null);

    printf("\n=== Results: %d/%d passed, %d failed ===\n",
           tests_passed, tests_run, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}

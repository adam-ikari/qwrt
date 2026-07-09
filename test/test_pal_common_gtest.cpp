/*
 * PAL Common Unit Tests (Google Test)
 *
 * Tests for shared PAL helper functions (pal_json_escape, pal_build_headers_json,
 * pal_build_http_json, pal_build_json_array, pal_parse_url, pal_url_free).
 */

#include <gtest/gtest.h>

extern "C" {
#include "pal_common.h"
#include <stdlib.h>
#include <string.h>
}

/* ================================================================
 * Group 1: pal_json_escape
 * ================================================================ */

TEST(PalCommon, JsonEscapePlain) {
    char buf[256];
    int n = pal_json_escape("hello", 5, buf, sizeof(buf));
    EXPECT_EQ(n, 5);
    EXPECT_STREQ(buf, "hello");
}

TEST(PalCommon, JsonEscapeQuote) {
    char buf[256];
    int n = pal_json_escape("he\"llo", 6, buf, sizeof(buf));
    EXPECT_EQ(n, 7);
    EXPECT_STREQ(buf, "he\\\"llo");
}

TEST(PalCommon, JsonEscapeBackslash) {
    char buf[256];
    int n = pal_json_escape("a\\b", 3, buf, sizeof(buf));
    EXPECT_EQ(n, 4);
    EXPECT_STREQ(buf, "a\\\\b");
}

TEST(PalCommon, JsonEscapeControlChars) {
    char buf[256];
    /* Test the 5 C0 control chars that have short escapes */
    char src[] = { '\n', '\r', '\t', '\b', '\f' };
    int n = pal_json_escape(src, 5, buf, sizeof(buf));
    EXPECT_EQ(n, 10);
    EXPECT_STREQ(buf, "\\n\\r\\t\\b\\f");
}

TEST(PalCommon, JsonEscapeNullChar) {
    char buf[256];
    /* Null byte should escape as \\u0000 */
    int n = pal_json_escape("\x00", 1, buf, sizeof(buf));
    EXPECT_EQ(n, 6);
    EXPECT_STREQ(buf, "\\u0000");
}

TEST(PalCommon, JsonEscapeMixed) {
    char buf[512];
    const char *src = "line1\nline2\t\"quoted\"";
    int n = pal_json_escape(src, strlen(src), buf, sizeof(buf));
    EXPECT_GT(n, 0);
    EXPECT_NE(strstr(buf, "\\n"), nullptr);
    EXPECT_NE(strstr(buf, "\\t"), nullptr);
    EXPECT_NE(strstr(buf, "\\\""), nullptr);
}

TEST(PalCommon, JsonEscapeEmpty) {
    char buf[16];
    int n = pal_json_escape("", 0, buf, sizeof(buf));
    EXPECT_EQ(n, 0);
    EXPECT_STREQ(buf, "");
}

TEST(PalCommon, JsonEscapeBufferTooSmall) {
    char buf[4];
    int n = pal_json_escape("hello", 5, buf, sizeof(buf));
    EXPECT_EQ(n, -1);
}

TEST(PalCommon, JsonEscapeExactFit) {
    /* "abc" — needs 4 bytes (3 chars + NUL) */
    char buf[4];
    int n = pal_json_escape("abc", 3, buf, sizeof(buf));
    EXPECT_EQ(n, 3);
    EXPECT_STREQ(buf, "abc");
}

/* ================================================================
 * Group 2: pal_build_headers_json
 * ================================================================ */

TEST(PalCommon, BuildHeadersBasic) {
    const char *hdr = "Content-Type: text/html\r\nX-Custom: value\r\n";
    size_t out_len = 0;
    char *json = pal_build_headers_json(hdr, strlen(hdr), &out_len);
    ASSERT_NE(json, nullptr);
    EXPECT_GT(out_len, 0u);
    EXPECT_EQ(json[0], '{');
    EXPECT_EQ(json[out_len - 1], '}');
    EXPECT_NE(strstr(json, "\"Content-Type\""), nullptr);
    EXPECT_NE(strstr(json, "\"text/html\""), nullptr);
    EXPECT_NE(strstr(json, "\"X-Custom\""), nullptr);
    EXPECT_NE(strstr(json, "\"value\""), nullptr);
    free(json);
}

TEST(PalCommon, BuildHeadersEmpty) {
    const char *hdr = "";
    size_t out_len = 0;
    char *json = pal_build_headers_json(hdr, 0, &out_len);
    ASSERT_NE(json, nullptr);
    EXPECT_STREQ(json, "{}");
    free(json);
}

TEST(PalCommon, BuildHeadersNoColon) {
    /* Line without colon should be skipped */
    const char *hdr = "InvalidHeader\r\n";
    size_t out_len = 0;
    char *json = pal_build_headers_json(hdr, strlen(hdr), &out_len);
    ASSERT_NE(json, nullptr);
    EXPECT_STREQ(json, "{}");
    free(json);
}

/* ================================================================
 * Group 3: pal_build_http_json
 * ================================================================ */

TEST(PalCommon, BuildHttpBasic) {
    size_t out_len = 0;
    char *json = pal_build_http_json(200, "{}", 2, "Hello", 5, &out_len);
    ASSERT_NE(json, nullptr);
    EXPECT_GT(out_len, 0u);
    EXPECT_NE(strstr(json, "\"status\":200"), nullptr);
    EXPECT_NE(strstr(json, "\"headers\":{}"), nullptr);
    EXPECT_NE(strstr(json, "\"body\":\"Hello\""), nullptr);
    free(json);
}

TEST(PalCommon, BuildHttpNullHeaders) {
    size_t out_len = 0;
    char *json = pal_build_http_json(404, NULL, 0, "Not Found", 9, &out_len);
    ASSERT_NE(json, nullptr);
    EXPECT_NE(strstr(json, "\"headers\":{}"), nullptr);
    EXPECT_NE(strstr(json, "\"status\":404"), nullptr);
    free(json);
}

TEST(PalCommon, BuildHttpEmptyBody) {
    size_t out_len = 0;
    char *json = pal_build_http_json(204, "{}", 2, "", 0, &out_len);
    ASSERT_NE(json, nullptr);
    EXPECT_NE(strstr(json, "\"body\":\"\""), nullptr);
    free(json);
}

TEST(PalCommon, BuildHttpBodyWithSpecials) {
    size_t out_len = 0;
    const char *body = "line1\nline2\t\"quoted\"";
    char *json = pal_build_http_json(200, "{}", 2, body, strlen(body), &out_len);
    ASSERT_NE(json, nullptr);
    EXPECT_NE(strstr(json, "\\n"), nullptr);
    EXPECT_NE(strstr(json, "\\\""), nullptr);
    free(json);
}

/* ================================================================
 * Group 4: pal_build_json_array
 * ================================================================ */

TEST(PalCommon, BuildJsonArrayBasic) {
    const char *items[] = { "a", "b", "c", NULL };
    size_t out_len = 0;
    char *json = pal_build_json_array(items, 3, &out_len);
    ASSERT_NE(json, nullptr);
    EXPECT_STREQ(json, "[\"a\",\"b\",\"c\"]");
    free(json);
}

TEST(PalCommon, BuildJsonArraySingle) {
    const char *items[] = { "only", NULL };
    size_t out_len = 0;
    char *json = pal_build_json_array(items, 1, &out_len);
    ASSERT_NE(json, nullptr);
    EXPECT_STREQ(json, "[\"only\"]");
    free(json);
}

TEST(PalCommon, BuildJsonArrayEmpty) {
    size_t out_len = 0;
    char *json = pal_build_json_array(NULL, 0, &out_len);
    ASSERT_NE(json, nullptr);
    EXPECT_STREQ(json, "[]");
    free(json);
}

TEST(PalCommon, BuildJsonArraySpecialChars) {
    const char *items[] = { "a\"b", "c\nd", NULL };
    size_t out_len = 0;
    char *json = pal_build_json_array(items, 2, &out_len);
    ASSERT_NE(json, nullptr);
    EXPECT_NE(strstr(json, "a\\\"b"), nullptr);
    EXPECT_NE(strstr(json, "c\\nd"), nullptr);
    free(json);
}

/* ================================================================
 * Group 5: pal_parse_url
 * ================================================================ */

TEST(PalCommon, ParseUrlHttp) {
    pal_url_t url = {0};
    int rc = pal_parse_url("http://example.com/path", &url);
    EXPECT_EQ(rc, 0);
    EXPECT_STREQ(url.host, "example.com");
    EXPECT_EQ(url.port, 80);
    EXPECT_STREQ(url.path, "/path");
    EXPECT_EQ(url.tls, 0);
    pal_url_free(&url);
}

TEST(PalCommon, ParseUrlHttps) {
    pal_url_t url = {0};
    int rc = pal_parse_url("https://secure.example.com:8443/api/v1", &url);
    EXPECT_EQ(rc, 0);
    EXPECT_STREQ(url.host, "secure.example.com");
    EXPECT_EQ(url.port, 8443);
    EXPECT_STREQ(url.path, "/api/v1");
    EXPECT_EQ(url.tls, 1);
    pal_url_free(&url);
}

TEST(PalCommon, ParseUrlNoPath) {
    pal_url_t url = {0};
    int rc = pal_parse_url("http://example.com", &url);
    EXPECT_EQ(rc, 0);
    EXPECT_STREQ(url.host, "example.com");
    EXPECT_STREQ(url.path, "/");
    EXPECT_EQ(url.port, 80);
    pal_url_free(&url);
}

TEST(PalCommon, ParseUrlDefaultPorts) {
    pal_url_t url_http = {0};
    pal_url_t url_https = {0};
    pal_parse_url("http://a.com", &url_http);
    pal_parse_url("https://b.com", &url_https);
    EXPECT_EQ(url_http.port, 80);
    EXPECT_EQ(url_https.port, 443);
    pal_url_free(&url_http);
    pal_url_free(&url_https);
}

TEST(PalCommon, ParseUrlBadScheme) {
    pal_url_t url = {0};
    int rc = pal_parse_url("ftp://example.com/file", &url);
    EXPECT_LT(rc, 0);
    EXPECT_EQ(url.host, nullptr);
}

TEST(PalCommon, ParseUrlNullInput) {
    pal_url_t url = {0};
    int rc = pal_parse_url(NULL, &url);
    EXPECT_LT(rc, 0);
}

TEST(PalCommon, ParseUrlInvalidPort) {
    pal_url_t url = {0};
    int rc = pal_parse_url("http://example.com:999999/path", &url);
    EXPECT_EQ(rc, 0);
    /* strtol overflow: port defaults to 80 */
    EXPECT_EQ(url.port, 80);
    pal_url_free(&url);
}

TEST(PalCommon, UrlFreeNull) {
    /* pal_url_free on NULL should be safe */
    pal_url_free(NULL);

    /* pal_url_free on zero-initialized should be safe */
    pal_url_t url = {0};
    pal_url_free(&url);
}

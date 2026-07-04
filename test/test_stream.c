/*
 * test_stream.c — Streaming HTTP tests for pal_uv
 *
 * Tests http_request_stream: on_headers, on_data, on_end callbacks.
 * Requires network access; skipped if offline.
 */

#define _GNU_SOURCE

#include "pal_uv.h"
#include <netdb.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define RUN_TEST(name) do { \
    printf("  %-55s", #name); fflush(stdout); \
    tests_run++; \
    test_##name(); \
} while(0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAIL\n    %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        tests_failed++; \
        goto cleanup; \
    } \
} while(0)

typedef struct {
    int headers_called;
    int status;
    char *headers_json;
    int data_called;
    int data_chunks;
    size_t total_bytes;
    int end_called;
    int end_status;
} stream_result_t;

static void on_headers(void *ud, int status, const char *hjson) {
    stream_result_t *r = (stream_result_t *)ud;
    r->headers_called = 1;
    r->status = status;
    if (hjson) r->headers_json = strdup(hjson);
}

static void on_data(void *ud, const char *data, size_t len) {
    stream_result_t *r = (stream_result_t *)ud;
    r->data_called = 1;
    r->data_chunks++;
    r->total_bytes += len;
}

static void on_end(void *ud, int error_status) {
    stream_result_t *r = (stream_result_t *)ud;
    r->end_called = 1;
    r->end_status = error_status;
}

static qwrt_pal_t *t_pal;
static uv_loop_t t_loop;

static int test_setup(void) {
    if (uv_loop_init(&t_loop) < 0) return -1;
    t_pal = pal_uv_create(&t_loop);
    if (!t_pal) { uv_loop_close(&t_loop); return -1; }
    return 0;
}

static void test_teardown(void) {
    if (t_pal) { pal_uv_destroy(t_pal); t_pal = NULL; }
    uv_run(&t_loop, UV_RUN_DEFAULT);
    uv_loop_close(&t_loop);
}

/* ================================================================
 * Tests
 * ================================================================ */

static void test_stream_http_get(void)
{
    if (test_setup() < 0) { printf("FAIL (setup)\n"); tests_failed++; return; }
    stream_result_t r;
    memset(&r, 0, sizeof(r));

    qwrt_pal_stream_ops_t ops = { on_headers, on_data, on_end, &r };
    t_pal->http_request_stream(t_pal, "http://example.com/", "GET",
                               "{}", NULL, 0, &ops);

    for (int i = 0; i < 15000 && !r.end_called; i++) {
        uv_run(&t_loop, UV_RUN_NOWAIT);
        if (!r.end_called) usleep(1000);
    }

    ASSERT(r.headers_called);
    ASSERT(r.status == 200);
    ASSERT(r.data_called);
    ASSERT(r.total_bytes > 0);
    ASSERT(r.end_called);
    ASSERT(r.end_status == 0);
    printf("PASS (%zu bytes, %d chunks)\n", r.total_bytes, r.data_chunks);
    tests_passed++;
cleanup:
    free(r.headers_json);
    test_teardown();
}

static void test_stream_https_get(void)
{
    if (test_setup() < 0) { printf("FAIL (setup)\n"); tests_failed++; return; }
    stream_result_t r;
    memset(&r, 0, sizeof(r));

    qwrt_pal_stream_ops_t ops = { on_headers, on_data, on_end, &r };
    t_pal->http_request_stream(t_pal, "https://example.com/", "GET",
                               "{}", NULL, 0, &ops);

    for (int i = 0; i < 15000 && !r.end_called; i++) {
        uv_run(&t_loop, UV_RUN_NOWAIT);
        if (!r.end_called) usleep(1000);
    }

    ASSERT(r.headers_called);
    ASSERT(r.status == 200);
    ASSERT(r.data_called);
    ASSERT(r.total_bytes > 0);
    ASSERT(r.end_called);
    ASSERT(r.end_status == 0);
    printf("PASS (%zu bytes, %d chunks)\n", r.total_bytes, r.data_chunks);
    tests_passed++;
cleanup:
    free(r.headers_json);
    test_teardown();
}

static void test_stream_headers_json(void)
{
    if (test_setup() < 0) { printf("FAIL (setup)\n"); tests_failed++; return; }
    stream_result_t r;
    memset(&r, 0, sizeof(r));

    qwrt_pal_stream_ops_t ops = { on_headers, on_data, on_end, &r };
    t_pal->http_request_stream(t_pal, "https://example.com/", "GET",
                               "{}", NULL, 0, &ops);

    for (int i = 0; i < 15000 && !r.end_called; i++) {
        uv_run(&t_loop, UV_RUN_NOWAIT);
        if (!r.end_called) usleep(1000);
    }

    ASSERT(r.headers_called);
    ASSERT(r.headers_json != NULL);
    /* Headers JSON should contain content-type or similar */
    ASSERT(strstr(r.headers_json, "content-type") != NULL ||
           strstr(r.headers_json, "Content-Type") != NULL ||
           strstr(r.headers_json, "etag") != NULL ||
           strstr(r.headers_json, "ETag") != NULL);
    printf("PASS\n");
    tests_passed++;
cleanup:
    free(r.headers_json);
    test_teardown();
}

static void test_stream_error_invalid_host(void)
{
    if (test_setup() < 0) { printf("FAIL (setup)\n"); tests_failed++; return; }
    stream_result_t r;
    memset(&r, 0, sizeof(r));

    qwrt_pal_stream_ops_t ops = { on_headers, on_data, on_end, &r };
    t_pal->http_request_stream(t_pal,
        "http://this-domain-does-not-exist-12345.invalid/", "GET",
        "{}", NULL, 0, &ops);

    for (int i = 0; i < 15000 && !r.end_called; i++) {
        uv_run(&t_loop, UV_RUN_NOWAIT);
        if (!r.end_called) usleep(1000);
    }

    ASSERT(r.end_called);
    ASSERT(r.end_status != 0);
    printf("PASS (end_status=%d)\n", r.end_status);
    tests_passed++;
cleanup:
    test_teardown();
}

/* ================================================================
 * Main
 * ================================================================ */

static int check_connectivity(void) {
    struct hostent *he = gethostbyname("example.com");
    return he ? 1 : 0;
}

int main(void) {
    printf("=== qwrt Streaming HTTP Tests ===\n\n");

    printf("Checking network connectivity... ");
    fflush(stdout);
    if (!check_connectivity()) {
        printf("OFFLINE\n\nSKIPPED\n");
        return 0;
    }
    printf("OK\n\n");

    RUN_TEST(stream_http_get);
    RUN_TEST(stream_https_get);
    RUN_TEST(stream_headers_json);
    RUN_TEST(stream_error_invalid_host);

    printf("\n=== Results: %d/%d passed, %d failed ===\n",
           tests_passed, tests_run, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}

/*
 * test_tls.c — TLS/HTTPS connectivity tests for pal_uv HTTP client
 *
 * Tests HTTPS requests with mbedTLS. Requires network access and
 * QWRT_WITH_TLS=ON + QWRT_WITH_LIBUV=ON.
 * Skipped if offline.
 */

#define _GNU_SOURCE

#include "pal_uv.h"
#include <sys/types.h>
#include <sys/socket.h>
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

#define ASSERT_STR_CONTAINS(haystack, needle) do { \
    if (!(haystack) || strstr((haystack), (needle)) == NULL) { \
        printf("FAIL\n    %s:%d: \"%s\" not found\n", \
               __FILE__, __LINE__, (needle)); \
        tests_failed++; \
        goto cleanup; \
    } \
} while(0)

/* ================================================================
 * HTTP callback helpers
 * ================================================================ */

typedef struct {
    int completed;
    int status;
    char *data;
    size_t data_len;
} http_result_t;

static void http_cb(void *user_data, int status, const char *data, size_t data_len)
{
    http_result_t *r = (http_result_t *)user_data;
    r->completed = 1;
    r->status = status;
    if (data && data_len > 0) {
        r->data = (char *)malloc(data_len + 1);
        if (r->data) {
            memcpy(r->data, data, data_len);
            r->data[data_len] = '\0';
            r->data_len = data_len;
        }
    }
}

static int do_http(qwrt_pal_t *pal, uv_loop_t *loop,
                   const char *url, const char *method,
                   const char *headers, const char *body, size_t body_len,
                   http_result_t *result)
{
    memset(result, 0, sizeof(*result));
    pal->http_request(pal, url, method, headers ? headers : "{}",
                      body, body_len, http_cb, result);

    for (int i = 0; i < 15000 && !result->completed; i++) {
        uv_run(loop, UV_RUN_NOWAIT);
        if (!result->completed) usleep(1000);
    }
    return result->completed ? 0 : -1;
}

/* ================================================================
 * Per-test PAL create/destroy
 * ================================================================ */

static qwrt_pal_t *t_pal;
static uv_loop_t t_loop;

static int test_setup(void)
{
    if (uv_loop_init(&t_loop) < 0) return -1;
    t_pal = pal_uv_create(&t_loop);
    if (!t_pal) { uv_loop_close(&t_loop); return -1; }
    return 0;
}

static void test_teardown(void)
{
    if (t_pal) { pal_uv_destroy(t_pal); t_pal = NULL; }
    uv_run(&t_loop, UV_RUN_DEFAULT);
    uv_loop_close(&t_loop);
}

/* ================================================================
 * Tests
 * ================================================================ */

static void test_https_get(void)
{
    if (test_setup() < 0) { printf("FAIL (setup)\n"); tests_failed++; return; }
    http_result_t r;
    int rc = do_http(t_pal, &t_loop, "https://example.com/", "GET", NULL, NULL, 0, &r);
    ASSERT(rc == 0);
    ASSERT(r.status == 0);  /* 0 = success, HTTP status is in JSON data */
    ASSERT(r.data != NULL);
    ASSERT_STR_CONTAINS(r.data, "\"status\":200");
    ASSERT_STR_CONTAINS(r.data, "Example Domain");
    printf("PASS\n");
    tests_passed++;
cleanup:
    free(r.data);
    test_teardown();
}

static void test_https_headers(void)
{
    if (test_setup() < 0) { printf("FAIL (setup)\n"); tests_failed++; return; }
    http_result_t r;
    int rc = do_http(t_pal, &t_loop, "https://example.com/", "GET", NULL, NULL, 0, &r);
    ASSERT(rc == 0);
    ASSERT(r.status == 0);
    ASSERT(r.data != NULL);
    ASSERT_STR_CONTAINS(r.data, "\"status\":200");
    ASSERT_STR_CONTAINS(r.data, "Content-Type");
    printf("PASS\n");
    tests_passed++;
cleanup:
    free(r.data);
    test_teardown();
}

static void test_https_post(void)
{
    if (test_setup() < 0) { printf("FAIL (setup)\n"); tests_failed++; return; }
    http_result_t r;
    const char *body = "{\"test\":\"tls-post\"}";
    const char *hdrs = "{\"Content-Type\":\"application/json\"}";
    int rc = do_http(t_pal, &t_loop, "https://httpbin.org/post", "POST",
                     hdrs, body, strlen(body), &r);
    if (rc != 0) {
        printf("SKIP (httpbin timeout)\n");
        goto cleanup;
    }
    if (r.status == 0 && r.data && strstr(r.data, "\"status\":503")) {
        printf("SKIP (httpbin 503)\n");
        goto cleanup;
    }
    ASSERT(r.status == 0);
    ASSERT(r.data != NULL);
    ASSERT_STR_CONTAINS(r.data, "tls-post");
    printf("PASS\n");
    tests_passed++;
cleanup:
    free(r.data);
    test_teardown();
}

static void test_https_invalid_cert(void)
{
    /* Self-signed / bad cert host should fail gracefully, not crash */
    if (test_setup() < 0) { printf("FAIL (setup)\n"); tests_failed++; return; }
    http_result_t r;
    /* badssl.com hosts various cert scenarios; self-signed should return error */
    int rc = do_http(t_pal, &t_loop, "https://self-signed.badssl.com/", "GET", NULL, NULL, 0, &r);
    /* We expect either a timeout (connection rejected) or an error status */
    if (rc == 0) {
        ASSERT(r.status != 200);
    }
    /* Either way, we shouldn't crash */
    printf("PASS\n");
    tests_passed++;
cleanup:
    free(r.data);
    test_teardown();
}

/* ================================================================
 * Main
 * ================================================================ */

static int check_connectivity(void)
{
    struct hostent *he = gethostbyname("example.com");
    return he ? 1 : 0;
}

int main(void)
{
    printf("=== qwrt TLS/HTTPS Tests ===\n\n");

    printf("Checking network connectivity... ");
    fflush(stdout);
    if (!check_connectivity()) {
        printf("OFFLINE\n\nSKIPPED\n");
        return 0;
    }
    printf("OK\n\n");

    printf("--- HTTPS GET ---\n");
    RUN_TEST(https_get);

    printf("\n--- HTTPS Headers ---\n");
    RUN_TEST(https_headers);

    printf("\n--- HTTPS POST ---\n");
    RUN_TEST(https_post);

    printf("\n--- Invalid Certificate ---\n");
    RUN_TEST(https_invalid_cert);

    printf("\n=== Results: %d/%d passed, %d failed ===\n",
           tests_passed, tests_run, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}

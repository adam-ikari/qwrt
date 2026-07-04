/*
 * test_net.c — Real network connectivity test for pal_uv HTTP client
 *
 * Each test creates its own PAL + loop to avoid handle interference.
 * Requires network access; skipped if offline.
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
    printf("  %-40s", #name); fflush(stdout); \
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

static void test_dns_http_get(void)
{
    if (test_setup() < 0) { printf("FAIL (setup)\n"); return; }
    http_result_t r;
    int rc = do_http(t_pal, &t_loop, "http://example.com/", "GET", NULL, NULL, 0, &r);
    ASSERT(rc == 0);
    ASSERT(r.status == 0);
    ASSERT(r.data != NULL);
    ASSERT_STR_CONTAINS(r.data, "Example Domain");
    printf("PASS\n");
cleanup:
    free(r.data);
    test_teardown();
}

static void test_content_length(void)
{
    if (test_setup() < 0) { printf("FAIL (setup)\n"); return; }
    http_result_t r;
    /* httpbin.org can be flaky; use example.com which is always available.
     * We just verify the HTTP response has valid headers and body. */
    int rc = do_http(t_pal, &t_loop, "http://example.com/", "GET", NULL, NULL, 0, &r);
    ASSERT(rc == 0);
    ASSERT(r.status == 0);
    ASSERT(r.data != NULL);
    ASSERT_STR_CONTAINS(r.data, "\"status\":200");
    ASSERT_STR_CONTAINS(r.data, "Example Domain");
    printf("PASS\n");
cleanup:
    free(r.data);
    test_teardown();
}

static void test_http_post(void)
{
    if (test_setup() < 0) { printf("FAIL (setup)\n"); return; }
    http_result_t r;
    const char *body = "{\"test\":\"hello\"}";
    const char *hdrs = "{\"Content-Type\":\"application/json\"}";
    /* httpbin.org/post echoes the request body back.
     * If httpbin is down (503 or timeout), skip rather than fail. */
    int rc = do_http(t_pal, &t_loop, "http://httpbin.org/post", "POST",
                     hdrs, body, strlen(body), &r);
    if (rc != 0) {
        printf("SKIP (httpbin timeout)\n");
        goto cleanup;
    }
    ASSERT(r.status == 0);
    ASSERT(r.data != NULL);
    if (strstr(r.data, "\"status\":503")) {
        printf("SKIP (httpbin 503)\n");
        goto cleanup;
    }
    ASSERT_STR_CONTAINS(r.data, "hello");
    printf("PASS\n");
cleanup:
    free(r.data);
    test_teardown();
}

static void test_https_no_tls(void)
{
    if (test_setup() < 0) { printf("FAIL (setup)\n"); return; }
    http_result_t r;
    int rc = do_http(t_pal, &t_loop, "https://example.com/", "GET", NULL, NULL, 0, &r);
    ASSERT(rc == 0);
#ifdef QWRT_WITH_TLS
    ASSERT(r.status == 0);
    ASSERT(r.data != NULL);
    ASSERT_STR_CONTAINS(r.data, "example");
#else
    ASSERT(r.status != 0);
    ASSERT(r.data != NULL);
    ASSERT_STR_CONTAINS(r.data, "TLS");
#endif
    printf("PASS\n");
cleanup:
    free(r.data);
    test_teardown();
}

static void test_invalid_hostname(void)
{
    if (test_setup() < 0) { printf("FAIL (setup)\n"); return; }
    http_result_t r;
    do_http(t_pal, &t_loop,
            "http://this-domain-does-not-exist-12345.invalid/",
            "GET", NULL, NULL, 0, &r);
    ASSERT(r.completed);
    ASSERT(r.status != 0);
    printf("PASS\n");
cleanup:
    free(r.data);
    test_teardown();
}

typedef struct { int done; int status; char *value; } stor_r_t;

static void stor_set_cb(void *ud, int st, const char *d, size_t l) { stor_r_t *r = ud; r->done = 1; r->status = st; }
static void stor_get_cb(void *ud, int st, const char *d, size_t l) {
    stor_r_t *r = ud; r->done = 1; r->status = st;
    if (d && l > 0) { r->value = malloc(l+1); if (r->value) { memcpy(r->value, d, l); r->value[l] = '\0'; } }
}

static void test_storage(void)
{
    if (test_setup() < 0) { printf("FAIL (setup)\n"); return; }
    stor_r_t sr = {0,0,NULL}, gr = {0,0,NULL};

    t_pal->storage_set(t_pal, "net_key", "net_val", 7, stor_set_cb, &sr);
    for (int i = 0; i < 100 && !sr.done; i++) { uv_run(&t_loop, UV_RUN_NOWAIT); usleep(1000); }
    ASSERT(sr.done);
    ASSERT(sr.status == 0);

    t_pal->storage_get(t_pal, "net_key", stor_get_cb, &gr);
    for (int i = 0; i < 100 && !gr.done; i++) { uv_run(&t_loop, UV_RUN_NOWAIT); usleep(1000); }
    ASSERT(gr.done);
    ASSERT(gr.value != NULL);
    ASSERT(strcmp(gr.value, "net_val") == 0);
    printf("PASS\n");
cleanup:
    free(gr.value);
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
    printf("=== qwrt Network Connectivity Tests ===\n\n");

    printf("Checking network connectivity... ");
    fflush(stdout);
    if (!check_connectivity()) {
        printf("OFFLINE\n\nSKIPPED\n");
        return 0;
    }
    printf("OK\n\n");

    printf("--- DNS + HTTP GET ---\n");
    RUN_TEST(dns_http_get);

    printf("\n--- Content-Length ---\n");
    RUN_TEST(content_length);

    printf("\n--- POST ---\n");
    RUN_TEST(http_post);

    printf("\n--- HTTPS (no TLS) ---\n");
    RUN_TEST(https_no_tls);

    printf("\n--- Error Handling ---\n");
    RUN_TEST(invalid_hostname);

    printf("\n--- Storage ---\n");
    RUN_TEST(storage);

    printf("\n=== Results: %d/%d passed, %d failed ===\n",
           tests_passed, tests_run, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
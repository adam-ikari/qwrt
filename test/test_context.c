/*
 * qwrt Multi-Context Tests
 *
 * Tests for spawn, suspend, resume, destroy_ctx, and per-context PAL.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qwrt/qwrt.h"
#include "pal_mock.h"

/* ================================================================
 * Minimal test framework (same as test_qwrt.h)
 * ================================================================ */

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)

#define ASSERT(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "  FAIL: %s (line %d)\n", #expr, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_STR_EQ(a, b) do { \
    if (strcmp((a), (b)) != 0) { \
        fprintf(stderr, "  FAIL: \"%s\" != \"%s\" (line %d)\n", (a), (b), __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_NULL(p) ASSERT((p) == NULL)
#define ASSERT_NOT_NULL(p) ASSERT((p) != NULL)
#define ASSERT_EQ(a, b) ASSERT((a) == (b))

#define RUN_TEST(name) do { \
    int prev_failed = tests_failed; \
    printf("  %s...", #name); \
    tests_run++; \
    test_##name(); \
    if (tests_failed == prev_failed) { \
        printf(" PASS\n"); \
        tests_passed++; \
    } else { \
        printf("\n"); \
    } \
} while(0)

/* testHelper: exposes raw PAL functions via __pal__ for low-level tests */
static const char testHelper_def[] =
    "globalThis.testHelper = {\n"
    "  storageSet: function(k, v) { return __pal__.storageSet(k, v); },\n"
    "  storageGet: function(k) { return __pal__.storageGet(k); },\n"
    "  timeNow: function() { return __pal__.timeNow(); },\n"
    "  log: function(level, msg) { __pal__.log(level, msg); },\n"
    "  fsWrite: function(p, d) { return __pal__.fsWrite(p, d); },\n"
    "  fsRead: function(p) { return __pal__.fsRead(p); },\n"
    "  httpRequest: function(url, method, headers, body) {\n"
    "    return __pal__.httpRequest(url, method, headers, body);\n"
    "  },\n"
    "  timerStart: function(delay, repeat) { return __pal__.timerStart(delay, repeat); },\n"
    "  timerStop: function(handle) { return __pal__.timerStop(handle); },\n"
    "};\n";

/* ================================================================
 * Helper: fill a qwrt_config_t with test defaults
 * ================================================================ */

static void fill_test_config(qwrt_config_t *config, const qwrt_pal_t *pal)
{
    memset(config, 0, sizeof(*config));
    config->pal = pal;
}

/* ================================================================
 * Tests
 * ================================================================ */

TEST(spawn_basic) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NOT_NULL(pal);

    qwrt_config_t config;
    fill_test_config(&config, pal);

    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NOT_NULL(rt);

    /* Original context (id 0) should be active */
    int active_id = qwrt_get_active_ctx_id(rt);
    ASSERT_EQ(active_id, 0);

    /* Set a variable in ctx0 */
    int rc = qwrt_eval(rt, "var ctx0var = 'hello'", NULL);
    ASSERT_EQ(rc, 0);

    /* Spawn a second context — spawn makes the new context active */
    int ctx1_id = qwrt_spawn(rt, &config);
    ASSERT(ctx1_id >= 0);
    ASSERT(ctx1_id != 0);

    /* The newly spawned context should be active */
    active_id = qwrt_get_active_ctx_id(rt);
    ASSERT_EQ(active_id, ctx1_id);

    /* Switch back to ctx0 — its variable should still be accessible */
    rc = qwrt_resume(rt, 0);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(qwrt_get_active_ctx_id(rt), 0);

    char *result = NULL;
    rc = qwrt_eval(rt, "ctx0var", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(result, "\"hello\"");
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

TEST(suspend_resume) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NOT_NULL(pal);

    qwrt_config_t config;
    fill_test_config(&config, pal);

    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NOT_NULL(rt);

    /* Set state in ctx0 */
    int rc = qwrt_eval(rt, "var ctx0state = 42", NULL);
    ASSERT_EQ(rc, 0);

    /* Suspend ctx0 */
    rc = qwrt_suspend(rt);
    ASSERT_EQ(rc, 0);

    /* No context should be active */
    int active_id = qwrt_get_active_ctx_id(rt);
    ASSERT_EQ(active_id, -1);

    /* Spawn ctx1 */
    int ctx1_id = qwrt_spawn(rt, &config);
    ASSERT(ctx1_id >= 0);

    /* Resume ctx1 */
    rc = qwrt_resume(rt, ctx1_id);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(qwrt_get_active_ctx_id(rt), ctx1_id);

    /* ctx1 should not see ctx0's variables — it's a fresh context */
    char *result = NULL;
    rc = qwrt_eval(rt, "typeof ctx0state", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(result, "\"undefined\"");
    qwrt_free(result);

    /* Set state in ctx1 */
    rc = qwrt_eval(rt, "var ctx1state = 99", NULL);
    ASSERT_EQ(rc, 0);

    /* Resume ctx0 — this auto-suspends ctx1 */
    rc = qwrt_resume(rt, 0);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(qwrt_get_active_ctx_id(rt), 0);

    /* ctx0's state should be preserved */
    rc = qwrt_eval(rt, "ctx0state", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(result, "42");
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

TEST(resume_auto_suspends_previous) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NOT_NULL(pal);

    qwrt_config_t config;
    fill_test_config(&config, pal);

    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NOT_NULL(rt);

    /* Spawn ctx1 */
    int ctx1_id = qwrt_spawn(rt, &config);
    ASSERT(ctx1_id >= 0);

    /* Resume ctx1 — auto-suspends ctx0 */
    int rc = qwrt_resume(rt, ctx1_id);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(qwrt_get_active_ctx_id(rt), ctx1_id);

    /* Resume ctx0 — auto-suspends ctx1 */
    rc = qwrt_resume(rt, 0);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(qwrt_get_active_ctx_id(rt), 0);

    /* Resume ctx1 again — auto-suspends ctx0 */
    rc = qwrt_resume(rt, ctx1_id);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(qwrt_get_active_ctx_id(rt), ctx1_id);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

TEST(destroy_ctx) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NOT_NULL(pal);

    qwrt_config_t config;
    fill_test_config(&config, pal);

    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NOT_NULL(rt);

    /* Cannot destroy the only context */
    int rc = qwrt_destroy_ctx(rt, 0);
    ASSERT_EQ(rc, -1);

    /* Spawn a second context */
    int ctx1_id = qwrt_spawn(rt, &config);
    ASSERT(ctx1_id >= 0);

    /* Now we can destroy ctx1 (we have 2 contexts) */
    rc = qwrt_destroy_ctx(rt, ctx1_id);
    ASSERT_EQ(rc, 0);

    /* Cannot destroy the last remaining context */
    rc = qwrt_destroy_ctx(rt, 0);
    ASSERT_EQ(rc, -1);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

TEST(spawn_different_pal) {
    qwrt_pal_t *pal_full = pal_mock_create();
    qwrt_pal_t *pal_no_http = pal_mock_create_no_http();
    ASSERT_NOT_NULL(pal_full);
    ASSERT_NOT_NULL(pal_no_http);

    qwrt_config_t config_full;
    fill_test_config(&config_full, pal_full);

    qwrt_config_t config_no_http;
    fill_test_config(&config_no_http, pal_no_http);

    qwrt_t *rt = qwrt_create(&config_full);
    ASSERT_NOT_NULL(rt);
    qwrt_eval(rt, testHelper_def, NULL);

    /* HTTP works in ctx0 (full PAL) */
    int rc = qwrt_eval(rt,
        "var _ctx0http = null; "
        "testHelper.httpRequest('http://test', 'GET', '', null).then(function(v) { "
        "  _ctx0http = v; "
        "})", NULL);
    ASSERT_EQ(rc, 0);
    rc = qwrt_tick(rt);
    ASSERT_EQ(rc, 0);

    /* Spawn ctx1 with no-http PAL */
    int ctx1_id = qwrt_spawn(rt, &config_no_http);
    ASSERT(ctx1_id >= 0);

    /* Resume ctx1 */
    rc = qwrt_resume(rt, ctx1_id);
    ASSERT_EQ(rc, 0);
    qwrt_eval(rt, testHelper_def, NULL);

    /* HTTP should be denied in ctx1 */
    char *result = NULL;
    rc = qwrt_eval(rt,
        "var _ctx1http = null; "
        "testHelper.httpRequest('http://test', 'GET', '', null).then("
        "  function(v) { _ctx1http = 'ok'; },"
        "  function(e) { _ctx1http = 'err:' + e; }"
        ")", NULL);
    ASSERT_EQ(rc, 0);
    rc = qwrt_tick(rt);
    ASSERT_EQ(rc, 0);

    rc = qwrt_eval(rt, "_ctx1http", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(result);
    ASSERT(strstr(result, "Permission denied") != NULL);
    qwrt_free(result);

    /* Go back to ctx0 — HTTP should still work */
    rc = qwrt_resume(rt, 0);
    ASSERT_EQ(rc, 0);

    rc = qwrt_eval(rt,
        "var _ctx0http2 = null; "
        "testHelper.httpRequest('http://test2', 'GET', '', null).then(function(v) { "
        "  _ctx0http2 = v; "
        "})", NULL);
    ASSERT_EQ(rc, 0);
    rc = qwrt_tick(rt);
    ASSERT_EQ(rc, 0);

    rc = qwrt_eval(rt, "_ctx0http2", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(result);
    /* Should contain mock response, not permission error */
    ASSERT(strstr(result, "mock response") != NULL);
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_mock_destroy(pal_full);
    pal_mock_destroy(pal_no_http);
}

/* ================================================================
 * Edge cases
 * ================================================================ */

TEST(resume_active_ctx_is_noop) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NOT_NULL(pal);

    qwrt_config_t config;
    fill_test_config(&config, pal);

    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NOT_NULL(rt);

    int active_id = qwrt_get_active_ctx_id(rt);
    ASSERT_EQ(active_id, 0);

    /* Resume ctx0 when it's already active — should be a no-op */
    int rc = qwrt_resume(rt, 0);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(qwrt_get_active_ctx_id(rt), 0);

    /* Variable should still be there */
    rc = qwrt_eval(rt, "var _noop_test = 1", NULL);
    ASSERT_EQ(rc, 0);

    char *result = NULL;
    rc = qwrt_eval(rt, "_noop_test", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(result, "1");
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

TEST(closures_preserved_after_suspend) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NOT_NULL(pal);

    qwrt_config_t config;
    fill_test_config(&config, pal);

    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NOT_NULL(rt);

    /* Create a closure with captured variable */
    int rc = qwrt_eval(rt,
        "var _counter = 0; "
        "function _inc() { _counter++; return _counter; }",
        NULL);
    ASSERT_EQ(rc, 0);

    /* Call it to advance state */
    char *result = NULL;
    rc = qwrt_eval(rt, "_inc()", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(result, "1");
    qwrt_free(result);

    /* Suspend ctx0 */
    rc = qwrt_suspend(rt);
    ASSERT_EQ(rc, 0);

    /* Spawn ctx1 to confirm ctx0 is truly suspended */
    int ctx1_id = qwrt_spawn(rt, &config);
    ASSERT(ctx1_id >= 0);
    rc = qwrt_resume(rt, ctx1_id);
    ASSERT_EQ(rc, 0);

    /* ctx1 should not see _counter */
    rc = qwrt_eval(rt, "typeof _counter", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(result, "\"undefined\"");
    qwrt_free(result);

    /* Resume ctx0 — closure and captured var should still work */
    rc = qwrt_resume(rt, 0);
    ASSERT_EQ(rc, 0);

    rc = qwrt_eval(rt, "_counter", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(result, "1");
    qwrt_free(result);

    rc = qwrt_eval(rt, "_inc()", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(result, "2");
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

TEST(three_contexts_round_robin) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NOT_NULL(pal);

    qwrt_config_t config;
    fill_test_config(&config, pal);

    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NOT_NULL(rt);

    /* Set state in ctx0 */
    int rc = qwrt_eval(rt, "var _id = 'A'", NULL);
    ASSERT_EQ(rc, 0);

    /* Spawn ctx1, set its state */
    int ctx1 = qwrt_spawn(rt, &config);
    ASSERT(ctx1 >= 0);
    rc = qwrt_resume(rt, ctx1);
    ASSERT_EQ(rc, 0);
    rc = qwrt_eval(rt, "var _id = 'B'", NULL);
    ASSERT_EQ(rc, 0);

    /* Spawn ctx2, set its state */
    int ctx2 = qwrt_spawn(rt, &config);
    ASSERT(ctx2 >= 0);
    rc = qwrt_resume(rt, ctx2);
    ASSERT_EQ(rc, 0);
    rc = qwrt_eval(rt, "var _id = 'C'", NULL);
    ASSERT_EQ(rc, 0);

    /* Round-robin: A → B → C → A → B → C */
    char *result = NULL;

    rc = qwrt_resume(rt, 0);
    ASSERT_EQ(rc, 0);
    rc = qwrt_eval(rt, "_id", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(result, "\"A\"");
    qwrt_free(result);

    rc = qwrt_resume(rt, ctx1);
    ASSERT_EQ(rc, 0);
    rc = qwrt_eval(rt, "_id", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(result, "\"B\"");
    qwrt_free(result);

    rc = qwrt_resume(rt, ctx2);
    ASSERT_EQ(rc, 0);
    rc = qwrt_eval(rt, "_id", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(result, "\"C\"");
    qwrt_free(result);

    /* Second round */
    rc = qwrt_resume(rt, 0);
    ASSERT_EQ(rc, 0);
    rc = qwrt_eval(rt, "_id", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(result, "\"A\"");
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

TEST(multiple_suspend_resume_cycles) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NOT_NULL(pal);

    qwrt_config_t config;
    fill_test_config(&config, pal);

    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NOT_NULL(rt);

    /* Set state */
    int rc = qwrt_eval(rt, "var _cycle = 0", NULL);
    ASSERT_EQ(rc, 0);

    /* Do 10 suspend/resume cycles, each time incrementing _cycle */
    for (int i = 0; i < 10; i++) {
        rc = qwrt_suspend(rt);
        ASSERT_EQ(rc, 0);
        ASSERT_EQ(qwrt_get_active_ctx_id(rt), -1);

        rc = qwrt_resume(rt, 0);
        ASSERT_EQ(rc, 0);
        ASSERT_EQ(qwrt_get_active_ctx_id(rt), 0);

        char *result = NULL;
        rc = qwrt_eval(rt, "_cycle++; _cycle", &result);
        ASSERT_EQ(rc, 0);
        ASSERT_NOT_NULL(result);
        char expected[8];
        snprintf(expected, sizeof(expected), "%d", i + 1);
        ASSERT_STR_EQ(result, expected);
        qwrt_free(result);
    }

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

TEST(suspend_with_pending_timer) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NOT_NULL(pal);

    qwrt_config_t config;
    fill_test_config(&config, pal);

    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NOT_NULL(rt);
    qwrt_eval(rt, testHelper_def, NULL);

    /* Start a timer — timerStart returns {handle, promise} */
    int rc = qwrt_eval(rt,
        "var _timerFired = false; "
        "testHelper.timerStart(1000, 0).promise.then(function() { _timerFired = true; })",
        NULL);
    ASSERT_EQ(rc, 0);

    /* Fire the mock timer (first timer gets handle_id 1) */
    pal_mock_fire_timer(pal, 1);
    rc = qwrt_tick(rt);
    ASSERT_EQ(rc, 0);

    /* Timer should have fired */
    char *result = NULL;
    rc = qwrt_eval(rt, "_timerFired", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(result, "true");
    qwrt_free(result);

    /* Suspend, resume — timer cleanup should not crash */
    rc = qwrt_suspend(rt);
    ASSERT_EQ(rc, 0);

    rc = qwrt_resume(rt, 0);
    ASSERT_EQ(rc, 0);

    /* Runtime should still work after resume */
    rc = qwrt_eval(rt, "1 + 1", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(result, "2");
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

TEST(spawn_resume_destroy_cycle) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NOT_NULL(pal);

    qwrt_config_t config;
    fill_test_config(&config, pal);

    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NOT_NULL(rt);

    int rc;
    /* Spawn, use, destroy — repeat 5 times */
    for (int i = 0; i < 5; i++) {
        int ctx_id = qwrt_spawn(rt, &config);
        ASSERT(ctx_id >= 0);

        rc = qwrt_resume(rt, ctx_id);
        ASSERT_EQ(rc, 0);

        char code[64];
        snprintf(code, sizeof(code), "var _x = %d", i * 10);
        rc = qwrt_eval(rt, code, NULL);
        ASSERT_EQ(rc, 0);

        char *result = NULL;
        rc = qwrt_eval(rt, "_x", &result);
        ASSERT_EQ(rc, 0);
        char expected[16];
        snprintf(expected, sizeof(expected), "%d", i * 10);
        ASSERT_STR_EQ(result, expected);
        qwrt_free(result);

        rc = qwrt_destroy_ctx(rt, ctx_id);
        ASSERT_EQ(rc, 0);
    }

    /* Resume ctx0 — the last destroy left active_ctx_id = -1 */
    rc = qwrt_resume(rt, 0);
    ASSERT_EQ(rc, 0);

    /* ctx0 should still be intact */
    char *result = NULL;
    rc = qwrt_eval(rt, "typeof _x", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(result, "\"undefined\"");
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

/* ================================================================
 * Main
 * ================================================================ */

int main(void)
{
    printf("=== qwrt Multi-Context Tests ===\n\n");

    printf("--- spawn ---\n");
    RUN_TEST(spawn_basic);

    printf("\n--- suspend/resume ---\n");
    RUN_TEST(suspend_resume);
    RUN_TEST(resume_auto_suspends_previous);
    RUN_TEST(multiple_suspend_resume_cycles);

    printf("\n--- edge cases ---\n");
    RUN_TEST(resume_active_ctx_is_noop);
    RUN_TEST(closures_preserved_after_suspend);
    RUN_TEST(suspend_with_pending_timer);

    printf("\n--- multi-context ---\n");
    RUN_TEST(three_contexts_round_robin);
    RUN_TEST(spawn_resume_destroy_cycle);

    printf("\n--- destroy_ctx ---\n");
    RUN_TEST(destroy_ctx);

    printf("\n--- per-context PAL ---\n");
    RUN_TEST(spawn_different_pal);

    printf("\n=== Results: %d/%d passed, %d failed ===\n",
           tests_passed, tests_run, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}

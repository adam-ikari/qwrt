/*
 * qwrt Multi-Context Tests (Google Test)
 *
 * Tests for spawn, suspend, resume, destroy_ctx, and per-context PAL.
 */

#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "qwrt/qwrt.h"
#include "pal_mock.h"
}

/* ================================================================
 * Minimal test polyfill — exposes PAL primitives as testHelper.*
 * ================================================================ */

static const char test_polyfill[] =
    "(function(pal) {\n"
    "  globalThis.testHelper = {\n"
    "    storageSet: function(k, v) { return pal.storageSet(k, v); },\n"
    "    storageGet: function(k) { return pal.storageGet(k); },\n"
    "    timeNow: function() { return pal.timeNow(); },\n"
    "    log: function(level, msg) { pal.log(level, msg); },\n"
    "    fsWrite: function(p, d) { return pal.fsWrite(p, d); },\n"
    "    fsRead: function(p) { return pal.fsRead(p); },\n"
    "    httpRequest: function(url, method, headers, body) {\n"
    "      return pal.httpRequest(url, method, headers, body);\n"
    "    },\n"
    "    timerStart: function(delay, repeat) { return pal.timerStart(delay, repeat); },\n"
    "    timerStop: function(handle) { return pal.timerStop(handle); },\n"
    "  };\n"
    "  globalThis.console = {\n"
    "    log: function() { pal.log(0, Array.from(arguments).join(' ')); },\n"
    "    error: function() { pal.log(2, Array.from(arguments).join(' ')); },\n"
    "  };\n"
    "  globalThis.performance = {\n"
    "    now: function() { return pal.timeNow(); },\n"
    "  };\n"
    "})(__pal__);";

/* ================================================================
 * Helper: fill a qwrt_config_t with test defaults
 * ================================================================ */

static void fill_test_config(qwrt_config_t *config, const qwrt_pal_t *pal)
{
    memset(config, 0, sizeof(*config));
    config->pal = pal;
}

/*
 * Helper: create a runtime and inject the test polyfill so testHelper.*
 * and console.* are available.  Returns the runtime (never NULL — fatal
 * via ASSERT if creation fails).
 */
static qwrt_t *create_runtime_with_polyfill(const qwrt_pal_t *pal)
{
    qwrt_config_t config;
    fill_test_config(&config, pal);
    qwrt_t *rt = qwrt_create(&config);
    EXPECT_NE(rt, nullptr);
    if (!rt) return nullptr;

    int rc = qwrt_eval(rt, test_polyfill, nullptr);
    EXPECT_GE(rc, 0);
    return rt;
}

/* ================================================================
 * Tests
 * ================================================================ */

TEST(QwrtContext, SpawnBasic) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);

    qwrt_config_t config;
    fill_test_config(&config, pal);

    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NE(rt, nullptr);

    /* Original context (id 0) should be active */
    int active_id = qwrt_get_active_ctx_id(rt);
    EXPECT_EQ(active_id, 0);

    /* Set a variable in ctx0 */
    int rc = qwrt_eval(rt, "var ctx0var = 'hello'", nullptr);
    EXPECT_GE(rc, 0);

    /* Spawn a second context — spawn makes the new context active */
    int ctx1_id = qwrt_spawn(rt, &config);
    EXPECT_GE(ctx1_id, 0);
    EXPECT_NE(ctx1_id, 0);

    /* The newly spawned context should be active */
    active_id = qwrt_get_active_ctx_id(rt);
    EXPECT_EQ(active_id, ctx1_id);

    /* Switch back to ctx0 — its variable should still be accessible */
    rc = qwrt_resume(rt, 0);
    EXPECT_GE(rc, 0);
    EXPECT_EQ(qwrt_get_active_ctx_id(rt), 0);

    char *result = nullptr;
    rc = qwrt_eval(rt, "ctx0var", &result);
    EXPECT_GE(rc, 0);
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result, "\"hello\"");
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

TEST(QwrtContext, SuspendResume) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);

    qwrt_config_t config;
    fill_test_config(&config, pal);

    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NE(rt, nullptr);

    /* Set state in ctx0 */
    int rc = qwrt_eval(rt, "var ctx0state = 42", nullptr);
    EXPECT_GE(rc, 0);

    /* Suspend ctx0 */
    rc = qwrt_suspend(rt);
    EXPECT_GE(rc, 0);

    /* No context should be active */
    int active_id = qwrt_get_active_ctx_id(rt);
    EXPECT_EQ(active_id, -1);

    /* Spawn ctx1 */
    int ctx1_id = qwrt_spawn(rt, &config);
    EXPECT_GE(ctx1_id, 0);

    /* Resume ctx1 */
    rc = qwrt_resume(rt, ctx1_id);
    EXPECT_GE(rc, 0);
    EXPECT_EQ(qwrt_get_active_ctx_id(rt), ctx1_id);

    /* ctx1 should not see ctx0's variables — it's a fresh context */
    char *result = nullptr;
    rc = qwrt_eval(rt, "typeof ctx0state", &result);
    EXPECT_GE(rc, 0);
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result, "\"undefined\"");
    qwrt_free(result);

    /* Set state in ctx1 */
    rc = qwrt_eval(rt, "var ctx1state = 99", nullptr);
    EXPECT_GE(rc, 0);

    /* Resume ctx0 — this auto-suspends ctx1 */
    rc = qwrt_resume(rt, 0);
    EXPECT_GE(rc, 0);
    EXPECT_EQ(qwrt_get_active_ctx_id(rt), 0);

    /* ctx0's state should be preserved */
    rc = qwrt_eval(rt, "ctx0state", &result);
    EXPECT_GE(rc, 0);
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result, "42");
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

TEST(QwrtContext, ResumeAutoSuspendsPrevious) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);

    qwrt_config_t config;
    fill_test_config(&config, pal);

    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NE(rt, nullptr);

    /* Spawn ctx1 */
    int ctx1_id = qwrt_spawn(rt, &config);
    EXPECT_GE(ctx1_id, 0);

    /* Resume ctx1 — auto-suspends ctx0 */
    int rc = qwrt_resume(rt, ctx1_id);
    EXPECT_GE(rc, 0);
    EXPECT_EQ(qwrt_get_active_ctx_id(rt), ctx1_id);

    /* Resume ctx0 — auto-suspends ctx1 */
    rc = qwrt_resume(rt, 0);
    EXPECT_GE(rc, 0);
    EXPECT_EQ(qwrt_get_active_ctx_id(rt), 0);

    /* Resume ctx1 again — auto-suspends ctx0 */
    rc = qwrt_resume(rt, ctx1_id);
    EXPECT_GE(rc, 0);
    EXPECT_EQ(qwrt_get_active_ctx_id(rt), ctx1_id);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

TEST(QwrtContext, DestroyCtx) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);

    qwrt_config_t config;
    fill_test_config(&config, pal);

    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NE(rt, nullptr);

    /* Cannot destroy the only context */
    int rc = qwrt_destroy_ctx(rt, 0);
    EXPECT_EQ(rc, -1);

    /* Spawn a second context */
    int ctx1_id = qwrt_spawn(rt, &config);
    EXPECT_GE(ctx1_id, 0);

    /* Now we can destroy ctx1 (we have 2 contexts) */
    rc = qwrt_destroy_ctx(rt, ctx1_id);
    EXPECT_GE(rc, 0);

    /* Cannot destroy the last remaining context */
    rc = qwrt_destroy_ctx(rt, 0);
    EXPECT_EQ(rc, -1);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

TEST(QwrtContext, SpawnDifferentPal) {
    qwrt_pal_t *pal_full = pal_mock_create();
    qwrt_pal_t *pal_no_http = pal_mock_create_no_http();
    ASSERT_NE(pal_full, nullptr);
    ASSERT_NE(pal_no_http, nullptr);

    qwrt_config_t config_no_http;
    fill_test_config(&config_no_http, pal_no_http);

    qwrt_t *rt = create_runtime_with_polyfill(pal_full);
    ASSERT_NE(rt, nullptr);

    /* HTTP works in ctx0 (full PAL) */
    int rc = qwrt_eval(rt,
        "var _ctx0http = null; "
        "testHelper.httpRequest('http://test', 'GET', '', null).then(function(v) { "
        "  _ctx0http = v; "
        "})", nullptr);
    EXPECT_GE(rc, 0);
    rc = qwrt_tick(rt, 100);
    EXPECT_GE(rc, 0);

    /* Spawn ctx1 with no-http PAL */
    int ctx1_id = qwrt_spawn(rt, &config_no_http);
    EXPECT_GE(ctx1_id, 0);

    /* Resume ctx1 */
    rc = qwrt_resume(rt, ctx1_id);
    EXPECT_GE(rc, 0);

    /* Inject polyfill into ctx1 so testHelper is available */
    rc = qwrt_eval(rt, test_polyfill, nullptr);
    EXPECT_GE(rc, 0);

    /* HTTP should be denied in ctx1 */
    char *result = nullptr;
    rc = qwrt_eval(rt,
        "var _ctx1http = null; "
        "testHelper.httpRequest('http://test', 'GET', '', null).then("
        "  function(v) { _ctx1http = 'ok'; },"
        "  function(e) { _ctx1http = 'err:' + e; }"
        ")", nullptr);
    EXPECT_GE(rc, 0);
    rc = qwrt_tick(rt, 100);
    EXPECT_GE(rc, 0);

    rc = qwrt_eval(rt, "_ctx1http", &result);
    EXPECT_GE(rc, 0);
    ASSERT_NE(result, nullptr);
    EXPECT_NE(strstr(result, "Permission denied"), nullptr);
    qwrt_free(result);

    /* Go back to ctx0 — HTTP should still work */
    rc = qwrt_resume(rt, 0);
    EXPECT_GE(rc, 0);

    rc = qwrt_eval(rt,
        "var _ctx0http2 = null; "
        "testHelper.httpRequest('http://test2', 'GET', '', null).then(function(v) { "
        "  _ctx0http2 = v; "
        "})", nullptr);
    EXPECT_GE(rc, 0);
    rc = qwrt_tick(rt, 100);
    EXPECT_GE(rc, 0);

    rc = qwrt_eval(rt, "_ctx0http2", &result);
    EXPECT_GE(rc, 0);
    ASSERT_NE(result, nullptr);
    /* Should contain mock response, not permission error */
    EXPECT_NE(strstr(result, "mock response"), nullptr);
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_mock_destroy(pal_full);
    pal_mock_destroy(pal_no_http);
}

/* ================================================================
 * Edge cases
 * ================================================================ */

TEST(QwrtContext, ResumeActiveCtxIsNoop) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);

    qwrt_config_t config;
    fill_test_config(&config, pal);

    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NE(rt, nullptr);

    int active_id = qwrt_get_active_ctx_id(rt);
    EXPECT_EQ(active_id, 0);

    /* Resume ctx0 when it's already active — should be a no-op */
    int rc = qwrt_resume(rt, 0);
    EXPECT_GE(rc, 0);
    EXPECT_EQ(qwrt_get_active_ctx_id(rt), 0);

    /* Variable should still be there */
    rc = qwrt_eval(rt, "var _noop_test = 1", nullptr);
    EXPECT_GE(rc, 0);

    char *result = nullptr;
    rc = qwrt_eval(rt, "_noop_test", &result);
    EXPECT_GE(rc, 0);
    EXPECT_STREQ(result, "1");
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

TEST(QwrtContext, ClosuresPreservedAfterSuspend) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);

    qwrt_config_t config;
    fill_test_config(&config, pal);

    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NE(rt, nullptr);

    /* Create a closure with captured variable */
    int rc = qwrt_eval(rt,
        "var _counter = 0; "
        "function _inc() { _counter++; return _counter; }",
        nullptr);
    EXPECT_GE(rc, 0);

    /* Call it to advance state */
    char *result = nullptr;
    rc = qwrt_eval(rt, "_inc()", &result);
    EXPECT_GE(rc, 0);
    EXPECT_STREQ(result, "1");
    qwrt_free(result);

    /* Suspend ctx0 */
    rc = qwrt_suspend(rt);
    EXPECT_GE(rc, 0);

    /* Spawn ctx1 to confirm ctx0 is truly suspended */
    int ctx1_id = qwrt_spawn(rt, &config);
    EXPECT_GE(ctx1_id, 0);
    rc = qwrt_resume(rt, ctx1_id);
    EXPECT_GE(rc, 0);

    /* ctx1 should not see _counter */
    rc = qwrt_eval(rt, "typeof _counter", &result);
    EXPECT_GE(rc, 0);
    EXPECT_STREQ(result, "\"undefined\"");
    qwrt_free(result);

    /* Resume ctx0 — closure and captured var should still work */
    rc = qwrt_resume(rt, 0);
    EXPECT_GE(rc, 0);

    rc = qwrt_eval(rt, "_counter", &result);
    EXPECT_GE(rc, 0);
    EXPECT_STREQ(result, "1");
    qwrt_free(result);

    rc = qwrt_eval(rt, "_inc()", &result);
    EXPECT_GE(rc, 0);
    EXPECT_STREQ(result, "2");
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

TEST(QwrtContext, ThreeContextsRoundRobin) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);

    qwrt_config_t config;
    fill_test_config(&config, pal);

    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NE(rt, nullptr);

    /* Set state in ctx0 */
    int rc = qwrt_eval(rt, "var _id = 'A'", nullptr);
    EXPECT_GE(rc, 0);

    /* Spawn ctx1, set its state */
    int ctx1 = qwrt_spawn(rt, &config);
    EXPECT_GE(ctx1, 0);
    rc = qwrt_resume(rt, ctx1);
    EXPECT_GE(rc, 0);
    rc = qwrt_eval(rt, "var _id = 'B'", nullptr);
    EXPECT_GE(rc, 0);

    /* Spawn ctx2, set its state */
    int ctx2 = qwrt_spawn(rt, &config);
    EXPECT_GE(ctx2, 0);
    rc = qwrt_resume(rt, ctx2);
    EXPECT_GE(rc, 0);
    rc = qwrt_eval(rt, "var _id = 'C'", nullptr);
    EXPECT_GE(rc, 0);

    /* Round-robin: A -> B -> C -> A */
    char *result = nullptr;

    rc = qwrt_resume(rt, 0);
    EXPECT_GE(rc, 0);
    rc = qwrt_eval(rt, "_id", &result);
    EXPECT_GE(rc, 0);
    EXPECT_STREQ(result, "\"A\"");
    qwrt_free(result);

    rc = qwrt_resume(rt, ctx1);
    EXPECT_GE(rc, 0);
    rc = qwrt_eval(rt, "_id", &result);
    EXPECT_GE(rc, 0);
    EXPECT_STREQ(result, "\"B\"");
    qwrt_free(result);

    rc = qwrt_resume(rt, ctx2);
    EXPECT_GE(rc, 0);
    rc = qwrt_eval(rt, "_id", &result);
    EXPECT_GE(rc, 0);
    EXPECT_STREQ(result, "\"C\"");
    qwrt_free(result);

    /* Second round */
    rc = qwrt_resume(rt, 0);
    EXPECT_GE(rc, 0);
    rc = qwrt_eval(rt, "_id", &result);
    EXPECT_GE(rc, 0);
    EXPECT_STREQ(result, "\"A\"");
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

TEST(QwrtContext, MultipleSuspendResumeCycles) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);

    qwrt_config_t config;
    fill_test_config(&config, pal);

    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NE(rt, nullptr);

    /* Set state */
    int rc = qwrt_eval(rt, "var _cycle = 0", nullptr);
    EXPECT_GE(rc, 0);

    /* Do 10 suspend/resume cycles */
    for (int i = 0; i < 10; i++) {
        rc = qwrt_suspend(rt);
        EXPECT_GE(rc, 0);
        EXPECT_EQ(qwrt_get_active_ctx_id(rt), -1);

        rc = qwrt_resume(rt, 0);
        EXPECT_GE(rc, 0);
        EXPECT_EQ(qwrt_get_active_ctx_id(rt), 0);

        char *result = nullptr;
        rc = qwrt_eval(rt, "_cycle++; _cycle", &result);
        EXPECT_GE(rc, 0);
        ASSERT_NE(result, nullptr);
        char expected[8];
        snprintf(expected, sizeof(expected), "%d", i + 1);
        EXPECT_STREQ(result, expected);
        qwrt_free(result);
    }

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

TEST(QwrtContext, SuspendWithPendingTimer) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);

    qwrt_t *rt = create_runtime_with_polyfill(pal);
    ASSERT_NE(rt, nullptr);

    /* Start a timer — timerStart returns {handle, promise} */
    int rc = qwrt_eval(rt,
        "var _timerFired = false; "
        "testHelper.timerStart(1000, 0).promise.then(function() { _timerFired = true; })",
        nullptr);
    EXPECT_GE(rc, 0);

    /* Fire the mock timer (first timer gets handle_id 1) */
    pal_mock_fire_timer(pal, 1);
    rc = qwrt_tick(rt, 100);
    EXPECT_GE(rc, 0);

    /* Timer should have fired */
    char *result = nullptr;
    rc = qwrt_eval(rt, "_timerFired", &result);
    EXPECT_GE(rc, 0);
    EXPECT_STREQ(result, "true");
    qwrt_free(result);

    /* Suspend, resume — timer cleanup should not crash */
    rc = qwrt_suspend(rt);
    EXPECT_GE(rc, 0);

    rc = qwrt_resume(rt, 0);
    EXPECT_GE(rc, 0);

    /* Runtime should still work after resume */
    rc = qwrt_eval(rt, "1 + 1", &result);
    EXPECT_GE(rc, 0);
    EXPECT_STREQ(result, "2");
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

TEST(QwrtContext, SpawnResumeDestroyCycle) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);

    qwrt_config_t config;
    fill_test_config(&config, pal);

    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NE(rt, nullptr);

    int rc;
    /* Spawn, use, destroy — repeat 5 times */
    for (int i = 0; i < 5; i++) {
        int ctx_id = qwrt_spawn(rt, &config);
        EXPECT_GE(ctx_id, 0);

        rc = qwrt_resume(rt, ctx_id);
        EXPECT_GE(rc, 0);

        char code[64];
        snprintf(code, sizeof(code), "var _x = %d", i * 10);
        rc = qwrt_eval(rt, code, nullptr);
        EXPECT_GE(rc, 0);

        char *result = nullptr;
        rc = qwrt_eval(rt, "_x", &result);
        EXPECT_GE(rc, 0);
        char expected[16];
        snprintf(expected, sizeof(expected), "%d", i * 10);
        EXPECT_STREQ(result, expected);
        qwrt_free(result);

        rc = qwrt_destroy_ctx(rt, ctx_id);
        EXPECT_GE(rc, 0);
    }

    /* Resume ctx0 — the last destroy left active_ctx_id = -1 */
    rc = qwrt_resume(rt, 0);
    EXPECT_GE(rc, 0);

    /* ctx0 should still be intact */
    char *result = nullptr;
    rc = qwrt_eval(rt, "typeof _x", &result);
    EXPECT_GE(rc, 0);
    EXPECT_STREQ(result, "\"undefined\"");
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

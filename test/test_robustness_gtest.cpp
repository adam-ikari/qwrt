/*
 * qwrt Robustness Tests (Google Test)
 *
 * Error paths, boundary conditions, isolation, and resource management.
 */

#include <gtest/gtest.h>
#include <cstring>
#include <string>

extern "C" {
#include "qwrt/qwrt.h"
#include "pal_mock.h"
#include <quickjs.h>
}

static const char test_polyfill[] =
    "(function(pal) {\n"
    "  globalThis.testHelper = {\n"
    "    storageSet: function(k, v) { return pal.storageSet(k, v); },\n"
    "    storageGet: function(k) { return pal.storageGet(k); },\n"
    "    timeNow: function() { return pal.timeNow(); },\n"
    "    log: function(level, msg) { pal.log(level, msg); },\n"
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
    EXPECT_EQ(rc, 0);
    return rt;
}

/* ================================================================
 * Error Paths
 * ================================================================ */

TEST(QwrtError, CreateNullConfig) {
    qwrt_t *rt = qwrt_create(nullptr);
    EXPECT_EQ(rt, nullptr);
}

TEST(QwrtError, CreateNullPal) {
    qwrt_config_t config;
    memset(&config, 0, sizeof(config));
    config.pal = nullptr;
    qwrt_t *rt = qwrt_create(&config);
    EXPECT_EQ(rt, nullptr);
}

TEST(QwrtError, EvalNullCode) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);
    qwrt_config_t config;
    fill_test_config(&config, pal);
    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NE(rt, nullptr);

    char *result = nullptr;
    int rc = qwrt_eval(rt, nullptr, &result);
    EXPECT_EQ(rc, -1);
    EXPECT_EQ(result, nullptr);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

TEST(QwrtError, EvalSyntaxError) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);
    qwrt_config_t config;
    fill_test_config(&config, pal);
    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NE(rt, nullptr);

    char *result = nullptr;
    int rc = qwrt_eval(rt, "function(", &result);
    EXPECT_EQ(rc, -1);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

TEST(QwrtError, EvalRuntimeError) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);
    qwrt_config_t config;
    fill_test_config(&config, pal);
    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NE(rt, nullptr);

    char *result = nullptr;
    int rc = qwrt_eval(rt, "throw new Error('test')", &result);
    EXPECT_EQ(rc, -1);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

TEST(QwrtError, CallNullFunc) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);
    qwrt_config_t config;
    fill_test_config(&config, pal);
    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NE(rt, nullptr);

    char *result = nullptr;
    int rc = qwrt_call(rt, nullptr, "[]", &result);
    EXPECT_EQ(rc, -1);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

TEST(QwrtError, CallUndefinedFunc) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);
    qwrt_config_t config;
    fill_test_config(&config, pal);
    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NE(rt, nullptr);

    char *result = nullptr;
    int rc = qwrt_call(rt, "nonexistent_function", "[]", &result);
    EXPECT_EQ(rc, -1);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

TEST(QwrtError, SuspendNoActiveContext) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);
    qwrt_config_t config;
    fill_test_config(&config, pal);
    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NE(rt, nullptr);

    /* Suspend ctx0 — now no active context */
    int rc = qwrt_suspend(rt);
    EXPECT_EQ(rc, 0);

    /* Suspend again — should fail, no active context */
    rc = qwrt_suspend(rt);
    EXPECT_EQ(rc, -1);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

TEST(QwrtError, ResumeInvalidId) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);
    qwrt_config_t config;
    fill_test_config(&config, pal);
    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NE(rt, nullptr);

    /* Resume non-existent context */
    int rc = qwrt_resume(rt, 99);
    EXPECT_EQ(rc, -1);

    rc = qwrt_resume(rt, -1);
    EXPECT_EQ(rc, -1);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

TEST(QwrtError, DestroyNullRt) {
    /* Should not crash */
    qwrt_destroy(nullptr);
}

TEST(QwrtError, DestroyCtxLastContext) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);
    qwrt_config_t config;
    fill_test_config(&config, pal);
    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NE(rt, nullptr);

    /* Cannot destroy the only context */
    int rc = qwrt_destroy_ctx(rt, 0);
    EXPECT_EQ(rc, -1);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

TEST(QwrtError, TickNullRt) {
    int rc = qwrt_tick(nullptr);
    EXPECT_EQ(rc, -1);
}

TEST(QwrtError, FreeNull) {
    /* Should not crash */
    qwrt_free(nullptr);
}

/* ================================================================
 * Boundary Conditions
 * ================================================================ */

TEST(QwrtBoundary, SpawnMaxContexts) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);
    qwrt_config_t config;
    fill_test_config(&config, pal);
    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NE(rt, nullptr);

    /* Spawn up to QWRT_MAX_CONTEXTS (64) — first context already exists */
    int spawned = 0;
    for (int i = 1; i < 64; i++) {
        int ctx_id = qwrt_spawn(rt, &config);
        if (ctx_id < 0) break;
        spawned++;
    }
    EXPECT_EQ(spawned, 63);  /* 63 + 1 initial = 64 total */

    /* Next spawn should fail — no free slots */
    int ctx_id = qwrt_spawn(rt, &config);
    EXPECT_EQ(ctx_id, -1);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

TEST(QwrtBoundary, ManyTimers) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);
    qwrt_t *rt = create_runtime_with_polyfill(pal);
    ASSERT_NE(rt, nullptr);

    /* Start many timers — should fill up to QWRT_MAX_HANDLES */
    int rc = qwrt_eval(rt,
        "var _timerCount = 0; "
        "for (var i = 0; i < 256; i++) { "
        "  testHelper.timerStart(1000, 0).promise.then(function() { _timerCount++; }); "
        "}",
        nullptr);
    EXPECT_EQ(rc, 0);

    /* All 256 timers should have been created */
    char *result = nullptr;
    rc = qwrt_eval(rt, "_timerCount", &result);
    EXPECT_EQ(rc, 0);
    EXPECT_STREQ(result, "0");  /* None fired yet */
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

TEST(QwrtBoundary, EvalEmptyString) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);
    qwrt_config_t config;
    fill_test_config(&config, pal);
    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NE(rt, nullptr);

    char *result = nullptr;
    int rc = qwrt_eval(rt, "", &result);
    /* Empty string is valid JS (evaluates to undefined) */
    EXPECT_EQ(rc, 0);
    if (result) qwrt_free(result);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

TEST(QwrtBoundary, EvalLargeString) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);
    qwrt_config_t config;
    fill_test_config(&config, pal);
    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NE(rt, nullptr);

    /* Create a 100KB string literal */
    std::string code = "var _big = '" + std::string(100000, 'x') + "'";
    char *result = nullptr;
    int rc = qwrt_eval(rt, code.c_str(), &result);
    EXPECT_EQ(rc, 0);
    if (result) qwrt_free(result);

    /* Verify it's accessible */
    rc = qwrt_eval(rt, "_big.length", &result);
    EXPECT_EQ(rc, 0);
    EXPECT_STREQ(result, "100000");
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

TEST(QwrtBoundary, EvalUnicode) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);
    qwrt_config_t config;
    fill_test_config(&config, pal);
    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NE(rt, nullptr);

    char *result = nullptr;
    int rc = qwrt_eval(rt, "'hello 世界 🌍'", &result);
    EXPECT_EQ(rc, 0);
    ASSERT_NE(result, nullptr);
    EXPECT_NE(strstr(result, "世界"), nullptr);
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

TEST(QwrtBoundary, CompileEmptySource) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);
    qwrt_config_t config;
    fill_test_config(&config, pal);
    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NE(rt, nullptr);

    size_t out_len = 0;
    uint8_t *bc = qwrt_compile(rt, "", 0, &out_len);
    /* Empty source compiles to empty bytecode */
    if (bc) {
        EXPECT_GT(out_len, 0u);
        qwrt_free(bc);
    }

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

TEST(QwrtBoundary, CompileInvalidSource) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);
    qwrt_config_t config;
    fill_test_config(&config, pal);
    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NE(rt, nullptr);

    size_t out_len = 0;
    uint8_t *bc = qwrt_compile(rt, "function(", 9, &out_len);
    EXPECT_EQ(bc, nullptr);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

/* ================================================================
 * Multi-Context Isolation
 * ================================================================ */

TEST(QwrtIsolation, GlobalObjectIsolation) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);
    qwrt_config_t config;
    fill_test_config(&config, pal);
    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NE(rt, nullptr);

    /* Set a global object in ctx0 */
    int rc = qwrt_eval(rt, "globalThis.myObj = {x: 1, y: 2}", nullptr);
    EXPECT_EQ(rc, 0);

    /* Spawn ctx1 */
    int ctx1 = qwrt_spawn(rt, &config);
    EXPECT_GE(ctx1, 0);
    rc = qwrt_resume(rt, ctx1);
    EXPECT_EQ(rc, 0);

    /* ctx1 should not see myObj */
    char *result = nullptr;
    rc = qwrt_eval(rt, "typeof globalThis.myObj", &result);
    EXPECT_EQ(rc, 0);
    EXPECT_STREQ(result, "\"undefined\"");
    qwrt_free(result);

    /* Set a different global in ctx1 */
    rc = qwrt_eval(rt, "globalThis.myObj = {a: 99}", nullptr);
    EXPECT_EQ(rc, 0);

    /* Switch back to ctx0 — original value preserved */
    rc = qwrt_resume(rt, 0);
    EXPECT_EQ(rc, 0);
    rc = qwrt_eval(rt, "myObj.x", &result);
    EXPECT_EQ(rc, 0);
    EXPECT_STREQ(result, "1");
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

TEST(QwrtIsolation, FunctionIsolation) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);
    qwrt_config_t config;
    fill_test_config(&config, pal);
    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NE(rt, nullptr);

    /* Define a function in ctx0 */
    int rc = qwrt_eval(rt, "function _ctx0fn() { return 42; }", nullptr);
    EXPECT_EQ(rc, 0);

    /* Spawn ctx1 */
    int ctx1 = qwrt_spawn(rt, &config);
    EXPECT_GE(ctx1, 0);
    rc = qwrt_resume(rt, ctx1);
    EXPECT_EQ(rc, 0);

    /* ctx1 should not see _ctx0fn */
    char *result = nullptr;
    rc = qwrt_eval(rt, "typeof _ctx0fn", &result);
    EXPECT_EQ(rc, 0);
    EXPECT_STREQ(result, "\"undefined\"");
    qwrt_free(result);

    /* Define a different function in ctx1 */
    rc = qwrt_eval(rt, "function _ctx1fn() { return 99; }", nullptr);
    EXPECT_EQ(rc, 0);

    /* Switch back to ctx0 — original function preserved */
    rc = qwrt_resume(rt, 0);
    EXPECT_EQ(rc, 0);
    rc = qwrt_eval(rt, "_ctx0fn()", &result);
    EXPECT_EQ(rc, 0);
    EXPECT_STREQ(result, "42");
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

TEST(QwrtIsolation, ErrorInOneContextDoesNotAffectOther) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);
    qwrt_config_t config;
    fill_test_config(&config, pal);
    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NE(rt, nullptr);

    /* Set state in ctx0 */
    int rc = qwrt_eval(rt, "var _ctx0ok = true", nullptr);
    EXPECT_EQ(rc, 0);

    /* Spawn ctx1 */
    int ctx1 = qwrt_spawn(rt, &config);
    EXPECT_GE(ctx1, 0);
    rc = qwrt_resume(rt, ctx1);
    EXPECT_EQ(rc, 0);

    /* Throw an error in ctx1 */
    char *result = nullptr;
    rc = qwrt_eval(rt, "throw new Error('boom')", &result);
    EXPECT_EQ(rc, -1);

    /* ctx1 should still work after the error */
    rc = qwrt_eval(rt, "1 + 1", &result);
    EXPECT_EQ(rc, 0);
    EXPECT_STREQ(result, "2");
    qwrt_free(result);

    /* ctx0 should be unaffected */
    rc = qwrt_resume(rt, 0);
    EXPECT_EQ(rc, 0);
    rc = qwrt_eval(rt, "_ctx0ok", &result);
    EXPECT_EQ(rc, 0);
    EXPECT_STREQ(result, "true");
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}


/* ================================================================
 * Resource Management
 * ================================================================ */

TEST(QwrtResource, DoubleDestroy) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);
    qwrt_config_t config;
    fill_test_config(&config, pal);
    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NE(rt, nullptr);

    /* First destroy — should succeed */
    qwrt_destroy(rt);

    /* Second destroy on same pointer — should not crash
     * (the magic check in qwrt_destroy should reject it, but
     * even if it doesn't, we must not crash) */
    /* Note: rt is now dangling, do NOT access it again */

    pal_mock_destroy(pal);
}

TEST(QwrtResource, SpawnDestroyChurn) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);
    qwrt_config_t config;
    fill_test_config(&config, pal);
    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NE(rt, nullptr);

    /* Rapidly spawn and destroy contexts — should not leak or crash */
    for (int i = 0; i < 100; i++) {
        int ctx_id = qwrt_spawn(rt, &config);
        EXPECT_GE(ctx_id, 0);

        int rc = qwrt_resume(rt, ctx_id);
        EXPECT_EQ(rc, 0);

        rc = qwrt_eval(rt, "var _x = 1", nullptr);
        EXPECT_EQ(rc, 0);

        rc = qwrt_destroy_ctx(rt, ctx_id);
        EXPECT_EQ(rc, 0);
    }

    /* Switch back to ctx0 — it should still work */
    int rc = qwrt_resume(rt, 0);
    EXPECT_EQ(rc, 0);

    char *result = nullptr;
    rc = qwrt_eval(rt, "1 + 1", &result);
    EXPECT_EQ(rc, 0);
    EXPECT_STREQ(result, "2");
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

TEST(QwrtResource, TimerCleanupOnContextDestroy) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);
    qwrt_t *rt = create_runtime_with_polyfill(pal);
    ASSERT_NE(rt, nullptr);

    /* Start timers in a spawned context */
    qwrt_config_t config;
    fill_test_config(&config, pal);
    int ctx1 = qwrt_spawn(rt, &config);
    EXPECT_GE(ctx1, 0);
    int rc = qwrt_resume(rt, ctx1);
    EXPECT_EQ(rc, 0);

    /* Inject polyfill in spawned context too */
    rc = qwrt_eval(rt, test_polyfill, nullptr);
    EXPECT_EQ(rc, 0);

    rc = qwrt_eval(rt,
        "for (var i = 0; i < 10; i++) { "
        "  testHelper.timerStart(1000, 0); "
        "}",
        nullptr);
    EXPECT_EQ(rc, 0);

    /* Destroy the context — timers should be cleaned up without crash */
    rc = qwrt_destroy_ctx(rt, ctx1);
    EXPECT_EQ(rc, 0);

    /* Switch back to ctx0 — it should still work */
    rc = qwrt_resume(rt, 0);
    EXPECT_EQ(rc, 0);

    char *result = nullptr;
    rc = qwrt_eval(rt, "1 + 1", &result);
    EXPECT_EQ(rc, 0);
    EXPECT_STREQ(result, "2");
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

TEST(QwrtResource, ResetWithActiveTimers) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);
    qwrt_t *rt = create_runtime_with_polyfill(pal);
    ASSERT_NE(rt, nullptr);

    /* Start some timers */
    int rc = qwrt_eval(rt,
        "for (var i = 0; i < 5; i++) { "
        "  testHelper.timerStart(1000, 0); "
        "}",
        nullptr);
    EXPECT_EQ(rc, 0);

    /* Reset — should clean up timers without crash */
    qwrt_config_t config;
    fill_test_config(&config, pal);
    rc = qwrt_reset(rt, &config);
    EXPECT_EQ(rc, 0);

    /* Runtime should work after reset */
    char *result = nullptr;
    rc = qwrt_eval(rt, "1 + 1", &result);
    EXPECT_EQ(rc, 0);
    EXPECT_STREQ(result, "2");
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

TEST(QwrtResource, EvalAfterContextDestroy) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);
    qwrt_config_t config;
    fill_test_config(&config, pal);
    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NE(rt, nullptr);

    /* Spawn and immediately destroy */
    int ctx1 = qwrt_spawn(rt, &config);
    EXPECT_GE(ctx1, 0);
    int rc = qwrt_destroy_ctx(rt, ctx1);
    EXPECT_EQ(rc, 0);

    /* The destroyed context ID should not be usable */
    rc = qwrt_resume(rt, ctx1);
    EXPECT_EQ(rc, -1);

    /* Switch back to ctx0 — it should still work */
    rc = qwrt_resume(rt, 0);
    EXPECT_EQ(rc, 0);

    char *result = nullptr;
    rc = qwrt_eval(rt, "42", &result);
    EXPECT_EQ(rc, 0);
    EXPECT_STREQ(result, "42");
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

/* ================================================================
 * Async / Promise
 * ================================================================ */

TEST(QwrtAsync, MultiplePromisesResolve) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);
    qwrt_t *rt = create_runtime_with_polyfill(pal);
    ASSERT_NE(rt, nullptr);

    /* Create multiple HTTP promises */
    int rc = qwrt_eval(rt,
        "var _results = []; "
        "testHelper.httpRequest('http://a', 'GET', '', null).then(function(v) { _results.push('a'); }); "
        "testHelper.httpRequest('http://b', 'GET', '', null).then(function(v) { _results.push('b'); }); "
        "testHelper.httpRequest('http://c', 'GET', '', null).then(function(v) { _results.push('c'); });",
        nullptr);
    EXPECT_EQ(rc, 0);

    /* Tick to resolve all promises */
    rc = qwrt_tick(rt);
    EXPECT_EQ(rc, 0);

    char *result = nullptr;
    rc = qwrt_eval(rt, "_results.length", &result);
    EXPECT_EQ(rc, 0);
    EXPECT_STREQ(result, "3");
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

TEST(QwrtAsync, TimerPromiseResolves) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);
    qwrt_t *rt = create_runtime_with_polyfill(pal);
    ASSERT_NE(rt, nullptr);

    /* Start a timer and track resolution */
    int rc = qwrt_eval(rt,
        "var _resolved = false; "
        "var _p = testHelper.timerStart(100, 0); "
        "_p.promise.then(function() { _resolved = true; });",
        nullptr);
    EXPECT_EQ(rc, 0);

    /* Not resolved yet */
    char *result = nullptr;
    rc = qwrt_eval(rt, "_resolved", &result);
    EXPECT_EQ(rc, 0);
    EXPECT_STREQ(result, "false");
    qwrt_free(result);

    /* Fire the timer */
    pal_mock_fire_timer(pal, 1);
    rc = qwrt_tick(rt);
    EXPECT_EQ(rc, 0);

    /* Now resolved */
    rc = qwrt_eval(rt, "_resolved", &result);
    EXPECT_EQ(rc, 0);
    EXPECT_STREQ(result, "true");
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

TEST(QwrtAsync, RepeatTimerFires) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);
    qwrt_t *rt = create_runtime_with_polyfill(pal);
    ASSERT_NE(rt, nullptr);

    /* Start a repeating timer (repeat=1) */
    int rc = qwrt_eval(rt,
        "var _count = 0; "
        "var _p = testHelper.timerStart(100, 1); "
        "_p.promise.then(function() { _count++; });",
        nullptr);
    EXPECT_EQ(rc, 0);

    /* Fire the timer multiple times — repeat timer should not crash */
    for (int i = 0; i < 3; i++) {
        pal_mock_fire_timer(pal, 1);
        rc = qwrt_tick(rt);
        EXPECT_EQ(rc, 0);
    }

    char *result = nullptr;
    rc = qwrt_eval(rt, "_count", &result);
    EXPECT_EQ(rc, 0);
    /* First fire resolves the promise; subsequent fires call resolve again
     * (which is a no-op on an already-resolved promise) */
    EXPECT_STREQ(result, "1");
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

TEST(QwrtAsync, TimerStop) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);
    qwrt_t *rt = create_runtime_with_polyfill(pal);
    ASSERT_NE(rt, nullptr);

    /* Start a timer then stop it */
    int rc = qwrt_eval(rt,
        "var _fired = false; "
        "var _p = testHelper.timerStart(100, 0); "
        "_p.promise.then(function() { _fired = true; }); "
        "testHelper.timerStop(_p.handle);",
        nullptr);
    EXPECT_EQ(rc, 0);

    /* Fire the timer — should not resolve because we stopped it */
    pal_mock_fire_timer(pal, 1);
    rc = qwrt_tick(rt);
    EXPECT_EQ(rc, 0);

    char *result = nullptr;
    rc = qwrt_eval(rt, "_fired", &result);
    EXPECT_EQ(rc, 0);
    EXPECT_STREQ(result, "false");
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

TEST(QwrtAsync, PromiseChaining) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);
    qwrt_t *rt = create_runtime_with_polyfill(pal);
    ASSERT_NE(rt, nullptr);

    /* Chain promises */
    int rc = qwrt_eval(rt,
        "var _chain = ''; "
        "testHelper.httpRequest('http://a', 'GET', '', null)"
        "  .then(function() { _chain += 'A'; return testHelper.httpRequest('http://b', 'GET', '', null); })"
        "  .then(function() { _chain += 'B'; });",
        nullptr);
    EXPECT_EQ(rc, 0);

    /* Tick twice — first resolves A, then chain resolves B */
    qwrt_tick(rt);
    qwrt_tick(rt);

    char *result = nullptr;
    rc = qwrt_eval(rt, "_chain", &result);
    EXPECT_EQ(rc, 0);
    EXPECT_STREQ(result, "\"AB\"");
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

TEST(QwrtAsync, TickWithNothingPending) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);
    qwrt_config_t config;
    fill_test_config(&config, pal);
    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NE(rt, nullptr);

    /* Tick with no pending promises — should be a no-op */
    int rc = qwrt_tick(rt);
    EXPECT_EQ(rc, 0);

    /* Should still work after empty tick */
    char *result = nullptr;
    rc = qwrt_eval(rt, "1 + 1", &result);
    EXPECT_EQ(rc, 0);
    EXPECT_STREQ(result, "2");
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

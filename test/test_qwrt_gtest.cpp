/*
 * qwrt Unit Tests (Google Test)
 *
 * Tests for core runtime lifecycle, eval, call, tick, PAL bridge,
 * timers, memory management, bytecode compilation, and error paths.
 */

#include <gtest/gtest.h>

extern "C" {
#include "qwrt/qwrt.h"
#include "pal_mock.h"
#include <string.h>
#include <stdlib.h>
}

/* ================================================================
 * testHelper: exposes raw PAL functions via __pal__ for low-level tests
 * ================================================================ */

static const char testHelper_def[] =
    "globalThis.testHelper = {\n"
    "  timeNow: function() { return __pal__.timeNow(); },\n"
    "  log: function(level, msg) { __pal__.log(level, msg); },\n"
    "  storageSet: function(k, v) { return __pal__.storageSet(k, v); },\n"
    "  storageGet: function(k) { return __pal__.storageGet(k); },\n"
    "  fsWrite: function(p, d) { return __pal__.fsWrite(p, d); },\n"
    "  fsRead: function(p) { return __pal__.fsRead(p); },\n"
    "  httpRequest: function(url, method, headers, body) {\n"
    "    return __pal__.httpRequest(url, method, headers, body);\n"
    "  },\n"
    "  timerStart: function(delay, repeat) { return __pal__.timerStart(delay, repeat); },\n"
    "  timerStop: function(handle) { return __pal__.timerStop(handle); },\n"
    "};\n";

/* ================================================================
 * Fixture: QwrtTestBase — creates runtime with mock PAL + polyfill
 * ================================================================ */

class QwrtTestBase : public ::testing::Test {
protected:
    qwrt_t *rt;
    qwrt_pal_t *pal;

    void SetUp() override {
        pal = pal_mock_create();
        ASSERT_NE(pal, nullptr);

        qwrt_config_t config;
        memset(&config, 0, sizeof(config));
        config.pal = pal;

        rt = qwrt_create(&config);
        ASSERT_NE(rt, nullptr);

        /* Expose raw PAL functions as testHelper */
        qwrt_eval(rt, testHelper_def, NULL);
    }

    void TearDown() override {
        qwrt_destroy(rt);
        pal_mock_destroy(pal);
    }
};

/* ================================================================
 * Group 1: Runtime lifecycle
 * ================================================================ */

TEST(QwrtLifecycle, CreateDestroy) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NE(pal, nullptr);

    qwrt_config_t config;
    memset(&config, 0, sizeof(config));
    config.pal = pal;

    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NE(rt, nullptr);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

TEST(QwrtLifecycle, CreateNullPal) {
    qwrt_config_t config;
    memset(&config, 0, sizeof(config));
    /* pal is NULL */
    qwrt_t *rt = qwrt_create(&config);
    EXPECT_EQ(rt, nullptr);

    /* Also test NULL config */
    rt = qwrt_create(NULL);
    EXPECT_EQ(rt, nullptr);
}

TEST_F(QwrtTestBase, CreateNoPolyfill) {
    /* __pal__ should be accessible */
    char *result = NULL;
    int rc = qwrt_eval(rt, "typeof __pal__", &result);
    EXPECT_EQ(rc, 0);
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result, "\"object\"");
    qwrt_free(result);
}

/* ================================================================
 * Group 2: Eval
 * ================================================================ */

TEST_F(QwrtTestBase, EvalSimple) {
    char *result = NULL;
    int rc = qwrt_eval(rt, "1 + 1", &result);
    EXPECT_EQ(rc, 0);
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result, "2");
    qwrt_free(result);
}

TEST_F(QwrtTestBase, EvalString) {
    char *result = NULL;
    int rc = qwrt_eval(rt, "'hello'", &result);
    EXPECT_EQ(rc, 0);
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result, "\"hello\"");
    qwrt_free(result);
}

TEST_F(QwrtTestBase, EvalObject) {
    char *result = NULL;
    int rc = qwrt_eval(rt, "({a: 1})", &result);
    EXPECT_EQ(rc, 0);
    ASSERT_NE(result, nullptr);
    /* JSON-like: should contain "a" and "1" */
    EXPECT_NE(strstr(result, "\"a\""), nullptr);
    EXPECT_NE(strstr(result, "1"), nullptr);
    qwrt_free(result);
}

TEST_F(QwrtTestBase, EvalError) {
    char *result = NULL;
    int rc = qwrt_eval(rt, "throw new Error('test')", &result);
    EXPECT_EQ(rc, -1);
    qwrt_free(result);
}

/* ================================================================
 * Group 3: Call
 * ================================================================ */

TEST_F(QwrtTestBase, CallFunction) {
    /* Define a function */
    int rc = qwrt_eval(rt, "function add(a, b) { return a + b; }", NULL);
    EXPECT_EQ(rc, 0);

    /* Call it */
    char *result = NULL;
    rc = qwrt_call(rt, "add", "[3, 4]", &result);
    EXPECT_EQ(rc, 0);
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result, "7");
    qwrt_free(result);
}

TEST_F(QwrtTestBase, CallWithArgs) {
    /* Define a function that uses args */
    int rc = qwrt_eval(rt, "function greet(name) { return 'Hello, ' + name; }", NULL);
    EXPECT_EQ(rc, 0);

    /* Call with JSON args */
    char *result = NULL;
    rc = qwrt_call(rt, "greet", "[\"World\"]", &result);
    EXPECT_EQ(rc, 0);
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result, "\"Hello, World\"");
    qwrt_free(result);
}

TEST_F(QwrtTestBase, CallNonexistent) {
    char *result = NULL;
    int rc = qwrt_call(rt, "doesNotExist", NULL, &result);
    EXPECT_EQ(rc, -1);
    qwrt_free(result);
}

/* ================================================================
 * Group 4: Call error paths
 * ================================================================ */

TEST_F(QwrtTestBase, CallMalformedArgs) {
    /* Define a function */
    qwrt_eval(rt, "function testCall(a, b) { return a + b; }", NULL);

    /* Call with malformed JSON args */
    char *result = NULL;
    int rc = qwrt_call(rt, "testCall", "{invalid", &result);
    EXPECT_LT(rc, 0);
    EXPECT_EQ(result, nullptr);
}

TEST_F(QwrtTestBase, CallNullFunc) {
    char *result = NULL;
    int rc = qwrt_call(rt, NULL, "[]", &result);
    EXPECT_LT(rc, 0);
    EXPECT_EQ(result, nullptr);
}

TEST_F(QwrtTestBase, CallNonexistentFunc) {
    char *result = NULL;
    int rc = qwrt_call(rt, "noSuchFunction", "[]", &result);
    EXPECT_LT(rc, 0);
}

/* ================================================================
 * Group 5: Tick
 * ================================================================ */

TEST_F(QwrtTestBase, TickNoPending) {
    /* No pending jobs — tick should return 0 */
    int rc = qwrt_tick(rt, 100);
    EXPECT_EQ(rc, 0);
}

TEST_F(QwrtTestBase, TickWithPromise) {
    /* Use a PAL async function that returns a Promise.
     * Mock PAL fires synchronously, so the promise resolve callback
     * will be queued as a microtask. */
    int rc = qwrt_eval(rt,
        "var _storageResult = null; "
        "testHelper.storageSet('tickKey', 'tickVal').then(function(v) { "
        "  _storageResult = v; "
        "})",
        NULL);
    EXPECT_EQ(rc, 0);

    /* Tick to process the resolved promise */
    rc = qwrt_tick(rt, 100);
    EXPECT_EQ(rc, 0);

    /* Check that the .then callback ran */
    char *result = NULL;
    rc = qwrt_eval(rt, "_storageResult", &result);
    EXPECT_EQ(rc, 0);
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result, "\"ok\"");
    qwrt_free(result);
}

/* ================================================================
 * Group 6: PAL bridge — sync
 * ================================================================ */

TEST_F(QwrtTestBase, PalTimeNow) {
    /* Set mock time */
    pal_mock_set_time(pal, 12345);

    /* Access via testHelper.timeNow (raw PAL, uses mock time) */
    char *result = NULL;
    int rc = qwrt_eval(rt, "testHelper.timeNow()", &result);
    EXPECT_EQ(rc, 0);
    ASSERT_NE(result, nullptr);
    EXPECT_NE(strstr(result, "12345"), nullptr);
    qwrt_free(result);
}

TEST_F(QwrtTestBase, PalLog) {
    /* Log via console.log polyfill */
    int rc = qwrt_eval(rt, "console.log('hello world')", NULL);
    EXPECT_EQ(rc, 0);

    /* Check mock PAL log buffer */
    int log_count = 0;
    const char *log = pal_mock_get_log(pal, &log_count);
    EXPECT_EQ(log_count, 1);
    EXPECT_NE(strstr(log, "hello world"), nullptr);
    pal_mock_clear_log(pal);
}

/* ================================================================
 * Group 7: PAL bridge — async (via mock PAL)
 * ================================================================ */

TEST_F(QwrtTestBase, MockStorage) {
    /* Set a value via storage */
    int rc = qwrt_eval(rt,
        "var _setResult = null; "
        "testHelper.storageSet('key1', 'value1').then(function(v) { "
        "  _setResult = v; "
        "})",
        NULL);
    EXPECT_EQ(rc, 0);

    /* Tick to process the resolved promise */
    rc = qwrt_tick(rt, 100);
    EXPECT_EQ(rc, 0);

    /* Verify set result */
    char *result = NULL;
    rc = qwrt_eval(rt, "_setResult", &result);
    EXPECT_EQ(rc, 0);
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result, "\"ok\"");
    qwrt_free(result);

    /* Get the value back */
    rc = qwrt_eval(rt,
        "var _getResult = null; "
        "testHelper.storageGet('key1').then(function(v) { "
        "  _getResult = v; "
        "})",
        NULL);
    EXPECT_EQ(rc, 0);

    rc = qwrt_tick(rt, 100);
    EXPECT_EQ(rc, 0);

    rc = qwrt_eval(rt, "_getResult", &result);
    EXPECT_EQ(rc, 0);
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result, "\"value1\"");
    qwrt_free(result);
}

TEST_F(QwrtTestBase, MockFs) {
    /* Write a file */
    int rc = qwrt_eval(rt,
        "var _writeResult = null; "
        "testHelper.fsWrite('/tmp/test.txt', 'file content').then(function(v) { "
        "  _writeResult = v; "
        "})",
        NULL);
    EXPECT_EQ(rc, 0);

    rc = qwrt_tick(rt, 100);
    EXPECT_EQ(rc, 0);

    char *result = NULL;
    rc = qwrt_eval(rt, "_writeResult", &result);
    EXPECT_EQ(rc, 0);
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result, "\"ok\"");
    qwrt_free(result);

    /* Read the file back */
    rc = qwrt_eval(rt,
        "var _readResult = null; "
        "testHelper.fsRead('/tmp/test.txt').then(function(v) { "
        "  _readResult = v; "
        "})",
        NULL);
    EXPECT_EQ(rc, 0);

    rc = qwrt_tick(rt, 100);
    EXPECT_EQ(rc, 0);

    rc = qwrt_eval(rt, "_readResult", &result);
    EXPECT_EQ(rc, 0);
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result, "\"file content\"");
    qwrt_free(result);
}

TEST_F(QwrtTestBase, MockHttp) {
    /* Set a custom mock response */
    pal_mock_set_http_response(pal,
        "{\"status\":200,\"headers\":{},\"body\":\"test body\"}");

    /* Call httpRequest */
    int rc = qwrt_eval(rt,
        "var _httpResult = null; "
        "testHelper.httpRequest('http://example.com', 'GET', '', null).then(function(v) { "
        "  _httpResult = v; "
        "})",
        NULL);
    EXPECT_EQ(rc, 0);

    rc = qwrt_tick(rt, 100);
    EXPECT_EQ(rc, 0);

    char *result = NULL;
    rc = qwrt_eval(rt, "_httpResult", &result);
    EXPECT_EQ(rc, 0);
    ASSERT_NE(result, nullptr);
    /* Should contain the mock response body */
    EXPECT_NE(strstr(result, "test body"), nullptr);
    qwrt_free(result);
}

/* ================================================================
 * Group 8: Timer
 * ================================================================ */

TEST_F(QwrtTestBase, MockTimer) {
    /* Start a timer via testHelper (which calls pal.timerStart) */
    int rc = qwrt_eval(rt,
        "var _timerFired = false; "
        "var _timerInfo = testHelper.timerStart(1000, 0); "
        "_timerInfo.promise.then(function() { _timerFired = true; })",
        NULL);
    EXPECT_EQ(rc, 0);

    /* Process the initial promise setup */
    rc = qwrt_tick(rt, 100);
    EXPECT_EQ(rc, 0);

    /* Timer should not have fired yet */
    char *result = NULL;
    rc = qwrt_eval(rt, "_timerFired", &result);
    EXPECT_EQ(rc, 0);
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result, "false");
    qwrt_free(result);

    /* Fire the mock timer (handle_id 1 — first timer gets id 1) */
    pal_mock_fire_timer(pal, 1);

    /* Tick to process the resolved promise */
    rc = qwrt_tick(rt, 100);
    EXPECT_EQ(rc, 0);

    /* Now the callback should have run */
    rc = qwrt_eval(rt, "_timerFired", &result);
    EXPECT_EQ(rc, 0);
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result, "true");
    qwrt_free(result);
}

/* ================================================================
 * Group 9: Memory
 * ================================================================ */

TEST(QwrtMemory, FreeNull) {
    /* qwrt_free on NULL should not crash */
    qwrt_free(NULL);
}

TEST(QwrtMemory, FreeValid) {
    /* qwrt_free on a valid string should not crash */
    char *ptr = strdup("test");
    ASSERT_NE(ptr, nullptr);
    qwrt_free(ptr);
}

/* ================================================================
 * Group 10: qwrt_reset
 * ================================================================ */

TEST_F(QwrtTestBase, ResetBasic) {
    int rc = qwrt_eval(rt, "var resetTestVar = 42", NULL);
    EXPECT_EQ(rc, 0);

    qwrt_config_t config;
    memset(&config, 0, sizeof(config));
    config.pal = pal;

    rc = qwrt_reset(rt, &config);
    EXPECT_EQ(rc, 0);

    char *result = NULL;
    rc = qwrt_eval(rt, "typeof resetTestVar", &result);
    EXPECT_EQ(rc, 0);
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result, "\"undefined\"");
    qwrt_free(result);

    rc = qwrt_eval(rt, "1 + 1", &result);
    EXPECT_EQ(rc, 0);
    EXPECT_STREQ(result, "2");
    qwrt_free(result);
}

TEST_F(QwrtTestBase, ResetClearsTimers) {
    int rc = qwrt_eval(rt, "var _resetTimerInfo = testHelper.timerStart(1000, 0)", NULL);
    EXPECT_EQ(rc, 0);
    rc = qwrt_tick(rt, 100);
    EXPECT_EQ(rc, 0);

    qwrt_config_t config;
    memset(&config, 0, sizeof(config));
    config.pal = pal;

    rc = qwrt_reset(rt, &config);
    EXPECT_EQ(rc, 0);

    char *result = NULL;
    rc = qwrt_eval(rt, "1 + 2", &result);
    EXPECT_EQ(rc, 0);
    EXPECT_STREQ(result, "3");
    qwrt_free(result);
}

TEST(QwrtReset, ResetDifferentPal) {
    qwrt_pal_t *pal_full = pal_mock_create();
    qwrt_pal_t *pal_no_http = pal_mock_create_no_http();
    ASSERT_NE(pal_full, nullptr);
    ASSERT_NE(pal_no_http, nullptr);

    qwrt_config_t config;
    memset(&config, 0, sizeof(config));
    config.pal = pal_full;

    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NE(rt, nullptr);

    /* Expose testHelper for PAL access */
    qwrt_eval(rt, testHelper_def, NULL);

    /* HTTP should work with full PAL */
    int rc = qwrt_eval(rt,
        "var _httpOk = null; "
        "testHelper.httpRequest('http://test', 'GET', '', null).then(function(v) { "
        "  _httpOk = v; "
        "})", NULL);
    EXPECT_EQ(rc, 0);
    rc = qwrt_tick(rt, 100);
    EXPECT_EQ(rc, 0);

    /* Reset with no-http PAL */
    config.pal = pal_no_http;
    rc = qwrt_reset(rt, &config);
    EXPECT_EQ(rc, 0);

    /* Re-expose testHelper (reset clears JS state) */
    qwrt_eval(rt, testHelper_def, NULL);

    /* HTTP should now be denied */
    char *result = NULL;
    rc = qwrt_eval(rt,
        "var _httpDenied = null; "
        "testHelper.httpRequest('http://test', 'GET', '', null).then("
        "  function(v) { _httpDenied = 'ok'; },"
        "  function(e) { _httpDenied = 'err:' + e; }"
        ")", NULL);
    EXPECT_EQ(rc, 0);
    rc = qwrt_tick(rt, 100);
    EXPECT_EQ(rc, 0);

    rc = qwrt_eval(rt, "_httpDenied", &result);
    EXPECT_EQ(rc, 0);
    ASSERT_NE(result, nullptr);
    EXPECT_NE(strstr(result, "Permission denied"), nullptr);
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_mock_destroy(pal_full);
    pal_mock_destroy(pal_no_http);
}

TEST_F(QwrtTestBase, ResetNullConfig) {
    int rc = qwrt_reset(rt, NULL);
    EXPECT_LT(rc, 0);
}

/* ================================================================
 * Group 11: Permission-denied PAL
 * ================================================================ */

TEST(QwrtPermission, PalDeniedFsWrite) {
    qwrt_pal_t *pal = pal_mock_create_readonly_fs();
    ASSERT_NE(pal, nullptr);

    qwrt_config_t config;
    memset(&config, 0, sizeof(config));
    config.pal = pal;

    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NE(rt, nullptr);
    qwrt_eval(rt, testHelper_def, NULL);

    char *result = NULL;
    int rc = qwrt_eval(rt,
        "var _writeErr = null; "
        "testHelper.fsWrite('/tmp/test.txt', 'data').then("
        "  function(v) { _writeErr = 'ok'; },"
        "  function(e) { _writeErr = 'err:' + e; }"
        ")", NULL);
    EXPECT_EQ(rc, 0);
    rc = qwrt_tick(rt, 100);
    EXPECT_EQ(rc, 0);

    rc = qwrt_eval(rt, "_writeErr", &result);
    EXPECT_EQ(rc, 0);
    ASSERT_NE(result, nullptr);
    EXPECT_NE(strstr(result, "Permission denied"), nullptr);
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

TEST(QwrtPermission, PalDeniedHttp) {
    qwrt_pal_t *pal = pal_mock_create_no_http();
    ASSERT_NE(pal, nullptr);

    qwrt_config_t config;
    memset(&config, 0, sizeof(config));
    config.pal = pal;

    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NE(rt, nullptr);
    qwrt_eval(rt, testHelper_def, NULL);

    char *result = NULL;
    int rc = qwrt_eval(rt,
        "var _httpErr = null; "
        "testHelper.httpRequest('http://test', 'GET', '', null).then("
        "  function(v) { _httpErr = 'ok'; },"
        "  function(e) { _httpErr = 'err:' + e; }"
        ")", NULL);
    EXPECT_EQ(rc, 0);
    rc = qwrt_tick(rt, 100);
    EXPECT_EQ(rc, 0);

    rc = qwrt_eval(rt, "_httpErr", &result);
    EXPECT_EQ(rc, 0);
    ASSERT_NE(result, nullptr);
    EXPECT_NE(strstr(result, "Permission denied"), nullptr);
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

/* ================================================================
 * Group 12: Bytecode compilation
 * ================================================================ */

TEST_F(QwrtTestBase, CompileScript) {
    const char *code = "function add(a, b) { return a + b; }";
    size_t bc_len = 0;
    uint8_t *bc = qwrt_compile(rt, code, strlen(code), &bc_len);
    ASSERT_NE(bc, nullptr);
    EXPECT_GT(bc_len, 0u);
    qwrt_free(bc);
}

TEST_F(QwrtTestBase, CompileModule) {
    const char *code = "export function mul(a, b) { return a * b; }";
    size_t bc_len = 0;
    uint8_t *bc = qwrt_compile_module(rt, code, strlen(code), &bc_len);
    ASSERT_NE(bc, nullptr);
    EXPECT_GT(bc_len, 0u);
    qwrt_free(bc);
}

TEST_F(QwrtTestBase, CompileError) {
    const char *code = "function (invalid syntax";
    size_t bc_len = 0;
    uint8_t *bc = qwrt_compile(rt, code, strlen(code), &bc_len);
    EXPECT_EQ(bc, nullptr);
    EXPECT_EQ(bc_len, 0u);
}

TEST_F(QwrtTestBase, CompileRoundtrip) {
    /* Compile JS to bytecode */
    const char *code = "var compileTestVar = 42;";
    size_t bc_len = 0;
    uint8_t *bc = qwrt_compile(rt, code, strlen(code), &bc_len);
    ASSERT_NE(bc, nullptr);
    ASSERT_GT(bc_len, 0u);

    /* Reset context to clear JS state */
    qwrt_config_t config;
    memset(&config, 0, sizeof(config));
    config.pal = pal;
    int rc = qwrt_reset(rt, &config);
    EXPECT_EQ(rc, 0);

    /* Verify variable doesn't exist yet */
    char *result = NULL;
    rc = qwrt_eval(rt, "typeof compileTestVar", &result);
    EXPECT_EQ(rc, 0);
    EXPECT_STREQ(result, "\"undefined\"");
    qwrt_free(result);

    /* Load and execute the bytecode */
    rc = qwrt_eval_bytecode(rt, bc, bc_len, NULL);
    EXPECT_EQ(rc, 0);

    /* Now the variable should exist */
    rc = qwrt_eval(rt, "compileTestVar", &result);
    EXPECT_EQ(rc, 0);
    EXPECT_STREQ(result, "42");
    qwrt_free(result);

    qwrt_free(bc);
}

TEST_F(QwrtTestBase, CompileNullOutLen) {
    const char *code = "1 + 1";
    uint8_t *bc = qwrt_compile(rt, code, strlen(code), NULL);
    /* NULL out_len should return NULL */
    EXPECT_EQ(bc, nullptr);
}

TEST_F(QwrtTestBase, CompileEmptyCode) {
    size_t bc_len = 0;
    uint8_t *bc = qwrt_compile(rt, "", 0, &bc_len);
    /* Empty code may or may not produce valid bytecode depending on engine */
    if (bc) {
        EXPECT_GT(bc_len, 0u);
        qwrt_free(bc);
    }
}

TEST_F(QwrtTestBase, CompileModuleNullOutLen) {
    const char *code = "export const x = 1;";
    uint8_t *bc = qwrt_compile_module(rt, code, strlen(code), NULL);
    /* NULL out_len should return NULL */
    EXPECT_EQ(bc, nullptr);
}

/* ================================================================
 * Group 13: Bytecode edge cases and error paths
 * ================================================================ */

TEST_F(QwrtTestBase, BytecodeInvalidMagic) {
    /* Create bytecode with wrong magic bytes */
    uint8_t bad_bc[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    int rc = qwrt_eval_bytecode(rt, bad_bc, sizeof(bad_bc), NULL);
    EXPECT_LT(rc, 0);
}

TEST_F(QwrtTestBase, BytecodeTruncated) {
    /* Valid QuickJS bytecode starts with magic, truncated at 4 bytes */
    uint8_t truncated[] = { 0x02, 0x00, 0x00, 0x00 };
    int rc = qwrt_eval_bytecode(rt, truncated, sizeof(truncated), NULL);
    EXPECT_LT(rc, 0);
}

TEST_F(QwrtTestBase, BytecodeNullData) {
    int rc = qwrt_eval_bytecode(rt, NULL, 0, NULL);
    EXPECT_LT(rc, 0);
}

TEST_F(QwrtTestBase, BytecodeResultOnError) {
    /* Compile valid code that throws at runtime */
    const char *code = "throw new Error('test error');";
    size_t bc_len = 0;
    uint8_t *bc = qwrt_compile(rt, code, strlen(code), &bc_len);
    ASSERT_NE(bc, nullptr);

    /* Result should be NULL on error */
    char *result = NULL;
    int rc = qwrt_eval_bytecode(rt, bc, bc_len, &result);
    EXPECT_LT(rc, 0);
    EXPECT_EQ(result, nullptr);

    qwrt_free(bc);
}

/* ================================================================
 * Group 14: Internal API
 * ================================================================ */

TEST_F(QwrtTestBase, GetJsctxValid) {
    void *jsctx = qwrt_get_jsctx(rt);
    EXPECT_NE(jsctx, nullptr);
}

TEST_F(QwrtTestBase, GetActiveCtxId) {
    int id = qwrt_get_active_ctx_id(rt);
    EXPECT_EQ(id, 0);
}

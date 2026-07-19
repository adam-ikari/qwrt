/*
 * qwrt Unit Tests
 *
 * Tests for core runtime lifecycle, eval, call, tick, PAL bridge,
 * timers, and memory management.
 */

#define _POSIX_C_SOURCE 200809L

#include "test_qwrt.h"
#include "qwrt/qwrt.h"
#include "pal_mock.h"

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
 * Helper: create a runtime with mock PAL + default polyfill
 * ================================================================ */

static qwrt_t *create_test_runtime(qwrt_pal_t **pal_out)
{
    qwrt_pal_t *pal = pal_mock_create();
    if (!pal) return NULL;

    qwrt_config_t config;
    memset(&config, 0, sizeof(config));
    config.pal = pal;

    qwrt_t *rt = qwrt_create(&config);
    if (!rt) {
        pal_mock_destroy(pal);
        return NULL;
    }

    /* Expose raw PAL functions as testHelper */
    qwrt_eval(rt, testHelper_def, NULL);

    if (pal_out) *pal_out = pal;
    return rt;
}

static void destroy_test_runtime(qwrt_t *rt, qwrt_pal_t *pal)
{
    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

/* ================================================================
 * Group 1: Runtime lifecycle
 * ================================================================ */

TEST(create_destroy) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NOT_NULL(pal);

    qwrt_config_t config;
    memset(&config, 0, sizeof(config));
    config.pal = pal;

    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NOT_NULL(rt);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

TEST(create_null_pal) {
    qwrt_config_t config;
    memset(&config, 0, sizeof(config));
    /* pal is NULL */
    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NULL(rt);

    /* Also test NULL config */
    rt = qwrt_create(NULL);
    ASSERT_NULL(rt);
}

TEST(create_no_polyfill) {
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NOT_NULL(pal);

    qwrt_config_t config;
    memset(&config, 0, sizeof(config));
    config.pal = pal;
    /* No polyfill — should still create runtime with __pal__ global */

    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NOT_NULL(rt);

    /* Verify __pal__ is accessible */
    char *result = NULL;
    int rc = qwrt_eval(rt, "typeof __pal__", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(result);
    /* Should be "object" */
    ASSERT_STR_EQ(result, "\"object\"");

    qwrt_free(result);
    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

/* ================================================================
 * Group 2: Eval
 * ================================================================ */

TEST(eval_simple) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_test_runtime(&pal);
    ASSERT_NOT_NULL(rt);

    char *result = NULL;
    int rc = qwrt_eval(rt, "1 + 1", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(result, "2");

    qwrt_free(result);
    destroy_test_runtime(rt, pal);
}

TEST(eval_string) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_test_runtime(&pal);
    ASSERT_NOT_NULL(rt);

    char *result = NULL;
    int rc = qwrt_eval(rt, "'hello'", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(result, "\"hello\"");

    qwrt_free(result);
    destroy_test_runtime(rt, pal);
}

TEST(eval_object) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_test_runtime(&pal);
    ASSERT_NOT_NULL(rt);

    char *result = NULL;
    int rc = qwrt_eval(rt, "({a: 1})", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(result);
    /* JSON-like: should contain "a" and "1" */
    ASSERT(strstr(result, "\"a\"") != NULL);
    ASSERT(strstr(result, "1") != NULL);

    qwrt_free(result);
    destroy_test_runtime(rt, pal);
}

TEST(eval_error) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_test_runtime(&pal);
    ASSERT_NOT_NULL(rt);

    char *result = NULL;
    int rc = qwrt_eval(rt, "throw new Error('test')", &result);
    ASSERT_EQ(rc, -1);

    qwrt_free(result);
    destroy_test_runtime(rt, pal);
}

/* ================================================================
 * Group 3: Call
 * ================================================================ */

TEST(call_function) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_test_runtime(&pal);
    ASSERT_NOT_NULL(rt);

    /* Define a function */
    int rc = qwrt_eval(rt, "function add(a, b) { return a + b; }", NULL);
    ASSERT_EQ(rc, 0);

    /* Call it */
    char *result = NULL;
    rc = qwrt_call(rt, "add", "[3, 4]", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(result, "7");

    qwrt_free(result);
    destroy_test_runtime(rt, pal);
}

TEST(call_with_args) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_test_runtime(&pal);
    ASSERT_NOT_NULL(rt);

    /* Define a function that uses args */
    int rc = qwrt_eval(rt, "function greet(name) { return 'Hello, ' + name; }", NULL);
    ASSERT_EQ(rc, 0);

    /* Call with JSON args */
    char *result = NULL;
    rc = qwrt_call(rt, "greet", "[\"World\"]", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(result, "\"Hello, World\"");

    qwrt_free(result);
    destroy_test_runtime(rt, pal);
}

TEST(call_nonexistent) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_test_runtime(&pal);
    ASSERT_NOT_NULL(rt);

    char *result = NULL;
    int rc = qwrt_call(rt, "doesNotExist", NULL, &result);
    ASSERT_EQ(rc, -1);

    qwrt_free(result);
    destroy_test_runtime(rt, pal);
}

/* ================================================================
 * Group 4: Tick
 * ================================================================ */

TEST(tick_no_pending) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_test_runtime(&pal);
    ASSERT_NOT_NULL(rt);

    /* No pending jobs — tick should return 0 */
    int rc = qwrt_tick(rt, 100);
    ASSERT_EQ(rc, 0);

    destroy_test_runtime(rt, pal);
}

TEST(tick_with_promise) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_test_runtime(&pal);
    ASSERT_NOT_NULL(rt);

    /* Use a PAL async function that returns a Promise.
     * Mock PAL fires synchronously, so the promise resolve callback
     * will be queued as a microtask. */
    int rc = qwrt_eval(rt,
        "var _storageResult = null; "
        "testHelper.storageSet('tickKey', 'tickVal').then(function(v) { "
        "  _storageResult = v; "
        "})",
        NULL);
    ASSERT_EQ(rc, 0);

    /* Tick to process the resolved promise */
    rc = qwrt_tick(rt, 100);
    ASSERT_EQ(rc, 0);

    /* Check that the .then callback ran */
    char *result = NULL;
    rc = qwrt_eval(rt, "_storageResult", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(result, "\"ok\"");

    qwrt_free(result);
    destroy_test_runtime(rt, pal);
}

/* ================================================================
 * Group 5: PAL bridge — sync
 * ================================================================ */

TEST(pal_time_now) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_test_runtime(&pal);
    ASSERT_NOT_NULL(rt);

    /* Set mock time */
    pal_mock_set_time(pal, 12345);

    /* Access via testHelper.timeNow (raw PAL, uses mock time) */
    char *result = NULL;
    int rc = qwrt_eval(rt, "testHelper.timeNow()", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(result);
    /* Should be 12345 */
    ASSERT(strstr(result, "12345") != NULL);

    qwrt_free(result);
    destroy_test_runtime(rt, pal);
}

TEST(pal_log) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_test_runtime(&pal);
    ASSERT_NOT_NULL(rt);

    /* Log via console.log polyfill */
    int rc = qwrt_eval(rt, "console.log('hello world')", NULL);
    ASSERT_EQ(rc, 0);

    /* Check mock PAL log buffer */
    int log_count = 0;
    const char *log = pal_mock_get_log(pal, &log_count);
    ASSERT_EQ(log_count, 1);
    ASSERT(strstr(log, "hello world") != NULL);

    pal_mock_clear_log(pal);
    destroy_test_runtime(rt, pal);
}

/* ================================================================
 * Group 6: PAL bridge — async (via mock PAL)
 * ================================================================ */

TEST(mock_storage) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_test_runtime(&pal);
    ASSERT_NOT_NULL(rt);

    /* Set a value via storage */
    int rc = qwrt_eval(rt,
        "var _setResult = null; "
        "testHelper.storageSet('key1', 'value1').then(function(v) { "
        "  _setResult = v; "
        "})",
        NULL);
    ASSERT_EQ(rc, 0);

    /* Tick to process the resolved promise */
    rc = qwrt_tick(rt, 100);
    ASSERT_EQ(rc, 0);

    /* Verify set result */
    char *result = NULL;
    rc = qwrt_eval(rt, "_setResult", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(result, "\"ok\"");
    qwrt_free(result);

    /* Get the value back */
    rc = qwrt_eval(rt,
        "var _getResult = null; "
        "testHelper.storageGet('key1').then(function(v) { "
        "  _getResult = v; "
        "})",
        NULL);
    ASSERT_EQ(rc, 0);

    rc = qwrt_tick(rt, 100);
    ASSERT_EQ(rc, 0);

    rc = qwrt_eval(rt, "_getResult", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(result, "\"value1\"");

    qwrt_free(result);
    destroy_test_runtime(rt, pal);
}

TEST(mock_fs) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_test_runtime(&pal);
    ASSERT_NOT_NULL(rt);

    /* Write a file */
    int rc = qwrt_eval(rt,
        "var _writeResult = null; "
        "testHelper.fsWrite('/tmp/test.txt', 'file content').then(function(v) { "
        "  _writeResult = v; "
        "})",
        NULL);
    ASSERT_EQ(rc, 0);

    rc = qwrt_tick(rt, 100);
    ASSERT_EQ(rc, 0);

    char *result = NULL;
    rc = qwrt_eval(rt, "_writeResult", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(result, "\"ok\"");
    qwrt_free(result);

    /* Read the file back */
    rc = qwrt_eval(rt,
        "var _readResult = null; "
        "testHelper.fsRead('/tmp/test.txt').then(function(v) { "
        "  _readResult = v; "
        "})",
        NULL);
    ASSERT_EQ(rc, 0);

    rc = qwrt_tick(rt, 100);
    ASSERT_EQ(rc, 0);

    rc = qwrt_eval(rt, "_readResult", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(result, "\"file content\"");

    qwrt_free(result);
    destroy_test_runtime(rt, pal);
}

TEST(mock_http) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_test_runtime(&pal);
    ASSERT_NOT_NULL(rt);

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
    ASSERT_EQ(rc, 0);

    rc = qwrt_tick(rt, 100);
    ASSERT_EQ(rc, 0);

    char *result = NULL;
    rc = qwrt_eval(rt, "_httpResult", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(result);
    /* Should contain the mock response body */
    ASSERT(strstr(result, "test body") != NULL);

    qwrt_free(result);
    destroy_test_runtime(rt, pal);
}

/* ================================================================
 * Group 7: Timer
 * ================================================================ */

TEST(mock_timer) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_test_runtime(&pal);
    ASSERT_NOT_NULL(rt);

    /* Start a timer via testHelper (which calls pal.timerStart) */
    int rc = qwrt_eval(rt,
        "var _timerFired = false; "
        "var _timerInfo = testHelper.timerStart(1000, 0); "
        "_timerInfo.promise.then(function() { _timerFired = true; })",
        NULL);
    ASSERT_EQ(rc, 0);

    /* Process the initial promise setup */
    rc = qwrt_tick(rt, 100);
    ASSERT_EQ(rc, 0);

    /* Timer should not have fired yet */
    char *result = NULL;
    rc = qwrt_eval(rt, "_timerFired", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(result, "false");
    qwrt_free(result);

    /* Fire the mock timer (handle_id 1 — first timer gets id 1) */
    pal_mock_fire_timer(pal, 1);

    /* Tick to process the resolved promise */
    rc = qwrt_tick(rt, 100);
    ASSERT_EQ(rc, 0);

    /* Now the callback should have run */
    rc = qwrt_eval(rt, "_timerFired", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(result, "true");

    qwrt_free(result);
    destroy_test_runtime(rt, pal);
}

/* ================================================================
 * Group 8: Memory
 * ================================================================ */

TEST(free) {
    /* qwrt_free on NULL should not crash */
    qwrt_free(NULL);

    /* qwrt_free on a valid string should not crash */
    char *ptr = strdup("test");
    ASSERT_NOT_NULL(ptr);
    qwrt_free(ptr);
}

/* ================================================================
 * Group 9: qwrt_reset
 * ================================================================ */

TEST(reset_basic) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_test_runtime(&pal);
    ASSERT_NOT_NULL(rt);

    int rc = qwrt_eval(rt, "var resetTestVar = 42", NULL);
    ASSERT_EQ(rc, 0);

    qwrt_config_t config;
    memset(&config, 0, sizeof(config));
    config.pal = pal;

    rc = qwrt_reset(rt, &config);
    ASSERT_EQ(rc, 0);

    char *result = NULL;
    rc = qwrt_eval(rt, "typeof resetTestVar", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(result, "\"undefined\"");
    qwrt_free(result);

    rc = qwrt_eval(rt, "1 + 1", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(result, "2");
    qwrt_free(result);

    destroy_test_runtime(rt, pal);
}

TEST(reset_clears_timers) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_test_runtime(&pal);
    ASSERT_NOT_NULL(rt);

    int rc = qwrt_eval(rt, "var _resetTimerInfo = testHelper.timerStart(1000, 0)", NULL);
    ASSERT_EQ(rc, 0);
    rc = qwrt_tick(rt, 100);
    ASSERT_EQ(rc, 0);

    qwrt_config_t config;
    memset(&config, 0, sizeof(config));
    config.pal = pal;

    rc = qwrt_reset(rt, &config);
    ASSERT_EQ(rc, 0);

    char *result = NULL;
    rc = qwrt_eval(rt, "1 + 2", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(result, "3");
    qwrt_free(result);

    destroy_test_runtime(rt, pal);
}

TEST(reset_different_pal) {
    qwrt_pal_t *pal_full = pal_mock_create();
    qwrt_pal_t *pal_no_http = pal_mock_create_no_http();
    ASSERT_NOT_NULL(pal_full);
    ASSERT_NOT_NULL(pal_no_http);

    qwrt_config_t config;
    memset(&config, 0, sizeof(config));
    config.pal = pal_full;

    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NOT_NULL(rt);

    /* Expose testHelper for PAL access */
    qwrt_eval(rt, testHelper_def, NULL);

    /* HTTP should work with full PAL */
    int rc = qwrt_eval(rt,
        "var _httpOk = null; "
        "testHelper.httpRequest('http://test', 'GET', '', null).then(function(v) { "
        "  _httpOk = v; "
        "})", NULL);
    ASSERT_EQ(rc, 0);
    rc = qwrt_tick(rt, 100);
    ASSERT_EQ(rc, 0);

    /* Reset with no-http PAL */
    config.pal = pal_no_http;
    rc = qwrt_reset(rt, &config);
    ASSERT_EQ(rc, 0);

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
    ASSERT_EQ(rc, 0);
    rc = qwrt_tick(rt, 100);
    ASSERT_EQ(rc, 0);

    rc = qwrt_eval(rt, "_httpDenied", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(result);
    ASSERT(strstr(result, "Permission denied") != NULL);
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_mock_destroy(pal_full);
    pal_mock_destroy(pal_no_http);
}

/* ================================================================
 * Group 10: Permission-denied PAL
 * ================================================================ */

TEST(pal_denied_fs_write) {
    qwrt_pal_t *pal = pal_mock_create_readonly_fs();
    ASSERT_NOT_NULL(pal);

    qwrt_config_t config;
    memset(&config, 0, sizeof(config));
    config.pal = pal;

    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NOT_NULL(rt);
    qwrt_eval(rt, testHelper_def, NULL);

    char *result = NULL;
    int rc = qwrt_eval(rt,
        "var _writeErr = null; "
        "testHelper.fsWrite('/tmp/test.txt', 'data').then("
        "  function(v) { _writeErr = 'ok'; },"
        "  function(e) { _writeErr = 'err:' + e; }"
        ")", NULL);
    ASSERT_EQ(rc, 0);
    rc = qwrt_tick(rt, 100);
    ASSERT_EQ(rc, 0);

    rc = qwrt_eval(rt, "_writeErr", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(result);
    ASSERT(strstr(result, "Permission denied") != NULL);
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

TEST(pal_denied_http) {
    qwrt_pal_t *pal = pal_mock_create_no_http();
    ASSERT_NOT_NULL(pal);

    qwrt_config_t config;
    memset(&config, 0, sizeof(config));
    config.pal = pal;

    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NOT_NULL(rt);
    qwrt_eval(rt, testHelper_def, NULL);

    char *result = NULL;
    int rc = qwrt_eval(rt,
        "var _httpErr = null; "
        "testHelper.httpRequest('http://test', 'GET', '', null).then("
        "  function(v) { _httpErr = 'ok'; },"
        "  function(e) { _httpErr = 'err:' + e; }"
        ")", NULL);
    ASSERT_EQ(rc, 0);
    rc = qwrt_tick(rt, 100);
    ASSERT_EQ(rc, 0);

    rc = qwrt_eval(rt, "_httpErr", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(result);
    ASSERT(strstr(result, "Permission denied") != NULL);
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

/* ================================================================
 * Group 11: Bytecode compilation
 * ================================================================ */

TEST(compile_script) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_test_runtime(&pal);
    ASSERT_NOT_NULL(rt);

    const char *code = "function add(a, b) { return a + b; }";
    size_t bc_len = 0;
    uint8_t *bc = qwrt_compile(rt, code, strlen(code), &bc_len);
    ASSERT_NOT_NULL(bc);
    ASSERT(bc_len > 0);

    qwrt_free(bc);
    destroy_test_runtime(rt, pal);
}

TEST(compile_module) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_test_runtime(&pal);
    ASSERT_NOT_NULL(rt);

    const char *code = "export function mul(a, b) { return a * b; }";
    size_t bc_len = 0;
    uint8_t *bc = qwrt_compile_module(rt, code, strlen(code), &bc_len);
    ASSERT_NOT_NULL(bc);
    ASSERT(bc_len > 0);

    qwrt_free(bc);
    destroy_test_runtime(rt, pal);
}

TEST(compile_error) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_test_runtime(&pal);
    ASSERT_NOT_NULL(rt);

    const char *code = "function (invalid syntax";
    size_t bc_len = 0;
    uint8_t *bc = qwrt_compile(rt, code, strlen(code), &bc_len);
    ASSERT_NULL(bc);
    ASSERT_EQ(bc_len, 0);

    destroy_test_runtime(rt, pal);
}

TEST(compile_roundtrip) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_test_runtime(&pal);
    ASSERT_NOT_NULL(rt);

    /* Compile JS to bytecode */
    const char *code = "var compileTestVar = 42;";
    size_t bc_len = 0;
    uint8_t *bc = qwrt_compile(rt, code, strlen(code), &bc_len);
    ASSERT_NOT_NULL(bc);
    ASSERT(bc_len > 0);

    /* Reset context to clear JS state */
    qwrt_config_t config;
    memset(&config, 0, sizeof(config));
    config.pal = pal;
    int rc = qwrt_reset(rt, &config);
    ASSERT_EQ(rc, 0);

    /* Verify variable doesn't exist yet */
    char *result = NULL;
    rc = qwrt_eval(rt, "typeof compileTestVar", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(result, "\"undefined\"");
    qwrt_free(result);

    /* Load and execute the bytecode */
    rc = qwrt_eval_bytecode(rt, bc, bc_len, NULL);
    ASSERT_EQ(rc, 0);

    /* Now the variable should exist */
    rc = qwrt_eval(rt, "compileTestVar", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(result, "42");
    qwrt_free(result);

    qwrt_free(bc);
    destroy_test_runtime(rt, pal);
}

/* ================================================================
 * Group 12: Bytecode edge cases and error paths
 * ================================================================ */

TEST(bytecode_invalid_magic) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_test_runtime(&pal);
    ASSERT_NOT_NULL(rt);

    /* Create bytecode with wrong magic bytes */
    uint8_t bad_bc[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    int rc = qwrt_eval_bytecode(rt, bad_bc, sizeof(bad_bc), NULL);
    ASSERT(rc < 0);

    destroy_test_runtime(rt, pal);
}

TEST(bytecode_truncated) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_test_runtime(&pal);
    ASSERT_NOT_NULL(rt);

    /* Valid QuickJS bytecode starts with magic, truncated at 4 bytes */
    uint8_t truncated[] = { 0x02, 0x00, 0x00, 0x00 };
    int rc = qwrt_eval_bytecode(rt, truncated, sizeof(truncated), NULL);
    ASSERT(rc < 0);

    destroy_test_runtime(rt, pal);
}

TEST(bytecode_null_data) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_test_runtime(&pal);
    ASSERT_NOT_NULL(rt);

    int rc = qwrt_eval_bytecode(rt, NULL, 0, NULL);
    ASSERT(rc < 0);

    destroy_test_runtime(rt, pal);
}

TEST(compile_null_out_len) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_test_runtime(&pal);
    ASSERT_NOT_NULL(rt);

    const char *code = "1 + 1";
    uint8_t *bc = qwrt_compile(rt, code, strlen(code), NULL);
    /* NULL out_len should return NULL */
    ASSERT_NULL(bc);

    destroy_test_runtime(rt, pal);
}

TEST(compile_empty_code) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_test_runtime(&pal);
    ASSERT_NOT_NULL(rt);

    size_t bc_len = 0;
    uint8_t *bc = qwrt_compile(rt, "", 0, &bc_len);
    /* Empty code may or may not produce valid bytecode depending on engine */
    if (bc) {
        ASSERT(bc_len > 0);
        qwrt_free(bc);
    }

    destroy_test_runtime(rt, pal);
}

TEST(compile_module_null_out_len) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_test_runtime(&pal);
    ASSERT_NOT_NULL(rt);

    const char *code = "export const x = 1;";
    uint8_t *bc = qwrt_compile_module(rt, code, strlen(code), NULL);
    /* NULL out_len should return NULL */
    ASSERT_NULL(bc);

    destroy_test_runtime(rt, pal);
}

/* ================================================================
 * Group 13: qwrt_call error paths
 * ================================================================ */

TEST(call_malformed_args) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_test_runtime(&pal);
    ASSERT_NOT_NULL(rt);

    /* Define a function */
    qwrt_eval(rt, "function testCall(a, b) { return a + b; }", NULL);

    /* Call with malformed JSON args */
    char *result = NULL;
    int rc = qwrt_call(rt, "testCall", "{invalid", &result);
    ASSERT(rc < 0);
    ASSERT_NULL(result);

    destroy_test_runtime(rt, pal);
}

TEST(call_null_func) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_test_runtime(&pal);
    ASSERT_NOT_NULL(rt);

    char *result = NULL;
    int rc = qwrt_call(rt, NULL, "[]", &result);
    ASSERT(rc < 0);
    ASSERT_NULL(result);

    destroy_test_runtime(rt, pal);
}

TEST(call_nonexistent_func) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_test_runtime(&pal);
    ASSERT_NOT_NULL(rt);

    char *result = NULL;
    int rc = qwrt_call(rt, "noSuchFunction", "[]", &result);
    ASSERT(rc < 0);

    destroy_test_runtime(rt, pal);
}

/* ================================================================
 * Group 14: qwrt_get_jsctx and qwrt_get_active_ctx_id
 * ================================================================ */

TEST(get_jsctx_valid) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_test_runtime(&pal);
    ASSERT_NOT_NULL(rt);

    void *jsctx = qwrt_get_jsctx(rt);
    ASSERT_NOT_NULL(jsctx);

    destroy_test_runtime(rt, pal);
}

TEST(get_active_ctx_id) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_test_runtime(&pal);
    ASSERT_NOT_NULL(rt);

    int id = qwrt_get_active_ctx_id(rt);
    ASSERT_EQ(id, 0);

    destroy_test_runtime(rt, pal);
}

/* ================================================================
 * Group 15: qwrt_reset edge cases
 * ================================================================ */

TEST(reset_null_config) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_test_runtime(&pal);
    ASSERT_NOT_NULL(rt);

    int rc = qwrt_reset(rt, NULL);
    ASSERT(rc < 0);

    destroy_test_runtime(rt, pal);
}

/* ================================================================
 * Group 16: eval_bytecode error path — exception propagation
 * ================================================================ */

TEST(bytecode_result_on_error) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_test_runtime(&pal);
    ASSERT_NOT_NULL(rt);

    /* Compile valid code that throws at runtime */
    const char *code = "throw new Error('test error');";
    size_t bc_len = 0;
    uint8_t *bc = qwrt_compile(rt, code, strlen(code), &bc_len);
    ASSERT_NOT_NULL(bc);

    /* Result should be NULL on error */
    char *result = NULL;
    int rc = qwrt_eval_bytecode(rt, bc, bc_len, &result);
    ASSERT(rc < 0);
    ASSERT_NULL(result);

    qwrt_free(bc);
    destroy_test_runtime(rt, pal);
}

/* ================================================================
 * Main
 * ================================================================ */

int main(void)
{
    printf("=== qwrt Unit Tests ===\n\n");

    printf("--- Runtime lifecycle ---\n");
    RUN_TEST(create_destroy);
    RUN_TEST(create_null_pal);
    RUN_TEST(create_no_polyfill);

    printf("\n--- Eval ---\n");
    RUN_TEST(eval_simple);
    RUN_TEST(eval_string);
    RUN_TEST(eval_object);
    RUN_TEST(eval_error);

    printf("\n--- Call ---\n");
    RUN_TEST(call_function);
    RUN_TEST(call_with_args);
    RUN_TEST(call_nonexistent);
    RUN_TEST(call_malformed_args);
    RUN_TEST(call_null_func);
    RUN_TEST(call_nonexistent_func);

    printf("\n--- Tick ---\n");
    RUN_TEST(tick_no_pending);
    RUN_TEST(tick_with_promise);

    printf("\n--- PAL bridge (sync) ---\n");
    RUN_TEST(pal_time_now);
    RUN_TEST(pal_log);

    printf("\n--- PAL bridge (async) ---\n");
    RUN_TEST(mock_storage);
    RUN_TEST(mock_fs);
    RUN_TEST(mock_http);

    printf("\n--- Timer ---\n");
    RUN_TEST(mock_timer);

    printf("\n--- Memory ---\n");
    RUN_TEST(free);

    printf("\n--- qwrt_reset ---\n");
    RUN_TEST(reset_basic);
    RUN_TEST(reset_clears_timers);
    RUN_TEST(reset_different_pal);
    RUN_TEST(reset_null_config);

    printf("\n--- Permission-denied PAL ---\n");
    RUN_TEST(pal_denied_fs_write);
    RUN_TEST(pal_denied_http);

    printf("\n--- Bytecode compilation ---\n");
    RUN_TEST(compile_script);
    RUN_TEST(compile_module);
    RUN_TEST(compile_error);
    RUN_TEST(compile_roundtrip);
    RUN_TEST(compile_null_out_len);
    RUN_TEST(compile_empty_code);
    RUN_TEST(compile_module_null_out_len);

    printf("\n--- Bytecode edge cases ---\n");
    RUN_TEST(bytecode_invalid_magic);
    RUN_TEST(bytecode_truncated);
    RUN_TEST(bytecode_null_data);
    RUN_TEST(bytecode_result_on_error);

    printf("\n--- Internal API ---\n");
    RUN_TEST(get_jsctx_valid);
    RUN_TEST(get_active_ctx_id);

    printf("\n=== Results: %d/%d passed, %d failed ===\n",
           tests_passed, tests_run, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}

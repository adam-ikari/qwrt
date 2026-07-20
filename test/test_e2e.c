/*
 * qwrt End-to-End Integration Tests
 *
 * Tests the full stack: C runtime + polyfill + PAL + JS execution.
 * Uses a comprehensive test polyfill that implements WinterTC APIs.
 */

#define _POSIX_C_SOURCE 200809L

#include "test_qwrt.h"
#include "qwrt/qwrt.h"
#include "pal_mock.h"

/* ================================================================
 * E2E Test Polyfill — comprehensive WinterTC API implementation
 * ================================================================ */

static const char e2e_polyfill[] =
    "(function(pal) {\n"
    /* console */
    "  globalThis.console = {\n"
    "    log: function() { pal.log(0, Array.from(arguments).map(String).join(' ')); },\n"
    "    warn: function() { pal.log(1, Array.from(arguments).map(String).join(' ')); },\n"
    "    error: function() { pal.log(2, Array.from(arguments).map(String).join(' ')); },\n"
    "    info: function() { pal.log(0, Array.from(arguments).map(String).join(' ')); },\n"
    "  };\n"
    /* performance */
    "  globalThis.performance = { now: function() { return pal.timeNow(); } };\n"
    /* storage */
    "  globalThis.storage = {\n"
    "    get: function(k) { return pal.storageGet(k); },\n"
    "    set: function(k, v) { return pal.storageSet(k, v); },\n"
    "    delete: function(k) { return pal.storageDel(k); },\n"
    "  };\n"
    /* fs */
    "  globalThis.fs = {\n"
    "    readFile: function(p) { return pal.fsRead(p); },\n"
    "    writeFile: function(p, d) { return pal.fsWrite(p, d); },\n"
    "    exists: function(p) { return pal.fsExists(p).then(function(r) { return r === 'true'; }); },\n"
    "    readdir: function(p) { return pal.fsList(p).then(function(r) { return JSON.parse(r); }); },\n"
    "    unlink: function(p) { return pal.fsRemove(p); },\n"
    "  };\n"
    /* setTimeout */
    "  var _timers = new Map();\n"
    "  globalThis.setTimeout = function(fn, ms) {\n"
    "    var r = pal.timerStart(ms || 0, 0);\n"
    "    _timers.set(r.handle, fn);\n"
    "    r.promise.then(function() {\n"
    "      var cb = _timers.get(r.handle);\n"
    "      if (cb) { _timers.delete(r.handle); cb(); }\n"
    "    });\n"
    "    return r.handle;\n"
    "  };\n"
    "  globalThis.clearTimeout = function(h) {\n"
    "    _timers.delete(h);\n"
    "    pal.timerStop(h);\n"
    "  };\n"
    /* URL — minimal implementation */
    "  function parseURL(url) {\n"
    "    var m = url.match(/^(https?:)\\/\\/([^\\/\\?]+)(\\/[^\\?]*)?(\\?.*)?$/);\n"
    "    if (!m) throw new TypeError('Invalid URL: ' + url);\n"
    "    var sp = {};\n"
    "    if (m[4]) { m[4].slice(1).split('&').forEach(function(p) {\n"
    "      var kv = p.split('='); sp[decodeURIComponent(kv[0])] = kv[1] ? decodeURIComponent(kv[1]) : '';\n"
    "    }); }\n"
    "    return {\n"
    "      href: url, protocol: m[1], host: m[2], hostname: m[2],\n"
    "      pathname: m[3] || '/', search: m[4] || '',\n"
    "      searchParams: { get: function(k) { return sp[k] || null; }, has: function(k) { return k in sp; } }\n"
    "    };\n"
    "  }\n"
    "  globalThis.URL = function(url) { return parseURL(url); };\n"
    /* AbortController */
    "  globalThis.AbortController = function() {\n"
    "    this.signal = { aborted: false, _listeners: [],\n"
    "      addEventListener: function(t, fn) { this._listeners.push(fn); },\n"
    "    };\n"
    "    this.abort = function() {\n"
    "      this.signal.aborted = true;\n"
    "      this.signal._listeners.forEach(function(fn) { fn(); });\n"
    "    };\n"
    "  };\n"
    /* btoa/atob */
    "  var _b64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';\n"
    "  globalThis.btoa = function(s) {\n"
    "    var r = '', i = 0;\n"
    "    while (i < s.length) {\n"
    "      var a = s.charCodeAt(i++), b = s.charCodeAt(i++), c = s.charCodeAt(i++);\n"
    "      r += _b64[a>>2] + _b64[((a&3)<<4)|(b>>4)] + (isNaN(b)?'=':_b64[((b&15)<<2)|(c>>6)]) + (isNaN(c)?'=':_b64[c&63]);\n"
    "    }\n"
    "    return r;\n"
    "  };\n"
    "  globalThis.atob = function(s) {\n"
    "    var r = '', i = 0;\n"
    "    s = s.replace(/=+$/, '');\n"
    "    while (i < s.length) {\n"
    "      var a = _b64.indexOf(s[i++]), b = _b64.indexOf(s[i++]),\n"
    "          c = _b64.indexOf(s[i++]), d = _b64.indexOf(s[i++]);\n"
    "      r += String.fromCharCode((a<<2)|(b>>4)) + (c>=0?String.fromCharCode(((b&15)<<4)|(c>>2)):'') + (d>=0?String.fromCharCode(((c&3)<<6)|d):'');\n"
    "    }\n"
    "    return r;\n"
    "  };\n"
    /* fetch */
    "  globalThis.fetch = function(url, init) {\n"
    "    var method = (init && init.method) || 'GET';\n"
    "    var headers = (init && init.headers) || {};\n"
    "    var body = (init && init.body) || null;\n"
    "    var hJson = JSON.stringify(headers);\n"
    "    return pal.httpRequest(url, method, hJson, body).then(function(respJson) {\n"
    "      var parsed = JSON.parse(respJson);\n"
    "      return {\n"
    "        status: parsed.status, ok: parsed.status >= 200 && parsed.status < 300,\n"
    "        headers: parsed.headers || {},\n"
    "        text: function() { return Promise.resolve(parsed.body || ''); },\n"
    "        json: function() { return Promise.resolve(JSON.parse(parsed.body || '{}')); },\n"
    "      };\n"
    "    });\n"
    "  };\n"
    "})(__pal_inject__);\n";

/* ================================================================
 * Helper: create a runtime with mock PAL + E2E polyfill
 * ================================================================ */

static qwrt_t *create_e2e_runtime(qwrt_pal_t **pal_out)
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

    if (pal_out) *pal_out = pal;
    return rt;
}

static void destroy_e2e_runtime(qwrt_t *rt, qwrt_pal_t *pal)
{
    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

/* ================================================================
 * Test 1: console.log works end-to-end
 * ================================================================ */

TEST(e2e_console) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_e2e_runtime(&pal);
    ASSERT_NOT_NULL(rt);

    /* Set time to a known value */
    pal_mock_set_time(pal, 1000);

    /* Eval code using console.log */
    int rc = qwrt_eval(rt, "console.log('hello', 'world')", NULL);
    ASSERT_EQ(rc, 0);

    /* Check mock PAL log */
    int log_count = 0;
    const char *log = pal_mock_get_log(pal, &log_count);
    ASSERT_EQ(log_count, 1);
    ASSERT(strstr(log, "hello world") != NULL);

    pal_mock_clear_log(pal);
    destroy_e2e_runtime(rt, pal);
}

/* ================================================================
 * Test 2: performance.now() works end-to-end
 * ================================================================ */

TEST(e2e_performance) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_e2e_runtime(&pal);
    ASSERT_NOT_NULL(rt);

    pal_mock_set_time(pal, 12345);

    char *result = NULL;
    int rc = qwrt_eval(rt, "performance.now()", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_NOT_NULL(result);
    /* Result should be ~12345 (as a float: pal.hrtime() ms converted via /1e6) */
    ASSERT(strstr(result, "12345") != NULL);

    qwrt_free(result);
    destroy_e2e_runtime(rt, pal);
}

/* ================================================================
 * Test 3: storage.get/set works end-to-end
 * ================================================================ */

TEST(e2e_storage) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_e2e_runtime(&pal);
    ASSERT_NOT_NULL(rt);

    /* Set a value (returns Promise) */
    int rc = qwrt_eval(rt,
        "var _r = null; storage.set('test_key', 'test_value').then(function() { _r = 'ok'; })",
        NULL);
    ASSERT_EQ(rc, 0);
    qwrt_tick(rt, 100);

    /* Verify set worked */
    char *result = NULL;
    rc = qwrt_eval(rt, "_r", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(result, "\"ok\"");
    qwrt_free(result);

    /* Get the value */
    rc = qwrt_eval(rt,
        "var _v = null; storage.get('test_key').then(function(v) { _v = v; })",
        NULL);
    ASSERT_EQ(rc, 0);
    qwrt_tick(rt, 100);

    result = NULL;
    rc = qwrt_eval(rt, "_v", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(result, "\"test_value\"");

    qwrt_free(result);
    destroy_e2e_runtime(rt, pal);
}

/* ================================================================
 * Test 4: fs.writeFile/readFile works end-to-end
 * ================================================================ */

TEST(e2e_fs) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_e2e_runtime(&pal);
    ASSERT_NOT_NULL(rt);

    /* Write a file */
    int rc = qwrt_eval(rt,
        "var _fw = null; fs.writeFile('/test.txt', 'hello fs').then(function() { _fw = 'written'; })",
        NULL);
    ASSERT_EQ(rc, 0);
    qwrt_tick(rt, 100);

    char *result = NULL;
    rc = qwrt_eval(rt, "_fw", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(result, "\"written\"");
    qwrt_free(result);

    /* Read it back */
    rc = qwrt_eval(rt,
        "var _fr = null; fs.readFile('/test.txt').then(function(d) { _fr = d; })",
        NULL);
    ASSERT_EQ(rc, 0);
    qwrt_tick(rt, 100);

    result = NULL;
    rc = qwrt_eval(rt, "_fr", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(result, "\"hello fs\"");

    qwrt_free(result);
    destroy_e2e_runtime(rt, pal);
}

/* ================================================================
 * Test 5: fetch() works end-to-end with mock
 * ================================================================ */

TEST(e2e_fetch) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_e2e_runtime(&pal);
    ASSERT_NOT_NULL(rt);

    /* Mock PAL returns {"status":200,"headers":{},"body":"mock response"} */
    int rc = qwrt_eval(rt,
        "var _fetch_result = null;"
        "fetch('http://example.com/api').then(function(r) { return r.text(); }).then(function(t) { _fetch_result = t; })",
        NULL);
    ASSERT_EQ(rc, 0);
    qwrt_tick(rt, 100);

    char *result = NULL;
    rc = qwrt_eval(rt, "_fetch_result", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(result, "\"mock response\"");

    qwrt_free(result);
    destroy_e2e_runtime(rt, pal);
}

/* ================================================================
 * Test 6: setTimeout works with mock PAL
 * ================================================================ */

TEST(e2e_timer) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_e2e_runtime(&pal);
    ASSERT_NOT_NULL(rt);

    int rc = qwrt_eval(rt,
        "var _timer_fired = false;"
        "setTimeout(function() { _timer_fired = true; }, 100)",
        NULL);
    ASSERT_EQ(rc, 0);

    /* Process the timer setup */
    qwrt_tick(rt, 100);

    /* Timer hasn't fired yet */
    char *result = NULL;
    rc = qwrt_eval(rt, "_timer_fired", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(result, "false");
    qwrt_free(result);

    /* Fire all mock timers */
    pal_mock_fire_all_timers(pal);
    qwrt_tick(rt, 100);

    /* Now it should have fired */
    rc = qwrt_eval(rt, "_timer_fired", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(result, "true");

    qwrt_free(result);
    destroy_e2e_runtime(rt, pal);
}

/* ================================================================
 * Test 7: URL parsing works
 * ================================================================ */

TEST(e2e_url) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_e2e_runtime(&pal);
    ASSERT_NOT_NULL(rt);

    char *result = NULL;
    int rc = qwrt_eval(rt, "new URL('https://example.com/path?q=1').searchParams.get('q')", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(result, "\"1\"");

    qwrt_free(result);
    destroy_e2e_runtime(rt, pal);
}

/* ================================================================
 * Test 8: AbortController works
 * ================================================================ */

TEST(e2e_abort) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_e2e_runtime(&pal);
    ASSERT_NOT_NULL(rt);

    int rc = qwrt_eval(rt,
        "var _abort_result = ''; "
        "var ac = new AbortController(); "
        "ac.signal.addEventListener('abort', function() { _abort_result = 'aborted'; }); "
        "ac.abort();",
        NULL);
    ASSERT_EQ(rc, 0);

    char *result = NULL;
    rc = qwrt_eval(rt, "_abort_result", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(result, "\"aborted\"");

    qwrt_free(result);
    destroy_e2e_runtime(rt, pal);
}

/* ================================================================
 * Test 9: btoa/atob works
 * ================================================================ */

TEST(e2e_encoding) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_e2e_runtime(&pal);
    ASSERT_NOT_NULL(rt);

    /* Test btoa */
    char *result = NULL;
    int rc = qwrt_eval(rt, "btoa('hello')", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(result, "\"aGVsbG8=\"");
    qwrt_free(result);

    /* Test atob */
    result = NULL;
    rc = qwrt_eval(rt, "atob('aGVsbG8=')", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(result, "\"hello\"");

    qwrt_free(result);
    destroy_e2e_runtime(rt, pal);
}

/* ================================================================
 * Test 10: Simulated agent workflow: fetch -> process -> store
 * ================================================================ */

TEST(e2e_full_agent) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_e2e_runtime(&pal);
    ASSERT_NOT_NULL(rt);

    /* Simulate: fetch data -> parse -> store result.
     * The fetch polyfill uses streaming path (httpRequestStream).
     * Mock extracts body via simple string parsing — no JSON unescaping.
     * Use a JSON body without characters needing escaping in the mock wrapper. */
    pal_mock_set_http_response(pal,
        "{\"status\":200,\"headers\":{\"Content-Type\":\"application/json\"},"
        "\"body\":\"[12345]\"}");

    int rc = qwrt_eval(rt,
        "var _agent_done = false; var _agent_error = null;"
        "fetch('http://api.example.com/data').then(function(r) { return r.json(); })"
        ".then(function(data) {"
        "  return storage.set('last_result', JSON.stringify(data));"
        "}).then(function() {"
        "  _agent_done = true;"
        "}).catch(function(e) {"
        "  _agent_error = String(e);"
        "})",
        NULL);
    ASSERT_EQ(rc, 0);

    /* Process async operations */
    qwrt_tick(rt, 100);

    /* Verify agent completed without error */
    char *result = NULL;
    rc = qwrt_eval(rt, "_agent_error", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(result, "null");
    qwrt_free(result);

    rc = qwrt_eval(rt, "_agent_done", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(result, "true");
    qwrt_free(result);

    /* Verify stored data */
    rc = qwrt_eval(rt,
        "var _stored = null; storage.get('last_result').then(function(v) { _stored = v; })",
        NULL);
    ASSERT_EQ(rc, 0);
    qwrt_tick(rt, 100);

    result = NULL;
    rc = qwrt_eval(rt, "_stored", &result);
    ASSERT_EQ(rc, 0);
    ASSERT(strstr(result, "12345") != NULL);

    qwrt_free(result);
    destroy_e2e_runtime(rt, pal);
}

/* ================================================================
 * Main
 * ================================================================ */

int main(void)
{
    printf("=== qwrt End-to-End Integration Tests ===\n\n");

    printf("--- Console ---\n");
    RUN_TEST(e2e_console);

    printf("\n--- Performance ---\n");
    RUN_TEST(e2e_performance);

    printf("\n--- Storage ---\n");
    RUN_TEST(e2e_storage);

    printf("\n--- Filesystem ---\n");
    RUN_TEST(e2e_fs);

    printf("\n--- Fetch ---\n");
    RUN_TEST(e2e_fetch);

    printf("\n--- Timer ---\n");
    RUN_TEST(e2e_timer);

    printf("\n--- URL ---\n");
    RUN_TEST(e2e_url);

    printf("\n--- AbortController ---\n");
    RUN_TEST(e2e_abort);

    printf("\n--- Encoding ---\n");
    RUN_TEST(e2e_encoding);

    printf("\n--- Full Agent Workflow ---\n");
    RUN_TEST(e2e_full_agent);

    printf("\n=== Results: %d/%d passed, %d failed ===\n",
           tests_passed, tests_run, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}

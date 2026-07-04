/*
 * ACE-Core End-to-End Integration Tests
 *
 * Tests the full pipeline: polyfill -> adapter -> ace-core -> session creation.
 * Loads adapter.js and ace-core-qwrt.js from disk at runtime.
 */

#define _POSIX_C_SOURCE 200809L

#include "qwrt/qwrt.h"
#include "pal_mock.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ================================================================
 * Test polyfill — sets up global storage/fs/console/etc
 * (same as used in test_e2e.c)
 * ================================================================ */

static const char integration_polyfill[] =
    "(function(pal) {\n"
    "  globalThis.console = {\n"
    "    log: function() { pal.log(0, Array.from(arguments).map(String).join(' ')); },\n"
    "    warn: function() { pal.log(1, Array.from(arguments).map(String).join(' ')); },\n"
    "    error: function() { pal.log(2, Array.from(arguments).map(String).join(' ')); },\n"
    "    info: function() { pal.log(0, Array.from(arguments).map(String).join(' ')); },\n"
    "  };\n"
    "  globalThis.performance = { now: function() { return pal.timeNow(); } };\n"
    "  globalThis.storage = {\n"
    "    get: function(k) { return pal.storageGet(k); },\n"
    "    set: function(k, v) { return pal.storageSet(k, v); },\n"
    "    delete: function(k) { return pal.storageDel(k); },\n"
    "  };\n"
    "  globalThis.fs = {\n"
    "    readFile: function(p) { return pal.fsRead(p); },\n"
    "    writeFile: function(p, d) { return pal.fsWrite(p, d); },\n"
    "    exists: function(p) { return pal.fsExists(p).then(function(r) { return r === 'true'; }); },\n"
    "    readdir: function(p) { return pal.fsList(p).then(function(r) { return JSON.parse(r); }); },\n"
    "    unlink: function(p) { return pal.fsRemove(p); },\n"
    "  };\n"
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
    "  globalThis.AbortController = function() {\n"
    "    this.signal = { aborted: false, _listeners: [],\n"
    "      addEventListener: function(t, fn) { this._listeners.push(fn); },\n"
    "    };\n"
    "    this.abort = function() {\n"
    "      this.signal.aborted = true;\n"
    "      this.signal._listeners.forEach(function(fn) { fn(); });\n"
    "    };\n"
    "  };\n"
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
 * Helpers
 * ================================================================ */

static int tests_run = 0;
static int tests_failed = 0;

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t nread = fread(buf, 1, (size_t)len, f);
    buf[nread] = '\0';
    fclose(f);
    return buf;
}

static qwrt_t *create_integration_runtime(qwrt_pal_t *pal)
{
    qwrt_config_t config;
    memset(&config, 0, sizeof(config));
    config.pal = pal;
    return qwrt_create(&config);
}

static int eval_file_multi(qwrt_t *rt, const char **paths, int npaths)
{
#ifdef ACE_SOURCE_DIR
    /* Use compile-time source directory — no relative path guessing */
    for (int i = 0; i < npaths; i++) {
        char full[1024];
        snprintf(full, sizeof(full), "%s/%s", ACE_SOURCE_DIR, paths[i]);
        char *js = read_file(full);
        if (js) {
            int rc = qwrt_eval(rt, js, NULL);
            free(js);
            return rc;
        }
    }
#else
    /* Fallback: check ACE_PROJECT_ROOT env var */
    const char *root = getenv("ACE_PROJECT_ROOT");
    if (root) {
        for (int i = 0; i < npaths; i++) {
            char full[1024];
            snprintf(full, sizeof(full), "%s/%s", root, paths[i]);
            char *js = read_file(full);
            if (js) {
                int rc = qwrt_eval(rt, js, NULL);
                free(js);
                return rc;
            }
        }
    }
#endif
    for (int i = 0; i < npaths; i++) {
        char *js = read_file(paths[i]);
        if (js) {
            int rc = qwrt_eval(rt, js, NULL);
            free(js);
            return rc;
        }
    }
    return -1;
}

static const char *adapter_paths[] = {
    "examples/ace-qwrt/adapter.js",
    "../examples/ace-qwrt/adapter.js",
    "../../examples/ace-qwrt/adapter.js",
};

static const char *ace_core_paths[] = {
    "packages/ace-core/dist/ace-core-qwrt.js",
    "../packages/ace-core/dist/ace-core-qwrt.js",
    "../../packages/ace-core/dist/ace-core-qwrt.js",
};

static int load_ace_core(qwrt_t *rt)
{
    if (eval_file_multi(rt, adapter_paths, 3) != 0) return -1;
    if (eval_file_multi(rt, ace_core_paths, 3) != 0) return -1;
    return 0;
}

#define TEST_BEGIN(name) do { \
    printf("  %-55s", name); \
    tests_run++; \
} while(0)

#define TEST_FAIL(msg) do { \
    printf("FAIL: %s\n", msg); \
    tests_failed++; \
    return; \
} while(0)

#define TEST_PASS() printf("PASS\n")

/* ================================================================
 * Test: adapter.js loads and exposes __createQwrtRuntime
 * ================================================================ */

static void test_adapter_loads(qwrt_pal_t *pal)
{
    TEST_BEGIN("adapter.js loads and exposes __createQwrtRuntime");

    qwrt_t *rt = create_integration_runtime(pal);
    if (!rt) TEST_FAIL("qwrt_create");

    if (eval_file_multi(rt, adapter_paths, 3) != 0)
        TEST_FAIL("eval adapter.js");

    char *result = NULL;
    int rc = qwrt_eval(rt, "typeof __createQwrtRuntime", &result);
    if (rc != 0 || !result || strcmp(result, "\"function\"") != 0) {
        TEST_FAIL("__createQwrtRuntime not found");
    }
    qwrt_free(result);

    /* Verify the runtime object has storage and fs */
    rc = qwrt_eval(rt,
        "var _r = __createQwrtRuntime();"
        "typeof _r.storage.get === 'function' && typeof _r.fs.readFile === 'function'",
        &result);
    if (rc != 0 || !result || strcmp(result, "true") != 0) {
        TEST_FAIL("runtime missing storage or fs");
    }
    qwrt_free(result);

    TEST_PASS();
    qwrt_destroy(rt);
}

/* ================================================================
 * Test: ace-core-qwrt.js loads and exposes createAceSession
 * ================================================================ */

static void test_ace_core_loads(qwrt_pal_t *pal)
{
    TEST_BEGIN("ace-core-qwrt.js loads and exposes createAceSession");

    qwrt_t *rt = create_integration_runtime(pal);
    if (!rt) TEST_FAIL("qwrt_create");

    if (load_ace_core(rt) != 0)
        TEST_FAIL("load adapter + ace-core");

    char *result = NULL;
    int rc = qwrt_eval(rt, "typeof aceCore.createAceSession", &result);
    if (rc != 0 || !result || strcmp(result, "\"function\"") != 0) {
        TEST_FAIL("createAceSession not found");
    }
    qwrt_free(result);

    TEST_PASS();
    qwrt_destroy(rt);
}

/* ================================================================
 * Test: createAceSession succeeds with QwrtRuntime
 * ================================================================ */

static void test_session_creates(qwrt_pal_t *pal)
{
    TEST_BEGIN("createAceSession succeeds with QwrtRuntime");

    qwrt_t *rt = create_integration_runtime(pal);
    if (!rt) TEST_FAIL("qwrt_create");

    if (load_ace_core(rt) != 0)
        TEST_FAIL("load adapter + ace-core");

    /* Start the async session creation, store result in a global */
    const char *code =
        "var _session_result = null;\n"
        "(async function() {\n"
        "  try {\n"
        "    var runtime = __createQwrtRuntime();\n"
        "    var session = await aceCore.createAceSession({\n"
        "      config: { runtime: runtime },\n"
        "      skipConfigLoad: true,\n"
        "      skipContextInjection: true\n"
        "    });\n"
        "    _session_result = typeof session.ace.execute === 'function' ? 'ok' : 'no_execute';\n"
        "  } catch(e) {\n"
        "    _session_result = 'error:' + e.message;\n"
        "  }\n"
        "})();\n";

    int rc = qwrt_eval(rt, code, NULL);
    if (rc != 0) TEST_FAIL("eval session creation code");

    /* Drive event loop to resolve async promises */
    for (int i = 0; i < 200; i++) {
        qwrt_tick(rt);
    }

    char *result = NULL;
    rc = qwrt_eval(rt, "_session_result", &result);
    if (rc != 0 || !result) {
        TEST_FAIL("could not read _session_result");
    }
    if (strcmp(result, "\"ok\"") != 0) {
        char msg[256];
        snprintf(msg, sizeof(msg), "expected '\"ok\"', got '%s'", result);
        qwrt_free(result);
        TEST_FAIL(msg);
    }
    qwrt_free(result);

    TEST_PASS();
    qwrt_destroy(rt);
}

/* ================================================================
 * Main
 * ================================================================ */

int main(void)
{
    qwrt_pal_t *pal = pal_mock_create();
    if (!pal) {
        fprintf(stderr, "Failed to create mock PAL\n");
        return 1;
    }

    printf("ace-core integration tests:\n");

    test_adapter_loads(pal);
    test_ace_core_loads(pal);
    test_session_creates(pal);

    printf("\n%d/%d tests passed\n", tests_run - tests_failed, tests_run);

    pal_mock_destroy(pal);
    return tests_failed ? 1 : 0;
}

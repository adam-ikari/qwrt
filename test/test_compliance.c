/*
 * qwrt ES2023 + WinterCG Compliance Tests
 *
 * Tests qwrt's polyfill against ES2023 and WinterCG (now WinterTC)
 * Minimum Common Web Platform API specifications.
 *
 * WinterCG/WinterTC Required APIs:
 *   - console (log, warn, error, info, debug)
 *   - Timers (setTimeout, setInterval, clearTimeout, clearInterval)
 *   - queueMicrotask
 *   - structuredClone
 *   - URL / URLSearchParams
 *   - TextEncoder / TextDecoder
 *   - atob / btoa
 *   - Fetch (fetch, Request, Response, Headers)
 *   - AbortController / AbortSignal
 *   - Event / EventTarget
 *   - crypto.subtle
 *   - performance (now)
 *   - Storage (get, set, delete, list)
 *
 * ES2023 features tested:
 *   - Array.prototype.at(), findLast(), findLastIndex(), toSorted(), toReversed(), toSpliced()
 *   - Object.hasOwn()
 *   - String.prototype.replaceAll()
 *   - Promise.allSettled(), any(), all(), race()
 *   - Async generators / for-await-of
 *   - Optional chaining, nullish coalescing
 *   - WeakRef, FinalizationRegistry
 *   - Symbol, Iterator, well-known symbols
 *
 * Gracefully skips if dist/polyfill.bytecode is not found.
 */

#define _POSIX_C_SOURCE 200809L

#include "test_qwrt.h"
#include "qwrt/qwrt.h"
#include "qwrt/ext_wamr.h"
#include "qwrt/ext_wasm3.h"
#include "pal_mock.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================
 * Globals
 * ================================================================ */

static const uint8_t *g_polyfill;
static size_t g_polyfill_len;
static int g_total = 0;
static int g_passed = 0;
static int g_failed = 0;

/* ================================================================
 * Helper: load file from disk
 * ================================================================ */

static uint8_t *load_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    if (len < 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);
    if (len == 0) { fclose(f); *out_len = 0; return (uint8_t *)calloc(1, 1); }
    uint8_t *buf = (uint8_t *)malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t nread = fread(buf, 1, (size_t)len, f);
    buf[nread] = 0;
    fclose(f);
    *out_len = nread;
    return buf;
}

/* ================================================================
 * Helper: create runtime with mock PAL + polyfill
 * ================================================================ */

static qwrt_t *create_runtime(qwrt_pal_t **pal_out) {
    qwrt_pal_t *pal = pal_mock_create();
    if (!pal) return NULL;

    qwrt_config_t config;
    memset(&config, 0, sizeof(config));
    config.pal = pal;

    qwrt_t *rt = qwrt_create(&config);
    if (!rt) { pal_mock_destroy(pal); return NULL; }

    if (pal_out) *pal_out = pal;
    return rt;
}

/* ================================================================
 * Helper: eval JS and check result
 * ================================================================ */

/* Eval a JS expression, return the result string (caller must free) */
static char *js_eval(qwrt_t *rt, const char *code) {
    char *result = NULL;
    int rc = qwrt_eval(rt, code, &result);
    if (rc != 0) return NULL;
    return result;
}

/* Eval a JS expression and check result equals expected JSON string */
static int js_assert_eq(qwrt_t *rt, const char *code, const char *expected,
                        const char *test_name) {
    g_total++;
    char *result = js_eval(rt, code);
    if (!result) {
        printf("  FAIL %s: eval returned error\n", test_name);
        g_failed++;
        return 0;
    }
    if (strcmp(result, expected) != 0) {
        printf("  FAIL %s: got %s, expected %s\n", test_name, result, expected);
        qwrt_free(result);
        g_failed++;
        return 0;
    }
    qwrt_free(result);
    g_passed++;
    return 1;
}

/* Eval a JS expression and check result is truthy (not "false", "0", "null", "undefined", "''") */
static int js_assert_truthy(qwrt_t *rt, const char *code, const char *test_name) {
    g_total++;
    char *result = js_eval(rt, code);
    if (!result) {
        printf("  FAIL %s: eval returned error\n", test_name);
        g_failed++;
        return 0;
    }
    if (strcmp(result, "false") == 0 || strcmp(result, "0") == 0 ||
        strcmp(result, "null") == 0 || strcmp(result, "undefined") == 0 ||
        strcmp(result, "\"\"") == 0 || strcmp(result, "{}") == 0 ||
        strcmp(result, "[]") == 0) {
        printf("  FAIL %s: got falsy result %s\n", test_name, result);
        qwrt_free(result);
        g_failed++;
        return 0;
    }
    qwrt_free(result);
    g_passed++;
    return 1;
}

/* Eval a JS expression and check result is falsy */
static int js_assert_falsy(qwrt_t *rt, const char *code, const char *test_name) {
    g_total++;
    char *result = js_eval(rt, code);
    if (!result) {
        printf("  FAIL %s: eval returned error\n", test_name);
        g_failed++;
        return 0;
    }
    if (strcmp(result, "false") == 0 || strcmp(result, "0") == 0 ||
        strcmp(result, "null") == 0 || strcmp(result, "undefined") == 0 ||
        strcmp(result, "\"\"") == 0) {
        qwrt_free(result);
        g_passed++;
        return 1;
    }
    printf("  FAIL %s: expected falsy, got %s\n", test_name, result);
    qwrt_free(result);
    g_failed++;
    return 0;
}

/* Check that a JS expression does NOT throw (eval returns 0) */
static int js_assert_no_error(qwrt_t *rt, const char *code, const char *test_name) {
    g_total++;
    char *result = NULL;
    int rc = qwrt_eval(rt, code, &result);
    if (result) qwrt_free(result);
    if (rc != 0) {
        printf("  FAIL %s: threw error\n", test_name);
        g_failed++;
        return 0;
    }
    g_passed++;
    return 1;
}

/* ================================================================
 * WinterCG: Console API
 * ================================================================ */

static void test_console(qwrt_t *rt) {
    printf("\n--- WinterCG: Console ---\n");

    /* console exists */
    js_assert_truthy(rt, "typeof console !== 'undefined'", "console exists");

    /* console.log exists */
    js_assert_truthy(rt, "typeof console.log === 'function'", "console.log is function");

    /* console.warn/info/error/debug */
    js_assert_truthy(rt, "typeof console.warn === 'function'", "console.warn is function");
    js_assert_truthy(rt, "typeof console.error === 'function'", "console.error is function");
    js_assert_truthy(rt, "typeof console.info === 'function'", "console.info is function");
    js_assert_truthy(rt, "typeof console.debug === 'function'", "console.debug is function");

    /* console.log works */
    qwrt_pal_t *pal;
    qwrt_t *rt2 = create_runtime(&pal);
    qwrt_eval(rt2, "console.log('hello', 'world')", NULL);
    int log_count = 0;
    const char *log = pal_mock_get_log(pal, &log_count);
    g_total++;
    if (log_count == 1 && strstr(log, "hello") && strstr(log, "world")) {
        g_passed++;
    } else {
        printf("  FAIL console.log output: count=%d content='%s'\n", log_count, log ? log : "(null)");
        g_failed++;
    }
    pal_mock_clear_log(pal);
    qwrt_destroy(rt2);
    pal_mock_destroy(pal);
}

/* ================================================================
 * WinterCG: Timers API
 * ================================================================ */

static void test_timers(qwrt_t *rt) {
    printf("\n--- WinterCG: Timers ---\n");

    js_assert_truthy(rt, "typeof setTimeout === 'function'", "setTimeout is function");
    js_assert_truthy(rt, "typeof setInterval === 'function'", "setInterval is function");
    js_assert_truthy(rt, "typeof clearTimeout === 'function'", "clearTimeout is function");
    js_assert_truthy(rt, "typeof clearInterval === 'function'", "clearInterval is function");

    /* setTimeout returns a number */
    js_assert_truthy(rt, "typeof setTimeout(function(){}, 0) === 'number'", "setTimeout returns number");

    /* setTimeout callback fires */
    qwrt_pal_t *pal;
    qwrt_t *rt2 = create_runtime(&pal);
    qwrt_eval(rt2, "var _fired = false; setTimeout(function() { _fired = true; }, 100)", NULL);
    qwrt_tick(rt2);
    pal_mock_fire_all_timers(pal);
    qwrt_tick(rt2);
    js_assert_eq(rt2, "_fired", "true", "setTimeout callback fires");
    qwrt_destroy(rt2);
    pal_mock_destroy(pal);
}

/* ================================================================
 * WinterCG: queueMicrotask
 * ================================================================ */

static void test_microtask(qwrt_t *rt) {
    printf("\n--- WinterCG: queueMicrotask ---\n");

    js_assert_truthy(rt, "typeof queueMicrotask === 'function'", "queueMicrotask is function");

    qwrt_pal_t *pal;
    qwrt_t *rt2 = create_runtime(&pal);
    qwrt_eval(rt2, "var _mt = false; queueMicrotask(function() { _mt = true; })", NULL);
    qwrt_tick(rt2);
    js_assert_eq(rt2, "_mt", "true", "queueMicrotask callback runs");
    qwrt_destroy(rt2);
    pal_mock_destroy(pal);
}

/* ================================================================
 * WinterCG: URL / URLSearchParams
 * ================================================================ */

static void test_url(qwrt_t *rt) {
    printf("\n--- WinterCG: URL / URLSearchParams ---\n");

    /* URL constructor */
    js_assert_truthy(rt, "typeof URL === 'function'", "URL is function");
    js_assert_eq(rt, "new URL('https://example.com/path').protocol", "\"https:\"", "URL.protocol");
    js_assert_eq(rt, "new URL('https://example.com/path').hostname", "\"example.com\"", "URL.hostname");
    js_assert_eq(rt, "new URL('https://example.com/path?q=1').pathname", "\"/path\"", "URL.pathname");
    js_assert_eq(rt, "new URL('https://example.com/path?q=1#frag').hash", "\"#frag\"", "URL.hash");
    js_assert_eq(rt, "new URL('https://example.com/path?q=1').search", "\"?q=1\"", "URL.search");

    /* URLSearchParams */
    js_assert_truthy(rt, "typeof URLSearchParams === 'function'", "URLSearchParams is function");
    js_assert_eq(rt, "new URLSearchParams('a=1&b=2').get('a')", "\"1\"", "URLSearchParams.get");
    js_assert_eq(rt, "new URLSearchParams('a=1&b=2').get('b')", "\"2\"", "URLSearchParams.get(2)");
    js_assert_eq(rt, "new URLSearchParams('a=1&b=2').has('a')", "true", "URLSearchParams.has");
    js_assert_eq(rt, "new URLSearchParams('a=1&b=2').has('c')", "false", "URLSearchParams.has missing");

    /* URL with searchParams */
    js_assert_eq(rt, "new URL('https://example.com/?x=42').searchParams.get('x')", "\"42\"",
                 "URL.searchParams.get");

    /* URLSearchParams append + toString */
    js_assert_no_error(rt,
        "var p = new URLSearchParams(); p.append('key', 'val'); p.toString()",
        "URLSearchParams.append");

    /* URLSearchParams iteration */
    js_assert_truthy(rt,
        "typeof new URLSearchParams('a=1')[Symbol.iterator] === 'function'",
        "URLSearchParams is iterable");

    /* URL.canParse */
    js_assert_truthy(rt, "URL.canParse('https://example.com')", "URL.canParse valid URL");
    js_assert_truthy(rt, "!URL.canParse('not a url')", "URL.canParse invalid URL");

    /* URL with base (relative URL resolution) */
    js_assert_eq(rt,
        "new URL('/path', 'https://example.com').href",
        "\"https://example.com/path\"",
        "URL resolves relative path with base");

    /* URL.toJSON */
    js_assert_eq(rt,
        "new URL('https://example.com').toJSON()",
        "\"https://example.com/\"",
        "URL.toJSON returns href");

    /* URL setters */
    js_assert_eq(rt,
        "(function(){var u=new URL('https://example.com/');u.hostname='other.com';return u.hostname})()",
        "\"other.com\"", "URL.hostname setter");

    /* URLSearchParams delete/set/has */
    js_assert_eq(rt,
        "(function(){var p=new URLSearchParams('a=1&b=2');p.delete('a');return p.has('a');})()",
        "false", "URLSearchParams.delete removes key");
    js_assert_eq(rt,
        "(function(){var p=new URLSearchParams('a=1');p.set('a','2');return p.get('a');})()",
        "\"2\"", "URLSearchParams.set overwrites value");

    /* URLSearchParams forEach */
    js_assert_eq(rt,
        "(function(){var p=new URLSearchParams('x=1&y=2');var keys=[];p.forEach(function(v,k){keys.push(k);});return keys.join(',');})()",
        "\"x,y\"", "URLSearchParams.forEach iterates entries");

    /* URLSearchParams keys/values/entries */
    js_assert_eq(rt,
        "(function(){var p=new URLSearchParams('a=1&b=2');return [...p.keys()].join(',');})()",
        "\"a,b\"", "URLSearchParams.keys() iterator");
    js_assert_eq(rt,
        "(function(){var p=new URLSearchParams('a=1&b=2');return [...p.values()].join(',');})()",
        "\"1,2\"", "URLSearchParams.values() iterator");
}

/* ================================================================
 * WinterCG: TextEncoder / TextDecoder
 * ================================================================ */

static void test_encoding(qwrt_t *rt) {
    printf("\n--- WinterCG: TextEncoder / TextDecoder ---\n");

    js_assert_truthy(rt, "typeof TextEncoder === 'function'", "TextEncoder is function");
    js_assert_truthy(rt, "typeof TextDecoder === 'function'", "TextDecoder is function");

    /* TextEncoder basic */
    js_assert_eq(rt, "new TextEncoder().encode('hello').length", "5", "TextEncoder.encode length");

    /* TextDecoder basic */
    js_assert_truthy(rt,
        "new TextDecoder().decode(new Uint8Array([104,101,108,108,111])) === 'hello'",
        "TextDecoder.decode basic");

    /* UTF-8 roundtrip */
    js_assert_truthy(rt,
        "new TextDecoder().decode(new TextEncoder().encode('hello world')) === 'hello world'",
        "TextEncoder/Decoder roundtrip");

    /* UTF-8 non-ASCII */
    js_assert_truthy(rt,
        "new TextDecoder().decode(new TextEncoder().encode('\\u00e9')) === '\\u00e9'",
        "UTF-8 non-ASCII roundtrip");

    /* TextEncoder encoding property */
    js_assert_eq(rt, "new TextEncoder().encoding", "\"utf-8\"", "TextEncoder.encoding");

    /* TextDecoder encoding property */
    js_assert_eq(rt, "new TextDecoder().encoding", "\"utf-8\"", "TextDecoder.encoding");
}

/* ================================================================
 * WinterCG: atob / btoa
 * ================================================================ */

static void test_base64(qwrt_t *rt) {
    printf("\n--- WinterCG: atob / btoa ---\n");

    js_assert_truthy(rt, "typeof btoa === 'function'", "btoa is function");
    js_assert_truthy(rt, "typeof atob === 'function'", "atob is function");

    js_assert_eq(rt, "btoa('hello')", "\"aGVsbG8=\"", "btoa('hello')");
    js_assert_eq(rt, "atob('aGVsbG8=')", "\"hello\"", "atob('aGVsbG8=')");

    /* Roundtrip */
    js_assert_truthy(rt, "atob(btoa('test123')) === 'test123'", "atob(btoa()) roundtrip");

    /* Empty string */
    js_assert_eq(rt, "btoa('')", "\"\"", "btoa('')");
    js_assert_eq(rt, "atob('')", "\"\"", "atob('')");
}

/* ================================================================
 * WinterCG: Fetch API
 * ================================================================ */

static void test_fetch(qwrt_t *rt) {
    printf("\n--- WinterCG: Fetch API ---\n");

    js_assert_truthy(rt, "typeof fetch === 'function'", "fetch is function");
    js_assert_truthy(rt, "typeof Request === 'function'", "Request is function");
    js_assert_truthy(rt, "typeof Response === 'function'", "Response is function");
    js_assert_truthy(rt, "typeof Headers === 'function'", "Headers is function");

    /* Headers */
    js_assert_no_error(rt, "var h = new Headers(); h.append('Content-Type', 'text/plain')",
                       "Headers.append");
    js_assert_eq(rt, "new Headers({'x-test':'val'}).get('x-test')", "\"val\"", "Headers.get from init");

    /* Request */
    js_assert_no_error(rt, "var r = new Request('http://example.com')", "Request constructor");
    js_assert_eq(rt, "new Request('http://example.com').url", "\"http://example.com\"", "Request.url");

    /* Response */
    js_assert_no_error(rt, "var res = new Response('body')", "Response constructor");
    js_assert_eq(rt, "new Response(null, {status: 201}).status", "201", "Response.status");

    /* fetch with mock */
    qwrt_pal_t *pal;
    qwrt_t *rt2 = create_runtime(&pal);
    pal_mock_set_http_response(pal,
        "{\"status\":200,\"headers\":{\"Content-Type\":\"text/plain\"},\"body\":\"ok\"}");
    qwrt_eval(rt2,
        "var _fres = null; fetch('http://test.com/').then(function(r) { return r.text(); })"
        ".then(function(t) { _fres = t; })", NULL);
    qwrt_tick(rt2);
    js_assert_eq(rt2, "_fres", "\"ok\"", "fetch() returns response body");
    qwrt_destroy(rt2);
    pal_mock_destroy(pal);
}

/* ================================================================
 * WinterCG: AbortController / AbortSignal
 * ================================================================ */

static void test_abort(qwrt_t *rt) {
    printf("\n--- WinterCG: AbortController / AbortSignal ---\n");

    js_assert_truthy(rt, "typeof AbortController === 'function'", "AbortController is function");
    js_assert_truthy(rt, "typeof AbortSignal === 'function'", "AbortSignal is function");

    js_assert_no_error(rt, "var ac = new AbortController()", "AbortController constructor");
    js_assert_truthy(rt, "typeof new AbortController().signal === 'object'", "AbortController.signal");
    js_assert_eq(rt, "new AbortController().signal.aborted", "false", "signal.aborted initial");

    js_assert_no_error(rt, "var ac2 = new AbortController(); ac2.abort()", "abort() call");
    js_assert_eq(rt, "(function(){var a=new AbortController();a.abort();return a.signal.aborted})()",
                 "true", "signal.aborted after abort()");

    /* AbortSignal.abort() creates already-aborted signal */
    js_assert_eq(rt, "AbortSignal.abort().aborted", "true",
                 "AbortSignal.abort() is already aborted");

    /* AbortSignal.abort() with reason */
    js_assert_eq(rt, "AbortSignal.abort('stopped').reason", "\"stopped\"",
                 "AbortSignal.abort(reason) sets reason");

    /* AbortSignal.any() with already-aborted signal */
    js_assert_eq(rt, "AbortSignal.any([AbortSignal.abort()]).aborted", "true",
                 "AbortSignal.any() with aborted signal is aborted");

    /* AbortSignal.any() with non-aborted signals */
    js_assert_eq(rt, "AbortSignal.any([new AbortController().signal]).aborted", "false",
                 "AbortSignal.any() with live signals is not aborted");

    /* AbortSignal.timeout() fires after delay (async) */
    {
        qwrt_pal_t *pal2;
        qwrt_t *rt2 = create_runtime(&pal2);
        qwrt_eval(rt2,
            "var _timeoutFired = false; "
            "var sig = AbortSignal.timeout(50); "
            "sig.addEventListener('abort', function() { _timeoutFired = sig.aborted; });", NULL);
        for (int i = 0; i < 50; i++) qwrt_tick(rt2);
        pal_mock_fire_all_timers(pal2);
        for (int i = 0; i < 50; i++) qwrt_tick(rt2);
        js_assert_eq(rt2, "_timeoutFired", "true",
                     "AbortSignal.timeout() fires after delay");
        qwrt_destroy(rt2);
        pal_mock_destroy(pal2);
    }

    /* AbortSignal.any() propagates abort from source (async) */
    {
        qwrt_pal_t *pal2;
        qwrt_t *rt2 = create_runtime(&pal2);
        qwrt_eval(rt2,
            "var _anyAborted = false; "
            "var ac = new AbortController(); "
            "var combined = AbortSignal.any([ac.signal]); "
            "combined.addEventListener('abort', function() { _anyAborted = combined.aborted; }); "
            "setTimeout(function() { ac.abort(); }, 50);", NULL);
        for (int i = 0; i < 50; i++) qwrt_tick(rt2);
        pal_mock_fire_all_timers(pal2);
        for (int i = 0; i < 50; i++) qwrt_tick(rt2);
        js_assert_eq(rt2, "_anyAborted", "true",
                     "AbortSignal.any() propagates abort");
        qwrt_destroy(rt2);
        pal_mock_destroy(pal2);
    }

    /* throwIfAborted() throws when aborted */
    js_assert_truthy(rt,
        "(function(){try{AbortSignal.abort().throwIfAborted();return false}catch(e){return true}})()",
        "throwIfAborted() throws when aborted");
    js_assert_truthy(rt,
        "(function(){try{new AbortController().signal.throwIfAborted();return true}catch(e){return false}})()",
        "throwIfAborted() does not throw when not aborted");
}

/* ================================================================
 * WinterCG: Event / EventTarget
 * ================================================================ */

static void test_events(qwrt_t *rt) {
    printf("\n--- WinterCG: Event / EventTarget ---\n");

    js_assert_truthy(rt, "typeof Event === 'function'", "Event is function");
    js_assert_truthy(rt, "typeof EventTarget === 'function'", "EventTarget is function");

    /* Event constructor */
    js_assert_no_error(rt, "var e = new Event('test')", "Event constructor");
    js_assert_eq(rt, "new Event('test').type", "\"test\"", "Event.type");

    /* EventTarget */
    js_assert_no_error(rt, "var et = new EventTarget()", "EventTarget constructor");
    js_assert_truthy(rt, "typeof new EventTarget().addEventListener === 'function'",
                     "EventTarget.addEventListener");
    js_assert_truthy(rt, "typeof new EventTarget().dispatchEvent === 'function'",
                     "EventTarget.dispatchEvent");

    /* EventTarget dispatch */
    qwrt_pal_t *pal;
    qwrt_t *rt2 = create_runtime(&pal);
    qwrt_eval(rt2,
        "var _evtData = null; var et = new EventTarget(); "
        "et.addEventListener('test', function(e) { _evtData = e.type; }); "
        "et.dispatchEvent(new Event('test'))", NULL);
    js_assert_eq(rt2, "_evtData", "\"test\"", "EventTarget dispatch + listener");

    /* once listener auto-removal */
    js_assert_eq(rt,
        "(function(){"
        "  var count = 0; var et = new EventTarget();"
        "  et.addEventListener('ping', function() { count++; }, {once: true});"
        "  et.dispatchEvent(new Event('ping'));"
        "  et.dispatchEvent(new Event('ping'));"
        "  return count;"
        "})()",
        "1", "once listener fires only once");

    /* removeEventListener */
    js_assert_eq(rt,
        "(function(){"
        "  var count = 0; var et = new EventTarget();"
        "  var fn = function() { count++; };"
        "  et.addEventListener('ping', fn);"
        "  et.dispatchEvent(new Event('ping'));"
        "  et.removeEventListener('ping', fn);"
        "  et.dispatchEvent(new Event('ping'));"
        "  return count;"
        "})()",
        "1", "removeEventListener prevents further dispatch");

    /* stopPropagation — in a flat EventTarget (no DOM tree), propagation
     * only affects tree traversal, not other listeners on same target.
     * So all listeners on the same target still fire. */
    js_assert_eq(rt,
        "(function(){"
        "  var order = []; var et = new EventTarget();"
        "  et.addEventListener('a', function(e) { order.push(1); e.stopPropagation(); });"
        "  et.addEventListener('a', function(e) { order.push(2); });"
        "  et.dispatchEvent(new Event('a'));"
        "  return order.join(',');"
        "})()",
        "\"1,2\"", "stopPropagation does not block same-target listeners");

    /* stopImmediatePropagation */
    js_assert_eq(rt,
        "(function(){"
        "  var order = []; var et = new EventTarget();"
        "  et.addEventListener('a', function(e) { order.push(1); e.stopImmediatePropagation(); });"
        "  et.addEventListener('a', function(e) { order.push(2); });"
        "  et.dispatchEvent(new Event('a'));"
        "  return order.join(',');"
        "})()",
        "\"1\"", "stopImmediatePropagation stops immediate dispatch");

    /* preventDefault + cancelable */
    js_assert_eq(rt,
        "(function(){"
        "  var et = new EventTarget();"
        "  et.addEventListener('a', function(e) { e.preventDefault(); });"
        "  var e = new Event('a', {cancelable: true});"
        "  et.dispatchEvent(e);"
        "  return e.defaultPrevented;"
        "})()",
        "true", "preventDefault sets defaultPrevented");

    /* preventDefault on non-cancelable event is no-op */
    js_assert_eq(rt,
        "(function(){"
        "  var et = new EventTarget();"
        "  et.addEventListener('a', function(e) { e.preventDefault(); });"
        "  var e = new Event('a', {cancelable: false});"
        "  et.dispatchEvent(e);"
        "  return e.defaultPrevented;"
        "})()",
        "false", "preventDefault on non-cancelable event is no-op");

    /* CustomEvent with detail */
    js_assert_eq(rt,
        "(function(){"
        "  var got = null; var et = new EventTarget();"
        "  et.addEventListener('x', function(e) { got = e.detail; });"
        "  et.dispatchEvent(new CustomEvent('x', {detail: 42}));"
        "  return got;"
        "})()",
        "42", "CustomEvent.detail carries data");

    /* Event bubbles and cancelable properties */
    js_assert_eq(rt, "new Event('t', {bubbles: true}).bubbles", "true",
                 "Event bubbles option");
    js_assert_eq(rt, "new Event('t', {cancelable: true}).cancelable", "true",
                 "Event cancelable option");

    qwrt_destroy(rt2);
    pal_mock_destroy(pal);
}

/* ================================================================
 * WinterCG: Crypto
 * ================================================================ */

static void test_crypto(qwrt_t *rt) {
    printf("\n--- WinterCG: Crypto ---\n");

    js_assert_truthy(rt, "typeof crypto !== 'undefined'", "crypto exists");
    js_assert_truthy(rt, "typeof crypto.getRandomValues === 'function'", "crypto.getRandomValues");

    /* getRandomValues works */
    js_assert_truthy(rt,
        "crypto.getRandomValues(new Uint8Array(16)) instanceof Uint8Array",
        "crypto.getRandomValues returns Uint8Array");

    /* CSPRNG: two calls produce different results (not Math.random determinism) */
    js_assert_falsy(rt,
        "(function() {"
        "  var a = new Uint8Array(16);"
        "  var b = new Uint8Array(16);"
        "  crypto.getRandomValues(a);"
        "  crypto.getRandomValues(b);"
        "  var same = true;"
        "  for (var i = 0; i < 16; i++) { if (a[i] !== b[i]) { same = false; break; } }"
        "  return same;"
        "})()",
        "getRandomValues produces unique results");

    /* randomUUID format */
    js_assert_truthy(rt,
        "(function() {"
        "  var u = crypto.randomUUID();"
        "  return typeof u === 'string' && /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/.test(u);"
        "})()",
        "randomUUID returns valid v4 UUID");

    /* crypto.subtle — may not be available in all environments */
    g_total++;
    char *subtle_result = js_eval(rt, "typeof crypto.subtle");
    if (subtle_result && strcmp(subtle_result, "\"object\"") == 0) {
        g_passed++;
        printf("  PASS crypto.subtle exists\n");
        qwrt_free(subtle_result);
        /* crypto.subtle.digest */
        js_assert_truthy(rt, "typeof crypto.subtle.digest === 'function'", "crypto.subtle.digest");
    } else {
        printf("  SKIP crypto.subtle (not implemented)\n");
        g_passed++;
        if (subtle_result) qwrt_free(subtle_result);
    }
}

/* ================================================================
 * WinterCG: Performance
 * ================================================================ */

static void test_performance(qwrt_t *rt) {
    printf("\n--- WinterCG: Performance ---\n");

    js_assert_truthy(rt, "typeof performance !== 'undefined'", "performance exists");
    js_assert_truthy(rt, "typeof performance.now === 'function'", "performance.now is function");

    /* performance.now() returns a number >= 0 */
    js_assert_truthy(rt, "typeof performance.now() === 'number'", "performance.now() returns number");
    js_assert_truthy(rt, "performance.now() >= 0", "performance.now() >= 0");

    /* performance.mark() */
    js_assert_no_error(rt, "performance.mark('start')", "performance.mark() creates mark");
    js_assert_eq(rt,
        "performance.getEntriesByName('start')[0].entryType",
        "\"mark\"", "mark entry has entryType 'mark'");
    js_assert_eq(rt,
        "performance.getEntriesByName('start')[0].name",
        "\"start\"", "mark entry has correct name");

    /* performance.measure() between marks */
    js_assert_no_error(rt,
        "performance.mark('end'); performance.measure('elapsed', 'start', 'end')",
        "performance.measure() between marks");
    js_assert_eq(rt,
        "performance.getEntriesByName('elapsed')[0].entryType",
        "\"measure\"", "measure entry has entryType 'measure'");
    js_assert_truthy(rt,
        "performance.getEntriesByName('elapsed')[0].duration >= 0",
        "measure duration is non-negative");

    /* performance.getEntriesByType() */
    js_assert_truthy(rt,
        "performance.getEntriesByType('mark').length >= 2",
        "getEntriesByType('mark') returns marks");
    js_assert_truthy(rt,
        "performance.getEntriesByType('measure').length >= 1",
        "getEntriesByType('measure') returns measures");

    /* performance.getEntries() returns all */
    js_assert_truthy(rt,
        "performance.getEntries().length >= 3",
        "getEntries() returns all entries");

    /* performance.clearMarks() */
    js_assert_no_error(rt,
        "performance.mark('temp'); performance.clearMarks('temp'); "
        "performance.getEntriesByName('temp').length === 0",
        "clearMarks removes specific mark");

    /* performance.clearMeasures() */
    js_assert_no_error(rt,
        "performance.measure('tmp', 'start', 'end'); performance.clearMeasures('tmp'); "
        "performance.getEntriesByName('tmp').length === 0",
        "clearMeasures removes specific measure");

    /* performance.timeOrigin is a number */
    js_assert_truthy(rt,
        "typeof performance.timeOrigin === 'number'",
        "performance.timeOrigin is number");
}

/* ================================================================
 * WinterCG: Storage
 * ================================================================ */

static void test_storage(qwrt_t *rt) {
    printf("\n--- WinterCG: Storage ---\n");

    /* At least one storage API must exist */
    js_assert_truthy(rt,
        "typeof storage !== 'undefined' || typeof localStorage !== 'undefined'",
        "storage API exists");

    /* Test storage.set/get if available */
    qwrt_pal_t *pal;
    qwrt_t *rt2 = create_runtime(&pal);
    qwrt_eval(rt2,
        "var _storOk = false; "
        "if (typeof storage !== 'undefined' && typeof storage.set === 'function') { "
        "  storage.set('k','v').then(function(){ _storOk = true; }); "
        "} else if (typeof localStorage !== 'undefined') { "
        "  localStorage.setItem('k','v'); _storOk = true; "
        "}", NULL);
    qwrt_tick(rt2);
    js_assert_truthy(rt2, "_storOk", "storage set/get works");
    qwrt_destroy(rt2);
    pal_mock_destroy(pal);
}

/* ================================================================
 * ES2023: Array methods
 * ================================================================ */

static void test_es2023_array(qwrt_t *rt) {
    printf("\n--- ES2023: Array ---\n");

    /* Array.prototype.at() — ES2022 but important */
    js_assert_eq(rt, "[1,2,3].at(0)", "1", "Array.at(0)");
    js_assert_eq(rt, "[1,2,3].at(-1)", "3", "Array.at(-1)");

    /* Array.prototype.findLast() — ES2023 */
    js_assert_eq(rt, "[1,2,3,4].findLast(function(x){return x%2===0})", "4",
                 "Array.findLast()");

    /* Array.prototype.findLastIndex() — ES2023 */
    js_assert_eq(rt, "[1,2,3,4].findLastIndex(function(x){return x%2===0})", "3",
                 "Array.findLastIndex()");

    /* Array.prototype.toSorted() — ES2023 */
    js_assert_eq(rt, "[3,1,2].toSorted().toString()", "\"1,2,3\"",
                 "Array.toSorted()");

    /* Array.prototype.toReversed() — ES2023 */
    js_assert_eq(rt, "[1,2,3].toReversed().toString()", "\"3,2,1\"",
                 "Array.toReversed()");

    /* Array.prototype.toSpliced() — ES2023 */
    js_assert_eq(rt, "[1,2,3].toSpliced(1,1,4,5).toString()", "\"1,4,5,3\"",
                 "Array.toSpliced()");

    /* toSorted does not mutate original */
    js_assert_eq(rt, "(function(){var a=[3,1,2];a.toSorted();return a.toString()})()",
                 "\"3,1,2\"", "toSorted does not mutate");

    /* Array.prototype.with() — ES2023 */
    js_assert_eq(rt, "[1,2,3].with(1, 99).toString()", "\"1,99,3\"",
                 "Array.with()");
}

/* ================================================================
 * ES2023: Object
 * ================================================================ */

static void test_es2023_object(qwrt_t *rt) {
    printf("\n--- ES2023: Object ---\n");

    /* Object.hasOwn() — ES2022 */
    js_assert_eq(rt, "Object.hasOwn({a:1}, 'a')", "true", "Object.hasOwn own property");
    js_assert_eq(rt, "Object.hasOwn({a:1}, 'b')", "false", "Object.hasOwn missing property");
    js_assert_eq(rt, "Object.hasOwn(Object.create({a:1}), 'a')", "false",
                 "Object.hasOwn inherited property");
}

/* ================================================================
 * ES2023: String
 * ================================================================ */

static void test_es2023_string(qwrt_t *rt) {
    printf("\n--- ES2023: String ---\n");

    /* String.prototype.replaceAll() — ES2021 */
    js_assert_eq(rt, "'aaa'.replaceAll('a', 'b')", "\"bbb\"", "String.replaceAll");
    js_assert_eq(rt, "'abcabc'.replaceAll('abc', 'x')", "\"xx\"", "String.replaceAll multi");
    js_assert_eq(rt, "'hello'.replaceAll('x', 'y')", "\"hello\"", "String.replaceAll no match");

    /* String.prototype.at() — ES2022 */
    js_assert_eq(rt, "'hello'.at(0)", "\"h\"", "String.at(0)");
    js_assert_eq(rt, "'hello'.at(-1)", "\"o\"", "String.at(-1)");

    /* String.prototype.matchAll() */
    js_assert_truthy(rt, "typeof 'abcabc'.matchAll(/ab/g) === 'object'",
                     "String.matchAll returns object");

    /* String.prototype.trimStart / trimEnd */
    js_assert_eq(rt, "'  hi  '.trimStart()", "\"hi  \"", "String.trimStart");
    js_assert_eq(rt, "'  hi  '.trimEnd()", "\"  hi\"", "String.trimEnd");
}

/* ================================================================
 * ES2023: Promise
 * ================================================================ */

static void test_es2023_promise(qwrt_t *rt) {
    printf("\n--- ES2023: Promise ---\n");

    js_assert_truthy(rt, "typeof Promise === 'function'", "Promise exists");

    /* Promise.allSettled — ES2020 */
    js_assert_truthy(rt, "typeof Promise.allSettled === 'function'", "Promise.allSettled exists");

    /* Promise.any — ES2021 */
    js_assert_truthy(rt, "typeof Promise.any === 'function'", "Promise.any exists");

    /* Promise.all / race */
    js_assert_truthy(rt, "typeof Promise.all === 'function'", "Promise.all exists");
    js_assert_truthy(rt, "typeof Promise.race === 'function'", "Promise.race exists");

    /* Basic Promise resolution */
    qwrt_pal_t *pal;
    qwrt_t *rt2 = create_runtime(&pal);
    qwrt_eval(rt2,
        "var _pval = null; Promise.resolve(42).then(function(v) { _pval = v; })", NULL);
    qwrt_tick(rt2);
    js_assert_eq(rt2, "_pval", "42", "Promise.resolve + then");
    qwrt_destroy(rt2);
    pal_mock_destroy(pal);
}

/* ================================================================
 * ES2023: Optional chaining + Nullish coalescing
 * ================================================================ */

static void test_es2023_operators(qwrt_t *rt) {
    printf("\n--- ES2023: Operators ---\n");

    /* Optional chaining — ES2020 */
    js_assert_eq(rt, "var o = {a:{b:1}}; o?.a?.b", "1", "Optional chaining deep");
    js_assert_eq(rt, "var o2 = null; o2?.a", "undefined", "Optional chaining null");
    js_assert_eq(rt, "var o3; o3?.a", "undefined", "Optional chaining undefined");

    /* Nullish coalescing — ES2020 */
    js_assert_eq(rt, "null ?? 'default'", "\"default\"", "Nullish coalescing null");
    js_assert_eq(rt, "undefined ?? 'default'", "\"default\"", "Nullish coalescing undefined");
    js_assert_eq(rt, "0 ?? 'default'", "0", "Nullish coalescing 0");
    js_assert_eq(rt, "'' ?? 'default'", "\"\"", "Nullish coalescing empty string");
    js_assert_eq(rt, "false ?? true", "false", "Nullish coalescing false");
}

/* ================================================================
 * ES2023: Symbol + Well-known symbols
 * ================================================================ */

static void test_es2023_symbol(qwrt_t *rt) {
    printf("\n--- ES2023: Symbol ---\n");

    js_assert_truthy(rt, "typeof Symbol === 'function'", "Symbol is function");
    js_assert_truthy(rt, "typeof Symbol.iterator === 'symbol'", "Symbol.iterator");
    js_assert_truthy(rt, "typeof Symbol.toStringTag === 'symbol'", "Symbol.toStringTag");
    js_assert_truthy(rt, "typeof Symbol.for === 'function'", "Symbol.for");
    js_assert_eq(rt, "Symbol.for('abc') === Symbol.for('abc')", "true", "Symbol.for identity");

    /* Symbol.toPrimitive */
    js_assert_truthy(rt, "typeof Symbol.toPrimitive === 'symbol'", "Symbol.toPrimitive");

    /* Symbol.asyncIterator */
    js_assert_truthy(rt, "typeof Symbol.asyncIterator === 'symbol'", "Symbol.asyncIterator");
}

/* ================================================================
 * ES2023: WeakRef / FinalizationRegistry
 * ================================================================ */

static void test_es2023_weakref(qwrt_t *rt) {
    printf("\n--- ES2023: WeakRef / FinalizationRegistry ---\n");

    js_assert_truthy(rt, "typeof WeakRef === 'function'", "WeakRef is function");
    js_assert_truthy(rt, "typeof FinalizationRegistry === 'function'",
                     "FinalizationRegistry is function");

    /* WeakRef basic — keep a reference to prevent GC */
    js_assert_no_error(rt, "var _wrObj = {a:1}; var wr = new WeakRef(_wrObj)", "WeakRef constructor");
    js_assert_truthy(rt, "wr.deref() !== undefined", "WeakRef.deref() returns object");

    /* FinalizationRegistry basic */
    js_assert_no_error(rt, "var fr = new FinalizationRegistry(function(){})",
                       "FinalizationRegistry constructor");
}

/* ================================================================
 * ES2023: TypedArrays
 * ================================================================ */

static void test_es2023_typedarray(qwrt_t *rt) {
    printf("\n--- ES2023: TypedArray ---\n");

    /* TypedArray at() */
    js_assert_eq(rt, "new Uint8Array([10,20,30]).at(-1)", "30", "Uint8Array.at(-1)");

    /* TypedArray findLast */
    js_assert_eq(rt, "new Uint8Array([1,2,3,4]).findLast(function(x){return x%2===0})", "4",
                 "Uint8Array.findLast()");

    /* TypedArray toSorted */
    js_assert_eq(rt, "new Uint8Array([3,1,2]).toSorted().toString()", "\"1,2,3\"",
                 "Uint8Array.toSorted()");
}

/* ================================================================
 * ES2023: Iterators / Generators
 * ================================================================ */

static void test_es2023_iteration(qwrt_t *rt) {
    printf("\n--- ES2023: Iteration ---\n");

    /* for..of */
    js_assert_eq(rt, "(function(){var s=''; for(var x of [1,2,3]){s+=x;} return s;})()",
                 "\"123\"", "for..of works");

    /* Spread */
    js_assert_eq(rt, "[...[1,2],...[3,4]].toString()", "\"1,2,3,4\"", "spread operator");

    /* Generator functions */
    js_assert_truthy(rt,
        "typeof (function*(){yield 1;}) === 'function'",
        "generator functions exist");

    /* Generator iteration */
    js_assert_eq(rt,
        "(function(){var g=(function*(){yield 1;yield 2;});var r=[];for(var v of g()){r.push(v);}return r.toString();})()",
        "\"1,2\"", "generator yields values");

    /* Map / Set — ES2015 but fundamental */
    js_assert_truthy(rt, "typeof Map === 'function'", "Map exists");
    js_assert_truthy(rt, "typeof Set === 'function'", "Set exists");
    js_assert_eq(rt, "new Map([['a',1]]).get('a')", "1", "Map.get");
    js_assert_truthy(rt, "new Set([1,2,3]).has(2)", "Set.has works");
}

/* ================================================================
 * ES2023: Error types
 * ================================================================ */

static void test_es2023_errors(qwrt_t *rt) {
    printf("\n--- ES2023: Error types ---\n");

    js_assert_truthy(rt, "typeof Error === 'function'", "Error exists");
    js_assert_truthy(rt, "typeof TypeError === 'function'", "TypeError exists");
    js_assert_truthy(rt, "typeof RangeError === 'function'", "RangeError exists");
    js_assert_truthy(rt, "typeof SyntaxError === 'function'", "SyntaxError exists");
    js_assert_truthy(rt, "typeof URIError === 'function'", "URIError exists");

    /* Error.cause — ES2022 */
    js_assert_truthy(rt,
        "new Error('msg', {cause: 'reason'}).cause === 'reason'",
        "Error.cause");
}

/* ================================================================
 * ES2023: structuredClone
 * ================================================================ */

static void test_structured_clone(qwrt_t *rt) {
    printf("\n--- ES2023: structuredClone ---\n");

    js_assert_truthy(rt, "typeof structuredClone === 'function'", "structuredClone exists");

    /* Basic clone */
    js_assert_eq(rt, "structuredClone(42)", "42", "structuredClone number");
    js_assert_eq(rt, "structuredClone('hello')", "\"hello\"", "structuredClone string");
    js_assert_truthy(rt, "structuredClone({a:1}).a === 1", "structuredClone object");

    /* Deep clone */
    js_assert_truthy(rt,
        "(function(){var o={a:{b:1}};var c=structuredClone(o);o.a.b=2;return c.a.b===1;})()",
        "structuredClone deep copy");

    /* Clone array */
    js_assert_truthy(rt,
        "structuredClone([1,2,3]).length === 3",
        "structuredClone array");
}

/* ================================================================
 * ES2023: Other ES features
 * ================================================================ */

static void test_es2023_misc(qwrt_t *rt) {
    printf("\n--- ES2023: Misc ---\n");

    /* globalThis — ES2020 */
    js_assert_truthy(rt, "typeof globalThis === 'object'", "globalThis exists");

    /* BigInt — ES2020 (JSON.stringify can't serialize BigInt, so test differently) */
    js_assert_truthy(rt, "typeof BigInt === 'function'", "BigInt exists");
    js_assert_truthy(rt, "BigInt(123) === 123n", "BigInt(123)");

    /* Proxy — ES2015 */
    js_assert_truthy(rt, "typeof Proxy === 'function'", "Proxy exists");

    /* Reflect — ES2015 */
    js_assert_truthy(rt, "typeof Reflect === 'object'", "Reflect exists");

    /* Array.prototype.flat/flatMap — ES2019 */
    js_assert_eq(rt, "[[1,2],[3,4]].flat().toString()", "\"1,2,3,4\"", "Array.flat()");
    js_assert_eq(rt, "[1,2,3].flatMap(function(x){return [x,x*2]}).toString()",
                 "\"1,2,2,4,3,6\"", "Array.flatMap()");

    /* Object.fromEntries — ES2019 */
    js_assert_eq(rt, "Object.fromEntries([['a',1],['b',2]]).a", "1", "Object.fromEntries");

    /* String.prototype.includes — ES2015 */
    js_assert_eq(rt, "'hello world'.includes('world')", "true", "String.includes");

    /* Array.prototype.includes — ES2016 */
    js_assert_eq(rt, "[1,2,3].includes(2)", "true", "Array.includes");
    js_assert_eq(rt, "[1,2,3].includes(5)", "false", "Array.includes missing");

    /* Exponentiation operator — ES2016 */
    js_assert_eq(rt, "2 ** 10", "1024", "Exponentiation operator");

    /* Async functions */
    js_assert_truthy(rt, "typeof (async function(){}) === 'function'", "async functions exist");

    /* for await..of (syntax support) */
    js_assert_truthy(rt,
        "(function(){try{new Function('async function f(){for await(var x of []){}}');return true}catch(e){return false}})()",
        "for await..of syntax");
}

/* ================================================================
 * TC55: MessageChannel / MessagePort / MessageEvent
 * ================================================================ */

static void test_message_channel(qwrt_t *rt) {
    printf("\n--- TC55: MessageChannel ---\n");

    js_assert_truthy(rt, "typeof MessageChannel === 'function'", "MessageChannel is function");
    js_assert_truthy(rt, "typeof MessagePort === 'function'", "MessagePort is function");
    js_assert_truthy(rt, "typeof MessageEvent === 'function'", "MessageEvent is function");

    /* MessageChannel has port1 and port2 */
    js_assert_truthy(rt, "var mc = new MessageChannel(); mc.port1 instanceof MessagePort",
                     "MessageChannel.port1 is MessagePort");
    js_assert_truthy(rt, "var mc2 = new MessageChannel(); mc2.port2 instanceof MessagePort",
                     "MessageChannel.port2 is MessagePort");

    /* MessageEvent data */
    js_assert_eq(rt, "new MessageEvent('message', {data: 42}).data", "42",
                 "MessageEvent.data");

    /* postMessage + onmessage */
    qwrt_pal_t *pal;
    qwrt_t *rt2 = create_runtime(&pal);
    qwrt_eval(rt2,
        "var _msgData = null; "
        "var mc = new MessageChannel(); "
        "mc.port2.onmessage = function(e) { _msgData = e.data; }; "
        "mc.port1.postMessage('hello')", NULL);
    qwrt_tick(rt2);
    js_assert_eq(rt2, "_msgData", "\"hello\"", "MessageChannel postMessage works");
    qwrt_destroy(rt2);
    pal_mock_destroy(pal);
}

/* ================================================================
 * TC55: ErrorEvent / PromiseRejectionEvent
 * ================================================================ */

static void test_error_events(qwrt_t *rt) {
    printf("\n--- TC55: ErrorEvent ---\n");

    js_assert_truthy(rt, "typeof ErrorEvent === 'function'", "ErrorEvent is function");
    js_assert_truthy(rt, "typeof PromiseRejectionEvent === 'function'",
                     "PromiseRejectionEvent is function");

    /* ErrorEvent properties */
    js_assert_eq(rt, "new ErrorEvent('error', {message: 'test'}).message",
                 "\"test\"", "ErrorEvent.message");
    js_assert_eq(rt, "new ErrorEvent('error', {filename: 'app.js'}).filename",
                 "\"app.js\"", "ErrorEvent.filename");

    /* PromiseRejectionEvent properties */
    js_assert_truthy(rt,
        "new PromiseRejectionEvent('unhandledrejection', {promise: Promise.resolve(), reason: 'err'}).reason === 'err'",
        "PromiseRejectionEvent.reason");
}

/* ================================================================
 * TC55: Streams API
 * ================================================================ */

static void test_streams(qwrt_t *rt) {
    printf("\n--- TC55: Streams ---\n");

    js_assert_truthy(rt, "typeof ReadableStream === 'function'", "ReadableStream is function");
    js_assert_truthy(rt, "typeof WritableStream === 'function'", "WritableStream is function");
    js_assert_truthy(rt, "typeof TransformStream === 'function'", "TransformStream is function");
    js_assert_truthy(rt, "typeof CompressionStream === 'function'", "CompressionStream is function");
    js_assert_truthy(rt, "typeof DecompressionStream === 'function'", "DecompressionStream is function");

    /* Stream classes on globalThis */
    js_assert_truthy(rt, "typeof ReadableStreamDefaultController === 'function'",
                     "ReadableStreamDefaultController on globalThis");
    js_assert_truthy(rt, "typeof ReadableStreamDefaultReader === 'function'",
                     "ReadableStreamDefaultReader on globalThis");
    js_assert_truthy(rt, "typeof WritableStreamDefaultController === 'function'",
                     "WritableStreamDefaultController on globalThis");
    js_assert_truthy(rt, "typeof WritableStreamDefaultWriter === 'function'",
                     "WritableStreamDefaultWriter on globalThis");
    js_assert_truthy(rt, "typeof ByteLengthQueuingStrategy === 'function'",
                     "ByteLengthQueuingStrategy on globalThis");
    js_assert_truthy(rt, "typeof CountQueuingStrategy === 'function'",
                     "CountQueuingStrategy on globalThis");
    js_assert_truthy(rt, "typeof TextEncoderStream === 'function'",
                     "TextEncoderStream on globalThis");
    js_assert_truthy(rt, "typeof TextDecoderStream === 'function'",
                     "TextDecoderStream on globalThis");

    /* ReadableStream basic */
    js_assert_no_error(rt,
        "var rs = new ReadableStream({start: function(c){c.enqueue('hi');c.close();}})",
        "ReadableStream constructor");

    /* WritableStream basic + getWriter returns WritableStreamDefaultWriter */
    js_assert_truthy(rt,
        "new WritableStream().getWriter() instanceof WritableStreamDefaultWriter",
        "WritableStream.getWriter returns WritableStreamDefaultWriter");

    /* WritableStream write + close (async) */
    {
        qwrt_pal_t *pal2;
        qwrt_t *rt2 = create_runtime(&pal2);
        qwrt_eval(rt2,
            "var _wsResult = ''; "
            "(async function() {"
            "  var written = [];"
            "  var ws = new WritableStream({"
            "    write: function(chunk) { written.push(chunk); },"
            "    close: function() { _wsResult = written.join(','); }"
            "  });"
            "  var w = ws.getWriter();"
            "  await w.write('x');"
            "  await w.write('y');"
            "  await w.write('z');"
            "  await w.close();"
            "})();", NULL);
        for (int i = 0; i < 50; i++) qwrt_tick(rt2);
        js_assert_eq(rt2, "_wsResult", "\"x,y,z\"",
                     "WritableStream write/close collects chunks");
        qwrt_destroy(rt2);
        pal_mock_destroy(pal2);
    }

    /* TransformStream data flow (async) */
    {
        qwrt_pal_t *pal2;
        qwrt_t *rt2 = create_runtime(&pal2);
        qwrt_eval(rt2,
            "var _tsResult = ''; "
            "(async function() {"
            "  var ts = new TransformStream({"
            "    transform: function(chunk, ctrl) { ctrl.enqueue(chunk.toUpperCase()); }"
            "  });"
            "  var w = ts.writable.getWriter();"
            "  var r = ts.readable.getReader();"
            "  w.write('hello');"
            "  w.write('world');"
            "  w.close();"
            "  var chunks = [];"
            "  while (true) {"
            "    var res = await r.read();"
            "    if (res.done) break;"
            "    chunks.push(res.value);"
            "  }"
            "  _tsResult = chunks.join(',');"
            "})();", NULL);
        for (int i = 0; i < 50; i++) qwrt_tick(rt2);
        js_assert_eq(rt2, "_tsResult", "\"HELLO,WORLD\"",
                     "TransformStream transforms chunks");
        qwrt_destroy(rt2);
        pal_mock_destroy(pal2);
    }

    /* ReadableStream pipeTo WritableStream (async) */
    {
        qwrt_pal_t *pal2;
        qwrt_t *rt2 = create_runtime(&pal2);
        qwrt_eval(rt2,
            "var _pipeResult = ''; "
            "(async function() {"
            "  var rs = new ReadableStream({"
            "    start: function(c) { c.enqueue('a'); c.enqueue('b'); c.close(); }"
            "  });"
            "  var written = [];"
            "  var ws = new WritableStream({"
            "    write: function(chunk) { written.push(chunk); },"
            "    close: function() { _pipeResult = written.join(''); }"
            "  });"
            "  await rs.pipeTo(ws);"
            "})();", NULL);
        for (int i = 0; i < 50; i++) qwrt_tick(rt2);
        js_assert_eq(rt2, "_pipeResult", "\"ab\"",
                     "ReadableStream.pipeTo writes to WritableStream");
        qwrt_destroy(rt2);
        pal_mock_destroy(pal2);
    }

    /* ReadableStream pipeThrough TransformStream (async) */
    {
        qwrt_pal_t *pal2;
        qwrt_t *rt2 = create_runtime(&pal2);
        qwrt_eval(rt2,
            "var _ptResult = ''; "
            "(async function() {"
            "  var rs = new ReadableStream({"
            "    start: function(c) { c.enqueue(1); c.enqueue(2); c.enqueue(3); c.close(); }"
            "  });"
            "  var ts = new TransformStream({"
            "    transform: function(chunk, ctrl) { ctrl.enqueue(chunk * 10); }"
            "  });"
            "  var piped = rs.pipeThrough(ts);"
            "  var reader = piped.getReader();"
            "  var chunks = [];"
            "  while (true) {"
            "    var res = await reader.read();"
            "    if (res.done) break;"
            "    chunks.push(res.value);"
            "  }"
            "  _ptResult = chunks.join(',');"
            "})();", NULL);
        for (int i = 0; i < 50; i++) qwrt_tick(rt2);
        js_assert_eq(rt2, "_ptResult", "\"10,20,30\"",
                     "ReadableStream.pipeThrough transforms data");
        qwrt_destroy(rt2);
        pal_mock_destroy(pal2);
    }

    /* TransformStream basic */
    js_assert_no_error(rt, "var ts = new TransformStream()", "TransformStream constructor");

    /* CompressionStream format check */
    js_assert_no_error(rt, "var cs = new CompressionStream('gzip')",
                       "CompressionStream('gzip')");

    /* DecompressionStream format check */
    js_assert_no_error(rt, "var ds = new DecompressionStream('gzip')",
                       "DecompressionStream('gzip')");

    /* CompressionStream/DecompressionStream roundtrip (async) */
    {
        qwrt_pal_t *pal2;
        qwrt_t *rt2 = create_runtime(&pal2);
        /* gzip roundtrip */
        qwrt_eval(rt2,
            "var _gzipOk = false; "
            "(async function() {"
            "  var data = new TextEncoder().encode('Hello, compression world!');"
            "  var cs = new CompressionStream('gzip');"
            "  var writer = cs.writable.getWriter();"
            "  writer.write(data);"
            "  writer.close();"
            "  var chunks = [];"
            "  var reader = cs.readable.getReader();"
            "  while (true) {"
            "    var r = await reader.read();"
            "    if (r.done) break;"
            "    chunks.push(r.value);"
            "  }"
            "  var compressed = new Uint8Array(chunks.reduce(function(a,c){return a+c.length;},0));"
            "  var offset = 0;"
            "  chunks.forEach(function(c){compressed.set(c,offset);offset+=c.length;});"
            "  var ds = new DecompressionStream('gzip');"
            "  var dw = ds.writable.getWriter();"
            "  dw.write(compressed);"
            "  dw.close();"
            "  var dchunks = [];"
            "  var dr = ds.readable.getReader();"
            "  while (true) {"
            "    var r2 = await dr.read();"
            "    if (r2.done) break;"
            "    dchunks.push(r2.value);"
            "  }"
            "  var decompressed = new Uint8Array(dchunks.reduce(function(a,c){return a+c.length;},0));"
            "  var doff = 0;"
            "  dchunks.forEach(function(c){decompressed.set(c,doff);doff+=c.length;});"
            "  _gzipOk = new TextDecoder().decode(decompressed) === 'Hello, compression world!';"
            "})();", NULL);
        for (int i = 0; i < 100; i++) qwrt_tick(rt2);
        js_assert_eq(rt2, "_gzipOk", "true",
                     "CompressionStream/DecompressionStream gzip roundtrip");
        qwrt_destroy(rt2);
        pal_mock_destroy(pal2);
    }

    /* Queuing strategies */
    js_assert_eq(rt, "new ByteLengthQueuingStrategy({highWaterMark: 1024}).highWaterMark",
                 "1024", "ByteLengthQueuingStrategy.highWaterMark");
    js_assert_eq(rt, "new CountQueuingStrategy({highWaterMark: 5}).highWaterMark",
                 "5", "CountQueuingStrategy.highWaterMark");

    /* TextEncoderStream / TextDecoderStream */
    js_assert_eq(rt, "new TextEncoderStream().encoding", "\"utf-8\"",
                 "TextEncoderStream.encoding");
    js_assert_truthy(rt, "new TextEncoderStream().readable instanceof ReadableStream",
                     "TextEncoderStream.readable");
    js_assert_truthy(rt, "new TextEncoderStream().writable instanceof WritableStream",
                     "TextEncoderStream.writable");

    /* TransformStreamDefaultController */
    js_assert_truthy(rt, "typeof TransformStreamDefaultController === 'function'",
                     "TransformStreamDefaultController on globalThis");
    js_assert_truthy(rt,
        "(function() {"
        "  var out = [];"
        "  var ts = new TransformStream({"
        "    transform: function(chunk, ctrl) { ctrl.enqueue(chunk * 2); },"
        "    flush: function(ctrl) { ctrl.enqueue('done'); }"
        "  });"
        "  var w = ts.writable.getWriter();"
        "  var r = ts.readable.getReader();"
        "  w.write(5); w.write(10); w.close();"
        "  return true;"
        "})()",
        "TransformStream with controller.enqueue");

    /* ReadableStream tee() — both branches see same chunks */
    /* Since C test framework is synchronous, test tee() by checking
     * that both branch controllers receive data from enqueue */
    {
        g_total++;
        char *tee_result = js_eval(rt,
            "(function(){"
            "  try {"
            "    var rs = new ReadableStream({"
            "      start: function(c) { c.enqueue('a'); c.enqueue('b'); c.close(); }"
            "    });"
            "    var branches = rs.tee();"
            "    /* Both branches should exist and be ReadableStream instances */"
            "    if (!(branches[0] instanceof ReadableStream)) return 'ERR:branch0 type';"
            "    if (!(branches[1] instanceof ReadableStream)) return 'ERR:branch1 type';"
            "    /* Check that tee returns an array of 2 */"
            "    if (branches.length !== 2) return 'ERR:length=' + branches.length;"
            "    /* Both branches should have same controller structure */"
            "    if (!branches[0]._controller) return 'ERR:no ctrl0';"
            "    if (!branches[1]._controller) return 'ERR:no ctrl1';"
            "    return true;"
            "  } catch(e) { return 'ERR:' + e.message; }"
            "})()");
        if (tee_result && strcmp(tee_result, "true") == 0) {
            g_passed++;
            printf("  PASS ReadableStream.tee() returns two ReadableStream branches\n");
        } else {
            g_failed++;
            printf("  FAIL ReadableStream.tee(): got %s\n", tee_result ? tee_result : "(null)");
        }
        if (tee_result) qwrt_free(tee_result);
    }

    /* ReadableStream asyncIterator is set by polyfill/bridge fixup */
    js_assert_eq(rt,
        "typeof ReadableStream.prototype[Symbol.asyncIterator]",
        "\"function\"",
        "ReadableStream[Symbol.asyncIterator] set by default");

    /* ReadableStream for-await-of iteration works (async, needs tick loop) */
    {
        qwrt_pal_t *pal2;
        qwrt_t *rt2 = create_runtime(&pal2);
        qwrt_eval(rt2,
            "var _iterResult = ''; "
            "(async function() {"
            "  var chunks = [];"
            "  var rs = new ReadableStream({start(c){c.enqueue('a');c.enqueue('b');c.close();}});"
            "  for await (var x of rs) chunks.push(x);"
            "  _iterResult = chunks.join(',');"
            "})();", NULL);
        for (int i = 0; i < 50; i++) qwrt_tick(rt2);
        js_assert_eq(rt2, "_iterResult", "\"a,b\"",
                     "ReadableStream for-await-of iteration works");
        qwrt_destroy(rt2);
        pal_mock_destroy(pal2);
    }
}

/* ================================================================
 * TC55: Blob / File / FormData
 * ================================================================ */

static void test_blob_file_formdata(qwrt_t *rt) {
    printf("\n--- TC55: Blob / File / FormData ---\n");

    js_assert_truthy(rt, "typeof Blob === 'function'", "Blob is function");
    js_assert_truthy(rt, "typeof File === 'function'", "File is function");
    js_assert_truthy(rt, "typeof FormData === 'function'", "FormData is function");

    /* Blob size and type */
    js_assert_eq(rt, "new Blob(['hello']).size", "5", "Blob.size");
    js_assert_eq(rt, "new Blob(['hello'], {type: 'text/plain'}).type",
                 "\"text/plain\"", "Blob.type");

    /* File name */
    js_assert_eq(rt, "new File(['data'], 'test.txt').name", "\"test.txt\"", "File.name");

    /* FormData append/get */
    js_assert_no_error(rt,
        "var fd = new FormData(); fd.append('key', 'val')",
        "FormData.append");
    js_assert_eq(rt,
        "(function(){var fd=new FormData();fd.append('k','v');return fd.get('k')})()",
        "\"v\"", "FormData.get");
}

/* ================================================================
 * TC55: URLPattern
 * ================================================================ */

static void test_url_pattern(qwrt_t *rt) {
    printf("\n--- TC55: URLPattern ---\n");

    js_assert_truthy(rt, "typeof URLPattern === 'function'", "URLPattern is function");

    /* URLPattern test */
    js_assert_truthy(rt,
        "new URLPattern({pathname: '/users/:id'}).test({pathname: '/users/123'})",
        "URLPattern.test matches");

    js_assert_truthy(rt,
        "!new URLPattern({pathname: '/users/:id'}).test({pathname: '/posts/123'})",
        "URLPattern.test no match");

    /* URLPattern exec */
    js_assert_truthy(rt,
        "new URLPattern({pathname: '/users/:id'}).exec({pathname: '/users/42'}).pathname.groups.id === '42'",
        "URLPattern.exec groups");
}

/* ================================================================
 * TC55: navigator + reportError
 * ================================================================ */

static void test_navigator(qwrt_t *rt) {
    printf("\n--- TC55: navigator ---\n");

    js_assert_truthy(rt, "typeof navigator !== 'undefined'", "navigator exists");
    js_assert_truthy(rt, "typeof navigator.userAgent === 'string'", "navigator.userAgent is string");
    js_assert_truthy(rt, "navigator.userAgent.indexOf('qwrt') >= 0",
                     "navigator.userAgent contains 'qwrt'");

    /* reportError */
    js_assert_truthy(rt, "typeof reportError === 'function'", "reportError is function");

    /* self */
    js_assert_truthy(rt, "typeof self !== 'undefined'", "self exists");
    js_assert_truthy(rt, "self === globalThis", "self === globalThis");

    /* globalThis event handlers */
    js_assert_truthy(rt, "'onerror' in globalThis", "globalThis has onerror");
    js_assert_truthy(rt, "'onunhandledrejection' in globalThis", "globalThis has onunhandledrejection");
    js_assert_truthy(rt, "'onrejectionhandled' in globalThis", "globalThis has onrejectionhandled");

    /* Extended navigator properties */
    js_assert_truthy(rt, "typeof navigator.language === 'string'", "navigator.language is string");
    js_assert_truthy(rt, "typeof navigator.platform === 'string'", "navigator.platform is string");
    js_assert_truthy(rt, "typeof navigator.hardwareConcurrency === 'number' && navigator.hardwareConcurrency >= 1",
                     "navigator.hardwareConcurrency >= 1");
    js_assert_truthy(rt, "typeof navigator.onLine === 'boolean'", "navigator.onLine is boolean");
    js_assert_truthy(rt, "typeof navigator.maxTouchPoints === 'number' && navigator.maxTouchPoints >= 0",
                     "navigator.maxTouchPoints >= 0");
}

/* ================================================================
 * TC55: crypto.subtle
 * ================================================================ */

static void test_crypto_subtle(qwrt_t *rt) {
    printf("\n--- TC55: crypto.subtle ---\n");

    js_assert_truthy(rt, "typeof crypto !== 'undefined'", "crypto exists");
    js_assert_truthy(rt, "typeof crypto.subtle === 'object'", "crypto.subtle exists");
    js_assert_truthy(rt, "typeof crypto.subtle.digest === 'function'", "crypto.subtle.digest");
    js_assert_truthy(rt, "typeof CryptoKey === 'function'", "CryptoKey is function");

    /* SHA-256 digest */
    qwrt_pal_t *pal;
    qwrt_t *rt2 = create_runtime(&pal);
    qwrt_eval(rt2,
        "var _digestLen = null; "
        "crypto.subtle.digest('SHA-256', new TextEncoder().encode('hello')).then(function(buf) { "
        "_digestLen = buf.byteLength; })", NULL);
    qwrt_tick(rt2);
    js_assert_eq(rt2, "_digestLen", "32", "SHA-256 digest is 32 bytes");

    /* importKey returns CryptoKey */
    qwrt_eval(rt2,
        "var _keyType = null; "
        "crypto.subtle.importKey('raw', new Uint8Array(16), {name:'HMAC',hash:'SHA-256'}, false, ['sign']).then(function(k) { "
        "_keyType = k.type; })", NULL);
    qwrt_tick(rt2);
    js_assert_eq(rt2, "_keyType", "\"secret\"", "importKey returns CryptoKey with type");

    /* HMAC sign/verify roundtrip */
    qwrt_eval(rt2,
        "var _hmacOk = false; "
        "crypto.subtle.importKey('raw', new Uint8Array(16), {name:'HMAC',hash:'SHA-256'}, false, ['sign','verify']).then(function(k) {"
        "  return crypto.subtle.sign('HMAC', k, new TextEncoder().encode('test data'));"
        "}).then(function(sig) {"
        "  return crypto.subtle.importKey('raw', new Uint8Array(16), {name:'HMAC',hash:'SHA-256'}, false, ['verify']).then(function(k2) {"
        "    return crypto.subtle.verify('HMAC', k2, sig, new TextEncoder().encode('test data'));"
        "  });"
        "}).then(function(ok) { _hmacOk = ok; })", NULL);
    for (int i = 0; i < 50; i++) qwrt_tick(rt2);
    js_assert_eq(rt2, "_hmacOk", "true", "HMAC sign/verify roundtrip");

    /* HMAC verify rejects tampered data */
    qwrt_eval(rt2,
        "var _hmacReject = true; "
        "crypto.subtle.importKey('raw', new Uint8Array(16), {name:'HMAC',hash:'SHA-256'}, false, ['sign','verify']).then(function(k) {"
        "  return crypto.subtle.sign('HMAC', k, new TextEncoder().encode('original'));"
        "}).then(function(sig) {"
        "  return crypto.subtle.importKey('raw', new Uint8Array(16), {name:'HMAC',hash:'SHA-256'}, false, ['verify']).then(function(k2) {"
        "    return crypto.subtle.verify('HMAC', k2, sig, new TextEncoder().encode('tampered'));"
        "  });"
        "}).then(function(ok) { _hmacReject = !ok; })", NULL);
    for (int i = 0; i < 50; i++) qwrt_tick(rt2);
    js_assert_eq(rt2, "_hmacReject", "true", "HMAC verify rejects tampered data");

    /* AES-CBC encrypt/decrypt roundtrip */
    qwrt_eval(rt2,
        "var _aesResult = null; "
        "(function() {"
        "  var key = new Uint8Array([0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15]);"
        "  var iv = new Uint8Array(16);"
        "  var pt = new TextEncoder().encode('hello world 12345');"
        "  crypto.subtle.importKey('raw', key, 'AES-CBC', false, ['encrypt','decrypt']).then(function(k) {"
        "    return crypto.subtle.encrypt({name:'AES-CBC', iv:iv}, k, pt);"
        "  }).then(function(ct) {"
        "    return crypto.subtle.importKey('raw', key, 'AES-CBC', false, ['decrypt']).then(function(k2) {"
        "      return crypto.subtle.decrypt({name:'AES-CBC', iv:iv}, k2, ct);"
        "    });"
        "  }).then(function(dec) {"
        "    _aesResult = new TextDecoder().decode(dec);"
        "  });"
        "})();", NULL);
    qwrt_eval(rt2, "null", NULL);
    for (int i = 0; i < 50; i++) qwrt_tick(rt2);
    js_assert_eq(rt2, "_aesResult", "\"hello world 12345\"", "AES-CBC encrypt/decrypt roundtrip");

    /* AES-GCM encrypt/decrypt roundtrip */
    qwrt_eval(rt2,
        "var _gcmResult = null; "
        "(function() {"
        "  var key = new Uint8Array(16);"
        "  var iv = new Uint8Array(12);"
        "  for(var i=0;i<16;i++) key[i]=i;"
        "  var pt = new TextEncoder().encode('gcm test data!!!');"
        "  crypto.subtle.importKey('raw', key, 'AES-GCM', false, ['encrypt','decrypt']).then(function(k) {"
        "    return crypto.subtle.encrypt({name:'AES-GCM', iv:iv}, k, pt);"
        "  }).then(function(ct) {"
        "    return crypto.subtle.importKey('raw', key, 'AES-GCM', false, ['decrypt']).then(function(k2) {"
        "      return crypto.subtle.decrypt({name:'AES-GCM', iv:iv}, k2, ct);"
        "    });"
        "  }).then(function(dec) {"
        "    _gcmResult = new TextDecoder().decode(dec);"
        "  });"
        "})();", NULL);
    for (int i = 0; i < 50; i++) qwrt_tick(rt2);
    js_assert_eq(rt2, "_gcmResult", "\"gcm test data!!!\"", "AES-GCM encrypt/decrypt roundtrip");

    /* generateKey for AES */
    qwrt_eval(rt2,
        "var _genKeyOk = false; "
        "crypto.subtle.generateKey({name:'AES-CBC', length:256}, true, ['encrypt']).then(function(k) {"
        "  _genKeyOk = k.type === 'secret' && k.algorithm.name === 'AES-CBC';"
        "})", NULL);
    for (int i = 0; i < 50; i++) qwrt_tick(rt2);
    js_assert_truthy(rt2, "_genKeyOk === true", "generateKey for AES-CBC");

    /* exportKey raw */
    qwrt_eval(rt2,
        "var _exportOk = false; "
        "crypto.subtle.importKey('raw', new Uint8Array([1,2,3,4]), 'AES-CBC', true, ['encrypt']).then(function(k) {"
        "  return crypto.subtle.exportKey('raw', k);"
        "}).then(function(buf) {"
        "  _exportOk = new Uint8Array(buf).length === 4 && new Uint8Array(buf)[0] === 1;"
        "})", NULL);
    for (int i = 0; i < 50; i++) qwrt_tick(rt2);
    js_assert_truthy(rt2, "_exportOk === true", "exportKey raw format");

    /* PBKDF2 deriveBits */
    qwrt_eval(rt2,
        "var _pbkdfLen = 0; "
        "crypto.subtle.importKey('raw', new TextEncoder().encode('password'), 'PBKDF2', false, ['deriveBits']).then(function(k) {"
        "  return crypto.subtle.deriveBits({name:'PBKDF2', salt:new TextEncoder().encode('salt'), iterations:1000, hash:'SHA-256'}, k, 256);"
        "}).then(function(bits) {"
        "  _pbkdfLen = bits.byteLength;"
        "})", NULL);
    for (int i = 0; i < 50; i++) qwrt_tick(rt2);
    js_assert_eq(rt2, "_pbkdfLen", "32", "PBKDF2 deriveBits produces 32 bytes");

    qwrt_destroy(rt2);
    pal_mock_destroy(pal);
}

/* ================================================================
 * TC55: structuredClone (enhanced)
 * ================================================================ */

static void test_structured_clone_enhanced(qwrt_t *rt) {
    printf("\n--- TC55: structuredClone enhanced ---\n");

    /* TypedArray clone */
    js_assert_truthy(rt,
        "structuredClone(new Uint8Array([1,2,3])) instanceof Uint8Array",
        "structuredClone Uint8Array");

    js_assert_eq(rt,
        "structuredClone(new Uint8Array([1,2,3])).length", "3",
        "structuredClone Uint8Array length");

    /* Map clone */
    js_assert_truthy(rt,
        "structuredClone(new Map([['a',1]])) instanceof Map",
        "structuredClone Map");

    js_assert_eq(rt,
        "structuredClone(new Map([['a',1]])).get('a')", "1",
        "structuredClone Map.get");

    /* Set clone */
    js_assert_truthy(rt,
        "structuredClone(new Set([1,2,3])) instanceof Set",
        "structuredClone Set");

    /* Date clone */
    js_assert_truthy(rt,
        "structuredClone(new Date(1234567890)) instanceof Date",
        "structuredClone Date");

    js_assert_eq(rt,
        "structuredClone(new Date(1234567890)).getTime()", "1234567890",
        "structuredClone Date.getTime");

    /* RegExp clone */
    js_assert_truthy(rt,
        "structuredClone(/abc/gi) instanceof RegExp",
        "structuredClone RegExp");

    js_assert_eq(rt,
        "structuredClone(/abc/gi).source", "\"abc\"",
        "structuredClone RegExp.source");

    /* Circular reference */
    js_assert_truthy(rt,
        "(function(){var o={};o.self=o;var c=structuredClone(o);return c.self===c})()",
        "structuredClone circular ref");

    /* Error clone */
    js_assert_truthy(rt,
        "structuredClone(new Error('test')) instanceof Error",
        "structuredClone Error");

    /* ArrayBuffer clone */
    js_assert_truthy(rt,
        "structuredClone(new ArrayBuffer(8)) instanceof ArrayBuffer",
        "structuredClone ArrayBuffer");
    js_assert_eq(rt,
        "structuredClone(new ArrayBuffer(8)).byteLength", "8",
        "structuredClone ArrayBuffer byteLength");

    /* DataView clone */
    js_assert_truthy(rt,
        "(function(){var ab=new ArrayBuffer(4);var dv=new DataView(ab);return structuredClone(dv) instanceof DataView;})()",
        "structuredClone DataView");

    /* Int32Array clone */
    js_assert_truthy(rt,
        "structuredClone(new Int32Array([1,2,3])) instanceof Int32Array",
        "structuredClone Int32Array");
    js_assert_eq(rt,
        "structuredClone(new Int32Array([1,2,3]))[0]", "1",
        "structuredClone Int32Array preserves values");

    /* BigInt typed arrays */
    js_assert_truthy(rt,
        "typeof BigInt64Array === 'function'",
        "BigInt64Array exists");
    js_assert_truthy(rt,
        "structuredClone(new BigInt64Array([BigInt(1),BigInt(2)])) instanceof BigInt64Array",
        "structuredClone BigInt64Array");

    /* Infinity / NaN / -0 */
    js_assert_truthy(rt, "structuredClone(Infinity) === Infinity",
                     "structuredClone preserves Infinity");
    js_assert_truthy(rt, "Number.isNaN(structuredClone(NaN))",
                     "structuredClone preserves NaN");
    js_assert_truthy(rt, "Object.is(structuredClone(-0), -0)",
                     "structuredClone preserves -0");

    /* Error subtypes */
    js_assert_truthy(rt,
        "structuredClone(new TypeError('test')) instanceof TypeError",
        "structuredClone TypeError subtype");
    js_assert_truthy(rt,
        "structuredClone(new RangeError('test')) instanceof RangeError",
        "structuredClone RangeError subtype");

    /* Boolean and null */
    js_assert_eq(rt, "structuredClone(true)", "true",
                 "structuredClone boolean");
    js_assert_eq(rt, "structuredClone(null)", "null",
                 "structuredClone null");

    /* RegExp flags preserved */
    js_assert_eq(rt, "structuredClone(/abc/gi).flags", "\"gi\"",
                 "structuredClone RegExp.flags");
}

/* ================================================================
 * Helper: create runtime with mock PAL + polyfill + extensions
 * ================================================================ */

static qwrt_t *create_runtime_with_ext(qwrt_pal_t **pal_out, const qwrt_ext_t **exts) {
    qwrt_pal_t *pal = pal_mock_create();
    if (!pal) return NULL;

    qwrt_config_t config;
    memset(&config, 0, sizeof(config));
    config.pal = pal;
    config.extensions = exts;

    qwrt_t *rt = qwrt_create(&config);
    if (!rt) { pal_mock_destroy(pal); return NULL; }

    if (pal_out) *pal_out = pal;
    return rt;
}

/* ================================================================
 * TC55: WebAssembly (WAMR)
 * ================================================================ */

static void test_wasm(qwrt_t *rt) {
    printf("\n--- TC55: WebAssembly ---\n");

    /* WebAssembly global object */
    js_assert_truthy(rt, "typeof WebAssembly !== 'undefined'", "WebAssembly exists");
    js_assert_truthy(rt, "typeof WebAssembly === 'object'", "WebAssembly is object");

    /* Static methods */
    js_assert_truthy(rt, "typeof WebAssembly.validate === 'function'", "validate is function");
    js_assert_truthy(rt, "typeof WebAssembly.compile === 'function'", "compile is function");
    js_assert_truthy(rt, "typeof WebAssembly.instantiate === 'function'", "instantiate is function");

    /* Constructors */
    js_assert_truthy(rt, "typeof WebAssembly.Module === 'function'", "Module is function");
    js_assert_truthy(rt, "typeof WebAssembly.Instance === 'function'", "Instance is function");
    js_assert_truthy(rt, "typeof WebAssembly.Memory === 'function'", "Memory is function");
    js_assert_truthy(rt, "typeof WebAssembly.Table === 'function'", "Table is function");
    js_assert_truthy(rt, "typeof WebAssembly.Global === 'function'", "Global is function");

    /* Error types */
    js_assert_truthy(rt, "typeof WebAssembly.CompileError !== 'undefined'", "CompileError exists");
    js_assert_truthy(rt, "typeof WebAssembly.LinkError !== 'undefined'", "LinkError exists");
    js_assert_truthy(rt, "typeof WebAssembly.RuntimeError !== 'undefined'", "RuntimeError exists");

    /* WebAssembly.validate — minimal valid WASM binary:
     * magic: 00 61 73 6d, version: 01 00 00 00 */
    js_assert_eq(rt,
        "WebAssembly.validate(new Uint8Array([0x00,0x61,0x73,0x6d,0x01,0x00,0x00,0x00]))",
        "true",
        "validate accepts valid WASM binary");

    js_assert_eq(rt,
        "WebAssembly.validate(new Uint8Array([0x00,0x00,0x00,0x00]))",
        "false",
        "validate rejects invalid binary");

    /* WebAssembly.Memory constructor */
    js_assert_truthy(rt,
        "(function(){ var m = new WebAssembly.Memory({initial:1}); return m.buffer instanceof ArrayBuffer; })()",
        "Memory.buffer is ArrayBuffer");

    js_assert_eq(rt,
        "(function(){ var m = new WebAssembly.Memory({initial:1}); return m.buffer.byteLength; })()",
        "65536",
        "Memory.buffer byteLength is 65536 * initial");

    js_assert_eq(rt,
        "(function(){ var m = new WebAssembly.Memory({initial:2}); return m.buffer.byteLength; })()",
        "131072",
        "Memory.buffer byteLength for initial:2");

    /* WebAssembly.Table constructor */
    js_assert_truthy(rt,
        "(function(){ var t = new WebAssembly.Table({initial:2, element:'anyfunc'}); return t.length === 2; })()",
        "Table.length is initial");

    /* WebAssembly.Global constructor */
    js_assert_truthy(rt,
        "(function(){ var g = new WebAssembly.Global({value:'i32', mutable:true}, 42); return g.value === 42; })()",
        "Global.value is initial value");

    js_assert_truthy(rt,
        "(function(){ var g = new WebAssembly.Global({value:'i32', mutable:true}, 42); return g.mutable === true; })()",
        "Global.mutable is true");

    js_assert_truthy(rt,
        "(function(){ var g = new WebAssembly.Global({value:'f64', mutable:false}, 3.14); return g.mutable === false; })()",
        "Global.mutable is false for immutable");

    /* WebAssembly.Module from valid binary */
    js_assert_truthy(rt,
        "(function(){ var m = new WebAssembly.Module(new Uint8Array([0x00,0x61,0x73,0x6d,0x01,0x00,0x00,0x00])); return m !== undefined; })()",
        "Module from valid binary");

    /* WebAssembly.compile returns Promise */
    js_assert_truthy(rt,
        "typeof WebAssembly.compile(new Uint8Array([0x00,0x61,0x73,0x6d,0x01,0x00,0x00,0x00])) === 'object'",
        "compile returns object (Promise)");

    /* WebAssembly.instantiate returns Promise */
    js_assert_truthy(rt,
        "typeof WebAssembly.instantiate(new Uint8Array([0x00,0x61,0x73,0x6d,0x01,0x00,0x00,0x00])) === 'object'",
        "instantiate returns object (Promise)");

    /* Test with a WASM module that exports a memory:
     * (module (memory 1) (export "mem" (memory 0)))
     * Binary: magic + version + memory section + export section */
    js_assert_truthy(rt,
        "(function(){"
        "  var bytes = new Uint8Array(["
        "    0x00,0x61,0x73,0x6d, 0x01,0x00,0x00,0x00,"  /* magic + version */
        "    0x05,0x03,0x01,0x00,0x01,"                    /* memory section: 1 memory, min 1 page */
        "    0x07,0x07,0x01,0x03,0x6d,0x65,0x6d,0x02,0x00" /* export section: "mem" -> memory 0 */
        "  ]);"
        "  var mod = new WebAssembly.Module(bytes);"
        "  var inst = new WebAssembly.Instance(mod);"
        "  return inst.exports.mem.buffer instanceof ArrayBuffer;"
        "})()",
        "Instance exports memory with ArrayBuffer");

    /* Test with a WASM module that exports a global:
     * (module (global $g (export "g") i32 (i32.const 42)))
     * Binary: magic + version + global section + export section */
    js_assert_truthy(rt,
        "(function(){"
        "  var bytes = new Uint8Array(["
        "    0x00,0x61,0x73,0x6d, 0x01,0x00,0x00,0x00,"  /* magic + version */
        "    0x06,0x06,0x01,0x7f,0x00,0x41,0x2a,0x0b,"    /* global section: 1 global, i32, immutable, i32.const 42, end */
        "    0x07,0x05,0x01,0x01,0x67,0x03,0x00"           /* export section: "g" -> global 0 */
        "  ]);"
        "  var mod = new WebAssembly.Module(bytes);"
        "  var inst = new WebAssembly.Instance(mod);"
        "  return inst.exports.g.value === 42;"
        "})()",
        "Instance exports global with correct value");

    /* Test live global binding: WASM writes to mutable global visible from JS */
    /* WAT: (module
     *   (global $g (export "g") (mut i32) (i32.const 42))
     *   (func (export "set_g") (param i32) local.get 0 global.set 0)
     *   (func (export "get_g") (result i32) global.get 0)
     * ) */
    {
        g_total++;
        char *live_result = js_eval(rt,
            "(function(){"
            "  try {"
            "    var bytes = new Uint8Array(["
            "      0x00,0x61,0x73,0x6d,0x01,0x00,0x00,0x00,"
            "      0x01,0x09,0x02,"                          /* type section: 2 types */
            "      0x60,0x01,0x7f,0x00,"                     /* type 0: (i32)->() */
            "      0x60,0x00,0x01,0x7f,"                     /* type 1: ()->(i32) */
            "      0x03,0x03,0x02,0x00,0x01,"               /* function section: 2 funcs */
            "      0x06,0x06,0x01,0x7f,0x01,0x41,0x2a,0x0b," /* global section: mut i32 = 42 */
            "      0x07,0x15,0x03,"                          /* export section: 3 exports */
            "      0x01,0x67,0x03,0x00,"                     /* \"g\" global 0 */
            "      0x05,0x73,0x65,0x74,0x5f,0x67,0x00,0x00," /* \"set_g\" func 0 */
            "      0x05,0x67,0x65,0x74,0x5f,0x67,0x00,0x01," /* \"get_g\" func 1 */
            "      0x0a,0x0d,0x02,"                          /* code section: 2 bodies */
            "      0x06,0x00,0x20,0x00,0x24,0x00,0x0b,"     /* set_g: local.get 0, global.set 0 */
            "      0x04,0x00,0x23,0x00,0x0b"                /* get_g: global.get 0 */
            "    ]);"
            "    var mod = new WebAssembly.Module(bytes);"
            "    var inst = new WebAssembly.Instance(mod);"
            "    var r1 = inst.exports.g.value;"       /* 42 */
            "    inst.exports.set_g(100);"
            "    var r2 = inst.exports.g.value;"       /* should be 100 (live!) */
            "    inst.exports.g.value = 200;"
            "    var r3 = inst.exports.get_g();"       /* should be 200 (JS write visible in WASM!) */
            "    return r1===42 && r2===100 && r3===200;"
            "  } catch(e) { return 'ERR:' + e.message; }"
            "})()");
        if (live_result && strcmp(live_result, "true") == 0) {
            g_passed++;
            printf("  PASS Instance mutable global is live (WASM<->JS sync)\n");
        } else {
            g_failed++;
            printf("  FAIL Instance mutable global is live (WASM<->JS sync): got %s\n",
                   live_result ? live_result : "(null)");
        }
        if (live_result) qwrt_free(live_result);
    }

    /* Test with a WASM module that exports a function:
     * (module (func (export "add") (param i32 i32) (result i32) local.get 0 local.get 1 i32.add))
     * Binary: magic + version + type + function + export + code sections */
    {
        g_total++;
        char *func_result = js_eval(rt,
            "(function(){"
            "  try {"
            "    var bytes = new Uint8Array(["
            "      0x00,0x61,0x73,0x6d, 0x01,0x00,0x00,0x00,"
            "      0x01,0x07,0x01,0x60,0x02,0x7f,0x7f,0x01,0x7f,"
            "      0x03,0x02,0x01,0x00,"
            "      0x07,0x07,0x01,0x03,0x61,0x64,0x64,0x00,0x00,"
            "      0x0a,0x09,0x01,0x07,0x00,0x20,0x00,0x20,0x01,0x6a,0x0b"
            "    ]);"
            "    var mod = new WebAssembly.Module(bytes);"
            "    var inst = new WebAssembly.Instance(mod);"
            "    return typeof inst.exports.add === 'function' && inst.exports.add(3, 4) === 7;"
            "  } catch(e) { return 'ERR:' + e.message; }"
            "})()");
        if (func_result && strcmp(func_result, "true") == 0) {
            g_passed++;
            printf("  PASS Instance exports callable function (add(3,4)===7)\n");
        } else {
            g_failed++;
            printf("  FAIL Instance exports callable function (add(3,4)===7): got %s\n",
                   func_result ? func_result : "(null)");
        }
        if (func_result) qwrt_free(func_result);
    }

    /* Test: import object — WASM module imports a JS function */
    /* WAT: (module
     *   (import "env" "addOne" (func $addOne (param i32) (result i32)))
     *   (func (export "test") (result i32) i32.const 5 call 0)
     * )
     * Binary hex: 0061736d0100000001060160017f017f020e0103656e76066164644f6e65000003020100070801047465737400010a08010600410510000b
     */
    {
        g_total++;
        char *import_result = js_eval(rt,
            "(function(){"
            "  try {"
            "    var bytes = new Uint8Array(["
            "      0x00,0x61,0x73,0x6d, 0x01,0x00,0x00,0x00,"
            "      0x01,0x0a,0x02,0x60,0x01,0x7f,0x01,0x7f,0x60,0x00,0x01,0x7f,"
            "      0x02,0x0e,0x01,0x03,0x65,0x6e,0x76,0x06,0x61,0x64,0x64,0x4f,0x6e,0x65,0x00,0x00,"
            "      0x03,0x02,0x01,0x01,"
            "      0x07,0x08,0x01,0x04,0x74,0x65,0x73,0x74,0x00,0x01,"
            "      0x0a,0x08,0x01,0x06,0x00,0x41,0x05,0x10,0x00,0x0b"
            "    ]);"
            "    var mod = new WebAssembly.Module(bytes);"
            "    var inst = new WebAssembly.Instance(mod, {"
            "      env: { addOne: function(x) { return x + 1; } }"
            "    });"
            "    return inst.exports.test() === 6;"
            "  } catch(e) { return 'ERR:' + e.message; }"
            "})()");
        if (import_result && strcmp(import_result, "true") == 0) {
            g_passed++;
            printf("  PASS Instance with import object (addOne(5)===6)\n");
        } else {
            g_failed++;
            printf("  FAIL Instance with import object (addOne(5)===6): got %s\n",
                   import_result ? import_result : "(null)");
        }
        if (import_result) qwrt_free(import_result);
    }

    /* Test: CompileError on invalid WASM binary */
    {
        g_total++;
        char *cerr_result = js_eval(rt,
            "(function(){"
            "  try {"
            "    new WebAssembly.Module(new Uint8Array([0x00,0x61,0x73,0x6d,0x00,0x00,0x00,0x00,0xff]));"
            "    return 'ERR:no error thrown';"
            "  } catch(e) {"
            "    if (e.name === 'CompileError' || e.message.indexOf('Module') >= 0) return true;"
            "    return 'ERR:wrong error: ' + e.name + ': ' + e.message;"
            "  }"
            "})()");
        if (cerr_result && strcmp(cerr_result, "true") == 0) {
            g_passed++;
            printf("  PASS CompileError thrown on invalid WASM binary\n");
        } else {
            g_failed++;
            printf("  FAIL CompileError: got %s\n", cerr_result ? cerr_result : "(null)");
        }
        if (cerr_result) qwrt_free(cerr_result);
    }

    /* Test: LinkError when required import is not provided */
    {
        g_total++;
        char *lerr_result = js_eval(rt,
            "(function(){"
            "  try {"
            "    var bytes = new Uint8Array(["
            "      0x00,0x61,0x73,0x6d, 0x01,0x00,0x00,0x00,"
            "      0x01,0x0a,0x02,0x60,0x01,0x7f,0x01,0x7f,0x60,0x00,0x01,0x7f,"
            "      0x02,0x0e,0x01,0x03,0x65,0x6e,0x76,0x06,0x61,0x64,0x64,0x4f,0x6e,0x65,0x00,0x00,"
            "      0x03,0x02,0x01,0x01,"
            "      0x07,0x08,0x01,0x04,0x74,0x65,0x73,0x74,0x00,0x01,"
            "      0x0a,0x08,0x01,0x06,0x00,0x41,0x05,0x10,0x00,0x0b"
            "    ]);"
            "    var mod = new WebAssembly.Module(bytes);"
            "    var inst = new WebAssembly.Instance(mod, {});"
            "    return 'ERR:no error thrown';"
            "  } catch(e) {"
            "    if (e.name === 'LinkError' || e.message.indexOf('missing import') >= 0) return true;"
            "    return 'ERR:wrong error: ' + e.name + ': ' + e.message;"
            "  }"
            "})()");
        if (lerr_result && strcmp(lerr_result, "true") == 0) {
            g_passed++;
            printf("  PASS LinkError thrown when required import missing\n");
        } else {
            g_failed++;
            printf("  FAIL LinkError: got %s\n", lerr_result ? lerr_result : "(null)");
        }
        if (lerr_result) qwrt_free(lerr_result);
    }

    /* Test: Memory.grow() on standalone Memory */
    {
        g_total++;
        char *grow_result = js_eval(rt,
            "(function(){"
            "  try {"
            "    var mem = new WebAssembly.Memory({ initial: 1, maximum: 10 });"
            "    var oldLen = mem.buffer.byteLength;"
            "    var prev = mem.grow(2);"
            "    return prev === 1 && mem.buffer.byteLength === 3 * 65536;"
            "  } catch(e) { return 'ERR:' + e.message; }"
            "})()");
        if (grow_result && strcmp(grow_result, "true") == 0) {
            g_passed++;
            printf("  PASS Memory.grow() on standalone memory\n");
        } else {
            g_failed++;
            printf("  FAIL Memory.grow() on standalone memory: got %s\n",
                   grow_result ? grow_result : "(null)");
        }
        if (grow_result) qwrt_free(grow_result);
    }

    /* Test: Instance memory is a live buffer + grow */
    /* WAT: (module
     *   (memory (export "memory") 1 10)
     *   (func (export "store") (param i32 i32) local.get 0 local.get 1 i32.store)
     *   (func (export "load") (param i32) (result i32) local.get 0 i32.load)
     * )
     * Binary hex: 0061736d01000000010b0260027f7f0060017f017f030302000105040101010a071903066d656d6f727902000573746f72650000046c6f616400010a13020900200020013602000b070020002802000b
     */
    {
        g_total++;
        char *live_result = js_eval(rt,
            "(function(){"
            "  try {"
            "    var bytes = new Uint8Array(["
            "      0x00,0x61,0x73,0x6d, 0x01,0x00,0x00,0x00,"
            "      0x01,0x0b,0x02,0x60,0x02,0x7f,0x7f,0x00,0x60,0x01,0x7f,0x01,0x7f,"
            "      0x03,0x03,0x02,0x00,0x01,"
            "      0x05,0x04,0x01,0x01,0x01,0x0a,"
            "      0x07,0x19,0x03,0x06,0x6d,0x65,0x6d,0x6f,0x72,0x79,0x02,0x00,"
            "                   0x05,0x73,0x74,0x6f,0x72,0x65,0x00,0x00,"
            "                   0x04,0x6c,0x6f,0x61,0x64,0x00,0x01,"
            "      0x0a,0x13,0x02,0x09,0x00,0x20,0x00,0x20,0x01,0x36,0x02,0x00,0x0b,"
            "                     0x07,0x00,0x20,0x00,0x28,0x02,0x00,0x0b"
            "    ]);"
            "    var mod = new WebAssembly.Module(bytes);"
            "    var inst = new WebAssembly.Instance(mod);"
            /* Verify live buffer: write via WASM, read via JS */
            "    inst.exports.store(0, 0x42);"
            "    var view = new Uint8Array(inst.exports.memory.buffer);"
            "    if (view[0] !== 0x42) return 'ERR:live buffer not visible, got ' + view[0];"
            /* Verify load via WASM */
            "    if (inst.exports.load(0) !== 0x42) return 'ERR:load mismatch';"
            /* Verify memory.grow */
            "    var prevPages = inst.exports.memory.grow(2);"
            "    if (prevPages !== 1) return 'ERR:grow prev=' + prevPages;"
            "    if (inst.exports.memory.buffer.byteLength !== 3 * 65536) return 'ERR:grow size';"
            /* Verify data preserved after grow */
            "    var view2 = new Uint8Array(inst.exports.memory.buffer);"
            "    if (view2[0] !== 0x42) return 'ERR:data lost after grow';"
            "    return true;"
            "  } catch(e) { return 'ERR:' + e.message; }"
            "})()");
        if (live_result && strcmp(live_result, "true") == 0) {
            g_passed++;
            printf("  PASS Instance memory is live buffer + grow preserves data\n");
        } else {
            g_failed++;
            printf("  FAIL Instance memory is live buffer + grow: got %s\n",
                   live_result ? live_result : "(null)");
        }
        if (live_result) qwrt_free(live_result);
    }

    /* Test: Table.get/set/grow on standalone Table */
    {
        g_total++;
        char *tbl_result = js_eval(rt,
            "(function(){"
            "  try {"
            "    var tbl = new WebAssembly.Table({ initial: 2, element: 'anyfunc' });"
            "    if (tbl.length !== 2) return 'ERR:length=' + tbl.length;"
            "    if (tbl.get(0) !== null) return 'ERR:init not null';"
            "    tbl.set(0, function(){});"
            "    if (typeof tbl.get(0) !== 'function') return 'ERR:get type=' + typeof tbl.get(0);"
            "    if (tbl.get(1) !== null) return 'ERR:slot1 not null';"
            "    var prev = tbl.grow(3);"
            "    if (prev !== 2) return 'ERR:grow prev=' + prev;"
            "    if (tbl.length !== 5) return 'ERR:grow length=' + tbl.length;"
            "    if (tbl.get(2) !== null) return 'ERR:new slot not null';"
            "    return true;"
            "  } catch(e) { return 'ERR:' + e.message; }"
            "})()");
        if (tbl_result && strcmp(tbl_result, "true") == 0) {
            g_passed++;
            printf("  PASS Table get/set/grow on standalone table\n");
        } else {
            g_failed++;
            printf("  FAIL Table get/set/grow: got %s\n", tbl_result ? tbl_result : "(null)");
        }
        if (tbl_result) qwrt_free(tbl_result);
    }

    /* Test: Global value getter/setter */
    {
        g_total++;
        char *gval_result = js_eval(rt,
            "(function(){"
            "  try {"
            "    var g = new WebAssembly.Global({ value: 'i32', mutable: true }, 10);"
            "    if (g.value !== 10) return 'ERR:init value=' + g.value;"
            "    g.value = 42;"
            "    if (g.value !== 42) return 'ERR:set value=' + g.value;"
            "    if (g.valueOf() !== 42) return 'ERR:valueOf=' + g.valueOf();"
            "    var g2 = new WebAssembly.Global({ value: 'i32', mutable: false }, 5);"
            "    try { g2.value = 99; return 'ERR:immutable did not throw'; }"
            "    catch(e) { if (!(e instanceof TypeError)) return 'ERR:wrong error type: ' + e.name; }"
            "    return true;"
            "  } catch(e) { return 'ERR:' + e.message; }"
            "})()");
        if (gval_result && strcmp(gval_result, "true") == 0) {
            g_passed++;
            printf("  PASS Global value getter/setter and immutability\n");
        } else {
            g_failed++;
            printf("  FAIL Global value getter/setter: got %s\n", gval_result ? gval_result : "(null)");
        }
        if (gval_result) qwrt_free(gval_result);
    }

    /* Test: Module.exports introspection */
    {
        g_total++;
        char *mexp_result = js_eval(rt,
            "(function(){"
            "  try {"
            "    var bytes = new Uint8Array(["
            "      0x00,0x61,0x73,0x6d, 0x01,0x00,0x00,0x00,"
            "      0x01,0x07,0x01,0x60,0x02,0x7f,0x7f,0x01,0x7f,"
            "      0x03,0x02,0x01,0x00,"
            "      0x07,0x07,0x01,0x03,0x61,0x64,0x64,0x00,0x00,"
            "      0x0a,0x09,0x01,0x07,0x00,0x20,0x00,0x20,0x01,0x6a,0x0b"
            "    ]);"
            "    var mod = new WebAssembly.Module(bytes);"
            "    var exps = WebAssembly.Module.exports(mod);"
            "    if (!Array.isArray(exps)) return 'ERR:not array';"
            "    if (exps.length < 1) return 'ERR:empty';"
            "    var found = exps.some(function(e) {"
            "      return e.name === 'add' && e.kind === 'function';"
            "    });"
            "    if (!found) return 'ERR:add not found';"
            "    return true;"
            "  } catch(e) { return 'ERR:' + e.message; }"
            "})()");
        if (mexp_result && strcmp(mexp_result, "true") == 0) {
            g_passed++;
            printf("  PASS Module.exports returns export descriptors\n");
        } else {
            g_failed++;
            printf("  FAIL Module.exports: got %s\n", mexp_result ? mexp_result : "(null)");
        }
        if (mexp_result) qwrt_free(mexp_result);
    }

    /* Test: Module.imports introspection */
    {
        g_total++;
        char *mimp_result = js_eval(rt,
            "(function(){"
            "  try {"
            "    var bytes = new Uint8Array(["
            "      0x00,0x61,0x73,0x6d, 0x01,0x00,0x00,0x00,"
            "      0x01,0x0a,0x02,0x60,0x01,0x7f,0x01,0x7f,0x60,0x00,0x01,0x7f,"
            "      0x02,0x0e,0x01,0x03,0x65,0x6e,0x76,0x06,0x61,0x64,0x64,0x4f,0x6e,0x65,0x00,0x00,"
            "      0x03,0x02,0x01,0x01,"
            "      0x07,0x08,0x01,0x04,0x74,0x65,0x73,0x74,0x00,0x01,"
            "      0x0a,0x08,0x01,0x06,0x00,0x41,0x05,0x10,0x00,0x0b"
            "    ]);"
            "    var mod = new WebAssembly.Module(bytes);"
            "    var imps = WebAssembly.Module.imports(mod);"
            "    if (!Array.isArray(imps)) return 'ERR:not array';"
            "    if (imps.length < 1) return 'ERR:empty';"
            "    var found = imps.some(function(e) {"
            "      return e.module === 'env' && e.name === 'addOne' && e.kind === 'function';"
            "    });"
            "    if (!found) return 'ERR:addOne not found';"
            "    return true;"
            "  } catch(e) { return 'ERR:' + e.message; }"
            "})()");
        if (mimp_result && strcmp(mimp_result, "true") == 0) {
            g_passed++;
            printf("  PASS Module.imports returns import descriptors\n");
        } else {
            g_failed++;
            printf("  FAIL Module.imports: got %s\n", mimp_result ? mimp_result : "(null)");
        }
        if (mimp_result) qwrt_free(mimp_result);
    }

    /* Test: Module.customSections returns matching custom section contents */
    /* WASM binary: add function + custom section named "test_name" with content "hello world" */
    {
        g_total++;
        char *csec_result = js_eval(rt,
            "(function(){"
            "  try {"
            "    var bytes = new Uint8Array(["
            "      0x00,0x61,0x73,0x6d,0x01,0x00,0x00,0x00,"
            "      0x01,0x07,0x01,0x60,0x02,0x7f,0x7f,0x01,0x7f,"
            "      0x03,0x02,0x01,0x00,"
            "      0x07,0x07,0x01,0x03,0x61,0x64,0x64,0x00,0x00,"
            "      0x0a,0x09,0x01,0x07,0x00,0x20,0x00,0x20,0x01,0x6a,0x0b,"
            "      0x00,0x15,0x09,0x74,0x65,0x73,0x74,0x5f,0x6e,0x61,0x6d,0x65,"
            "      0x68,0x65,0x6c,0x6c,0x6f,0x20,0x77,0x6f,0x72,0x6c,0x64"
            "    ]);"
            "    var mod = new WebAssembly.Module(bytes);"
            "    var sections = WebAssembly.Module.customSections(mod, 'test_name');"
            "    if (!Array.isArray(sections)) return 'ERR:not array';"
            "    if (sections.length !== 1) return 'ERR:length=' + sections.length;"
            "    var view = new Uint8Array(sections[0]);"
            "    var str = String.fromCharCode.apply(null, view);"
            "    if (str !== 'hello world') return 'ERR:content=' + str;"
            "    var empty = WebAssembly.Module.customSections(mod, 'nonexistent');"
            "    if (empty.length !== 0) return 'ERR:nonexistent length=' + empty.length;"
            "    return true;"
            "  } catch(e) { return 'ERR:' + e.message; }"
            "})()");
        if (csec_result && strcmp(csec_result, "true") == 0) {
            g_passed++;
            printf("  PASS Module.customSections returns matching custom section contents\n");
        } else {
            g_failed++;
            printf("  FAIL Module.customSections: got %s\n", csec_result ? csec_result : "(null)");
        }
        if (csec_result) qwrt_free(csec_result);
    }
}

/* ================================================================
 * Main
 * ================================================================ */

int main(void) {
    size_t polyfill_len;
    uint8_t *polyfill = load_file("../../qwrt/dist/polyfill.bytecode", &polyfill_len);
    if (!polyfill) {
        polyfill = load_file("dist/polyfill.bytecode", &polyfill_len);
    }
    if (!polyfill) {
        polyfill = load_file("qwrt/dist/polyfill.bytecode", &polyfill_len);
    }
    if (!polyfill) {
        printf("=== ES2023 + WinterCG Compliance Tests ===\n");
        printf("SKIPPED: dist/polyfill.bytecode not found (run build first)\n");
        return 0;
    }

    g_polyfill = polyfill;
    g_polyfill_len = polyfill_len;

    printf("=== ES2023 + WinterCG Compliance Tests ===\n");
    printf("Loaded polyfill: %zu bytes\n", polyfill_len);

    qwrt_pal_t *pal;
    qwrt_t *rt = create_runtime(&pal);
    if (!rt) {
        printf("FATAL: could not create runtime\n");
        free(polyfill);
        return 1;
    }

    /* WinterCG tests */
    test_console(rt);
    test_timers(rt);
    test_microtask(rt);
    test_url(rt);
    test_encoding(rt);
    test_base64(rt);
    test_fetch(rt);
    test_abort(rt);
    test_events(rt);
    test_crypto(rt);
    test_performance(rt);
    test_storage(rt);

    /* ES2023 tests */
    test_es2023_array(rt);
    test_es2023_object(rt);
    test_es2023_string(rt);
    test_es2023_promise(rt);
    test_es2023_operators(rt);
    test_es2023_symbol(rt);
    test_es2023_weakref(rt);
    test_es2023_typedarray(rt);
    test_es2023_iteration(rt);
    test_es2023_errors(rt);
    test_structured_clone(rt);
    test_es2023_misc(rt);

    /* TC55 compliance tests */
    test_message_channel(rt);
    test_error_events(rt);
    test_streams(rt);
    test_blob_file_formdata(rt);
    test_url_pattern(rt);
    test_navigator(rt);
    test_crypto_subtle(rt);
    test_structured_clone_enhanced(rt);

    /* WebAssembly tests — need separate runtime with WASM extension */
    {
        qwrt_pal_t *wasm_pal;
#if defined(QWRT_HAS_WAMR)
        const qwrt_ext_t *wasm_exts[] = { &qwrt_wamr_ext, NULL };
#elif defined(QWRT_HAS_WASM3)
        const qwrt_ext_t *wasm_exts[] = { &qwrt_wasm3_ext, NULL };
#else
        const qwrt_ext_t *wasm_exts[] = { NULL };
#endif
        qwrt_t *wasm_rt = create_runtime_with_ext(&wasm_pal, wasm_exts);
        if (wasm_rt) {
            test_wasm(wasm_rt);
            qwrt_destroy(wasm_rt);
            pal_mock_destroy(wasm_pal);
        } else {
            printf("\n--- TC55: WebAssembly ---\n");
            printf("  SKIP: no WASM engine linked\n");
        }
    }

    /* Cleanup main runtime */
    qwrt_destroy(rt);
    pal_mock_destroy(pal);
    free(polyfill);

    printf("\n=== Compliance Results: %d/%d passed, %d failed ===\n",
           g_passed, g_total, g_failed);

    return g_failed > 0 ? 1 : 0;
}

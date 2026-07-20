/*
 * test_compliance_gtest.cpp — Google Test version of test_compliance.c
 *
 * Tests qwrt's polyfill against ES2023 and WinterTC (now WinterTC)
 * Minimum Common Web Platform API specifications.
 *
 * Gracefully skips WASM tests when no WASM engine is linked.
 */

#include <gtest/gtest.h>

extern "C" {
#include "qwrt/qwrt.h"
#include "qwrt/ext_wasm3.h"
#include "pal_mock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
}

/* ================================================================
 * Base fixture: mock PAL + polyfill (auto-injected by qwrt_create)
 * ================================================================ */

class ComplianceTestBase : public ::testing::Test {
protected:
    qwrt_t *rt;
    qwrt_pal_t *pal;

    void SetUp() override {
        pal = pal_mock_create();
        ASSERT_NE(pal, nullptr);

        qwrt_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.pal = pal;
        rt = qwrt_create(&cfg);
        ASSERT_NE(rt, nullptr);
    }

    void TearDown() override {
        qwrt_destroy(rt);
        pal_mock_destroy(pal);
    }

    /* Helper: eval JS, return result string (caller must free via qwrt_free) */
    char *js_eval(const char *code) {
        char *result = NULL;
        int rc = qwrt_eval(rt, code, &result);
        if (rc != 0) return NULL;
        return result;
    }

    /* Eval and assert result equals expected JSON string */
    void js_assert_eq(const char *code, const char *expected) {
        char *result = js_eval(code);
        ASSERT_NE(result, nullptr) << "eval returned error for: " << code;
        EXPECT_STREQ(result, expected) << "for: " << code;
        if (result) qwrt_free(result);
    }

    /* Eval and assert result is truthy */
    void js_assert_truthy(const char *code) {
        char *result = js_eval(code);
        ASSERT_NE(result, nullptr) << "eval returned error for: " << code;
        EXPECT_STRNE(result, "false") << "got falsy result for: " << code;
        EXPECT_STRNE(result, "0") << "got falsy result for: " << code;
        EXPECT_STRNE(result, "null") << "got falsy result for: " << code;
        EXPECT_STRNE(result, "undefined") << "got falsy result for: " << code;
        EXPECT_STRNE(result, "\"\"") << "got falsy result for: " << code;
        EXPECT_STRNE(result, "{}") << "got falsy result for: " << code;
        EXPECT_STRNE(result, "[]") << "got falsy result for: " << code;
        if (result) qwrt_free(result);
    }

    /* Eval and assert result is falsy */
    void js_assert_falsy(const char *code) {
        char *result = js_eval(code);
        ASSERT_NE(result, nullptr) << "eval returned error for: " << code;
        bool is_falsy = (strcmp(result, "false") == 0 ||
                         strcmp(result, "0") == 0 ||
                         strcmp(result, "null") == 0 ||
                         strcmp(result, "undefined") == 0 ||
                         strcmp(result, "\"\"") == 0);
        EXPECT_TRUE(is_falsy) << "expected falsy, got " << result << " for: " << code;
        if (result) qwrt_free(result);
    }

    /* Eval and assert no error (rc == 0) */
    void js_assert_no_error(const char *code) {
        char *result = NULL;
        int rc = qwrt_eval(rt, code, &result);
        if (result) qwrt_free(result);
        EXPECT_EQ(rc, 0) << "threw error for: " << code;
    }
};

/* ================================================================
 * WinterTC: Console
 * ================================================================ */

TEST_F(ComplianceTestBase, ConsoleExists) {
    js_assert_truthy("typeof console !== 'undefined'");
}

TEST_F(ComplianceTestBase, ConsoleLogIsFunction) {
    js_assert_truthy("typeof console.log === 'function'");
}

TEST_F(ComplianceTestBase, ConsoleWarnErrorInfoDebug) {
    js_assert_truthy("typeof console.warn === 'function'");
    js_assert_truthy("typeof console.error === 'function'");
    js_assert_truthy("typeof console.info === 'function'");
    js_assert_truthy("typeof console.debug === 'function'");
}

TEST_F(ComplianceTestBase, ConsoleLogOutput) {
    qwrt_pal_t *pal2;
    qwrt_t *rt2;
    {
        pal2 = pal_mock_create();
        ASSERT_NE(pal2, nullptr);
        qwrt_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.pal = pal2;
        rt2 = qwrt_create(&cfg);
        ASSERT_NE(rt2, nullptr);
    }
    qwrt_eval(rt2, "console.log('hello', 'world')", NULL);
    int log_count = 0;
    const char *log = pal_mock_get_log(pal2, &log_count);
    EXPECT_EQ(log_count, 1);
    EXPECT_NE(strstr(log, "hello"), nullptr);
    EXPECT_NE(strstr(log, "world"), nullptr);
    pal_mock_clear_log(pal2);
    qwrt_destroy(rt2);
    pal_mock_destroy(pal2);
}

/* ================================================================
 * WinterTC: Timers
 * ================================================================ */

TEST_F(ComplianceTestBase, TimerFunctionsExist) {
    js_assert_truthy("typeof setTimeout === 'function'");
    js_assert_truthy("typeof setInterval === 'function'");
    js_assert_truthy("typeof clearTimeout === 'function'");
    js_assert_truthy("typeof clearInterval === 'function'");
}

TEST_F(ComplianceTestBase, SetTimeoutReturnsNumber) {
    js_assert_truthy("typeof setTimeout(function(){}, 0) === 'number'");
}

TEST_F(ComplianceTestBase, SetTimeoutCallbackFires) {
    qwrt_pal_t *pal2;
    qwrt_t *rt2;
    {
        pal2 = pal_mock_create();
        ASSERT_NE(pal2, nullptr);
        qwrt_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.pal = pal2;
        rt2 = qwrt_create(&cfg);
        ASSERT_NE(rt2, nullptr);
    }
    qwrt_eval(rt2, "var _fired = false; setTimeout(function() { _fired = true; }, 100)", NULL);
    qwrt_tick(rt2, 100);
    pal_mock_fire_all_timers(pal2);
    qwrt_tick(rt2, 100);
    {
        char *r = NULL;
        qwrt_eval(rt2, "_fired", &r);
        ASSERT_NE(r, nullptr);
        EXPECT_STREQ(r, "true");
        qwrt_free(r);
    }
    qwrt_destroy(rt2);
    pal_mock_destroy(pal2);
}

/* ================================================================
 * WinterTC: queueMicrotask
 * ================================================================ */

TEST_F(ComplianceTestBase, QueueMicrotaskExists) {
    js_assert_truthy("typeof queueMicrotask === 'function'");
}

TEST_F(ComplianceTestBase, QueueMicrotaskCallbackRuns) {
    qwrt_pal_t *pal2;
    qwrt_t *rt2;
    {
        pal2 = pal_mock_create();
        ASSERT_NE(pal2, nullptr);
        qwrt_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.pal = pal2;
        rt2 = qwrt_create(&cfg);
        ASSERT_NE(rt2, nullptr);
    }
    qwrt_eval(rt2, "var _mt = false; queueMicrotask(function() { _mt = true; })", NULL);
    qwrt_tick(rt2, 100);
    {
        char *r = NULL;
        qwrt_eval(rt2, "_mt", &r);
        ASSERT_NE(r, nullptr);
        EXPECT_STREQ(r, "true");
        qwrt_free(r);
    }
    qwrt_destroy(rt2);
    pal_mock_destroy(pal2);
}

/* ================================================================
 * WinterTC: URL / URLSearchParams
 * ================================================================ */

TEST_F(ComplianceTestBase, UrlConstructor) {
    js_assert_truthy("typeof URL === 'function'");
    js_assert_eq("new URL('https://example.com/path').protocol", "\"https:\"");
    js_assert_eq("new URL('https://example.com/path').hostname", "\"example.com\"");
    js_assert_eq("new URL('https://example.com/path?q=1').pathname", "\"/path\"");
    js_assert_eq("new URL('https://example.com/path?q=1#frag').hash", "\"#frag\"");
    js_assert_eq("new URL('https://example.com/path?q=1').search", "\"?q=1\"");
}

TEST_F(ComplianceTestBase, UrlSearchParams) {
    js_assert_truthy("typeof URLSearchParams === 'function'");
    js_assert_eq("new URLSearchParams('a=1&b=2').get('a')", "\"1\"");
    js_assert_eq("new URLSearchParams('a=1&b=2').get('b')", "\"2\"");
    js_assert_eq("new URLSearchParams('a=1&b=2').has('a')", "true");
    js_assert_eq("new URLSearchParams('a=1&b=2').has('c')", "false");
    js_assert_eq("new URL('https://example.com/?x=42').searchParams.get('x')", "\"42\"");
}

TEST_F(ComplianceTestBase, UrlSearchParamsAppend) {
    js_assert_no_error(
        "var p = new URLSearchParams(); p.append('key', 'val'); p.toString()");
}

TEST_F(ComplianceTestBase, UrlSearchParamsIterable) {
    js_assert_truthy("typeof new URLSearchParams('a=1')[Symbol.iterator] === 'function'");
}

TEST_F(ComplianceTestBase, UrlCanParse) {
    js_assert_truthy("URL.canParse('https://example.com')");
    js_assert_truthy("!URL.canParse('not a url')");
}

TEST_F(ComplianceTestBase, UrlRelativeResolution) {
    js_assert_eq(
        "new URL('/path', 'https://example.com').href",
        "\"https://example.com/path\"");
}

TEST_F(ComplianceTestBase, UrlToJSON) {
    js_assert_eq(
        "new URL('https://example.com').toJSON()",
        "\"https://example.com/\"");
}

TEST_F(ComplianceTestBase, UrlHostnameSetter) {
    js_assert_eq(
        "(function(){var u=new URL('https://example.com/');u.hostname='other.com';return u.hostname})()",
        "\"other.com\"");
}

TEST_F(ComplianceTestBase, UrlSearchParamsDelete) {
    js_assert_eq(
        "(function(){var p=new URLSearchParams('a=1&b=2');p.delete('a');return p.has('a');})()",
        "false");
}

TEST_F(ComplianceTestBase, UrlSearchParamsSet) {
    js_assert_eq(
        "(function(){var p=new URLSearchParams('a=1');p.set('a','2');return p.get('a');})()",
        "\"2\"");
}

TEST_F(ComplianceTestBase, UrlSearchParamsForEach) {
    js_assert_eq(
        "(function(){var p=new URLSearchParams('x=1&y=2');var keys=[];p.forEach(function(v,k){keys.push(k);});return keys.join(',');})()",
        "\"x,y\"");
}

TEST_F(ComplianceTestBase, UrlSearchParamsKeysValues) {
    js_assert_eq(
        "(function(){var p=new URLSearchParams('a=1&b=2');return [...p.keys()].join(',');})()",
        "\"a,b\"");
    js_assert_eq(
        "(function(){var p=new URLSearchParams('a=1&b=2');return [...p.values()].join(',');})()",
        "\"1,2\"");
}

/* ================================================================
 * WinterTC: TextEncoder / TextDecoder
 * ================================================================ */

TEST_F(ComplianceTestBase, TextEncoderDecoderExist) {
    js_assert_truthy("typeof TextEncoder === 'function'");
    js_assert_truthy("typeof TextDecoder === 'function'");
}

TEST_F(ComplianceTestBase, TextEncoderEncode) {
    js_assert_eq("new TextEncoder().encode('hello').length", "5");
}

TEST_F(ComplianceTestBase, TextDecoderDecode) {
    js_assert_truthy(
        "new TextDecoder().decode(new Uint8Array([104,101,108,108,111])) === 'hello'");
}

TEST_F(ComplianceTestBase, TextEncoderDecoderRoundtrip) {
    js_assert_truthy(
        "new TextDecoder().decode(new TextEncoder().encode('hello world')) === 'hello world'");
}

TEST_F(ComplianceTestBase, Utf8NonAsciiRoundtrip) {
    js_assert_truthy(
        "new TextDecoder().decode(new TextEncoder().encode('\\u00e9')) === '\\u00e9'");
}

TEST_F(ComplianceTestBase, TextEncoderEncoding) {
    js_assert_eq("new TextEncoder().encoding", "\"utf-8\"");
}

TEST_F(ComplianceTestBase, TextDecoderEncoding) {
    js_assert_eq("new TextDecoder().encoding", "\"utf-8\"");
}

/* ================================================================
 * WinterTC: atob / btoa
 * ================================================================ */

TEST_F(ComplianceTestBase, AtobBtoaExist) {
    js_assert_truthy("typeof btoa === 'function'");
    js_assert_truthy("typeof atob === 'function'");
}

TEST_F(ComplianceTestBase, BtoaEncoding) {
    js_assert_eq("btoa('hello')", "\"aGVsbG8=\"");
    js_assert_eq("atob('aGVsbG8=')", "\"hello\"");
    js_assert_truthy("atob(btoa('test123')) === 'test123'");
}

TEST_F(ComplianceTestBase, AtobBtoaEmpty) {
    js_assert_eq("btoa('')", "\"\"");
    js_assert_eq("atob('')", "\"\"");
}

/* ================================================================
 * WinterTC: Fetch API
 * ================================================================ */

TEST_F(ComplianceTestBase, FetchApiExists) {
    js_assert_truthy("typeof fetch === 'function'");
    js_assert_truthy("typeof Request === 'function'");
    js_assert_truthy("typeof Response === 'function'");
    js_assert_truthy("typeof Headers === 'function'");
}

TEST_F(ComplianceTestBase, HeadersAppend) {
    js_assert_no_error("var h = new Headers(); h.append('Content-Type', 'text/plain')");
    js_assert_eq("new Headers({'x-test':'val'}).get('x-test')", "\"val\"");
}

TEST_F(ComplianceTestBase, RequestConstructor) {
    js_assert_no_error("var r = new Request('http://example.com')");
    js_assert_eq("new Request('http://example.com').url", "\"http://example.com\"");
}

TEST_F(ComplianceTestBase, ResponseConstructor) {
    js_assert_no_error("var res = new Response('body')");
    js_assert_eq("new Response(null, {status: 201}).status", "201");
}

TEST_F(ComplianceTestBase, FetchWithMock) {
    qwrt_pal_t *pal2;
    qwrt_t *rt2;
    {
        pal2 = pal_mock_create();
        ASSERT_NE(pal2, nullptr);
        qwrt_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.pal = pal2;
        rt2 = qwrt_create(&cfg);
        ASSERT_NE(rt2, nullptr);
    }
    pal_mock_set_http_response(pal2,
        "{\"status\":200,\"headers\":{\"Content-Type\":\"text/plain\"},\"body\":\"ok\"}");
    qwrt_eval(rt2,
        "var _fres = null; fetch('http://test.com/').then(function(r) { return r.text(); })"
        ".then(function(t) { _fres = t; })", NULL);
    qwrt_tick(rt2, 100);
    {
        char *r = NULL;
        qwrt_eval(rt2, "_fres", &r);
        ASSERT_NE(r, nullptr);
        EXPECT_STREQ(r, "\"ok\"");
        qwrt_free(r);
    }
    qwrt_destroy(rt2);
    pal_mock_destroy(pal2);
}

/* ================================================================
 * WinterTC: AbortController / AbortSignal
 * ================================================================ */

TEST_F(ComplianceTestBase, AbortControllerExists) {
    js_assert_truthy("typeof AbortController === 'function'");
    js_assert_truthy("typeof AbortSignal === 'function'");
}

TEST_F(ComplianceTestBase, AbortControllerConstructor) {
    js_assert_no_error("var ac = new AbortController()");
    js_assert_truthy("typeof new AbortController().signal === 'object'");
    js_assert_eq("new AbortController().signal.aborted", "false");
}

TEST_F(ComplianceTestBase, AbortSignalAborted) {
    js_assert_no_error("var ac2 = new AbortController(); ac2.abort()");
    js_assert_eq("(function(){var a=new AbortController();a.abort();return a.signal.aborted})()",
                 "true");
}

TEST_F(ComplianceTestBase, AbortSignalStaticAbort) {
    js_assert_eq("AbortSignal.abort().aborted", "true");
    js_assert_eq("AbortSignal.abort('stopped').reason", "\"stopped\"");
}

TEST_F(ComplianceTestBase, AbortSignalAny) {
    js_assert_eq("AbortSignal.any([AbortSignal.abort()]).aborted", "true");
    js_assert_eq("AbortSignal.any([new AbortController().signal]).aborted", "false");
}

TEST_F(ComplianceTestBase, AbortSignalTimeout) {
    qwrt_pal_t *pal2;
    qwrt_t *rt2;
    {
        pal2 = pal_mock_create();
        ASSERT_NE(pal2, nullptr);
        qwrt_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.pal = pal2;
        rt2 = qwrt_create(&cfg);
        ASSERT_NE(rt2, nullptr);
    }
    qwrt_eval(rt2,
        "var _timeoutFired = false; "
        "var sig = AbortSignal.timeout(50); "
        "sig.addEventListener('abort', function() { _timeoutFired = sig.aborted; });", NULL);
    for (int i = 0; i < 50; i++) qwrt_tick(rt2, 100);
    pal_mock_fire_all_timers(pal2);
    for (int i = 0; i < 50; i++) qwrt_tick(rt2, 100);
    {
        char *r = NULL;
        qwrt_eval(rt2, "_timeoutFired", &r);
        ASSERT_NE(r, nullptr);
        EXPECT_STREQ(r, "true");
        qwrt_free(r);
    }
    qwrt_destroy(rt2);
    pal_mock_destroy(pal2);
}

TEST_F(ComplianceTestBase, AbortSignalAnyPropagates) {
    qwrt_pal_t *pal2;
    qwrt_t *rt2;
    {
        pal2 = pal_mock_create();
        ASSERT_NE(pal2, nullptr);
        qwrt_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.pal = pal2;
        rt2 = qwrt_create(&cfg);
        ASSERT_NE(rt2, nullptr);
    }
    qwrt_eval(rt2,
        "var _anyAborted = false; "
        "var ac = new AbortController(); "
        "var combined = AbortSignal.any([ac.signal]); "
        "combined.addEventListener('abort', function() { _anyAborted = combined.aborted; }); "
        "setTimeout(function() { ac.abort(); }, 50);", NULL);
    for (int i = 0; i < 50; i++) qwrt_tick(rt2, 100);
    pal_mock_fire_all_timers(pal2);
    for (int i = 0; i < 50; i++) qwrt_tick(rt2, 100);
    {
        char *r = NULL;
        qwrt_eval(rt2, "_anyAborted", &r);
        ASSERT_NE(r, nullptr);
        EXPECT_STREQ(r, "true");
        qwrt_free(r);
    }
    qwrt_destroy(rt2);
    pal_mock_destroy(pal2);
}

TEST_F(ComplianceTestBase, ThrowIfAborted) {
    js_assert_truthy(
        "(function(){try{AbortSignal.abort().throwIfAborted();return false}catch(e){return true}})()");
    js_assert_truthy(
        "(function(){try{new AbortController().signal.throwIfAborted();return true}catch(e){return false}})()");
}

/* ================================================================
 * WinterTC: Event / EventTarget
 * ================================================================ */

TEST_F(ComplianceTestBase, EventTargetExists) {
    js_assert_truthy("typeof Event === 'function'");
    js_assert_truthy("typeof EventTarget === 'function'");
}

TEST_F(ComplianceTestBase, EventConstructor) {
    js_assert_no_error("var e = new Event('test')");
    js_assert_eq("new Event('test').type", "\"test\"");
}

TEST_F(ComplianceTestBase, EventTargetDispatch) {
    js_assert_no_error("var et = new EventTarget()");
    js_assert_truthy("typeof new EventTarget().addEventListener === 'function'");
    js_assert_truthy("typeof new EventTarget().dispatchEvent === 'function'");
}

TEST_F(ComplianceTestBase, EventTargetDispatchListener) {
    qwrt_pal_t *pal2;
    qwrt_t *rt2;
    {
        pal2 = pal_mock_create();
        ASSERT_NE(pal2, nullptr);
        qwrt_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.pal = pal2;
        rt2 = qwrt_create(&cfg);
        ASSERT_NE(rt2, nullptr);
    }
    qwrt_eval(rt2,
        "var _evtData = null; var et = new EventTarget(); "
        "et.addEventListener('test', function(e) { _evtData = e.type; }); "
        "et.dispatchEvent(new Event('test'))", NULL);
    {
        char *r = NULL;
        qwrt_eval(rt2, "_evtData", &r);
        ASSERT_NE(r, nullptr);
        EXPECT_STREQ(r, "\"test\"");
        qwrt_free(r);
    }
    qwrt_destroy(rt2);
    pal_mock_destroy(pal2);
}

TEST_F(ComplianceTestBase, OnceListener) {
    js_assert_eq(
        "(function(){"
        "  var count = 0; var et = new EventTarget();"
        "  et.addEventListener('ping', function() { count++; }, {once: true});"
        "  et.dispatchEvent(new Event('ping'));"
        "  et.dispatchEvent(new Event('ping'));"
        "  return count;"
        "})()",
        "1");
}

TEST_F(ComplianceTestBase, RemoveEventListener) {
    js_assert_eq(
        "(function(){"
        "  var count = 0; var et = new EventTarget();"
        "  var fn = function() { count++; };"
        "  et.addEventListener('ping', fn);"
        "  et.dispatchEvent(new Event('ping'));"
        "  et.removeEventListener('ping', fn);"
        "  et.dispatchEvent(new Event('ping'));"
        "  return count;"
        "})()",
        "1");
}

TEST_F(ComplianceTestBase, StopPropagation) {
    js_assert_eq(
        "(function(){"
        "  var order = []; var et = new EventTarget();"
        "  et.addEventListener('a', function(e) { order.push(1); e.stopPropagation(); });"
        "  et.addEventListener('a', function(e) { order.push(2); });"
        "  et.dispatchEvent(new Event('a'));"
        "  return order.join(',');"
        "})()",
        "\"1,2\"");
}

TEST_F(ComplianceTestBase, StopImmediatePropagation) {
    js_assert_eq(
        "(function(){"
        "  var order = []; var et = new EventTarget();"
        "  et.addEventListener('a', function(e) { order.push(1); e.stopImmediatePropagation(); });"
        "  et.addEventListener('a', function(e) { order.push(2); });"
        "  et.dispatchEvent(new Event('a'));"
        "  return order.join(',');"
        "})()",
        "\"1\"");
}

TEST_F(ComplianceTestBase, PreventDefault) {
    js_assert_eq(
        "(function(){"
        "  var et = new EventTarget();"
        "  et.addEventListener('a', function(e) { e.preventDefault(); });"
        "  var e = new Event('a', {cancelable: true});"
        "  et.dispatchEvent(e);"
        "  return e.defaultPrevented;"
        "})()",
        "true");
}

TEST_F(ComplianceTestBase, PreventDefaultNonCancelable) {
    js_assert_eq(
        "(function(){"
        "  var et = new EventTarget();"
        "  et.addEventListener('a', function(e) { e.preventDefault(); });"
        "  var e = new Event('a', {cancelable: false});"
        "  et.dispatchEvent(e);"
        "  return e.defaultPrevented;"
        "})()",
        "false");
}

TEST_F(ComplianceTestBase, CustomEventDetail) {
    js_assert_eq(
        "(function(){"
        "  var got = null; var et = new EventTarget();"
        "  et.addEventListener('x', function(e) { got = e.detail; });"
        "  et.dispatchEvent(new CustomEvent('x', {detail: 42}));"
        "  return got;"
        "})()",
        "42");
}

TEST_F(ComplianceTestBase, EventBubblesCancelable) {
    js_assert_eq("new Event('t', {bubbles: true}).bubbles", "true");
    js_assert_eq("new Event('t', {cancelable: true}).cancelable", "true");
}

/* ================================================================
 * WinterTC: Crypto
 * ================================================================ */

TEST_F(ComplianceTestBase, CryptoExists) {
    js_assert_truthy("typeof crypto !== 'undefined'");
    js_assert_truthy("typeof crypto.getRandomValues === 'function'");
}

TEST_F(ComplianceTestBase, CryptoGetRandomValues) {
    js_assert_truthy(
        "crypto.getRandomValues(new Uint8Array(16)) instanceof Uint8Array");
}

/* Web Crypto spec: getRandomValues must reject > 65536 bytes with a
 * QuotaExceededError. The cap is enforced in the JS polyfill (not the C
 * bridge), so this also guards the bridge-thinness discipline. */
TEST_F(ComplianceTestBase, CryptoGetRandomValuesQuotaCap) {
    js_assert_eq(
        "(function(){ try { crypto.getRandomValues(new Uint8Array(65537)); return 'no-throw'; } "
        "catch(e){ return e && e.name ? e.name : String(e); } })()",
        "\"QuotaExceededError\"");
    /* Exactly 65536 is allowed (the cap is strict >). */
    js_assert_truthy(
        "crypto.getRandomValues(new Uint8Array(65536)) instanceof Uint8Array");
}

TEST_F(ComplianceTestBase, CryptoRandomIsNotDeterministic) {
    js_assert_falsy(
        "(function() {"
        "  var a = new Uint8Array(16);"
        "  var b = new Uint8Array(16);"
        "  crypto.getRandomValues(a);"
        "  crypto.getRandomValues(b);"
        "  var same = true;"
        "  for (var i = 0; i < 16; i++) { if (a[i] !== b[i]) { same = false; break; } }"
        "  return same;"
        "})()");
}

TEST_F(ComplianceTestBase, RandomUUID) {
    js_assert_truthy(
        "(function() {"
        "  var u = crypto.randomUUID();"
        "  return typeof u === 'string' && /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/.test(u);"
        "})()");
}

TEST_F(ComplianceTestBase, CryptoSubtleExists) {
    char *subtle_result = js_eval("typeof crypto.subtle");
    ASSERT_NE(subtle_result, nullptr);
    if (strcmp(subtle_result, "\"object\"") == 0) {
        qwrt_free(subtle_result);
        js_assert_truthy("typeof crypto.subtle.digest === 'function'");
    } else {
        /* crypto.subtle may not be available in all environments — not a failure */
        qwrt_free(subtle_result);
        GTEST_SKIP() << "crypto.subtle not implemented in this build";
    }
}

/* ================================================================
 * WinterTC: Performance
 * ================================================================ */

TEST_F(ComplianceTestBase, PerformanceExists) {
    js_assert_truthy("typeof performance !== 'undefined'");
    js_assert_truthy("typeof performance.now === 'function'");
}

TEST_F(ComplianceTestBase, PerformanceNow) {
    js_assert_truthy("typeof performance.now() === 'number'");
    js_assert_truthy("performance.now() >= 0");
}

TEST_F(ComplianceTestBase, PerformanceMark) {
    js_assert_no_error("performance.mark('start')");
    js_assert_eq("performance.getEntriesByName('start')[0].entryType", "\"mark\"");
    js_assert_eq("performance.getEntriesByName('start')[0].name", "\"start\"");
}

TEST_F(ComplianceTestBase, PerformanceMeasure) {
    js_assert_no_error(
        "performance.mark('start'); "
        "performance.mark('end'); "
        "performance.measure('elapsed', 'start', 'end')");
    js_assert_eq("performance.getEntriesByName('elapsed')[0].entryType", "\"measure\"");
    js_assert_truthy("performance.getEntriesByName('elapsed')[0].duration >= 0");
}

TEST_F(ComplianceTestBase, PerformanceGetEntriesByType) {
    js_assert_no_error(
        "performance.mark('m1'); performance.mark('m2'); "
        "performance.measure('me', 'm1', 'm2')");
    js_assert_truthy("performance.getEntriesByType('mark').length >= 2");
    js_assert_truthy("performance.getEntriesByType('measure').length >= 1");
}

TEST_F(ComplianceTestBase, PerformanceGetEntries) {
    js_assert_no_error(
        "performance.mark('a'); performance.mark('b'); performance.mark('c'); "
        "performance.measure('x', 'a', 'c')");
    js_assert_truthy("performance.getEntries().length >= 4");
}

TEST_F(ComplianceTestBase, PerformanceClearMarks) {
    js_assert_no_error(
        "performance.mark('temp'); performance.clearMarks('temp'); "
        "performance.getEntriesByName('temp').length === 0");
}

TEST_F(ComplianceTestBase, PerformanceClearMeasures) {
    js_assert_no_error(
        "performance.mark('a'); performance.mark('b'); "
        "performance.measure('tmp', 'a', 'b'); performance.clearMeasures('tmp'); "
        "performance.getEntriesByName('tmp').length === 0");
}

TEST_F(ComplianceTestBase, PerformanceTimeOrigin) {
    js_assert_truthy("typeof performance.timeOrigin === 'number'");
}

/* ================================================================
 * WinterTC: Storage
 * ================================================================ */

TEST_F(ComplianceTestBase, StorageApiExists) {
    js_assert_truthy(
        "typeof qwrt !== 'undefined' && typeof qwrt.storage !== 'undefined'");
}

TEST_F(ComplianceTestBase, StorageSetGet) {
    qwrt_pal_t *pal2;
    qwrt_t *rt2;
    {
        pal2 = pal_mock_create();
        ASSERT_NE(pal2, nullptr);
        qwrt_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.pal = pal2;
        rt2 = qwrt_create(&cfg);
        ASSERT_NE(rt2, nullptr);
    }
    qwrt_eval(rt2,
        "var _storOk = false; "
        "if (typeof qwrt !== 'undefined' && qwrt.storage && typeof qwrt.storage.set === 'function') { "
        "  qwrt.storage.set('k','v').then(function(){ _storOk = true; }); "
        "} else if (typeof localStorage !== 'undefined') { "
        "  localStorage.setItem('k','v'); _storOk = true; "
        "} else if (typeof storage !== 'undefined' && typeof storage.set === 'function') { "
        "  storage.set('k','v').then(function(){ _storOk = true; }); "
        "}", NULL);
    qwrt_tick(rt2, 100);
    {
        char *r = NULL;
        qwrt_eval(rt2, "_storOk", &r);
        ASSERT_NE(r, nullptr);
        EXPECT_STREQ(r, "true");
        qwrt_free(r);
    }
    qwrt_destroy(rt2);
    pal_mock_destroy(pal2);
}

/* ================================================================
 * ES2023: Array
 * ================================================================ */

TEST_F(ComplianceTestBase, ArrayAt) {
    js_assert_eq("[1,2,3].at(0)", "1");
    js_assert_eq("[1,2,3].at(-1)", "3");
}

TEST_F(ComplianceTestBase, ArrayFindLast) {
    js_assert_eq("[1,2,3,4].findLast(function(x){return x%2===0})", "4");
}

TEST_F(ComplianceTestBase, ArrayFindLastIndex) {
    js_assert_eq("[1,2,3,4].findLastIndex(function(x){return x%2===0})", "3");
}

TEST_F(ComplianceTestBase, ArrayToSorted) {
    js_assert_eq("[3,1,2].toSorted().toString()", "\"1,2,3\"");
}

TEST_F(ComplianceTestBase, ArrayToReversed) {
    js_assert_eq("[1,2,3].toReversed().toString()", "\"3,2,1\"");
}

TEST_F(ComplianceTestBase, ArrayToSpliced) {
    js_assert_eq("[1,2,3].toSpliced(1,1,4,5).toString()", "\"1,4,5,3\"");
}

TEST_F(ComplianceTestBase, ArrayToSortedNonMutating) {
    js_assert_eq("(function(){var a=[3,1,2];a.toSorted();return a.toString()})()",
                 "\"3,1,2\"");
}

TEST_F(ComplianceTestBase, ArrayWith) {
    js_assert_eq("[1,2,3].with(1, 99).toString()", "\"1,99,3\"");
}

/* ================================================================
 * ES2023: Object
 * ================================================================ */

TEST_F(ComplianceTestBase, ObjectHasOwn) {
    js_assert_eq("Object.hasOwn({a:1}, 'a')", "true");
    js_assert_eq("Object.hasOwn({a:1}, 'b')", "false");
    js_assert_eq("Object.hasOwn(Object.create({a:1}), 'a')", "false");
}

/* ================================================================
 * ES2023: String
 * ================================================================ */

TEST_F(ComplianceTestBase, StringReplaceAll) {
    js_assert_eq("'aaa'.replaceAll('a', 'b')", "\"bbb\"");
    js_assert_eq("'abcabc'.replaceAll('abc', 'x')", "\"xx\"");
    js_assert_eq("'hello'.replaceAll('x', 'y')", "\"hello\"");
}

TEST_F(ComplianceTestBase, StringAt) {
    js_assert_eq("'hello'.at(0)", "\"h\"");
    js_assert_eq("'hello'.at(-1)", "\"o\"");
}

TEST_F(ComplianceTestBase, StringMatchAll) {
    js_assert_truthy("typeof 'abcabc'.matchAll(/ab/g) === 'object'");
}

TEST_F(ComplianceTestBase, StringTrimStartEnd) {
    js_assert_eq("'  hi  '.trimStart()", "\"hi  \"");
    js_assert_eq("'  hi  '.trimEnd()", "\"  hi\"");
}

/* ================================================================
 * ES2023: Promise
 * ================================================================ */

TEST_F(ComplianceTestBase, PromiseExists) {
    js_assert_truthy("typeof Promise === 'function'");
}

TEST_F(ComplianceTestBase, PromiseCombinators) {
    js_assert_truthy("typeof Promise.allSettled === 'function'");
    js_assert_truthy("typeof Promise.any === 'function'");
    js_assert_truthy("typeof Promise.all === 'function'");
    js_assert_truthy("typeof Promise.race === 'function'");
}

TEST_F(ComplianceTestBase, PromiseResolveThen) {
    qwrt_pal_t *pal2;
    qwrt_t *rt2;
    {
        pal2 = pal_mock_create();
        ASSERT_NE(pal2, nullptr);
        qwrt_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.pal = pal2;
        rt2 = qwrt_create(&cfg);
        ASSERT_NE(rt2, nullptr);
    }
    qwrt_eval(rt2,
        "var _pval = null; Promise.resolve(42).then(function(v) { _pval = v; })", NULL);
    qwrt_tick(rt2, 100);
    {
        char *r = NULL;
        qwrt_eval(rt2, "_pval", &r);
        ASSERT_NE(r, nullptr);
        EXPECT_STREQ(r, "42");
        qwrt_free(r);
    }
    qwrt_destroy(rt2);
    pal_mock_destroy(pal2);
}

/* ================================================================
 * ES2023: Operators
 * ================================================================ */

TEST_F(ComplianceTestBase, OptionalChaining) {
    js_assert_eq("var o = {a:{b:1}}; o?.a?.b", "1");
    js_assert_eq("var o2 = null; o2?.a", "undefined");
    js_assert_eq("var o3; o3?.a", "undefined");
}

TEST_F(ComplianceTestBase, NullishCoalescing) {
    js_assert_eq("null ?? 'default'", "\"default\"");
    js_assert_eq("undefined ?? 'default'", "\"default\"");
    js_assert_eq("0 ?? 'default'", "0");
    js_assert_eq("'' ?? 'default'", "\"\"");
    js_assert_eq("false ?? true", "false");
}

/* ================================================================
 * ES2023: Symbol
 * ================================================================ */

TEST_F(ComplianceTestBase, SymbolExists) {
    js_assert_truthy("typeof Symbol === 'function'");
    js_assert_truthy("typeof Symbol.iterator === 'symbol'");
    js_assert_truthy("typeof Symbol.toStringTag === 'symbol'");
}

TEST_F(ComplianceTestBase, SymbolFor) {
    js_assert_truthy("typeof Symbol.for === 'function'");
    js_assert_eq("Symbol.for('abc') === Symbol.for('abc')", "true");
}

TEST_F(ComplianceTestBase, SymbolToPrimitiveAsyncIterator) {
    js_assert_truthy("typeof Symbol.toPrimitive === 'symbol'");
    js_assert_truthy("typeof Symbol.asyncIterator === 'symbol'");
}

/* ================================================================
 * ES2023: WeakRef / FinalizationRegistry
 * ================================================================ */

TEST_F(ComplianceTestBase, WeakRefExists) {
    js_assert_truthy("typeof WeakRef === 'function'");
    js_assert_truthy("typeof FinalizationRegistry === 'function'");
}

TEST_F(ComplianceTestBase, WeakRefConstructor) {
    js_assert_no_error("var _wrObj = {a:1}; var wr = new WeakRef(_wrObj)");
    js_assert_truthy("wr.deref() !== undefined");
}

TEST_F(ComplianceTestBase, FinalizationRegistryConstructor) {
    js_assert_no_error("var fr = new FinalizationRegistry(function(){})");
}

/* ================================================================
 * ES2023: TypedArray
 * ================================================================ */

TEST_F(ComplianceTestBase, TypedArrayAt) {
    js_assert_eq("new Uint8Array([10,20,30]).at(-1)", "30");
}

TEST_F(ComplianceTestBase, TypedArrayFindLast) {
    js_assert_eq("new Uint8Array([1,2,3,4]).findLast(function(x){return x%2===0})", "4");
}

TEST_F(ComplianceTestBase, TypedArrayToSorted) {
    js_assert_eq("new Uint8Array([3,1,2]).toSorted().toString()", "\"1,2,3\"");
}

/* ================================================================
 * ES2023: Iteration
 * ================================================================ */

TEST_F(ComplianceTestBase, ForOf) {
    js_assert_eq("(function(){var s=''; for(var x of [1,2,3]){s+=x;} return s;})()",
                 "\"123\"");
}

TEST_F(ComplianceTestBase, SpreadOperator) {
    js_assert_eq("[...[1,2],...[3,4]].toString()", "\"1,2,3,4\"");
}

TEST_F(ComplianceTestBase, GeneratorFunctions) {
    js_assert_truthy(
        "typeof (function*(){yield 1;}) === 'function'");
    js_assert_eq(
        "(function(){var g=(function*(){yield 1;yield 2;});var r=[];for(var v of g()){r.push(v);}return r.toString();})()",
        "\"1,2\"");
}

TEST_F(ComplianceTestBase, MapSet) {
    js_assert_truthy("typeof Map === 'function'");
    js_assert_truthy("typeof Set === 'function'");
    js_assert_eq("new Map([['a',1]]).get('a')", "1");
    js_assert_truthy("new Set([1,2,3]).has(2)");
}

/* ================================================================
 * ES2023: Error types
 * ================================================================ */

TEST_F(ComplianceTestBase, ErrorTypesExist) {
    js_assert_truthy("typeof Error === 'function'");
    js_assert_truthy("typeof TypeError === 'function'");
    js_assert_truthy("typeof RangeError === 'function'");
    js_assert_truthy("typeof SyntaxError === 'function'");
    js_assert_truthy("typeof URIError === 'function'");
}

TEST_F(ComplianceTestBase, ErrorCause) {
    js_assert_truthy(
        "new Error('msg', {cause: 'reason'}).cause === 'reason'");
}

/* ================================================================
 * ES2023: structuredClone
 * ================================================================ */

TEST_F(ComplianceTestBase, StructuredCloneExists) {
    js_assert_truthy("typeof structuredClone === 'function'");
}

TEST_F(ComplianceTestBase, StructuredClonePrimitives) {
    js_assert_eq("structuredClone(42)", "42");
    js_assert_eq("structuredClone('hello')", "\"hello\"");
    js_assert_truthy("structuredClone({a:1}).a === 1");
}

TEST_F(ComplianceTestBase, StructuredCloneDeep) {
    js_assert_truthy(
        "(function(){var o={a:{b:1}};var c=structuredClone(o);o.a.b=2;return c.a.b===1;})()");
}

TEST_F(ComplianceTestBase, StructuredCloneArray) {
    js_assert_truthy("structuredClone([1,2,3]).length === 3");
}

/* ================================================================
 * ES2023: Misc
 * ================================================================ */

TEST_F(ComplianceTestBase, GlobalThis) {
    js_assert_truthy("typeof globalThis === 'object'");
}

TEST_F(ComplianceTestBase, BigInt) {
    js_assert_truthy("typeof BigInt === 'function'");
    js_assert_truthy("BigInt(123) === 123n");
}

TEST_F(ComplianceTestBase, ProxyReflect) {
    js_assert_truthy("typeof Proxy === 'function'");
    js_assert_truthy("typeof Reflect === 'object'");
}

TEST_F(ComplianceTestBase, ArrayFlatFlatMap) {
    js_assert_eq("[[1,2],[3,4]].flat().toString()", "\"1,2,3,4\"");
    js_assert_eq("[1,2,3].flatMap(function(x){return [x,x*2]}).toString()",
                 "\"1,2,2,4,3,6\"");
}

TEST_F(ComplianceTestBase, ObjectFromEntries) {
    js_assert_eq("Object.fromEntries([['a',1],['b',2]]).a", "1");
}

TEST_F(ComplianceTestBase, StringArrayIncludes) {
    js_assert_eq("'hello world'.includes('world')", "true");
    js_assert_eq("[1,2,3].includes(2)", "true");
    js_assert_eq("[1,2,3].includes(5)", "false");
}

TEST_F(ComplianceTestBase, Exponentiation) {
    js_assert_eq("2 ** 10", "1024");
}

TEST_F(ComplianceTestBase, AsyncFunctions) {
    js_assert_truthy("typeof (async function(){}) === 'function'");
}

TEST_F(ComplianceTestBase, ForAwaitOf) {
    js_assert_truthy(
        "(function(){try{new Function('async function f(){for await(var x of []){}}');return true}catch(e){return false}})()");
}

/* ================================================================
 * TC55: MessageChannel
 * ================================================================ */

TEST_F(ComplianceTestBase, MessageChannelExists) {
    js_assert_truthy("typeof MessageChannel === 'function'");
    js_assert_truthy("typeof MessagePort === 'function'");
    js_assert_truthy("typeof MessageEvent === 'function'");
}

TEST_F(ComplianceTestBase, MessageChannelPorts) {
    js_assert_truthy("var mc = new MessageChannel(); mc.port1 instanceof MessagePort");
    js_assert_truthy("var mc2 = new MessageChannel(); mc2.port2 instanceof MessagePort");
}

TEST_F(ComplianceTestBase, MessageEventData) {
    js_assert_eq("new MessageEvent('message', {data: 42}).data", "42");
}

TEST_F(ComplianceTestBase, MessageChannelPostMessage) {
    qwrt_pal_t *pal2;
    qwrt_t *rt2;
    {
        pal2 = pal_mock_create();
        ASSERT_NE(pal2, nullptr);
        qwrt_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.pal = pal2;
        rt2 = qwrt_create(&cfg);
        ASSERT_NE(rt2, nullptr);
    }
    qwrt_eval(rt2,
        "var _msgData = null; "
        "var mc = new MessageChannel(); "
        "mc.port2.onmessage = function(e) { _msgData = e.data; }; "
        "mc.port1.postMessage('hello')", NULL);
    qwrt_tick(rt2, 100);
    {
        char *r = NULL;
        qwrt_eval(rt2, "_msgData", &r);
        ASSERT_NE(r, nullptr);
        EXPECT_STREQ(r, "\"hello\"");
        qwrt_free(r);
    }
    qwrt_destroy(rt2);
    pal_mock_destroy(pal2);
}

/* ================================================================
 * TC55: ErrorEvent / PromiseRejectionEvent
 * ================================================================ */

TEST_F(ComplianceTestBase, ErrorEventExists) {
    js_assert_truthy("typeof ErrorEvent === 'function'");
    js_assert_truthy("typeof PromiseRejectionEvent === 'function'");
}

TEST_F(ComplianceTestBase, ErrorEventProperties) {
    js_assert_eq("new ErrorEvent('error', {message: 'test'}).message", "\"test\"");
    js_assert_eq("new ErrorEvent('error', {filename: 'app.js'}).filename", "\"app.js\"");
}

TEST_F(ComplianceTestBase, PromiseRejectionEventReason) {
    js_assert_truthy(
        "new PromiseRejectionEvent('unhandledrejection', {promise: Promise.resolve(), reason: 'err'}).reason === 'err'");
}

/* ================================================================
 * TC55: Streams
 * ================================================================ */

TEST_F(ComplianceTestBase, StreamClassesExist) {
    js_assert_truthy("typeof ReadableStream === 'function'");
    js_assert_truthy("typeof WritableStream === 'function'");
    js_assert_truthy("typeof TransformStream === 'function'");
    js_assert_truthy("typeof CompressionStream === 'function'");
    js_assert_truthy("typeof DecompressionStream === 'function'");
}

TEST_F(ComplianceTestBase, StreamGlobals) {
    js_assert_truthy("typeof ReadableStreamDefaultController === 'function'");
    js_assert_truthy("typeof ReadableStreamDefaultReader === 'function'");
    js_assert_truthy("typeof WritableStreamDefaultController === 'function'");
    js_assert_truthy("typeof WritableStreamDefaultWriter === 'function'");
    js_assert_truthy("typeof ByteLengthQueuingStrategy === 'function'");
    js_assert_truthy("typeof CountQueuingStrategy === 'function'");
    js_assert_truthy("typeof TextEncoderStream === 'function'");
    js_assert_truthy("typeof TextDecoderStream === 'function'");
}

TEST_F(ComplianceTestBase, ReadableStreamConstructor) {
    js_assert_no_error(
        "var rs = new ReadableStream({start: function(c){c.enqueue('hi');c.close();}})");
}

TEST_F(ComplianceTestBase, WritableStreamGetWriter) {
    js_assert_truthy(
        "new WritableStream().getWriter() instanceof WritableStreamDefaultWriter");
}

TEST_F(ComplianceTestBase, WritableStreamWriteClose) {
    qwrt_pal_t *pal2;
    qwrt_t *rt2;
    {
        pal2 = pal_mock_create();
        ASSERT_NE(pal2, nullptr);
        qwrt_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.pal = pal2;
        rt2 = qwrt_create(&cfg);
        ASSERT_NE(rt2, nullptr);
    }
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
    for (int i = 0; i < 50; i++) qwrt_tick(rt2, 100);
    {
        char *r = NULL;
        qwrt_eval(rt2, "_wsResult", &r);
        ASSERT_NE(r, nullptr);
        EXPECT_STREQ(r, "\"x,y,z\"");
        qwrt_free(r);
    }
    qwrt_destroy(rt2);
    pal_mock_destroy(pal2);
}

TEST_F(ComplianceTestBase, TransformStreamFlow) {
    qwrt_pal_t *pal2;
    qwrt_t *rt2;
    {
        pal2 = pal_mock_create();
        ASSERT_NE(pal2, nullptr);
        qwrt_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.pal = pal2;
        rt2 = qwrt_create(&cfg);
        ASSERT_NE(rt2, nullptr);
    }
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
    for (int i = 0; i < 50; i++) qwrt_tick(rt2, 100);
    {
        char *r = NULL;
        qwrt_eval(rt2, "_tsResult", &r);
        ASSERT_NE(r, nullptr);
        EXPECT_STREQ(r, "\"HELLO,WORLD\"");
        qwrt_free(r);
    }
    qwrt_destroy(rt2);
    pal_mock_destroy(pal2);
}

TEST_F(ComplianceTestBase, ReadableStreamPipeTo) {
    qwrt_pal_t *pal2;
    qwrt_t *rt2;
    {
        pal2 = pal_mock_create();
        ASSERT_NE(pal2, nullptr);
        qwrt_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.pal = pal2;
        rt2 = qwrt_create(&cfg);
        ASSERT_NE(rt2, nullptr);
    }
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
    for (int i = 0; i < 50; i++) qwrt_tick(rt2, 100);
    {
        char *r = NULL;
        qwrt_eval(rt2, "_pipeResult", &r);
        ASSERT_NE(r, nullptr);
        EXPECT_STREQ(r, "\"ab\"");
        qwrt_free(r);
    }
    qwrt_destroy(rt2);
    pal_mock_destroy(pal2);
}

TEST_F(ComplianceTestBase, ReadableStreamPipeThrough) {
    qwrt_pal_t *pal2;
    qwrt_t *rt2;
    {
        pal2 = pal_mock_create();
        ASSERT_NE(pal2, nullptr);
        qwrt_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.pal = pal2;
        rt2 = qwrt_create(&cfg);
        ASSERT_NE(rt2, nullptr);
    }
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
    for (int i = 0; i < 50; i++) qwrt_tick(rt2, 100);
    {
        char *r = NULL;
        qwrt_eval(rt2, "_ptResult", &r);
        ASSERT_NE(r, nullptr);
        EXPECT_STREQ(r, "\"10,20,30\"");
        qwrt_free(r);
    }
    qwrt_destroy(rt2);
    pal_mock_destroy(pal2);
}

TEST_F(ComplianceTestBase, TransformStreamConstructor) {
    js_assert_no_error("var ts = new TransformStream()");
}

TEST_F(ComplianceTestBase, CompressionStreamConstructor) {
    js_assert_no_error("var cs = new CompressionStream('gzip')");
}

TEST_F(ComplianceTestBase, DecompressionStreamConstructor) {
    js_assert_no_error("var ds = new DecompressionStream('gzip')");
}

TEST_F(ComplianceTestBase, CompressionDecompressionRoundtrip) {
#if !QWRT_WITH_COMPRESS
    GTEST_SKIP() << "QWRT_WITH_COMPRESS is OFF — compression not compiled in";
#endif
    qwrt_pal_t *pal2;
    qwrt_t *rt2;
    {
        pal2 = pal_mock_create();
        ASSERT_NE(pal2, nullptr);
        qwrt_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.pal = pal2;
        rt2 = qwrt_create(&cfg);
        ASSERT_NE(rt2, nullptr);
    }
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
    for (int i = 0; i < 100; i++) qwrt_tick(rt2, 100);
    {
        char *r = NULL;
        qwrt_eval(rt2, "_gzipOk", &r);
        ASSERT_NE(r, nullptr);
        EXPECT_STREQ(r, "true");
        qwrt_free(r);
    }
    qwrt_destroy(rt2);
    pal_mock_destroy(pal2);
}

TEST_F(ComplianceTestBase, QueuingStrategies) {
    js_assert_eq("new ByteLengthQueuingStrategy({highWaterMark: 1024}).highWaterMark", "1024");
    js_assert_eq("new CountQueuingStrategy({highWaterMark: 5}).highWaterMark", "5");
}

TEST_F(ComplianceTestBase, TextEncoderStream) {
    js_assert_eq("new TextEncoderStream().encoding", "\"utf-8\"");
    js_assert_truthy("new TextEncoderStream().readable instanceof ReadableStream");
    js_assert_truthy("new TextEncoderStream().writable instanceof WritableStream");
}

TEST_F(ComplianceTestBase, TransformStreamDefaultController) {
    js_assert_truthy("typeof TransformStreamDefaultController === 'function'");
    js_assert_truthy(
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
        "})()");
}

TEST_F(ComplianceTestBase, ReadableStreamTee) {
    char *tee_result = js_eval(
        "(function(){"
        "  try {"
        "    var rs = new ReadableStream({"
        "      start: function(c) { c.enqueue('a'); c.enqueue('b'); c.close(); }"
        "    });"
        "    var branches = rs.tee();"
        "    if (!(branches[0] instanceof ReadableStream)) return 'ERR:branch0 type';"
        "    if (!(branches[1] instanceof ReadableStream)) return 'ERR:branch1 type';"
        "    if (branches.length !== 2) return 'ERR:length=' + branches.length;"
        "    if (!branches[0]._controller) return 'ERR:no ctrl0';"
        "    if (!branches[1]._controller) return 'ERR:no ctrl1';"
        "    return true;"
        "  } catch(e) { return 'ERR:' + e.message; }"
        "})()");
    ASSERT_NE(tee_result, nullptr);
    EXPECT_STREQ(tee_result, "true") << "ReadableStream.tee()";
    if (tee_result) qwrt_free(tee_result);
}

TEST_F(ComplianceTestBase, ReadableStreamAsyncIterator) {
    js_assert_eq(
        "typeof ReadableStream.prototype[Symbol.asyncIterator]",
        "\"function\"");
}

TEST_F(ComplianceTestBase, ReadableStreamForAwaitOf) {
    qwrt_pal_t *pal2;
    qwrt_t *rt2;
    {
        pal2 = pal_mock_create();
        ASSERT_NE(pal2, nullptr);
        qwrt_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.pal = pal2;
        rt2 = qwrt_create(&cfg);
        ASSERT_NE(rt2, nullptr);
    }
    qwrt_eval(rt2,
        "var _iterResult = ''; "
        "(async function() {"
        "  var chunks = [];"
        "  var rs = new ReadableStream({start(c){c.enqueue('a');c.enqueue('b');c.close();}});"
        "  for await (var x of rs) chunks.push(x);"
        "  _iterResult = chunks.join(',');"
        "})();", NULL);
    for (int i = 0; i < 50; i++) qwrt_tick(rt2, 100);
    {
        char *r = NULL;
        qwrt_eval(rt2, "_iterResult", &r);
        ASSERT_NE(r, nullptr);
        EXPECT_STREQ(r, "\"a,b\"");
        qwrt_free(r);
    }
    qwrt_destroy(rt2);
    pal_mock_destroy(pal2);
}

/* ================================================================
 * TC55: Blob / File / FormData
 * ================================================================ */

TEST_F(ComplianceTestBase, BlobFileFormDataExist) {
    js_assert_truthy("typeof Blob === 'function'");
    js_assert_truthy("typeof File === 'function'");
    js_assert_truthy("typeof FormData === 'function'");
}

TEST_F(ComplianceTestBase, BlobSizeType) {
    js_assert_eq("new Blob(['hello']).size", "5");
    js_assert_eq("new Blob(['hello'], {type: 'text/plain'}).type", "\"text/plain\"");
}

TEST_F(ComplianceTestBase, FileName) {
    js_assert_eq("new File(['data'], 'test.txt').name", "\"test.txt\"");
}

TEST_F(ComplianceTestBase, FormDataAppendGet) {
    js_assert_no_error("var fd = new FormData(); fd.append('key', 'val')");
    js_assert_eq(
        "(function(){var fd=new FormData();fd.append('k','v');return fd.get('k')})()",
        "\"v\"");
}

/* ================================================================
 * TC55: URLPattern
 * ================================================================ */

TEST_F(ComplianceTestBase, URLPatternExists) {
    js_assert_truthy("typeof URLPattern === 'function'");
}

TEST_F(ComplianceTestBase, URLPatternTest) {
    js_assert_truthy(
        "new URLPattern({pathname: '/users/:id'}).test({pathname: '/users/123'})");
    js_assert_truthy(
        "!new URLPattern({pathname: '/users/:id'}).test({pathname: '/posts/123'})");
}

TEST_F(ComplianceTestBase, URLPatternExec) {
    js_assert_truthy(
        "new URLPattern({pathname: '/users/:id'}).exec({pathname: '/users/42'}).pathname.groups.id === '42'");
}

/* ================================================================
 * TC55: navigator
 * ================================================================ */

TEST_F(ComplianceTestBase, NavigatorExists) {
    js_assert_truthy("typeof navigator !== 'undefined'");
    js_assert_truthy("typeof navigator.userAgent === 'string'");
    js_assert_truthy("navigator.userAgent.indexOf('qwrt') >= 0");
}

TEST_F(ComplianceTestBase, ReportError) {
    js_assert_truthy("typeof reportError === 'function'");
}

TEST_F(ComplianceTestBase, SelfGlobalThis) {
    js_assert_truthy("typeof self !== 'undefined'");
    js_assert_truthy("self === globalThis");
}

TEST_F(ComplianceTestBase, GlobalThisEventHandlers) {
    js_assert_truthy("'onerror' in globalThis");
    js_assert_truthy("'onunhandledrejection' in globalThis");
    js_assert_truthy("'onrejectionhandled' in globalThis");
}

TEST_F(ComplianceTestBase, NavigatorExtended) {
    js_assert_truthy("typeof navigator.language === 'string'");
    js_assert_truthy("typeof navigator.platform === 'string'");
    js_assert_truthy("typeof navigator.hardwareConcurrency === 'number' && navigator.hardwareConcurrency >= 1");
    js_assert_truthy("typeof navigator.onLine === 'boolean'");
    js_assert_truthy("typeof navigator.maxTouchPoints === 'number' && navigator.maxTouchPoints >= 0");
}

/* ================================================================
 * TC55: crypto.subtle operations (needs crypto extension)
 * ================================================================ */

#if !QWRT_WITH_CRYPTO_EXT
TEST_F(ComplianceTestBase, CryptoSubtleOps) {
    GTEST_SKIP() << "QWRT_WITH_CRYPTO_EXT off — operational crypto.subtle requires native crypto extension";
}
#else
TEST_F(ComplianceTestBase, CryptoSubtleDigest) {
    js_assert_truthy("typeof crypto !== 'undefined'");
    js_assert_truthy("typeof crypto.subtle === 'object'");
    js_assert_truthy("typeof crypto.subtle.digest === 'function'");
    js_assert_truthy("typeof CryptoKey === 'function'");
}

TEST_F(ComplianceTestBase, Sha256Digest) {
    qwrt_pal_t *pal2;
    qwrt_t *rt2;
    {
        pal2 = pal_mock_create();
        ASSERT_NE(pal2, nullptr);
        qwrt_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.pal = pal2;
        rt2 = qwrt_create(&cfg);
        ASSERT_NE(rt2, nullptr);
    }
    qwrt_eval(rt2,
        "var _digestLen = null; "
        "crypto.subtle.digest('SHA-256', new TextEncoder().encode('hello')).then(function(buf) { "
        "_digestLen = buf.byteLength; })", NULL);
    qwrt_tick(rt2, 100);
    {
        char *r = NULL;
        qwrt_eval(rt2, "_digestLen", &r);
        ASSERT_NE(r, nullptr);
        EXPECT_STREQ(r, "32");
        qwrt_free(r);
    }
    qwrt_destroy(rt2);
    pal_mock_destroy(pal2);
}

TEST_F(ComplianceTestBase, ImportKey) {
    qwrt_pal_t *pal2;
    qwrt_t *rt2;
    {
        pal2 = pal_mock_create();
        ASSERT_NE(pal2, nullptr);
        qwrt_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.pal = pal2;
        rt2 = qwrt_create(&cfg);
        ASSERT_NE(rt2, nullptr);
    }
    qwrt_eval(rt2,
        "var _keyType = null; "
        "crypto.subtle.importKey('raw', new Uint8Array(16), {name:'HMAC',hash:'SHA-256'}, false, ['sign']).then(function(k) { "
        "_keyType = k.type; })", NULL);
    qwrt_tick(rt2, 100);
    {
        char *r = NULL;
        qwrt_eval(rt2, "_keyType", &r);
        ASSERT_NE(r, nullptr);
        EXPECT_STREQ(r, "\"secret\"");
        qwrt_free(r);
    }
    qwrt_destroy(rt2);
    pal_mock_destroy(pal2);
}

/* TC: HMAC generateKey length is in BITS (WebCrypto), not bytes.
 * generateKey({...,length:256}) must yield a 32-byte key, not 256 bytes. */
TEST_F(ComplianceTestBase, HmacGenerateKeyLengthBits) {
    qwrt_pal_t *pal2;
    qwrt_t *rt2;
    {
        pal2 = pal_mock_create();
        ASSERT_NE(pal2, nullptr);
        qwrt_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.pal = pal2;
        rt2 = qwrt_create(&cfg);
        ASSERT_NE(rt2, nullptr);
    }
    qwrt_eval(rt2,
        "var _klen = null; "
        "crypto.subtle.generateKey({name:'HMAC',hash:'SHA-256',length:256}, true, ['sign']).then(function(k) { "
        "return crypto.subtle.exportKey('raw', k); }).then(function(buf) { _klen = buf.byteLength; })", NULL);
    qwrt_tick(rt2, 100);
    {
        char *r = NULL;
        qwrt_eval(rt2, "_klen", &r);
        ASSERT_NE(r, nullptr);
        EXPECT_STREQ(r, "32") << "HMAC length:256 (bits) must yield 32 bytes";
        qwrt_free(r);
    }
    qwrt_destroy(rt2);
    pal_mock_destroy(pal2);
}

/* TC: exportKey('raw') on a key imported from a subarray view returns only the
 * key bytes (byteOffset-aware), not the whole backing buffer. */
TEST_F(ComplianceTestBase, ExportKeySubarray) {
    qwrt_pal_t *pal2;
    qwrt_t *rt2;
    {
        pal2 = pal_mock_create();
        ASSERT_NE(pal2, nullptr);
        qwrt_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.pal = pal2;
        rt2 = qwrt_create(&cfg);
        ASSERT_NE(rt2, nullptr);
    }
    qwrt_eval(rt2,
        "var _elen = null; "
        "var big = new Uint8Array(32); "
        "var view = big.subarray(8, 16); "  /* 8-byte view at offset 8 */
        "crypto.subtle.importKey('raw', view, {name:'HMAC',hash:'SHA-256'}, true, ['sign']).then(function(k) { "
        "return crypto.subtle.exportKey('raw', k); }).then(function(buf) { _elen = buf.byteLength; })", NULL);
    qwrt_tick(rt2, 100);
    {
        char *r = NULL;
        qwrt_eval(rt2, "_elen", &r);
        ASSERT_NE(r, nullptr);
        EXPECT_STREQ(r, "8") << "exportKey raw of an 8-byte subarray view must be 8 bytes";
        qwrt_free(r);
    }
    qwrt_destroy(rt2);
    pal_mock_destroy(pal2);
}

TEST_F(ComplianceTestBase, HmacSignVerify) {
    qwrt_pal_t *pal2;
    qwrt_t *rt2;
    {
        pal2 = pal_mock_create();
        ASSERT_NE(pal2, nullptr);
        qwrt_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.pal = pal2;
        rt2 = qwrt_create(&cfg);
        ASSERT_NE(rt2, nullptr);
    }
    qwrt_eval(rt2,
        "var _hmacOk = false; "
        "crypto.subtle.importKey('raw', new Uint8Array(16), {name:'HMAC',hash:'SHA-256'}, false, ['sign','verify']).then(function(k) {"
        "  return crypto.subtle.sign('HMAC', k, new TextEncoder().encode('test data'));"
        "}).then(function(sig) {"
        "  return crypto.subtle.importKey('raw', new Uint8Array(16), {name:'HMAC',hash:'SHA-256'}, false, ['verify']).then(function(k2) {"
        "    return crypto.subtle.verify('HMAC', k2, sig, new TextEncoder().encode('test data'));"
        "  });"
        "}).then(function(ok) { _hmacOk = ok; })", NULL);
    for (int i = 0; i < 50; i++) qwrt_tick(rt2, 100);
    {
        char *r = NULL;
        qwrt_eval(rt2, "_hmacOk", &r);
        ASSERT_NE(r, nullptr);
        EXPECT_STREQ(r, "true");
        qwrt_free(r);
    }
    qwrt_destroy(rt2);
    pal_mock_destroy(pal2);
}

TEST_F(ComplianceTestBase, HmacRejectsTampered) {
    qwrt_pal_t *pal2;
    qwrt_t *rt2;
    {
        pal2 = pal_mock_create();
        ASSERT_NE(pal2, nullptr);
        qwrt_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.pal = pal2;
        rt2 = qwrt_create(&cfg);
        ASSERT_NE(rt2, nullptr);
    }
    qwrt_eval(rt2,
        "var _hmacReject = true; "
        "crypto.subtle.importKey('raw', new Uint8Array(16), {name:'HMAC',hash:'SHA-256'}, false, ['sign','verify']).then(function(k) {"
        "  return crypto.subtle.sign('HMAC', k, new TextEncoder().encode('original'));"
        "}).then(function(sig) {"
        "  return crypto.subtle.importKey('raw', new Uint8Array(16), {name:'HMAC',hash:'SHA-256'}, false, ['verify']).then(function(k2) {"
        "    return crypto.subtle.verify('HMAC', k2, sig, new TextEncoder().encode('tampered'));"
        "  });"
        "}).then(function(ok) { _hmacReject = !ok; })", NULL);
    for (int i = 0; i < 50; i++) qwrt_tick(rt2, 100);
    {
        char *r = NULL;
        qwrt_eval(rt2, "_hmacReject", &r);
        ASSERT_NE(r, nullptr);
        EXPECT_STREQ(r, "true");
        qwrt_free(r);
    }
    qwrt_destroy(rt2);
    pal_mock_destroy(pal2);
}

TEST_F(ComplianceTestBase, AesCbcEncryptDecrypt) {
    qwrt_pal_t *pal2;
    qwrt_t *rt2;
    {
        pal2 = pal_mock_create();
        ASSERT_NE(pal2, nullptr);
        qwrt_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.pal = pal2;
        rt2 = qwrt_create(&cfg);
        ASSERT_NE(rt2, nullptr);
    }
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
    for (int i = 0; i < 50; i++) qwrt_tick(rt2, 100);
    {
        char *r = NULL;
        qwrt_eval(rt2, "_aesResult", &r);
        ASSERT_NE(r, nullptr);
        EXPECT_STREQ(r, "\"hello world 12345\"");
        qwrt_free(r);
    }
    qwrt_destroy(rt2);
    pal_mock_destroy(pal2);
}

TEST_F(ComplianceTestBase, AesGcmEncryptDecrypt) {
    qwrt_pal_t *pal2;
    qwrt_t *rt2;
    {
        pal2 = pal_mock_create();
        ASSERT_NE(pal2, nullptr);
        qwrt_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.pal = pal2;
        rt2 = qwrt_create(&cfg);
        ASSERT_NE(rt2, nullptr);
    }
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
    for (int i = 0; i < 50; i++) qwrt_tick(rt2, 100);
    {
        char *r = NULL;
        qwrt_eval(rt2, "_gcmResult", &r);
        ASSERT_NE(r, nullptr);
        EXPECT_STREQ(r, "\"gcm test data!!!\"");
        qwrt_free(r);
    }
    qwrt_destroy(rt2);
    pal_mock_destroy(pal2);
}

TEST_F(ComplianceTestBase, GenerateKeyAesCbc) {
    qwrt_pal_t *pal2;
    qwrt_t *rt2;
    {
        pal2 = pal_mock_create();
        ASSERT_NE(pal2, nullptr);
        qwrt_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.pal = pal2;
        rt2 = qwrt_create(&cfg);
        ASSERT_NE(rt2, nullptr);
    }
    qwrt_eval(rt2,
        "var _genKeyOk = false; "
        "crypto.subtle.generateKey({name:'AES-CBC', length:256}, true, ['encrypt']).then(function(k) {"
        "  _genKeyOk = k.type === 'secret' && k.algorithm.name === 'AES-CBC';"
        "})", NULL);
    for (int i = 0; i < 50; i++) qwrt_tick(rt2, 100);
    {
        char *r = NULL;
        qwrt_eval(rt2, "_genKeyOk", &r);
        ASSERT_NE(r, nullptr);
        EXPECT_STREQ(r, "true");
        qwrt_free(r);
    }
    qwrt_destroy(rt2);
    pal_mock_destroy(pal2);
}

TEST_F(ComplianceTestBase, ExportKeyRaw) {
    qwrt_pal_t *pal2;
    qwrt_t *rt2;
    {
        pal2 = pal_mock_create();
        ASSERT_NE(pal2, nullptr);
        qwrt_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.pal = pal2;
        rt2 = qwrt_create(&cfg);
        ASSERT_NE(rt2, nullptr);
    }
    qwrt_eval(rt2,
        "var _exportOk = false; "
        "crypto.subtle.importKey('raw', new Uint8Array([1,2,3,4]), 'AES-CBC', true, ['encrypt']).then(function(k) {"
        "  return crypto.subtle.exportKey('raw', k);"
        "}).then(function(buf) {"
        "  _exportOk = new Uint8Array(buf).length === 4 && new Uint8Array(buf)[0] === 1;"
        "})", NULL);
    for (int i = 0; i < 50; i++) qwrt_tick(rt2, 100);
    {
        char *r = NULL;
        qwrt_eval(rt2, "_exportOk", &r);
        ASSERT_NE(r, nullptr);
        EXPECT_STREQ(r, "true");
        qwrt_free(r);
    }
    qwrt_destroy(rt2);
    pal_mock_destroy(pal2);
}

TEST_F(ComplianceTestBase, Pbkdf2DeriveBits) {
    qwrt_pal_t *pal2;
    qwrt_t *rt2;
    {
        pal2 = pal_mock_create();
        ASSERT_NE(pal2, nullptr);
        qwrt_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.pal = pal2;
        rt2 = qwrt_create(&cfg);
        ASSERT_NE(rt2, nullptr);
    }
    qwrt_eval(rt2,
        "var _pbkdfLen = 0; "
        "crypto.subtle.importKey('raw', new TextEncoder().encode('password'), 'PBKDF2', false, ['deriveBits']).then(function(k) {"
        "  return crypto.subtle.deriveBits({name:'PBKDF2', salt:new TextEncoder().encode('salt'), iterations:1000, hash:'SHA-256'}, k, 256);"
        "}).then(function(bits) {"
        "  _pbkdfLen = bits.byteLength;"
        "})", NULL);
    for (int i = 0; i < 50; i++) qwrt_tick(rt2, 100);
    {
        char *r = NULL;
        qwrt_eval(rt2, "_pbkdfLen", &r);
        ASSERT_NE(r, nullptr);
        EXPECT_STREQ(r, "32");
        qwrt_free(r);
    }
    qwrt_destroy(rt2);
    pal_mock_destroy(pal2);
}
#endif

/* ================================================================
 * TC55: structuredClone enhanced
 * ================================================================ */

TEST_F(ComplianceTestBase, StructuredCloneTypedArray) {
    js_assert_truthy(
        "structuredClone(new Uint8Array([1,2,3])) instanceof Uint8Array");
    js_assert_eq("structuredClone(new Uint8Array([1,2,3])).length", "3");
}

TEST_F(ComplianceTestBase, StructuredCloneMap) {
    js_assert_truthy(
        "structuredClone(new Map([['a',1]])) instanceof Map");
    js_assert_eq("structuredClone(new Map([['a',1]])).get('a')", "1");
}

TEST_F(ComplianceTestBase, StructuredCloneSet) {
    js_assert_truthy(
        "structuredClone(new Set([1,2,3])) instanceof Set");
}

TEST_F(ComplianceTestBase, StructuredCloneDate) {
    js_assert_truthy(
        "structuredClone(new Date(1234567890)) instanceof Date");
    js_assert_eq("structuredClone(new Date(1234567890)).getTime()", "1234567890");
}

TEST_F(ComplianceTestBase, StructuredCloneRegExp) {
    js_assert_truthy(
        "structuredClone(/abc/gi) instanceof RegExp");
    js_assert_eq("structuredClone(/abc/gi).source", "\"abc\"");
    js_assert_eq("structuredClone(/abc/gi).flags", "\"gi\"");
}

TEST_F(ComplianceTestBase, StructuredCloneCircular) {
    js_assert_truthy(
        "(function(){var o={};o.self=o;var c=structuredClone(o);return c.self===c})()");
}

TEST_F(ComplianceTestBase, StructuredCloneError) {
    js_assert_truthy(
        "structuredClone(new Error('test')) instanceof Error");
    js_assert_truthy(
        "structuredClone(new TypeError('test')) instanceof TypeError");
    js_assert_truthy(
        "structuredClone(new RangeError('test')) instanceof RangeError");
}

TEST_F(ComplianceTestBase, StructuredCloneArrayBuffer) {
    js_assert_truthy(
        "structuredClone(new ArrayBuffer(8)) instanceof ArrayBuffer");
    js_assert_eq("structuredClone(new ArrayBuffer(8)).byteLength", "8");
}

TEST_F(ComplianceTestBase, StructuredCloneDataView) {
    js_assert_truthy(
        "(function(){var ab=new ArrayBuffer(4);var dv=new DataView(ab);return structuredClone(dv) instanceof DataView;})()");
}

TEST_F(ComplianceTestBase, StructuredCloneInt32Array) {
    js_assert_truthy(
        "structuredClone(new Int32Array([1,2,3])) instanceof Int32Array");
    js_assert_eq("structuredClone(new Int32Array([1,2,3]))[0]", "1");
}

TEST_F(ComplianceTestBase, StructuredCloneBigInt64Array) {
    js_assert_truthy("typeof BigInt64Array === 'function'");
    js_assert_truthy(
        "structuredClone(new BigInt64Array([BigInt(1),BigInt(2)])) instanceof BigInt64Array");
}

TEST_F(ComplianceTestBase, StructuredCloneSpecialNumbers) {
    js_assert_truthy("structuredClone(Infinity) === Infinity");
    js_assert_truthy("Number.isNaN(structuredClone(NaN))");
    js_assert_truthy("Object.is(structuredClone(-0), -0)");
}

TEST_F(ComplianceTestBase, StructuredCloneBooleanNull) {
    js_assert_eq("structuredClone(true)", "true");
    js_assert_eq("structuredClone(null)", "null");
}

/* ================================================================
 * TC55: WebAssembly
 *
 * WASM tests need a separate runtime with a WASM extension linked.
 * Skip gracefully when no WASM engine is available.
 * ================================================================ */

class WasmTestBase : public ::testing::Test {
protected:
    qwrt_t *rt;
    qwrt_pal_t *pal;

    WasmTestBase() : rt(nullptr), pal(nullptr) {}

    void SetUp() override {
#if !defined(QWRT_HAS_WAMR) && !defined(QWRT_HAS_WASM3)
        GTEST_SKIP() << "No WASM engine linked (neither QWRT_HAS_WAMR nor QWRT_HAS_WASM3 defined)";
        return;
#endif
        pal = pal_mock_create();
        ASSERT_NE(pal, nullptr);

        qwrt_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.pal = pal;
        /* wasm3 is registered via the build-time QWRT_EXTENSIONS table. */

        rt = qwrt_create(&cfg);
        if (!rt) {
            pal_mock_destroy(pal);
            pal = nullptr;
            GTEST_SKIP() << "No WASM engine linked — cannot create runtime with WASM extension";
            return;
        }
    }

    void TearDown() override {
        if (rt) qwrt_destroy(rt);
        if (pal) pal_mock_destroy(pal);
    }

    char *js_eval(const char *code) {
        if (!rt) return NULL;
        char *result = NULL;
        int rc = qwrt_eval(rt, code, &result);
        if (rc != 0) return NULL;
        return result;
    }

    void js_assert_eq(const char *code, const char *expected) {
        if (!rt) return;
        char *result = js_eval(code);
        ASSERT_NE(result, nullptr) << "eval returned error for: " << code;
        EXPECT_STREQ(result, expected) << "for: " << code;
        if (result) qwrt_free(result);
    }

    void js_assert_truthy(const char *code) {
        if (!rt) return;
        char *result = js_eval(code);
        ASSERT_NE(result, nullptr) << "eval returned error for: " << code;
        EXPECT_STRNE(result, "false") << "got falsy result for: " << code;
        EXPECT_STRNE(result, "0") << "got falsy result for: " << code;
        EXPECT_STRNE(result, "null") << "got falsy result for: " << code;
        EXPECT_STRNE(result, "undefined") << "got falsy result for: " << code;
        EXPECT_STRNE(result, "\"\"") << "got falsy result for: " << code;
        if (result) qwrt_free(result);
    }
};

TEST_F(WasmTestBase, WebAssemblyExists) {
    js_assert_truthy("typeof WebAssembly !== 'undefined'");
    js_assert_truthy("typeof WebAssembly === 'object'");
}

TEST_F(WasmTestBase, WebAssemblyStaticMethods) {
    js_assert_truthy("typeof WebAssembly.validate === 'function'");
    js_assert_truthy("typeof WebAssembly.compile === 'function'");
    js_assert_truthy("typeof WebAssembly.instantiate === 'function'");
}

TEST_F(WasmTestBase, WebAssemblyConstructors) {
    js_assert_truthy("typeof WebAssembly.Module === 'function'");
    js_assert_truthy("typeof WebAssembly.Instance === 'function'");
    js_assert_truthy("typeof WebAssembly.Memory === 'function'");
    js_assert_truthy("typeof WebAssembly.Table === 'function'");
    js_assert_truthy("typeof WebAssembly.Global === 'function'");
}

TEST_F(WasmTestBase, WebAssemblyErrorTypes) {
    js_assert_truthy("typeof WebAssembly.CompileError !== 'undefined'");
    js_assert_truthy("typeof WebAssembly.LinkError !== 'undefined'");
    js_assert_truthy("typeof WebAssembly.RuntimeError !== 'undefined'");
}

TEST_F(WasmTestBase, WebAssemblyValidate) {
    js_assert_eq(
        "WebAssembly.validate(new Uint8Array([0x00,0x61,0x73,0x6d,0x01,0x00,0x00,0x00]))",
        "true");
    js_assert_eq(
        "WebAssembly.validate(new Uint8Array([0x00,0x00,0x00,0x00]))",
        "false");
}

TEST_F(WasmTestBase, WebAssemblyMemory) {
    js_assert_truthy(
        "(function(){ var m = new WebAssembly.Memory({initial:1}); return m.buffer instanceof ArrayBuffer; })()");
    js_assert_eq(
        "(function(){ var m = new WebAssembly.Memory({initial:1}); return m.buffer.byteLength; })()",
        "65536");
    js_assert_eq(
        "(function(){ var m = new WebAssembly.Memory({initial:2}); return m.buffer.byteLength; })()",
        "131072");
}

TEST_F(WasmTestBase, WebAssemblyTable) {
    js_assert_truthy(
        "(function(){ var t = new WebAssembly.Table({initial:2, element:'anyfunc'}); return t.length === 2; })()");
}

TEST_F(WasmTestBase, WebAssemblyGlobal) {
    js_assert_truthy(
        "(function(){ var g = new WebAssembly.Global({value:'i32', mutable:true}, 42); return g.value === 42; })()");
    js_assert_truthy(
        "(function(){ var g = new WebAssembly.Global({value:'i32', mutable:true}, 42); return g.mutable === true; })()");
    js_assert_truthy(
        "(function(){ var g = new WebAssembly.Global({value:'f64', mutable:false}, 3.14); return g.mutable === false; })()");
}

TEST_F(WasmTestBase, WebAssemblyModule) {
    js_assert_truthy(
        "(function(){ var m = new WebAssembly.Module(new Uint8Array([0x00,0x61,0x73,0x6d,0x01,0x00,0x00,0x00])); return m !== undefined; })()");
}

TEST_F(WasmTestBase, WebAssemblyCompileInstantiate) {
    js_assert_truthy(
        "typeof WebAssembly.compile(new Uint8Array([0x00,0x61,0x73,0x6d,0x01,0x00,0x00,0x00])) === 'object'");
    js_assert_truthy(
        "typeof WebAssembly.instantiate(new Uint8Array([0x00,0x61,0x73,0x6d,0x01,0x00,0x00,0x00])) === 'object'");
}

TEST_F(WasmTestBase, InstanceExportsMemory) {
    js_assert_truthy(
        "(function(){"
        "  var bytes = new Uint8Array(["
        "    0x00,0x61,0x73,0x6d, 0x01,0x00,0x00,0x00,"
        "    0x05,0x03,0x01,0x00,0x01,"
        "    0x07,0x07,0x01,0x03,0x6d,0x65,0x6d,0x02,0x00"
        "  ]);"
        "  var mod = new WebAssembly.Module(bytes);"
        "  var inst = new WebAssembly.Instance(mod);"
        "  return inst.exports.mem.buffer instanceof ArrayBuffer;"
        "})()");
}

TEST_F(WasmTestBase, InstanceExportsGlobal) {
    js_assert_truthy(
        "(function(){"
        "  var bytes = new Uint8Array(["
        "    0x00,0x61,0x73,0x6d, 0x01,0x00,0x00,0x00,"
        "    0x06,0x06,0x01,0x7f,0x00,0x41,0x2a,0x0b,"
        "    0x07,0x05,0x01,0x01,0x67,0x03,0x00"
        "  ]);"
        "  var mod = new WebAssembly.Module(bytes);"
        "  var inst = new WebAssembly.Instance(mod);"
        "  return inst.exports.g.value === 42;"
        "})()");
}

TEST_F(WasmTestBase, InstanceMutableGlobalLive) {
    char *live_result = js_eval(
        "(function(){"
        "  try {"
        "    var bytes = new Uint8Array(["
        "      0x00,0x61,0x73,0x6d,0x01,0x00,0x00,0x00,"
        "      0x01,0x09,0x02,"
        "      0x60,0x01,0x7f,0x00,"
        "      0x60,0x00,0x01,0x7f,"
        "      0x03,0x03,0x02,0x00,0x01,"
        "      0x06,0x06,0x01,0x7f,0x01,0x41,0x2a,0x0b,"
        "      0x07,0x15,0x03,"
        "      0x01,0x67,0x03,0x00,"
        "      0x05,0x73,0x65,0x74,0x5f,0x67,0x00,0x00,"
        "      0x05,0x67,0x65,0x74,0x5f,0x67,0x00,0x01,"
        "      0x0a,0x0d,0x02,"
        "      0x06,0x00,0x20,0x00,0x24,0x00,0x0b,"
        "      0x04,0x00,0x23,0x00,0x0b"
        "    ]);"
        "    var mod = new WebAssembly.Module(bytes);"
        "    var inst = new WebAssembly.Instance(mod);"
        "    var r1 = inst.exports.g.value;"
        "    inst.exports.set_g(100);"
        "    var r2 = inst.exports.g.value;"
        "    inst.exports.g.value = 200;"
        "    var r3 = inst.exports.get_g();"
        "    return r1===42 && r2===100 && r3===200;"
        "  } catch(e) { return 'ERR:' + e.message; }"
        "})()");
    ASSERT_NE(live_result, nullptr);
    EXPECT_STREQ(live_result, "true") << "Instance mutable global live binding";
    if (live_result) qwrt_free(live_result);
}

TEST_F(WasmTestBase, InstanceExportedFunctionCall) {
    char *func_result = js_eval(
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
        "    return inst.exports.add(2, 3);"
        "  } catch(e) { return 'ERR:' + e.message; }"
        "})()");
    ASSERT_NE(func_result, nullptr);
    EXPECT_STREQ(func_result, "5") << "WASM function call add(2,3)";
    if (func_result) qwrt_free(func_result);
}

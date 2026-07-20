/*
 * test_polyfill_gtest.cpp — Google Test version of test_polyfill.c
 *
 * Tests that the esbuild-bundled polyfill (auto-injected by qwrt_create)
 * works correctly with the qwrt runtime. The polyfill is compiled to
 * QuickJS bytecode and inlined as src/polyfill_default.c — no file
 * loading needed.
 */

#include <gtest/gtest.h>

extern "C" {
#include "qwrt/qwrt.h"
#include "pal_mock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
}

class PolyfillTestBase : public ::testing::Test {
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
};

/* ================================================================
 * Console
 * ================================================================ */

TEST_F(PolyfillTestBase, Console) {
    int rc = qwrt_eval(rt, "console.log('hello', 'polyfill')", NULL);
    EXPECT_EQ(rc, 0);

    int log_count = 0;
    const char *log = pal_mock_get_log(pal, &log_count);
    EXPECT_EQ(log_count, 1);
    EXPECT_NE(strstr(log, "hello polyfill"), nullptr);

    pal_mock_clear_log(pal);
}

/* ================================================================
 * Timer
 * ================================================================ */

TEST_F(PolyfillTestBase, Timer) {
    int rc = qwrt_eval(rt,
        "var _timerFired = false; "
        "setTimeout(function() { _timerFired = true; }, 100)",
        NULL);
    EXPECT_EQ(rc, 0);

    /* Process timer setup */
    qwrt_tick(rt, 100);

    /* Timer hasn't fired yet */
    char *result = NULL;
    rc = qwrt_eval(rt, "_timerFired", &result);
    EXPECT_EQ(rc, 0);
    EXPECT_STREQ(result, "false");
    qwrt_free(result);

    /* Fire mock timers */
    pal_mock_fire_all_timers(pal);
    qwrt_tick(rt, 100);

    /* Now callback should have run */
    rc = qwrt_eval(rt, "_timerFired", &result);
    EXPECT_EQ(rc, 0);
    EXPECT_STREQ(result, "true");

    qwrt_free(result);
}

/* ================================================================
 * Encoding (btoa / atob)
 * ================================================================ */

TEST_F(PolyfillTestBase, Encoding) {
    char *result = NULL;
    int rc = qwrt_eval(rt, "btoa('hello')", &result);
    EXPECT_EQ(rc, 0);
    EXPECT_STREQ(result, "\"aGVsbG8=\"");
    qwrt_free(result);

    result = NULL;
    rc = qwrt_eval(rt, "atob('aGVsbG8=')", &result);
    EXPECT_EQ(rc, 0);
    EXPECT_STREQ(result, "\"hello\"");

    qwrt_free(result);
}

/* ================================================================
 * URL
 * ================================================================ */

TEST_F(PolyfillTestBase, Url) {
    char *result = NULL;
    int rc = qwrt_eval(rt,
        "var u = new URL('https://example.com/path?q=1#frag'); u.searchParams.get('q')",
        &result);
    EXPECT_EQ(rc, 0);
    EXPECT_STREQ(result, "\"1\"");
    qwrt_free(result);

    /* Also check basic URL properties */
    rc = qwrt_eval(rt, "u.protocol", &result);
    EXPECT_EQ(rc, 0);
    EXPECT_STREQ(result, "\"https:\"");
    qwrt_free(result);

    rc = qwrt_eval(rt, "u.hostname", &result);
    EXPECT_EQ(rc, 0);
    EXPECT_STREQ(result, "\"example.com\"");
    qwrt_free(result);

    rc = qwrt_eval(rt, "u.pathname", &result);
    EXPECT_EQ(rc, 0);
    EXPECT_STREQ(result, "\"/path\"");

    qwrt_free(result);
}

/* ================================================================
 * Storage
 * ================================================================ */

TEST_F(PolyfillTestBase, Storage) {
    /* Use the polyfill's storage API — try common WinterTC patterns */
    int rc = qwrt_eval(rt,
        "var _setOk = null; "
        "if (typeof qwrt !== 'undefined' && qwrt.storage) { "
        "  qwrt.storage.set('pf_key', 'pf_value').then(function() { _setOk = 'yes'; }); "
        "} else if (typeof localStorage !== 'undefined') { "
        "  localStorage.setItem('pf_key', 'pf_value'); _setOk = 'yes'; "
        "} else if (typeof storage !== 'undefined') { "
        "  storage.set('pf_key', 'pf_value').then(function() { _setOk = 'yes'; }); "
        "} else { "
        "  _setOk = 'no_storage'; "
        "}",
        NULL);
    EXPECT_EQ(rc, 0);
    qwrt_tick(rt, 100);

    char *result = NULL;
    rc = qwrt_eval(rt, "_setOk", &result);
    EXPECT_EQ(rc, 0);
    /* Should not report 'no_storage' — at least one storage API must exist */
    EXPECT_STRNE(result, "\"no_storage\"");

    qwrt_free(result);
}

/* ================================================================
 * Fetch
 * ================================================================ */

TEST_F(PolyfillTestBase, Fetch) {
    /* Set a custom mock HTTP response */
    pal_mock_set_http_response(pal,
        "{\"status\":200,\"headers\":{\"Content-Type\":\"text/plain\"},"
        "\"body\":\"polyfill fetch works\"}");

    int rc = qwrt_eval(rt,
        "var _fetchResult = null; "
        "fetch('http://example.com/api').then(function(r) { return r.text(); }).then(function(t) { "
        "  _fetchResult = t; "
        "})",
        NULL);
    EXPECT_EQ(rc, 0);
    qwrt_tick(rt, 100);

    char *result = NULL;
    rc = qwrt_eval(rt, "_fetchResult", &result);
    EXPECT_EQ(rc, 0);
    EXPECT_STREQ(result, "\"polyfill fetch works\"");

    qwrt_free(result);
}

/* ================================================================
 * Bytecode — compile and eval via qwrt_compile / qwrt_eval_bytecode
 * ================================================================ */

TEST_F(PolyfillTestBase, Bytecode) {
    /* Compile JS source -> bytecode using the runtime's engine */
    const char *src = "var x = 1 + 2; x;";
    size_t bc_len = 0;
    uint8_t *bytecode = qwrt_compile(rt, src, strlen(src), &bc_len);
    ASSERT_NE(bytecode, nullptr);

    /* Eval the bytecode on the same runtime */
    char *result = NULL;
    int eval_rc = qwrt_eval_bytecode(rt, bytecode, bc_len, &result);
    EXPECT_EQ(eval_rc, 0);
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result, "3");

    qwrt_free(result);
    qwrt_free(bytecode);
}

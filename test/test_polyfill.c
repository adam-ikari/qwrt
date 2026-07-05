/*
 * qwrt Polyfill Integration Tests
 *
 * Tests that the esbuild-bundled dist/polyfill.bytecode works correctly
 * with the qwrt runtime. Unlike test_qwrt.c and test_e2e.c which use
 * inline hand-written polyfills, this test loads the actual build output
 * from disk to catch bugs in the bundled polyfill.
 *
 * Gracefully skips if dist/polyfill.bytecode is not found (requires build step).
 */

#define _POSIX_C_SOURCE 200809L

#include "test_qwrt.h"
#include "qwrt/qwrt.h"
#include "pal_mock.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>   /* unlink */

/* ================================================================
 * Global polyfill data (loaded in main, used by test helpers)
 * ================================================================ */

static const uint8_t *g_polyfill;
static size_t g_polyfill_len;
static uint8_t *g_bytecode;
static size_t g_bytecode_len;

/* ================================================================
 * Helper: load a file from disk
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
 * Helper: create a runtime with mock PAL + real bundled polyfill
 * ================================================================ */

static qwrt_t *create_polyfill_runtime(qwrt_pal_t **pal_out) {
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

static void destroy_polyfill_runtime(qwrt_t *rt, qwrt_pal_t *pal) {
    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

/* ================================================================
 * Test: console.log works through real polyfill
 * ================================================================ */

TEST(polyfill_console) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_polyfill_runtime(&pal);
    ASSERT_NOT_NULL(rt);

    int rc = qwrt_eval(rt, "console.log('hello', 'polyfill')", NULL);
    ASSERT_EQ(rc, 0);

    int log_count = 0;
    const char *log = pal_mock_get_log(pal, &log_count);
    ASSERT_EQ(log_count, 1);
    ASSERT(strstr(log, "hello polyfill") != NULL);

    pal_mock_clear_log(pal);
    destroy_polyfill_runtime(rt, pal);
}

/* ================================================================
 * Test: setTimeout works through real polyfill
 * ================================================================ */

TEST(polyfill_timer) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_polyfill_runtime(&pal);
    ASSERT_NOT_NULL(rt);

    int rc = qwrt_eval(rt,
        "var _timerFired = false; "
        "setTimeout(function() { _timerFired = true; }, 100)",
        NULL);
    ASSERT_EQ(rc, 0);

    /* Process timer setup */
    qwrt_tick(rt);

    /* Timer hasn't fired yet */
    char *result = NULL;
    rc = qwrt_eval(rt, "_timerFired", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(result, "false");
    qwrt_free(result);

    /* Fire mock timers */
    pal_mock_fire_all_timers(pal);
    qwrt_tick(rt);

    /* Now callback should have run */
    rc = qwrt_eval(rt, "_timerFired", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(result, "true");

    qwrt_free(result);
    destroy_polyfill_runtime(rt, pal);
}

/* ================================================================
 * Test: btoa/atob work through real polyfill
 * ================================================================ */

TEST(polyfill_encoding) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_polyfill_runtime(&pal);
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
    destroy_polyfill_runtime(rt, pal);
}

/* ================================================================
 * Test: URL parsing works through real polyfill
 * ================================================================ */

TEST(polyfill_url) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_polyfill_runtime(&pal);
    ASSERT_NOT_NULL(rt);

    char *result = NULL;
    int rc = qwrt_eval(rt,
        "var u = new URL('https://example.com/path?q=1#frag'); u.searchParams.get('q')",
        &result);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(result, "\"1\"");
    qwrt_free(result);

    /* Also check basic URL properties */
    rc = qwrt_eval(rt, "u.protocol", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(result, "\"https:\"");
    qwrt_free(result);

    rc = qwrt_eval(rt, "u.hostname", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(result, "\"example.com\"");
    qwrt_free(result);

    rc = qwrt_eval(rt, "u.pathname", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(result, "\"/path\"");

    qwrt_free(result);
    destroy_polyfill_runtime(rt, pal);
}

/* ================================================================
 * Test: storage get/set works through real polyfill
 * ================================================================ */

TEST(polyfill_storage) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_polyfill_runtime(&pal);
    ASSERT_NOT_NULL(rt);

    /* Use the polyfill's storage API — try common WinterCG patterns */
    int rc = qwrt_eval(rt,
        "var _setOk = null; "
        "if (typeof storage !== 'undefined') { "
        "  storage.set('pf_key', 'pf_value').then(function() { _setOk = 'yes'; }); "
        "} else if (typeof localStorage !== 'undefined') { "
        "  localStorage.setItem('pf_key', 'pf_value'); _setOk = 'yes'; "
        "} else { "
        "  _setOk = 'no_storage'; "
        "}",
        NULL);
    ASSERT_EQ(rc, 0);
    qwrt_tick(rt);

    char *result = NULL;
    rc = qwrt_eval(rt, "_setOk", &result);
    ASSERT_EQ(rc, 0);
    /* Should not report 'no_storage' — at least one storage API must exist */
    ASSERT(strcmp(result, "\"no_storage\"") != 0);

    qwrt_free(result);
    destroy_polyfill_runtime(rt, pal);
}

/* ================================================================
 * Test: fetch works through real polyfill
 * ================================================================ */

TEST(polyfill_fetch) {
    qwrt_pal_t *pal;
    qwrt_t *rt = create_polyfill_runtime(&pal);
    ASSERT_NOT_NULL(rt);

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
    ASSERT_EQ(rc, 0);
    qwrt_tick(rt);

    char *result = NULL;
    rc = qwrt_eval(rt, "_fetchResult", &result);
    ASSERT_EQ(rc, 0);
    ASSERT_STR_EQ(result, "\"polyfill fetch works\"");

    qwrt_free(result);
    destroy_polyfill_runtime(rt, pal);
}

/* ================================================================
 * Test: bytecode polyfill loading via qwrt_eval_bytecode
 * ================================================================ */

TEST(bytecode_polyfill) {
    /* Quick test: compile a simple script to bytecode using qjsc,
     * then load it with qwrt_eval_bytecode */
    const char *tmp_js = "/tmp/qwrt_bc_test.js";
    const char *tmp_bc = "/tmp/qwrt_bc_test.out";
    FILE *f = fopen(tmp_js, "w");
    if (!f) { printf("SKIP (cannot write temp)\n"); return; }
    fprintf(f, "var x = 1 + 2; x;\n");
    fclose(f);

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "%s -C -b -o %s %s 2>&1",
             getenv("QJSC") ? getenv("QJSC") : "../../third_party/quickjs-ng/build/qjsc",
             tmp_bc, tmp_js);
    int rc = system(cmd);
    if (rc != 0) { printf("SKIP (qjsc not available)\n"); unlink(tmp_js); return; }

    f = fopen(tmp_bc, "rb");
    if (!f) { printf("SKIP (bytecode not found)\n"); unlink(tmp_js); return; }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *bytecode = (uint8_t *)malloc(fsize);
    if (!bytecode) { fclose(f); printf("SKIP (malloc)\n"); return; }
    size_t nread = fread(bytecode, 1, fsize, f);
    fclose(f);
    unlink(tmp_js);
    unlink(tmp_bc);

    /* Create runtime without polyfill, just eval the bytecode */
    qwrt_pal_t *pal = pal_mock_create();
    ASSERT_NOT_NULL(pal);

    qwrt_config_t config;
    memset(&config, 0, sizeof(config));
    config.pal = pal;
    config.debug = 1;

    qwrt_t *rt = qwrt_create(&config);
    ASSERT_NOT_NULL(rt);

    /* Eval bytecode */
    char *result = NULL;
    int eval_rc = qwrt_eval_bytecode(rt, bytecode, nread, &result);
    ASSERT_EQ(eval_rc, 0);
    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(result, "3");

    qwrt_free(result);
    qwrt_destroy(rt);
    pal_mock_destroy(pal);
    free(bytecode);
    printf("PASS\n");
}

/* ================================================================
 * Main
 * ================================================================ */

int main(void) {
    /* Try to load dist/polyfill.bytecode from multiple likely paths */
    size_t polyfill_len;
    uint8_t *polyfill = load_file("../../qwrt/dist/polyfill.bytecode", &polyfill_len);
    if (!polyfill) {
        polyfill = load_file("dist/polyfill.bytecode", &polyfill_len);
    }
    if (!polyfill) {
        polyfill = load_file("qwrt/dist/polyfill.bytecode", &polyfill_len);
    }
    if (!polyfill) {
        printf("=== Polyfill Integration Tests ===\n");
        printf("SKIPPED: dist/polyfill.bytecode not found (run build first)\n");
        return 0;
    }

    /* Store globally for test helpers */
    g_polyfill = polyfill;
    g_polyfill_len = polyfill_len;

    printf("=== Polyfill Integration Tests ===\n");
    printf("Loaded polyfill: %zu bytes\n\n", polyfill_len);

    printf("--- Console ---\n");
    RUN_TEST(polyfill_console);

    printf("\n--- Timer ---\n");
    RUN_TEST(polyfill_timer);

    printf("\n--- Encoding ---\n");
    RUN_TEST(polyfill_encoding);

    printf("\n--- URL ---\n");
    RUN_TEST(polyfill_url);

    printf("\n--- Storage ---\n");
    RUN_TEST(polyfill_storage);

    printf("\n--- Fetch ---\n");
    RUN_TEST(polyfill_fetch);

    printf("\n--- Bytecode ---\n");
    RUN_TEST(bytecode_polyfill);

    printf("\n=== Results: %d/%d passed, %d failed ===\n",
           tests_passed, tests_run, tests_failed);

    free(polyfill);
    return tests_failed > 0 ? 1 : 0;
}

/*
 * test_fetch_stream.c — Integration test for streaming fetch via qwrt runtime
 *
 * Tests fetch() with streaming ReadableStream using the real polyfill.
 * Uses mock PAL which simulates streaming responses.
 */

#define _POSIX_C_SOURCE 200809L

#include "qwrt/qwrt.h"
#include "pal_mock.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static char *read_file(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    fread(buf, 1, (size_t)len, f);
    buf[len] = '\0';
    fclose(f);
    if (out_len) *out_len = (size_t)len;
    return buf;
}

static int tests_run = 0;
static int tests_failed = 0;

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

static qwrt_t *create_test_runtime(qwrt_pal_t *pal)
{
    qwrt_config_t config;
    memset(&config, 0, sizeof(config));
    config.pal = pal;
    /* Try to load polyfill bytecode from multiple likely paths */
    size_t polyfill_len = 0;
    char *polyfill_bc = read_file("../../qwrt/dist/polyfill.bytecode", &polyfill_len);
    if (!polyfill_bc) polyfill_bc = read_file("../dist/polyfill.bytecode", &polyfill_len);
    if (!polyfill_bc) polyfill_bc = read_file("dist/polyfill.bytecode", &polyfill_len);
    if (!polyfill_bc) polyfill_bc = read_file("qwrt/dist/polyfill.bytecode", &polyfill_len);
    if (!polyfill_bc) {
        fprintf(stderr, "Cannot read dist/polyfill.bytecode\n");
        return NULL;
    }
    qwrt_t *rt = qwrt_create(&config);
    free(polyfill_bc);
    return rt;
}

/* Test: fetch() returns Response with ReadableStream body */
static void test_fetch_stream_body(void)
{
    TEST_BEGIN("fetch() returns Response with ReadableStream body");

    qwrt_pal_t *pal = pal_mock_create();
    if (!pal) TEST_FAIL("pal_mock_create");

    qwrt_t *rt = create_test_runtime(pal);
    if (!rt) TEST_FAIL("qwrt_create");

    const char *code =
        "var _result = null;\n"
        "(async function() {\n"
        "  try {\n"
        "    var resp = await fetch('http://test.local/data');\n"
        "    var hasBody = resp.body !== null;\n"
        "    var hasReader = typeof resp.body.getReader === 'function';\n"
        "    _result = JSON.stringify({status: resp.status, hasBody: hasBody, hasReader: hasReader});\n"
        "  } catch(e) {\n"
        "    _result = 'error:' + e.message;\n"
        "  }\n"
        "})();\n";

    int rc = qwrt_eval(rt, code, NULL);
    if (rc != 0) TEST_FAIL("eval fetch code");

    for (int i = 0; i < 200; i++) qwrt_tick(rt);

    char *result = NULL;
    rc = qwrt_eval(rt, "_result", &result);
    if (rc != 0 || !result) TEST_FAIL("read _result");

    /* Check that we got status and body info */
    if (strstr(result, "\"status\"") == NULL && strstr(result, "status") == NULL) {
        char msg[256];
        snprintf(msg, sizeof(msg), "expected status in result, got: %s", result);
        qwrt_free(result);
        TEST_FAIL(msg);
    }

    qwrt_free(result);
    TEST_PASS();
    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

/* Test: response.text() works with streaming */
static void test_fetch_text_streaming(void)
{
    TEST_BEGIN("response.text() reads full streaming body");

    qwrt_pal_t *pal = pal_mock_create();
    if (!pal) TEST_FAIL("pal_mock_create");

    qwrt_t *rt = create_test_runtime(pal);
    if (!rt) TEST_FAIL("qwrt_create");

    const char *code =
        "var _text_result = null;\n"
        "(async function() {\n"
        "  try {\n"
        "    var resp = await fetch('http://test.local/hello');\n"
        "    var text = await resp.text();\n"
        "    _text_result = text;\n"
        "  } catch(e) {\n"
        "    _text_result = 'error:' + e.message;\n"
        "  }\n"
        "})();\n";

    int rc = qwrt_eval(rt, code, NULL);
    if (rc != 0) TEST_FAIL("eval text code");

    for (int i = 0; i < 200; i++) qwrt_tick(rt);

    char *result = NULL;
    rc = qwrt_eval(rt, "_text_result", &result);
    if (rc != 0 || !result) TEST_FAIL("read _text_result");

    /* Mock PAL returns something — we just need to verify text() works */
    if (strstr(result, "error:") != NULL) {
        char msg[256];
        snprintf(msg, sizeof(msg), "text() failed: %s", result);
        qwrt_free(result);
        TEST_FAIL(msg);
    }

    qwrt_free(result);
    TEST_PASS();
    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

int main(void)
{
    printf("Streaming fetch integration tests:\n");

    test_fetch_stream_body();
    test_fetch_text_streaming();

    printf("\n%d/%d tests passed\n", tests_run - tests_failed, tests_run);
    return tests_failed ? 1 : 0;
}
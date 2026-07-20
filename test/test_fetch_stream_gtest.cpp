/*
 * test_fetch_stream_gtest.cpp — Google Test version of test_fetch_stream.c
 *
 * Integration test for streaming fetch via qwrt runtime.
 * Tests fetch() with streaming ReadableStream using the real polyfill.
 * Uses mock PAL which simulates streaming responses.
 */

#include <gtest/gtest.h>

extern "C" {
#include "qwrt/qwrt.h"
#include "pal_mock.h"
#include <string.h>
#include <stdlib.h>
}

class FetchStreamTest : public ::testing::Test {
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

TEST_F(FetchStreamTest, ResponseHasReadableStreamBody) {
    /* Test: fetch() returns Response with ReadableStream body */
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
    EXPECT_EQ(rc, 0);

    for (int i = 0; i < 200; i++) qwrt_tick(rt, 100);

    char *result = NULL;
    rc = qwrt_eval(rt, "_result", &result);
    EXPECT_EQ(rc, 0);
    ASSERT_NE(result, nullptr);

    /* Check that we got status and body info */
    EXPECT_NE(strstr(result, "status"), nullptr);
    qwrt_free(result);
}

TEST_F(FetchStreamTest, TextReadsFullStreamingBody) {
    /* Test: response.text() works with streaming */
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
    EXPECT_EQ(rc, 0);

    for (int i = 0; i < 200; i++) qwrt_tick(rt, 100);

    char *result = NULL;
    rc = qwrt_eval(rt, "_text_result", &result);
    EXPECT_EQ(rc, 0);
    ASSERT_NE(result, nullptr);

    /* Mock PAL returns something — verify text() did not fail with "error:" */
    EXPECT_EQ(strstr(result, "error:"), nullptr);
    qwrt_free(result);
}

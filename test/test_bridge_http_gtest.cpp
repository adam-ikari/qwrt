/*
 * test_bridge_http_gtest.cpp — Google Test version of test_bridge_http.c
 *
 * Tests JS bridge non-streaming and streaming HTTP — calls __pal__.httpRequest()
 * and __pal__.httpRequestStream() from JS and verifies promises resolve.
 * Uses mock PAL so no network needed.
 */

#include <gtest/gtest.h>

extern "C" {
#include "qwrt/qwrt.h"
#include "pal_mock.h"
#include <string.h>
#include <stdlib.h>
}

class BridgeHttpTest : public ::testing::Test {
protected:
    qwrt_t *rt;
    qwrt_pal_t *pal;

    void SetUp() override {
        pal = pal_mock_create();
        ASSERT_NE(pal, nullptr);

        pal_mock_set_http_response(pal,
            "{\"status\":0,\"headers\":{\"Content-Type\":\"application/json\"},"
            "\"body\":\"{\\\"data\\\":\\\"test_value\\\"}\"}");

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

TEST_F(BridgeHttpTest, NonStreamingHttpRequest) {
    /* Test 1 from original: non-streaming httpRequest via JS bridge */
    const char *code =
        "var _done = false; var _result = null;\n"
        "console.log('Test 1: non-streaming httpRequest...');\n"
        "__pal__.httpRequest('http://api.example.com/data', 'GET', '{}', null)\n"
        ".then(function(data) {\n"
        "  console.log('Got response, length:', data.length);\n"
        "  _result = 'ok';\n"
        "  _done = true;\n"
        "}).catch(function(e) {\n"
        "  console.log('Error:', e);\n"
        "  _result = 'error';\n"
        "  _done = true;\n"
        "});\n";

    int rc = qwrt_eval(rt, code, NULL);
    EXPECT_EQ(rc, 0);

    /* Run tick — mock PAL completes synchronously, deferred callbacks fire */
    qwrt_tick(rt);

    char *result = NULL;
    rc = qwrt_eval(rt, "_result", &result);
    EXPECT_EQ(rc, 0);
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result, "\"ok\"");
    qwrt_free(result);
}

TEST_F(BridgeHttpTest, StreamingHttpRequestStream) {
    /* Test 2 from original: streaming httpRequestStream via JS bridge */
    const char *code2 =
        "var _done2 = false; var _result2 = null;\n"
        "var _hdrStatus = -1; var _chunks = 0;\n"
        "console.log('Test 2: streaming httpRequestStream...');\n"
        "__pal__.httpRequestStream(\n"
        "  'http://api.example.com/data',\n"
        "  'GET',\n"
        "  '{}',\n"
        "  null,\n"
        "  function(status, hdrs) {\n"
        "    console.log('onHeaders:', status);\n"
        "    _hdrStatus = status;\n"
        "  },\n"
        "  function(chunk) {\n"
        "    _chunks++;\n"
        "  },\n"
        "  function(errStatus) {\n"
        "    console.log('onEnd:', errStatus, 'chunks:', _chunks);\n"
        "    _result2 = errStatus === 0 ? 'ok' : 'error:' + errStatus;\n"
        "    _done2 = true;\n"
        "  }\n"
        ");\n"
        "console.log('Stream request sent');\n";

    int rc = qwrt_eval(rt, code2, NULL);
    EXPECT_EQ(rc, 0);

    qwrt_tick(rt);

    char *result = NULL;
    rc = qwrt_eval(rt, "_result2", &result);
    EXPECT_EQ(rc, 0);
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result, "\"ok\"");
    qwrt_free(result);
}

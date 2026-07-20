/*
 * test_bridge_stream_gtest.cpp — Google Test version of test_bridge_stream.c
 *
 * Tests JS bridge streaming — calls pal.httpRequestStream() from JS
 * and verifies JS callbacks fire correctly. Uses mock PAL so no network needed.
 */

#include <gtest/gtest.h>

extern "C" {
#include "qwrt/qwrt.h"
#include "pal_mock.h"
#include <string.h>
#include <stdlib.h>
}

class BridgeStreamTest : public ::testing::Test {
protected:
    qwrt_t *rt;
    qwrt_pal_t *pal;

    void SetUp() override {
        pal = pal_mock_create();
        ASSERT_NE(pal, nullptr);

        /* Configure mock PAL with a streaming response */
        pal_mock_set_http_response(pal,
            "{\"status\":200,\"headers\":{\"Content-Type\":\"application/json\"},"
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

TEST_F(BridgeStreamTest, HttpRequestStreamCallbacksFire) {
    /* JS code that calls __pal__.httpRequestStream directly (bypassing fetch) */
    const char *code =
        "var _done = false; var _result = null;\n"
        "var _headerStatus = -1;\n"
        "var _dataChunks = 0;\n"
        "console.log('Starting stream test...');\n"
        "__pal__.httpRequestStream(\n"
        "  'http://api.example.com/data',\n"
        "  'GET',\n"
        "  '{}',\n"
        "  null,\n"
        "  function(status, headersJson) {\n"
        "    console.log('onHeaders:', status);\n"
        "    _headerStatus = status;\n"
        "  },\n"
        "  function(chunk) {\n"
        "    _dataChunks++;\n"
        "  },\n"
        "  function(errorStatus) {\n"
        "    console.log('onEnd:', errorStatus);\n"
        "    _result = errorStatus === 0 ? 'ok' : 'error:' + errorStatus;\n"
        "    _done = true;\n"
        "  }\n"
        ");\n"
        "console.log('Stream request sent');\n";

    int rc = qwrt_eval(rt, code, NULL);
    EXPECT_EQ(rc, 0);

    /* Run tick — mock PAL completes synchronously, deferred callbacks fire */
    qwrt_tick(rt, 100);

    char *result = NULL;
    rc = qwrt_eval(rt, "_result", &result);
    EXPECT_EQ(rc, 0);
    ASSERT_NE(result, nullptr);
    EXPECT_STREQ(result, "\"ok\"");
    qwrt_free(result);

    char *header_status = NULL;
    rc = qwrt_eval(rt, "_headerStatus", &header_status);
    EXPECT_EQ(rc, 0);
    ASSERT_NE(header_status, nullptr);
    EXPECT_STREQ(header_status, "200");
    qwrt_free(header_status);

    char *data_chunks = NULL;
    rc = qwrt_eval(rt, "_dataChunks", &data_chunks);
    EXPECT_EQ(rc, 0);
    ASSERT_NE(data_chunks, nullptr);
    EXPECT_GT(data_chunks[0], '0');
    qwrt_free(data_chunks);
}

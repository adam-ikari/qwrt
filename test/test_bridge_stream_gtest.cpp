// test_bridge_stream_gtest.cpp — 流式桥接回调（执行模型 A / mock_libuv）
// 直接调 __native__.httpRequestStream（绕过 fetch），验证 onHeaders / onData /
// onEnd 三个回调全部按序触发。
#include "test_host.h"
#include <cstring>

class BridgeStreamTest : public ::testing::Test {
protected:
    HostCtx *h = nullptr;

    void SetUp() override {
        h = host_create();
        ASSERT_NE(nullptr, h);
    }
    void TearDown() override { host_destroy(h); }
};

TEST_F(BridgeStreamTest, HttpRequestStreamCallbacksFire) {
    /* body "body stream payload" = 19 字节；mock 在一次 read 中交付全部字节
     * 再交付 EOF，所以 onHeaders 一次、onData 一次（19 字节）、onEnd(0)。 */
    const char *resp =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 19\r\n"
        "\r\n"
        "body stream payload";
    ASSERT_EQ(0, mock_tcp_respond(&h->rt->loop, resp, strlen(resp)));

    std::string out;
    ASSERT_TRUE(host_eval(h,
        "var _result = null; var _headerStatus = -1; var _headerJson = '';\n"
        "var _dataChunks = 0; var _dataBytes = 0;\n"
        "__native__.httpRequestStream(\n"
        "  'http://api.example.com/data', 'GET', '{}', null,\n"
        "  function(status, hdrs){ _headerStatus = status; _headerJson = hdrs; },\n"
        "  function(chunk){ _dataChunks++; _dataBytes += (chunk instanceof ArrayBuffer) ? chunk.byteLength : chunk.length; },\n"
        "  function(errStatus){ _result = (errStatus === 0) ? 'ok' : 'err:' + errStatus; }\n"
        ");\n"
        "0", &out));

    std::string v;
    ASSERT_TRUE(host_poll_until_value(h, "_result", "ok", &v));              /* onEnd(0) */
    ASSERT_TRUE(host_poll_until_value(h, "_headerStatus", "200", &v));       /* onHeaders */
    ASSERT_TRUE(host_poll_until_value(h, "_headerJson", "Content-Type", &v));/* headers json */
    ASSERT_TRUE(host_poll_until_value(h, "_dataChunks", "1", &v));           /* 单次 data */
    ASSERT_TRUE(host_poll_until_value(h, "_dataBytes", "19", &v));           /* body 全长 */
}

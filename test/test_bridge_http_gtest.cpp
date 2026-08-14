// test_bridge_http_gtest.cpp — JS 桥接 HTTP（执行模型 A / mock_libuv）
// 用 host_eval 契约（test_host.h）+ mock_tcp_respond 预注册 canned HTTP
// 响应。非流式 __native__.httpRequest 走 Content-Length 完成路径；流式
// httpRequestStream 在 EOF 触发 on_end。
#include "test_host.h"
#include <cstring>

class BridgeHttpTest : public ::testing::Test {
protected:
    HostCtx *h = nullptr;

    void SetUp() override {
        h = host_create();
        ASSERT_NE(nullptr, h);
    }
    void TearDown() override { host_destroy(h); }
};

TEST_F(BridgeHttpTest, NonStreamingHttpRequest) {
    /* 响应：Content-Length 必须与 body 字节数精确匹配（10）。 */
    const char *resp =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 10\r\n"
        "\r\n"
        "test_value";
    ASSERT_EQ(0, mock_tcp_respond(&h->rt->loop, resp, strlen(resp)));

    std::string out;
    ASSERT_TRUE(host_eval(h,
        "var _result = null;\n"
        "__native__.httpRequest('http://api.example.com/data', 'GET', '{}', null)\n"
        "  .then(function(d){ _result = d; })\n"
        "  .catch(function(e){ _result = 'error:' + e; });\n"
        "0", &out));

    /* resolve 载荷是 raw string：{"status":200,"headers":{...},"body":"..."} */
    std::string v;
    ASSERT_TRUE(host_poll_until_value(h, "_result", "\"status\":200", &v));
    EXPECT_NE(std::string::npos, v.find("\"body\":\"test_value\""));
    EXPECT_NE(std::string::npos, v.find("Content-Type"));
}

TEST_F(BridgeHttpTest, StreamingHttpRequestStream) {
    const char *resp =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 16\r\n"
        "\r\n"
        "stream chunk one";
    ASSERT_EQ(0, mock_tcp_respond(&h->rt->loop, resp, strlen(resp)));

    std::string out;
    ASSERT_TRUE(host_eval(h,
        "var _result = null; var _headerStatus = -1; var _dataBytes = 0;\n"
        "__native__.httpRequestStream(\n"
        "  'http://api.example.com/data', 'GET', '{}', null,\n"
        "  function(status, hdrs){ _headerStatus = status; },\n"
        "  function(chunk){ _dataBytes += (chunk instanceof ArrayBuffer) ? chunk.byteLength : chunk.length; },\n"
        "  function(errStatus){ _result = (errStatus === 0) ? 'ok' : 'err:' + errStatus; }\n"
        ");\n"
        "0", &out));

    std::string v;
    ASSERT_TRUE(host_poll_until_value(h, "_result", "ok", &v));
    ASSERT_TRUE(host_poll_until_value(h, "_headerStatus", "200", &v));
    ASSERT_TRUE(host_poll_until_value(h, "_dataBytes", "16", &v));
}

// test_fetch_stream_gtest.cpp — fetch 流式路径（执行模型 A / mock_libuv）
// fetch 内部走 pal.httpRequestStream：onHeaders resolve fetch promise（Response
// 带 ReadableStream body），onData enqueue，onEnd(0) 关流 → resp.text() 完成。
#include "test_host.h"
#include <cstring>

class FetchStreamTest : public ::testing::Test {
protected:
    HostCtx *h = nullptr;

    void SetUp() override {
        h = host_create();
        ASSERT_NE(nullptr, h);
    }
    void TearDown() override { host_destroy(h); }
};

TEST_F(FetchStreamTest, ResponseHasReadableStreamBody) {
    const char *resp =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 11\r\n"
        "\r\n"
        "{\"ok\":true}";
    ASSERT_EQ(0, mock_tcp_respond(&h->rt->loop, resp, strlen(resp)));

    std::string out;
    ASSERT_TRUE(host_eval(h,
        "var _result = null;\n"
        "fetch('http://test.local/data').then(function(resp){\n"
        "  _result = JSON.stringify({\n"
        "    status: resp.status,\n"
        "    hasBody: resp.body !== null,\n"
        "    hasReader: (resp.body && typeof resp.body.getReader === 'function')\n"
        "  });\n"
        "}).catch(function(e){ _result = 'error:' + e.message; });\n"
        "0", &out));

    std::string v;
    ASSERT_TRUE(host_poll_until_value(h, "_result", "\"hasReader\":true", &v));
    EXPECT_NE(std::string::npos, v.find("\"status\":200"));
    EXPECT_NE(std::string::npos, v.find("\"hasBody\":true"));
}

TEST_F(FetchStreamTest, TextReadsFullStreamingBody) {
    const char *resp =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 20\r\n"
        "\r\n"
        "polyfill fetch works";
    ASSERT_EQ(0, mock_tcp_respond(&h->rt->loop, resp, strlen(resp)));

    std::string out;
    ASSERT_TRUE(host_eval(h,
        "var _text_result = null;\n"
        "fetch('http://test.local/hello').then(function(resp){\n"
        "  return resp.text();\n"
        "}).then(function(t){\n"
        "  _text_result = t;\n"
        "}).catch(function(e){ _text_result = 'error:' + e.message; });\n"
        "0", &out));

    std::string v;
    ASSERT_TRUE(host_poll_until_value(h, "_text_result", "polyfill fetch works", &v));
}

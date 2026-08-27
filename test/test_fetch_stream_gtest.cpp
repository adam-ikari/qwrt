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

// ================================================================
// fetch redirect 语义（redirect:'error' / 'manual' / 'follow'）
// ================================================================

TEST_F(FetchStreamTest, RedirectErrorRejects) {
    /* redirect:'error'：遇到 3xx 必须 reject，绝不返回 response */
    const char *resp =
        "HTTP/1.1 302 Found\r\n"
        "Location: http://test.local/other\r\n"
        "Content-Length: 0\r\n"
        "\r\n";
    ASSERT_EQ(0, mock_tcp_respond(&h->rt->loop, resp, strlen(resp)));

    std::string out;
    ASSERT_TRUE(host_eval(h,
        "var _redirect_err = null;\n"
        "fetch('http://test.local/a', {redirect:'error'})\n"
        "  .then(function(){ _redirect_err = 'resolved'; })\n"
        "  .catch(function(e){ _redirect_err = 'rejected:' + e.name; });\n"
        "0", &out));

    std::string v;
    ASSERT_TRUE(host_poll_until_value(h, "_redirect_err", "rejected", &v));
    /* 不能 resolve */
    EXPECT_NE(std::string::npos, v.find("rejected")) << "got: " << v;
}

TEST_F(FetchStreamTest, RedirectManualStatusZero) {
    /* redirect:'manual'：返回 status=0 的空 Response（opaqueredirect），不跟随 */
    const char *resp =
        "HTTP/1.1 302 Found\r\n"
        "Location: http://test.local/other\r\n"
        "Content-Length: 0\r\n"
        "\r\n";
    ASSERT_EQ(0, mock_tcp_respond(&h->rt->loop, resp, strlen(resp)));

    std::string out;
    ASSERT_TRUE(host_eval(h,
        "var _manual = null;\n"
        "fetch('http://test.local/a', {redirect:'manual'})\n"
        "  .then(function(r){ _manual = JSON.stringify({s: r.status, t: r.type}); })\n"
        "  .catch(function(e){ _manual = 'error:' + e.message; });\n"
        "0", &out));

    std::string v;
    ASSERT_TRUE(host_poll_until_value(h, "_manual", "\"s\":0", &v));
    EXPECT_NE(std::string::npos, v.find("\"t\":\"opaqueredirect\"")) << "got: " << v;
}

TEST_F(FetchStreamTest, RedirectFollow) {
    /* redirect:'follow'（默认）：跟随 Location 重新请求，最终拿到 200 body */
    const char *resp302 =
        "HTTP/1.1 302 Found\r\n"
        "Location: http://test.local/final\r\n"
        "Content-Length: 0\r\n"
        "\r\n";
    const char *resp200 =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 13\r\n"
        "\r\n"
        "followed body";
    ASSERT_EQ(0, mock_tcp_respond(&h->rt->loop, resp302, strlen(resp302)));
    ASSERT_EQ(0, mock_tcp_respond(&h->rt->loop, resp200, strlen(resp200)));

    std::string out;
    ASSERT_TRUE(host_eval(h,
        "var _followed = null;\n"
        "fetch('http://test.local/a')\n"
        "  .then(function(r){ return r.text(); })\n"
        "  .then(function(t){ _followed = t; })\n"
        "  .catch(function(e){ _followed = 'error:' + e.message; });\n"
        "0", &out));

    std::string v;
    ASSERT_TRUE(host_poll_until_value(h, "_followed", "followed body", &v));
}

// ================================================================
// fetch 请求体（serializeBody + whenBodyReady 链路）：
// body 统一序列化为字节（Uint8Array）传给 pal.httpRequestStream，
// stream body 异步读取完成后再发请求。
// ================================================================

TEST_F(FetchStreamTest, PostStringBody) {
    const char *resp =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 2\r\n"
        "\r\n"
        "ok";
    ASSERT_EQ(0, mock_tcp_respond(&h->rt->loop, resp, strlen(resp)));

    std::string out;
    ASSERT_TRUE(host_eval(h,
        "var _post = null;\n"
        "fetch('http://test.local/post', {method:'POST', body:'hello'})\n"
        "  .then(function(r){ return r.text(); })\n"
        "  .then(function(t){ _post = t; })\n"
        "  .catch(function(e){ _post = 'error:' + e.message; });\n"
        "0", &out));

    std::string v;
    ASSERT_TRUE(host_poll_until_value(h, "_post", "ok", &v));
}

TEST_F(FetchStreamTest, PostUint8ArrayBody) {
    const char *resp =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 2\r\n"
        "\r\n"
        "ok";
    ASSERT_EQ(0, mock_tcp_respond(&h->rt->loop, resp, strlen(resp)));

    std::string out;
    ASSERT_TRUE(host_eval(h,
        "var _post = null;\n"
        "var _bytes = new Uint8Array([104, 105]);  /* 'hi' as raw bytes */\n"
        "fetch('http://test.local/post', {method:'POST', body:_bytes})\n"
        "  .then(function(r){ return r.text(); })\n"
        "  .then(function(t){ _post = t; })\n"
        "  .catch(function(e){ _post = 'error:' + e.message; });\n"
        "0", &out));

    std::string v;
    ASSERT_TRUE(host_poll_until_value(h, "_post", "ok", &v));
}

TEST_F(FetchStreamTest, PostReadableStreamBody) {
    /* stream body：serializeBody 异步读取全部 chunk 后再发请求 */
    const char *resp =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 2\r\n"
        "\r\n"
        "ok";
    ASSERT_EQ(0, mock_tcp_respond(&h->rt->loop, resp, strlen(resp)));

    std::string out;
    ASSERT_TRUE(host_eval(h,
        "var _post = null;\n"
        "var _stream = new ReadableStream({\n"
        "  start: function(c){ c.enqueue('chunk1'); c.enqueue('chunk2'); c.close(); }\n"
        "});\n"
        "fetch('http://test.local/post', {method:'POST', body:_stream})\n"
        "  .then(function(r){ return r.text(); })\n"
        "  .then(function(t){ _post = t; })\n"
        "  .catch(function(e){ _post = 'error:' + e.message; });\n"
        "0", &out));

    std::string v;
    ASSERT_TRUE(host_poll_until_value(h, "_post", "ok", &v));
}

TEST_F(FetchStreamTest, PostStreamErrorRejects) {
    /* stream 读取失败 → whenBodyReady 的 reject 分支 */
    const char *resp =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 2\r\n"
        "\r\n"
        "ok";
    ASSERT_EQ(0, mock_tcp_respond(&h->rt->loop, resp, strlen(resp)));

    std::string out;
    ASSERT_TRUE(host_eval(h,
        "var _post = null;\n"
        "var _stream = new ReadableStream({\n"
        "  start: function(c){ c.error(new Error('boom')); }\n"
        "});\n"
        "fetch('http://test.local/post', {method:'POST', body:_stream})\n"
        "  .then(function(){ _post = 'resolved'; })\n"
        "  .catch(function(e){ _post = 'rejected:' + e.message; });\n"
        "0", &out));

    std::string v;
    ASSERT_TRUE(host_poll_until_value(h, "_post", "rejected", &v));
}

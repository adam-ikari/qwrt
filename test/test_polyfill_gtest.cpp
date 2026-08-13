// test_polyfill_gtest.cpp — polyfill 各模块（执行模型 A / mock_libuv）
// Console/Encoding/Url 同步求值；Timer/Storage/Fetch 异步，用 host_poll_until_value
// 轮询（每次 host_eval 都跑一轮 loop + 冲刷微任务）。
#include "test_host.h"
#include <cstring>

class PolyfillTest : public ::testing::Test {
protected:
    HostCtx *h = nullptr;

    void SetUp() override {
        h = host_create();
        ASSERT_NE(nullptr, h);
    }
    void TearDown() override { host_destroy(h); }
};

TEST_F(PolyfillTest, Console) {
    std::string out;
    ASSERT_TRUE(host_eval(h, "console.log('hello', 'polyfill'); 1", &out));
    EXPECT_NE(std::string::npos, out.find("\"ok\":true"));   /* 无异常即通过 */
}

TEST_F(PolyfillTest, Timer) {
    std::string out;
    ASSERT_TRUE(host_eval(h,
        "var _timerFired = false;\n"
        "setTimeout(function(){ _timerFired = true; }, 100);\n"
        "0", &out));

    std::string v;
    ASSERT_TRUE(host_poll_until_value(h, "_timerFired", "true", &v));
}

TEST_F(PolyfillTest, Encoding) {
    std::string v;
    ASSERT_TRUE(host_value(h, "btoa('hello')", &v));
    EXPECT_NE(std::string::npos, v.find("aGVsbG8="));
    ASSERT_TRUE(host_value(h, "atob('aGVsbG8=')", &v));
    EXPECT_NE(std::string::npos, v.find("hello"));
}

TEST_F(PolyfillTest, Url) {
    std::string v;
    ASSERT_TRUE(host_value(h,
        "var u = new URL('https://example.com/path?q=1#frag');\n"
        "JSON.stringify({q: u.searchParams.get('q'), proto: u.protocol,\n"
        "  host: u.hostname, path: u.pathname})", &v));
    EXPECT_NE(std::string::npos, v.find("\"q\":\"1\""));
    EXPECT_NE(std::string::npos, v.find("\"proto\":\"https:\""));
    EXPECT_NE(std::string::npos, v.find("\"host\":\"example.com\""));
    EXPECT_NE(std::string::npos, v.find("\"path\":\"/path\""));
}

TEST_F(PolyfillTest, Storage) {
    std::string v;
    ASSERT_TRUE(host_eval(h,
        "var _setOk = null; var _got = null; var _delOk = null;\n"
        "qwrt.storage.set('pf_key', 'pf_value').then(function(){ _setOk = 'yes'; });\n"
        "0", &v));
    ASSERT_TRUE(host_poll_until_value(h, "_setOk", "yes", &v));

    ASSERT_TRUE(host_eval(h,
        "qwrt.storage.get('pf_key').then(function(v){ _got = v; });\n"
        "0", &v));
    ASSERT_TRUE(host_poll_until_value(h, "_got", "pf_value", &v));

    ASSERT_TRUE(host_eval(h,
        "qwrt.storage.delete('pf_key').then(function(){ _delOk = 'yes'; });\n"
        "0", &v));
    ASSERT_TRUE(host_poll_until_value(h, "_delOk", "yes", &v));
}

TEST_F(PolyfillTest, Fetch) {
    const char *resp =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 20\r\n"
        "\r\n"
        "polyfill fetch works";
    ASSERT_EQ(0, mock_tcp_respond(&h->rt->loop, resp, strlen(resp)));

    std::string v;
    ASSERT_TRUE(host_eval(h,
        "var _fetchResult = null;\n"
        "fetch('http://example.com/api').then(function(r){ return r.text(); })\n"
        "  .then(function(t){ _fetchResult = t; });\n"
        "0", &v));
    ASSERT_TRUE(host_poll_until_value(h, "_fetchResult", "polyfill fetch works", &v));
}

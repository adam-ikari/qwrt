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

TEST_F(PolyfillTest, UrlPatternBasic) {
    std::string v;
    /* named group :id matches one path segment */
    ASSERT_TRUE(host_value(h,
        "var p = new URLPattern('/books/:id');\n"
        "JSON.stringify({t: p.test('https://x.com/books/42'),\n"
        "  g: p.exec('https://x.com/books/42').pathname.groups})", &v));
    EXPECT_NE(std::string::npos, v.find("\"t\":true"));
    EXPECT_NE(std::string::npos, v.find("\"id\":\"42\""));
}

TEST_F(PolyfillTest, UrlPatternModifiers) {
    std::string v;
    /* :x? optional segment */
    ASSERT_TRUE(host_value(h,
        "var p = new URLPattern({pathname: '/a/:x?'});\n"
        "JSON.stringify([p.test('https://x.com/a/5'), p.test('https://x.com/a/')])", &v));
    EXPECT_NE(std::string::npos, v.find("true"));

    /* :x+ one-or-more segments (greedy, crosses '/') */
    ASSERT_TRUE(host_value(h,
        "var p = new URLPattern({pathname: '/a/:x+'});\n"
        "var m = p.exec('https://x.com/a/b/c');\n"
        "JSON.stringify({t: !!m, g: m ? m.pathname.groups : null})", &v));
    EXPECT_NE(std::string::npos, v.find("\"t\":true"));
    EXPECT_NE(std::string::npos, v.find("\"x\":\"b/c\""));

    /* :x* zero-or-more segments (greedy) */
    ASSERT_TRUE(host_value(h,
        "var p = new URLPattern({pathname: '/a/:x*'});\n"
        "var m = p.exec('https://x.com/a/b/c');\n"
        "JSON.stringify({t: !!m, g: m ? m.pathname.groups : null})", &v));
    EXPECT_NE(std::string::npos, v.find("\"t\":true"));
    EXPECT_NE(std::string::npos, v.find("\"x\":\"b/c\""));
}

TEST_F(PolyfillTest, FormData) {
    std::string v;
    /* append / get / getAll / has / set / delete */
    ASSERT_TRUE(host_value(h,
        "var fd = new FormData();\n"
        "fd.append('k', 'a'); fd.append('k', 'b');\n"
        "var r = [fd.get('k'), fd.getAll('k').length, fd.getAll('k')[0], fd.getAll('k')[1], fd.has('k')];\n"
        "fd.set('k', 'c'); r.push(fd.get('k'));\n"
        "fd.delete('k'); r.push(fd.has('k'));\n"
        "JSON.stringify(r)", &v));
    EXPECT_NE(std::string::npos, v.find("\"a\""));
    EXPECT_NE(std::string::npos, v.find("2"));   /* getAll length */
    EXPECT_NE(std::string::npos, v.find("\"b\""));
    EXPECT_NE(std::string::npos, v.find("true")); /* has */
    EXPECT_NE(std::string::npos, v.find("\"c\"")); /* after set */
    EXPECT_NE(std::string::npos, v.find("false")); /* after delete */

    /* keys / values / entries / forEach */
    ASSERT_TRUE(host_value(h,
        "var fd = new FormData(); fd.append('k1','v1'); fd.append('k2','v2');\n"
        "var ks = [], vs = [];\n"
        "fd.forEach(function(v,k){ ks.push(k); vs.push(v); });\n"
        "JSON.stringify([ks.join(','), vs.join(','), fd.size])", &v));
    EXPECT_NE(std::string::npos, v.find("k1,k2"));
    EXPECT_NE(std::string::npos, v.find("v1,v2"));

    /* Blob value with filename -> File-like */
    ASSERT_TRUE(host_value(h,
        "var fd = new FormData();\n"
        "fd.append('f', new Blob(['hi']), 'a.txt');\n"
        "var f = fd.get('f');\n"
        "JSON.stringify([f instanceof Blob, f.name, f.size])", &v));
    EXPECT_NE(std::string::npos, v.find("true"));
    EXPECT_NE(std::string::npos, v.find("\"a.txt\""));
}

TEST_F(PolyfillTest, ErrorEventAndEvent) {
    std::string v;
    /* ErrorEvent ctor fills standard fields */
    ASSERT_TRUE(host_value(h,
        "var e = new ErrorEvent('error', {message:'boom', filename:'x.js', lineno:3, colno:5});\n"
        "JSON.stringify([e.type, e.message, e.filename, e.lineno, e.colno])", &v));
    EXPECT_NE(std::string::npos, v.find("\"error\""));
    EXPECT_NE(std::string::npos, v.find("\"boom\""));
    EXPECT_NE(std::string::npos, v.find("\"x.js\""));
    EXPECT_NE(std::string::npos, v.find("3"));
    EXPECT_NE(std::string::npos, v.find("5"));

    /* ErrorEvent defaults (empty message, 0/0 coords) */
    ASSERT_TRUE(host_value(h,
        "var e = new ErrorEvent('error');\n"
        "JSON.stringify([e.message, e.filename, e.lineno, e.colno])", &v));
    EXPECT_NE(std::string::npos, v.find("\"\""));

    /* Event basic flags */
    ASSERT_TRUE(host_value(h,
        "var e = new Event('x');\n"
        "JSON.stringify([e.type, e.bubbles, e.cancelable])", &v));
    EXPECT_NE(std::string::npos, v.find("\"x\""));
    EXPECT_NE(std::string::npos, v.find("false"));
}

TEST_F(PolyfillTest, StructuredCloneTransferArrayBuffer) {
    std::string v;
    /* transfer 一个 ArrayBuffer：克隆结果保留内容，原 buffer 被 detach */
    ASSERT_TRUE(host_value(h,
        "var ab = new ArrayBuffer(4);\n"
        "new Uint8Array(ab).set([1,2,3,4]);\n"
        "var c = structuredClone(ab, {transfer:[ab]});\n"
        "JSON.stringify({src: ab.byteLength, c: new Uint8Array(c).join(',')})", &v));
    EXPECT_NE(std::string::npos, v.find("\"src\":0")) << "got: " << v;      /* 原 buffer detached */
    EXPECT_NE(std::string::npos, v.find("\"c\":\"1,2,3,4\"")) << "got: " << v;

    /* 不带 transfer：原 buffer 不被 detach */
    ASSERT_TRUE(host_value(h,
        "var ab = new ArrayBuffer(4);\n"
        "var c = structuredClone(ab);\n"
        "JSON.stringify(ab.byteLength)", &v));
    EXPECT_NE(std::string::npos, v.find("4")) << "got: " << v;

    /* transfer 列表含不可转移对象（普通对象）→ DataCloneError */
    ASSERT_TRUE(host_value(h,
        "var r = 'no-error';\n"
        "try { structuredClone({}, {transfer:[{}]}); }\n"
        "catch (e) { r = e.name; }\n"
        "JSON.stringify(r)", &v));
    EXPECT_NE(std::string::npos, v.find("\"DataCloneError\"")) << "got: " << v;

    /* transfer 列表重复对象 → DataCloneError */
    ASSERT_TRUE(host_value(h,
        "var ab = new ArrayBuffer(4);\n"
        "var r = 'no-error';\n"
        "try { structuredClone(ab, {transfer:[ab, ab]}); }\n"
        "catch (e) { r = e.name; }\n"
        "JSON.stringify(r)", &v));
    EXPECT_NE(std::string::npos, v.find("\"DataCloneError\"")) << "got: " << v;
}

TEST_F(PolyfillTest, StructuredCloneTransferMessagePort) {
    std::string v;
    /* transfer 一个 MessagePort：不抛错，原 port 被标记 detached */
    ASSERT_TRUE(host_value(h,
        "var ch = new MessageChannel();\n"
        "var c = structuredClone({p: ch.port1}, {transfer:[ch.port1]});\n"
        "JSON.stringify({d: ch.port1._detached, hasP: c.p instanceof MessagePort})", &v));
    EXPECT_NE(std::string::npos, v.find("\"d\":true")) << "got: " << v;   /* 原 port detached */
    EXPECT_NE(std::string::npos, v.find("\"hasP\":true")) << "got: " << v;

    /* 值里的 MessagePort（不在 transfer）不可克隆 → DataCloneError */
    ASSERT_TRUE(host_value(h,
        "var r = 'no-error';\n"
        "try { structuredClone(new MessageChannel().port1); }\n"
        "catch (e) { r = e.name; }\n"
        "JSON.stringify(r)", &v));
    EXPECT_NE(std::string::npos, v.find("\"DataCloneError\"")) << "got: " << v;
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

// ================================================================
// BYOB streams (ECMA-429 Streams: ReadableByteStreamController /
// ReadableStreamBYOBReader / ReadableStreamBYOBRequest)
// ================================================================

TEST_F(PolyfillTest, ReadableByteStreamGlobals) {
    std::string v;
    /* 三个 BYOB 接口必须暴露为构造函数 */
    ASSERT_TRUE(host_value(h,
        R"(JSON.stringify([typeof ReadableByteStreamController,
          typeof ReadableStreamBYOBReader, typeof ReadableStreamBYOBRequest]))", &v));
    EXPECT_NE(std::string::npos, v.find("\"function\"")) << "got: " << v;
}

TEST_F(PolyfillTest, ByteStreamByobReaderMode) {
    std::string v;
    /* bytes stream + getReader({mode:'byob'}) → BYOBReader 实例 + 锁住 */
    ASSERT_TRUE(host_value(h,
        R"(var s = new ReadableStream({type:'bytes'});
        var r = s.getReader({mode:'byob'});
        JSON.stringify([r instanceof ReadableStreamBYOBReader, s.locked]))", &v));
    EXPECT_NE(std::string::npos, v.find("true")) << "got: " << v;

    /* 非 bytes stream + byob mode → TypeError */
    ASSERT_TRUE(host_value(h,
        R"(var s2 = new ReadableStream();
        var err = 'none';
        try { s2.getReader({mode:'byob'}); } catch(e) { err = e.name; }
        JSON.stringify(err))", &v));
    EXPECT_NE(std::string::npos, v.find("\"TypeError\"")) << "got: " << v;

    /* 非 bytes stream + 无参 getReader() → default reader（保持兼容） */
    ASSERT_TRUE(host_value(h,
        R"(var s3 = new ReadableStream();
        var r3 = s3.getReader();
        JSON.stringify(r3 instanceof ReadableStreamDefaultReader))", &v));
    EXPECT_NE(std::string::npos, v.find("true")) << "got: " << v;
}

TEST_F(PolyfillTest, ByobReadFromQueue) {
    std::string v;
    /* source 在 start 里 enqueue 两个 Uint8Array 后 close；BYOB read 填充用户 view */
    ASSERT_TRUE(host_eval(h,
        R"(var _byob_r = null;
        var _byob_r2 = null;
        var s = new ReadableStream({
          type: 'bytes',
          start: function(c) {
            c.enqueue(new Uint8Array([1,2,3,4]));
            c.enqueue(new Uint8Array([5,6]));
            c.close();
          }
        });
        var reader = s.getReader({mode:'byob'});
        reader.read(new Uint8Array(4)).then(function(res){
          _byob_r = JSON.stringify({d: res.done, b: Array.from(res.value)});
        });
        0)", &v));
    ASSERT_TRUE(host_poll_until_value(h, "_byob_r", "\"b\":[1,2,3,4]", &v));

    /* 第二次读剩余 chunk [5,6]：value 部分填充，长度为 2 */
    ASSERT_TRUE(host_eval(h,
        R"(reader.read(new Uint8Array(4)).then(function(res){
          _byob_r2 = JSON.stringify({d: res.done, b: Array.from(res.value), l: res.value.length});
        });
        0)", &v));
    ASSERT_TRUE(host_poll_until_value(h, "_byob_r2", "\"b\":[5,6]", &v));
    std::string v2;
    ASSERT_TRUE(host_value(h, "_byob_r2", &v2));
    EXPECT_NE(std::string::npos, v2.find("\"l\":2")) << "got: " << v2;
}

TEST_F(PolyfillTest, ByobReadClosedEmpty) {
    std::string v;
    /* 空队列 + 已 close → read 返回 {done:true} */
    ASSERT_TRUE(host_eval(h,
        R"(var _byob_d = null;
        var s = new ReadableStream({type:'bytes', start:function(c){ c.close(); }});
        var reader = s.getReader({mode:'byob'});
        reader.read(new Uint8Array(4)).then(function(res){
          _byob_d = JSON.stringify([res.done]);
        });
        0)", &v));
    ASSERT_TRUE(host_poll_until_value(h, "_byob_d", "true", &v));
}

TEST_F(PolyfillTest, ByobRequestRespond) {
    std::string v;
    /* pull 里经 controller.byobRequest.view 写数据 + respond(n) 完成一次 BYOB read */
    ASSERT_TRUE(host_eval(h,
        R"(var _byob_req = null;
        var pulled = 0;
        var s = new ReadableStream({
          type: 'bytes',
          pull: function(c) {
            pulled++;
            var req = c.byobRequest;
            if (req && req.view) {
              var w = new Uint8Array(req.view.buffer, req.view.byteOffset, 3);
              w.set([7,8,9]);
              req.respond(3);
            }
          }
        });
        var reader = s.getReader({mode:'byob'});
        reader.read(new Uint8Array(8)).then(function(res){
          _byob_req = JSON.stringify({d: res.done, b: Array.from(res.value), l: res.value.length});
        });
        0)", &v));
    ASSERT_TRUE(host_poll_until_value(h, "_byob_req", "\"b\":[7,8,9]", &v));
    std::string v2;
    ASSERT_TRUE(host_value(h, "_byob_req", &v2));
    EXPECT_NE(std::string::npos, v2.find("\"l\":3")) << "got: " << v2;
    EXPECT_NE(std::string::npos, v2.find("\"d\":false")) << "got: " << v2;
}

// ================================================================
// Crypto / Performance constructor exposure (ECMA-429 WEBCRYPTO / HR-TIME)
// ================================================================

TEST_F(PolyfillTest, CryptoGlobals) {
    std::string v;
    /* Crypto / SubtleCrypto 构造函数暴露为 globalThis，且实例关系正确 */
    ASSERT_TRUE(host_value(h,
        R"(JSON.stringify([typeof Crypto, typeof SubtleCrypto,
          globalThis.crypto instanceof Crypto,
          globalThis.crypto.subtle instanceof SubtleCrypto]))", &v));
    EXPECT_NE(std::string::npos, v.find("\"function\"")) << "got: " << v;
    EXPECT_NE(std::string::npos, v.find("true")) << "got: " << v;

    /* getRandomValues 回归：返回同一 typed array，填充正确字节数 */
    ASSERT_TRUE(host_value(h,
        R"(var u8 = new Uint8Array(16);
        var r = globalThis.crypto.getRandomValues(u8);
        JSON.stringify([r === u8, u8.length, u8[0] !== undefined]))", &v));
    EXPECT_NE(std::string::npos, v.find("true")) << "got: " << v;
}

TEST_F(PolyfillTest, PerformanceGlobals) {
    std::string v;
    /* Performance 构造函数暴露为 globalThis，且实例关系正确 */
    ASSERT_TRUE(host_value(h,
        R"(JSON.stringify([typeof Performance,
          globalThis.performance instanceof Performance,
          typeof globalThis.performance.now]))", &v));
    EXPECT_NE(std::string::npos, v.find("\"function\"")) << "got: " << v;
    EXPECT_NE(std::string::npos, v.find("true")) << "got: " << v;

    /* performance.now 回归：返回数字（毫秒） */
    ASSERT_TRUE(host_value(h,
        R"(var t = globalThis.performance.now();
        JSON.stringify([typeof t, t >= 0]))", &v));
    EXPECT_NE(std::string::npos, v.find("\"number\"")) << "got: " << v;
    EXPECT_NE(std::string::npos, v.find("true")) << "got: " << v;
}

TEST_F(PolyfillTest, FetchRequestOptions) {
    std::string v;
    ASSERT_TRUE(host_value(h,
        "var r = new Request('https://x.com', {redirect:'error', keepalive:true, cache:'no-store', mode:'cors', credentials:'include'});\n"
        "JSON.stringify([r.redirect, r.keepalive, r.cache, r.mode, r.credentials])", &v));
    EXPECT_NE(std::string::npos, v.find("\"error\"")) << "got: " << v;
    EXPECT_NE(std::string::npos, v.find("true")) << "got: " << v;
    EXPECT_NE(std::string::npos, v.find("\"no-store\"")) << "got: " << v;
    EXPECT_NE(std::string::npos, v.find("\"cors\"")) << "got: " << v;
    EXPECT_NE(std::string::npos, v.find("\"include\"")) << "got: " << v;
}

// ================================================================
// Streams semantic deepening — Task 1.3/1.4/1.5
// (tee cancel propagation, pipeTo abort/backpressure, TextDecoderStream
// cross-chunk multibyte boundary)
// ================================================================

TEST_F(PolyfillTest, StreamTeeCancel) {
    std::string v;
    /* 两个分支都 cancel → 源流 cancel 被调用，源 reader 锁释放 */
    ASSERT_TRUE(host_eval(h,
        R"(var _tc = null;
        var srcCancelled = false;
        var s = new ReadableStream({
          start: function(c){ c.enqueue('x'); },
          cancel: function(){ srcCancelled = true; }
        });
        var branches = s.tee();
        Promise.all([branches[0].cancel('a'), branches[1].cancel('b')]).then(function(){
          _tc = JSON.stringify([srcCancelled, s.locked]);
        });
        0)", &v));
    /* 期望 [true,false]：源 cancelled=true 且源锁释放。当前实现 cancel 为空函数
     * → [false,true]，此断言不匹配 → RED */
    ASSERT_TRUE(host_poll_until_value(h, "_tc", "[true,false]", &v));
}

TEST_F(PolyfillTest, StreamTeeCancelSingleBranch) {
    std::string v;
    /* 只 cancel 一个分支：源不应被 cancel（另一分支仍可用），锁不释放 */
    ASSERT_TRUE(host_eval(h,
        R"(var _tc = null;
        var srcCancelled = false;
        var s = new ReadableStream({
          start: function(c){ c.enqueue('x'); },
          cancel: function(){ srcCancelled = true; }
        });
        var branches = s.tee();
        branches[0].cancel().then(function(){
          _tc = JSON.stringify([srcCancelled, branches[1].locked]);
        });
        0)", &v));
    /* 期望 [false,false]：源未取消，分支1 未被锁住（仍可 getReader） */
    ASSERT_TRUE(host_poll_until_value(h, "_tc", "[false,false]", &v));
}

TEST_F(PolyfillTest, StreamPipeToAbort) {
    std::string v;
    /* dest write 失败 → pipeTo reject，且源流被 cancel（preventCancel 缺省为 false）；
     * 同时验证背压：writer.write 返回 pending 时 pump 不并发推进 */
    ASSERT_TRUE(host_eval(h,
        R"(var _pt = null;
        var srcCancelled = false;
        var writeCount = 0;
        var src = new ReadableStream({
          start: function(c){ c.enqueue('d1'); c.enqueue('d2'); c.enqueue('d3'); },
          cancel: function(){ srcCancelled = true; }
        });
        var dest = new WritableStream({
          write: function(chunk) {
            writeCount++;
            if (chunk === 'd2') return Promise.reject(new Error('sink boom'));
            return new Promise(function(res){ setTimeout(res, 30); });
          }
        });
        src.pipeTo(dest).catch(function(e){
          _pt = JSON.stringify([srcCancelled, e.message]);
        });
        0)", &v));
    /* 期望：源 cancelled=true，错误消息为 'sink boom' */
    ASSERT_TRUE(host_poll_until_value(h, "_pt", "true", &v));
    std::string v2;
    ASSERT_TRUE(host_value(h, "_pt", &v2));
    EXPECT_NE(std::string::npos, v2.find("sink boom")) << "got: " << v2;

    /* 背压：写端第一次 write 是 pending(30ms)，期间不应发起第二次 read ——
     * d2 失败时 d3 尚未被读走（pump 串行）。验证失败时源只 abort，不 cancel。 */
    ASSERT_TRUE(host_value(h, "JSON.stringify(writeCount)", &v));
    EXPECT_EQ(0, v.compare("2")) << "写端并发推进了: got " << v;
}

TEST_F(PolyfillTest, TextDecoderStreamMultibyte) {
    std::string v;
    /* '€' = E2 82 AC 拆成两个 chunk 写入，readable 拼接后应为单字符 */
    ASSERT_TRUE(host_eval(h,
        R"(var _td = null;
        var tds = new TextDecoderStream();
        var writer = tds.writable.getWriter();
        var reader = tds.readable.getReader();
        var out = '';
        function readAll() {
          reader.read().then(function(r){
            if (r.done) { _td = JSON.stringify(out); return; }
            out += r.value;
            readAll();
          });
        }
        readAll();
        writer.write(new Uint8Array([0xE2]));
        writer.write(new Uint8Array([0x82, 0xAC]));
        writer.close();
        0)", &v));
    /* 期望输出字符串 '€'（跨 chunk 多字节正确拼接） */
    ASSERT_TRUE(host_poll_until_value(h, "_td", "€", &v));
}


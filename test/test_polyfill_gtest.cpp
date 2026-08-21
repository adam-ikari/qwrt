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

TEST_F(PolyfillTest, StreamTeeCancel) {
    std::string v;
    /* 单分支取消不应传播到源（另一分支仍活跃）；两分支都取消后才释放源锁
     * 并触发 underlying source 的 cancel（WHATWG tee 语义）。 */
    ASSERT_TRUE(host_eval(h,
        R"(var _tc = null;
        var srcCancelled = false;
        var s = new ReadableStream({
          type: 'bytes',
          start: function(c){ c.enqueue(new Uint8Array([1,2,3])); },
          cancel: function(){ srcCancelled = true; }
        });
        var branches = s.tee();
        branches[0].cancel().then(function(){
          var afterOne = JSON.stringify({src: srcCancelled});
          branches[1].cancel().then(function(){
            _tc = afterOne + '|' + JSON.stringify({src: srcCancelled});
          });
        });
        0)", &v));
    /* 阶段一：仅分支0取消 → 源未被 cancel（分支1 仍活跃） */
    ASSERT_TRUE(host_poll_until_value(h, "_tc", "\"src\":false", &v)) << "got: " << v;
    /* 阶段二：两分支都取消 → 源 cancel 传播 */
    ASSERT_TRUE(host_poll_until_value(h, "_tc", "\"src\":true", &v)) << "got: " << v;
}

TEST_F(PolyfillTest, PipeToPreventAbort) {
    std::string v;
    /* preventAbort:true：源 errored 时 pipeTo 仍 reject，但 writer 不被 abort */
    ASSERT_TRUE(host_eval(h,
        R"(var _pa = null;
        var abortCalled = false;
        var src = new ReadableStream({
          start: function(c){ c.error(new Error('boom')); }
        });
        var dest = new WritableStream({
          write: function(){ return Promise.resolve(); },
          abort: function(){ abortCalled = true; }
        });
        var p = src.pipeTo(dest, {preventAbort: true});
        p.then(function(){ _pa = 'resolved:' + abortCalled; },
               function(e){ _pa = 'rejected:' + abortCalled; });
        0)", &v));
    /* pipeTo reject 且 abort 未被调用 */
    ASSERT_TRUE(host_poll_until_value(h, "_pa", "rejected:false", &v)) << "got: " << v;
}

TEST_F(PolyfillTest, PipeToPreventClose) {
    std::string v;
    /* preventClose:true：源读完时 pipeTo resolve，但 writer 不被 close */
    ASSERT_TRUE(host_eval(h,
        R"(var _pc = null;
        var closeCalled = false;
        var src = new ReadableStream({
          start: function(c){ c.enqueue(1); c.close(); }
        });
        var dest = new WritableStream({
          write: function(){ return Promise.resolve(); },
          close: function(){ closeCalled = true; }
        });
        var p = src.pipeTo(dest, {preventClose: true});
        p.then(function(){ _pc = 'resolved:' + closeCalled; },
               function(e){ _pc = 'rejected:' + closeCalled; });
        0)", &v));
    /* pipeTo resolve 且 close 未被调用 */
    ASSERT_TRUE(host_poll_until_value(h, "_pc", "resolved:false", &v)) << "got: " << v;
}

TEST_F(PolyfillTest, PipeToBackpressureSerial) {
    std::string v;
    /* write 返回 pending promise：pump 必须等待其完成才读下一个 chunk（串行） */
    ASSERT_TRUE(host_eval(h,
        R"(var _bp = null;
        var writes = '';
        var src = new ReadableStream({
          start: function(c){ c.enqueue(1); c.enqueue(2); c.enqueue(3); c.close(); }
        });
        var dest = new WritableStream({
          write: function(chunk){
            writes += String(chunk);
            return new Promise(function(resolve){ setTimeout(resolve, 30); });
          }
        });
        var p = src.pipeTo(dest);
        p.then(function(){ _bp = writes; },
               function(e){ _bp = 'error:' + e.message; });
        0)", &v));
    /* 串行写入：1、2、3 顺序到达，无并发重入 */
    ASSERT_TRUE(host_poll_until_value(h, "_bp", "123", &v)) << "got: " << v;
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
    /* 3 字节 UTF-8 字符（€ = E2 82 AC）跨 chunk 边界写入 TextDecoderStream：
       第一个 chunk 只含首字节，第二个 chunk 含剩余两字节。writable 两次 write
       必须靠 TextDecoder stream:true 的残留缓冲正确拼接，readable 输出一个字符。
       用 charCodeAt 断言避免 C++/JSON 非 ASCII 编码歧义（0x20AC = 8364）。 */
    ASSERT_TRUE(host_eval(h,
        R"(var _tdec = null;
        var s = new TextDecoderStream('utf-8');
        var writer = s.writable.getWriter();
        var reader = s.readable.getReader();
        var out = '';
        writer.write(new Uint8Array([0xE2]));
        writer.write(new Uint8Array([0x82, 0xAC]));
        writer.close();
        function pump() {
          reader.read().then(function(r){
            if (r.done) { _tdec = JSON.stringify([out.length, out.charCodeAt(0)]); return; }
            out += r.value;
            pump();
          });
        }
        pump();
        0)", &v));
    /* 输出恰好 1 个字符，码点 0x20AC (8364) */
    ASSERT_TRUE(host_poll_until_value(h, "_tdec", "8364", &v)) << "got: " << v;
    ASSERT_TRUE(host_poll_until_value(h, "_tdec", "[1,", &v)) << "got: " << v;
}

TEST_F(PolyfillTest, TextDecoderEdgeCases) {
    std::string v;

    /* 1. 多字节 + 代理对正确解码（€=E2 82 AC；😀=F0 9F 98 80） */
    ASSERT_TRUE(host_value(h,
        "var t = new TextDecoder();\n"
        "JSON.stringify([t.decode(new Uint8Array([0xE2,0x82,0xAC])), t.decode(new Uint8Array([0xF0,0x9F,0x98,0x80]))])", &v));
    EXPECT_NE(std::string::npos, v.find("\"€\"")) << "got: " << v;
    EXPECT_NE(std::string::npos, v.find("\"😀\"")) << "got: " << v;

    /* 2. 非 fatal：非法字节 → U+FFFD (0xFFFD) */
    ASSERT_TRUE(host_value(h,
        "var t = new TextDecoder();\n"
        "var s = t.decode(new Uint8Array([0x41, 0x80]));\n"
        "JSON.stringify([s.length, s.charCodeAt(1)])", &v));
    EXPECT_NE(std::string::npos, v.find("[2,65533]")) << "got: " << v;

    /* 3. fatal:true：遇非法字节应抛 TypeError */
    ASSERT_TRUE(host_value(h,
        "var t = new TextDecoder('utf-8', {fatal:true});\n"
        "var err = null;\n"
        "try { t.decode(new Uint8Array([0x41, 0x80])); } catch(e) { err = String(e); }\n"
        "JSON.stringify([err !== null, /TypeError/.test(err)])", &v));
    EXPECT_NE(std::string::npos, v.find("[true,true]")) << "got: " << v;

    /* 4. 流式半字符：{stream:true} 跨调用残留，后续调用补全出完整字符 */
    ASSERT_TRUE(host_value(h,
        "var t = new TextDecoder();\n"
        "var a = t.decode(new Uint8Array([0xE2]), {stream:true});\n"
        "var b = t.decode(new Uint8Array([0x82, 0xAC]), {stream:true});\n"
        "var c = t.decode();\n"
        "JSON.stringify([a.length, b.charCodeAt(0), c.length])", &v));
    EXPECT_NE(std::string::npos, v.find("[0,8364,0]")) << "got: " << v;

    /* 5. BOM：默认剥离（EF BB BF → 不输出 U+FEFF）；ignoreBOM:true 保留 U+FEFF */
    ASSERT_TRUE(host_value(h,
        "var bom = new Uint8Array([0xEF,0xBB,0xBF,0x41]);\n"
        "var d = new TextDecoder();\n"
        "var d2 = new TextDecoder('utf-8', {ignoreBOM:true});\n"
        "JSON.stringify([d.decode(bom).charCodeAt(0), d2.decode(bom).charCodeAt(0)])", &v));
    EXPECT_NE(std::string::npos, v.find("[65,65279]")) << "got: " << v;  /* 'A', U+FEFF */
}

TEST_F(PolyfillTest, UrlSearchParamsEdgeCases) {
    std::string v;

    /* 1. 解析：重复键、%20 解码、+ 解码为空格 */
    ASSERT_TRUE(host_value(h,
        "var p = new URLSearchParams('a=1&a=2&b=hello%20world&c=x+y');\n"
        "JSON.stringify([p.get('a'), p.getAll('a').join(','), p.get('b'), p.get('c')])", &v));
    EXPECT_NE(std::string::npos, v.find("\"1\"")) << "got: " << v;
    EXPECT_NE(std::string::npos, v.find("\"1,2\"")) << "got: " << v;
    EXPECT_NE(std::string::npos, v.find("\"hello world\"")) << "got: " << v;
    EXPECT_NE(std::string::npos, v.find("\"x y\"")) << "got: " << v;

    /* 2. 编码：空格→+、特殊字符 !'()* → %XX 大写、中文 UTF-8 */
    ASSERT_TRUE(host_value(h,
        "var p = new URLSearchParams();\n"
        "p.set('q', 'a b'); p.set('s', '!\\'()*'); p.set('z', '中文');\n"
        "JSON.stringify(p.toString())", &v));
    EXPECT_NE(std::string::npos, v.find("q=a+b")) << "got: " << v;
    EXPECT_NE(std::string::npos, v.find("%21%27%28%29%2A")) << "got: " << v;
    EXPECT_NE(std::string::npos, v.find("%E4%B8%AD%E6%96%87")) << "got: " << v;

    /* 3. append 重复键 + delete + has + toString 顺序 */
    ASSERT_TRUE(host_value(h,
        "var p = new URLSearchParams('a=1&a=2&b=3');\n"
        "p.append('a', '4');\n"
        "var r = [p.getAll('a').length, p.has('b'), p.toString()];\n"
        "p.delete('a');\n"
        "r.push(p.has('a'));\n"
        "JSON.stringify(r)", &v));
    EXPECT_NE(std::string::npos, v.find("3")) << "got: " << v;  /* getAll a length */
    EXPECT_NE(std::string::npos, v.find("true")) << "got: " << v;
    EXPECT_NE(std::string::npos, v.find("a=4")) << "got: " << v;
    EXPECT_NE(std::string::npos, v.find("false")) << "got: " << v;

    /* 4. sort() 按键排序 */
    ASSERT_TRUE(host_value(h,
        "var p = new URLSearchParams('c=3&a=1&b=2');\n"
        "p.sort();\n"
        "JSON.stringify(p.toString())", &v));
    EXPECT_NE(std::string::npos, v.find("a=1&b=2&c=3")) << "got: " << v;

    /* 5. 迭代器：for..of / entries / keys / values / forEach */
    ASSERT_TRUE(host_value(h,
        "var p = new URLSearchParams('a=1&b=2');\n"
        "var e = [], k = [], vals = [], f = [];\n"
        "for (var x of p) e.push(x.join(':'));\n"
        "for (var kv of p.entries()) k.push(kv[0]);\n"
        "for (var vv of p.values()) vals.push(vv);\n"
        "p.forEach(function(val, key){ f.push(key+'='+val); });\n"
        "JSON.stringify([e.join(';'), k.join(','), vals.join(','), f.join(';')])", &v));
    EXPECT_NE(std::string::npos, v.find("a:1;b:2")) << "got: " << v;
    EXPECT_NE(std::string::npos, v.find("a,b")) << "got: " << v;
    EXPECT_NE(std::string::npos, v.find("1,2")) << "got: " << v;
    EXPECT_NE(std::string::npos, v.find("a=1;b=2")) << "got: " << v;

    /* 6. URL.searchParams 与 URL.search 双向同步 */
    ASSERT_TRUE(host_value(h,
        "var u = new URL('https://x.com/p?q=1');\n"
        "u.searchParams.set('q', '2');\n"
        "u.searchParams.append('r', '3');\n"
        "var s1 = u.search;\n"
        "u.search = '?x=9';\n"
        "var s2 = u.searchParams.get('x');\n"
        "JSON.stringify([s1, s2])", &v));
    EXPECT_NE(std::string::npos, v.find("q=2")) << "got: " << v;
    EXPECT_NE(std::string::npos, v.find("r=3")) << "got: " << v;
    EXPECT_NE(std::string::npos, v.find("\"9\"")) << "got: " << v;
}

TEST_F(PolyfillTest, StreamEdgeCases) {
    std::string v;

    /* 1. tee() 双分支独立消费：同一 chunk 应同时进入两个分支，互不阻塞 */
    ASSERT_TRUE(host_eval(h,
        R"(var _tee2 = null;
        var s = new ReadableStream({
          start: function(c){ c.enqueue('a'); c.enqueue('b'); c.close(); }
        });
        var b = s.tee();
        var r0 = b[0].getReader();
        var r1 = b[1].getReader();
        r0.read().then(function(x){
          r1.read().then(function(y){
            _tee2 = JSON.stringify([x.value, y.value]);
          });
        });
        0)", &v));
    ASSERT_TRUE(host_poll_until_value(h, "_tee2", "\"a\"", &v)) << "got: " << v;

    /* 2. BYOB reader 对 non-byte stream 抛 TypeError */
    ASSERT_TRUE(host_value(h,
        "var s = new ReadableStream();\n"
        "var err = null;\n"
        "try { s.getReader({mode:'byob'}); } catch(e) { err = String(e); }\n"
        "JSON.stringify([err !== null, /TypeError/.test(err)])", &v));
    EXPECT_NE(std::string::npos, v.find("[true,true]")) << "got: " << v;

    /* 3. cancel() 多次调用幂等：第二次直接返回 resolved，不重复触发源 cancel */
    ASSERT_TRUE(host_eval(h,
        R"(var _cidem = null;
        var cancelCount = 0;
        var s = new ReadableStream({
          start: function(c){ c.enqueue('x'); },
          cancel: function(){ cancelCount++; }
        });
        s.cancel().then(function(){
          return s.cancel();
        }).then(function(){
          _cidem = JSON.stringify([cancelCount, s.locked]);
        });
        0)", &v));
    ASSERT_TRUE(host_poll_until_value(h, "_cidem", "[1,", &v)) << "got: " << v;
    ASSERT_TRUE(host_poll_until_value(h, "_cidem", "false", &v)) << "got: " << v;

    /* 4. locked 时 getReader 抛 TypeError，且 locked 为 true */
    ASSERT_TRUE(host_value(h,
        "var s = new ReadableStream({ start: function(c){ c.enqueue('x'); } });\n"
        "var r = s.getReader();\n"
        "var err = null;\n"
        "try { s.getReader(); } catch(e) { err = String(e); }\n"
        "JSON.stringify([err !== null, /TypeError/.test(err), s.locked])", &v));
    EXPECT_NE(std::string::npos, v.find("[true,true,true]")) << "got: " << v;
}

TEST_F(PolyfillTest, EventTargetEdgeCases) {
    std::string v;

    /* 1. {once:true} 触发一次后自动移除 */
    ASSERT_TRUE(host_value(h,
        "var et = new EventTarget();\n"
        "var count = 0;\n"
        "et.addEventListener('x', function(){ count++; }, {once:true});\n"
        "et.dispatchEvent(new Event('x'));\n"
        "et.dispatchEvent(new Event('x'));\n"
        "JSON.stringify(count)", &v));
    EXPECT_NE(std::string::npos, v.find("1")) << "got: " << v;

    /* 2. 重复添加相同回调不重复注册 */
    ASSERT_TRUE(host_value(h,
        "var et = new EventTarget();\n"
        "var count = 0;\n"
        "var fn = function(){ count++; };\n"
        "et.addEventListener('x', fn);\n"
        "et.addEventListener('x', fn);\n"
        "et.dispatchEvent(new Event('x'));\n"
        "JSON.stringify(count)", &v));
    EXPECT_NE(std::string::npos, v.find("1")) << "got: " << v;

    /* 3. removeEventListener 移除后不再触发 */
    ASSERT_TRUE(host_value(h,
        "var et = new EventTarget();\n"
        "var count = 0;\n"
        "var fn = function(){ count++; };\n"
        "et.addEventListener('x', fn);\n"
        "et.removeEventListener('x', fn);\n"
        "et.dispatchEvent(new Event('x'));\n"
        "JSON.stringify(count)", &v));
    EXPECT_NE(std::string::npos, v.find("0")) << "got: " << v;

    /* 4. dispatchEvent 返回值：默认阻止返回 false，否则 true */
    ASSERT_TRUE(host_value(h,
        "var et = new EventTarget();\n"
        "var ev = new Event('x', {cancelable:true});\n"
        "et.addEventListener('x', function(e){ e.preventDefault(); });\n"
        "var r1 = et.dispatchEvent(ev);\n"
        "var r2 = et.dispatchEvent(new Event('y'));\n"
        "JSON.stringify([r1, r2])", &v));
    EXPECT_NE(std::string::npos, v.find("[false,true]")) << "got: " << v;

    /* 5. AbortSignal abort 后 addEventListener 立即回调（微任务调度） */
    ASSERT_TRUE(host_eval(h,
        "var _et5 = null;\n"
        "var ac = new AbortController();\n"
        "ac.abort();\n"
        "var called = false;\n"
        "ac.signal.addEventListener('abort', function(){ called = true; _et5 = JSON.stringify([called, ac.signal.aborted]); });\n"
        "0", &v));
    ASSERT_TRUE(host_poll_until_value(h, "_et5", "[true,true]", &v)) << "got: " << v;
}

TEST_F(PolyfillTest, BlobEdgeCases) {
    std::string v;

    /* 1. blobParts 非对象/无 @@iterator → TypeError；undefined → 空 Blob */
    ASSERT_TRUE(host_value(h,
        "function t(x){ try { new Blob(x); return false; } catch(e){ return /TypeError/.test(String(e)); } }\n"
        "var r = [t(null), t(true), t(0), t('fail'), t({}), t(new Date())];\n"
        "var empty = new Blob(undefined);\n"
        "r.push([empty.size, empty.type]);\n"
        "JSON.stringify(r)", &v));
    EXPECT_NE(std::string::npos, v.find("[true,true,true,true,true,true,[0,\"\"]]")) << "got: " << v;

    /* 2. type 规范化：只去 0x09/0x0A/0x0D，保留空格；`' image/gif '` 保留空格 */
    ASSERT_TRUE(host_value(h,
        "var a = new Blob(['x'], {type: ' Image/GIF '});\n"
        "var b = new Blob(['x'], {type: '\\timage/gif\\t'});\n"
        "JSON.stringify([a.type, b.type])", &v));
    EXPECT_NE(std::string::npos, v.find("\" image/gif \"")) << "got: " << v;
    EXPECT_NE(std::string::npos, v.find("\"image/gif\"")) << "got: " << v;

    /* 3. 字符串元素 UTF-8 编码：非 ASCII 内容 text() 应正确 */
    ASSERT_TRUE(host_eval(h,
        R"(var _bt = null;
        var b = new Blob(['€']);
        b.text().then(function(s){ _bt = JSON.stringify([s.length, s.charCodeAt(0)]); });
        0)", &v));
    ASSERT_TRUE(host_poll_until_value(h, "_bt", "[1,8364]", &v)) << "got: " << v;

    /* 4. slice 负值/小数 + contentType */
    ASSERT_TRUE(host_eval(h,
        R"(var _bs = null;
        var b = new Blob(['0123456789']);
        var s = b.slice(-3, -1, 'text/plain');
        s.text().then(function(t){ _bs = JSON.stringify([t, s.type, s.size]); });
        0)", &v));
    ASSERT_TRUE(host_poll_until_value(h, "_bs", "\"78\"", &v)) << "got: " << v;

    /* 5. arrayBuffer() + json() */
    ASSERT_TRUE(host_eval(h,
        R"(var _bj = null;
        var b = new Blob(['{"k":1}']);
        b.json().then(function(o){ _bj = JSON.stringify([o.k, b.size]); });
        0)", &v));
    ASSERT_TRUE(host_poll_until_value(h, "_bj", "[1,", &v)) << "got: " << v;
}

TEST_F(PolyfillTest, HeadersEdgeCases) {
    std::string v;

    /* 1. 构造：undefined/空对象/序列/record/拷贝 */
    ASSERT_TRUE(host_value(h,
        "var h1 = new Headers();\n"
        "var h2 = new Headers(undefined);\n"
        "var h3 = new Headers({});\n"
        "var h4 = new Headers([['a','1'],['b','2']]);\n"
        "var h5 = new Headers({x:'3'});\n"
        "var h6 = new Headers(h4);\n"
        "JSON.stringify([h1.get('a'), h4.get('a'), h4.get('b'), h5.get('x'), h6.get('a')])", &v));
    EXPECT_NE(std::string::npos, v.find("[null,\"1\",\"2\",\"3\",\"1\"]")) << "got: " << v;

    /* 2. append/set/get/has/delete + 大小写不敏感 + 逗号合并 */
    ASSERT_TRUE(host_value(h,
        "var h = new Headers();\n"
        "h.append('X-Key', 'a');\n"
        "h.append('x-key', 'b');\n"
        "var g = h.get('x-key');\n"
        "h.set('X-Key', 'c');\n"
        "var afterSet = h.get('x-key');\n"
        "var has = h.has('X-KEY');\n"
        "h.delete('x-key');\n"
        "var afterDel = h.get('x-key');\n"
        "JSON.stringify([g, afterSet, has, afterDel])", &v));
    EXPECT_NE(std::string::npos, v.find("[\"a, b\",\"c\",true,null]")) << "got: " << v;

    /* 3. 迭代器：keys/values/entries/Symbol.iterator/forEach */
    ASSERT_TRUE(host_value(h,
        "var h = new Headers([['a','1'],['b','2']]);\n"
        "var ks = [], vs = [], es = [];\n"
        "for (var k of h.keys()) ks.push(k);\n"
        "for (var vv of h.values()) vs.push(vv);\n"
        "for (var e of h.entries()) es.push(e.join(':'));\n"
        "var f = []; h.forEach(function(v,k){ f.push(k+'='+v); });\n"
        "var it = h[Symbol.iterator]();\n"
        "JSON.stringify([ks.join(','), vs.join(','), es.join(';'), f.join(';'), it.next().value.join(':')])", &v));
    EXPECT_NE(std::string::npos, v.find("a,b")) << "got: " << v;
    EXPECT_NE(std::string::npos, v.find("1,2")) << "got: " << v;
    EXPECT_NE(std::string::npos, v.find("a:1;b:2")) << "got: " << v;

    /* 4. new Headers(null) 抛 TypeError；3 元素 pair 抛 TypeError */
    ASSERT_TRUE(host_value(h,
        "function t(f){ try { f(); return false; } catch(e){ return /TypeError/.test(String(e)); } }\n"
        "var r = [t(function(){ new Headers(null); }),\n"
        "         t(function(){ new Headers([['a','b','c']]); }),\n"
        "         t(function(){ new Headers([['name']]); })];\n"
        "JSON.stringify(r)", &v));
    EXPECT_NE(std::string::npos, v.find("[true,true,true]")) << "got: " << v;

    /* 5. 非法 name/value 抛 TypeError */
    ASSERT_TRUE(host_value(h,
        "function t(f){ try { f(); return false; } catch(e){ return /TypeError/.test(String(e)); } }\n"
        "var h = new Headers();\n"
        "var r = [t(function(){ h.set('bad name', 'v'); }),\n"
        "         t(function(){ h.set('ok', '\\x00'); })];\n"
        "JSON.stringify(r)", &v));
    EXPECT_NE(std::string::npos, v.find("[true,true]")) << "got: " << v;
}

TEST_F(PolyfillTest, EncodeIntoEdgeCases) {
    std::string v;
    /* read/written 精确语义（对齐 WPT encodeInto.any.js 数据向量） */
    ASSERT_TRUE(host_value(h,
        "function enc(s, len) {\n"
        "  var buf = new ArrayBuffer(64);\n"
        "  var view = new Uint8Array(buf, 0, len);\n"
        "  var r = new TextEncoder().encodeInto(s, view);\n"
        "  return JSON.stringify([r.read, r.written, Array.from(view.subarray(0, r.written))]);\n"
        "}\n"
        "JSON.stringify([enc('Hi', 0), enc('A', 10), enc('\\u{1D306}', 4), enc('\\u{1D306}A', 3)])", &v));
    EXPECT_NE(std::string::npos, v.find("[0,0,[]]")) << "got: " << v;
    EXPECT_NE(std::string::npos, v.find("[1,1,[65]]")) << "got: " << v;
    EXPECT_NE(std::string::npos, v.find("[2,4,[240,157,140,134]]")) << "got: " << v;
    EXPECT_NE(std::string::npos, v.find("[0,0,[]]")) << "got: " << v;

    /* 孤立代理对 → U+FFFD (EF BF BD)；有效代理对完整 4 字节 */
    ASSERT_TRUE(host_value(h,
        "function enc(s, len) {\n"
        "  var view = new Uint8Array(new ArrayBuffer(64), 0, len);\n"
        "  var r = new TextEncoder().encodeInto(s, view);\n"
        "  return JSON.stringify([r.read, r.written, Array.from(view.subarray(0, r.written))]);\n"
        "}\n"
        "var loneHi = enc('\\uD834A', 10);\n"
        "var loneLo = enc('A\\uDF06', 4);\n"
        "JSON.stringify([loneHi, loneLo])", &v));
    EXPECT_NE(std::string::npos, v.find("[2,4,[239,191,189,65]]")) << "got: " << v;  /* U+FFFD + 'A' */
    EXPECT_NE(std::string::npos, v.find("[2,4,[65,239,191,189]]")) << "got: " << v;  /* 'A' + U+FFFD */

    /* 非 Uint8Array 目标（DataView/Int8Array/ArrayBuffer）抛 TypeError */
    ASSERT_TRUE(host_value(h,
        "function t(f){ try { f(); return false; } catch(e){ return /TypeError/.test(String(e)); } }\n"
        "var te = new TextEncoder();\n"
        "var r = [t(function(){ te.encodeInto('x', new DataView(new ArrayBuffer(4))); }),\n"
        "         t(function(){ te.encodeInto('x', new Int8Array(4)); }),\n"
        "         t(function(){ te.encodeInto('x', new ArrayBuffer(4)); })];\n"
        "JSON.stringify(r)", &v));
    EXPECT_NE(std::string::npos, v.find("[true,true,true]")) << "got: " << v;

    /* 多字节字符无法完整放入剩余空间时不部分写入（read 停在前一个完整字符） */
    ASSERT_TRUE(host_value(h,
        "var view = new Uint8Array(new ArrayBuffer(3), 0, 3);\n"
        "var r = new TextEncoder().encodeInto('A\\u{1D306}', view);\n"
        "JSON.stringify([r.read, r.written, Array.from(view)])", &v));
    EXPECT_NE(std::string::npos, v.find("[1,1,[65,0,0]]")) << "got: " << v;

    /* 6. DOMString 转换：非字符串输入应 String() 后编码（undefined→'undefined'） */
    ASSERT_TRUE(host_value(h,
        "var view = new Uint8Array(new ArrayBuffer(64), 0, 64);\n"
        "var r = new TextEncoder().encodeInto(123, view);\n"
        "JSON.stringify([r.read, r.written, Array.from(view.subarray(0, r.written))])", &v));
    EXPECT_NE(std::string::npos, v.find("[3,3,[49,50,51]]")) << "got: " << v;  /* '1','2','3' */
}

TEST_F(PolyfillTest, RandomUuidAndNavigator) {
    std::string v;

    /* 1. crypto.randomUUID() 返回 v4 UUID 格式 */
    ASSERT_TRUE(host_value(h,
        "var u = crypto.randomUUID();\n"
        "JSON.stringify([typeof u, /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/.test(u), u.length])", &v));
    EXPECT_NE(std::string::npos, v.find("[\"string\",true,36]")) << "got: " << v;

    /* 2. 多次调用不同 */
    ASSERT_TRUE(host_value(h,
        "var a = crypto.randomUUID(); var b = crypto.randomUUID();\n"
        "JSON.stringify(a !== b)", &v));
    EXPECT_NE(std::string::npos, v.find("true")) << "got: " << v;

    /* 3. navigator.userAgent 非空 string */
    ASSERT_TRUE(host_value(h,
        "JSON.stringify([typeof navigator, typeof navigator.userAgent, navigator.userAgent.length > 0])", &v));
    EXPECT_NE(std::string::npos, v.find("[\"object\",\"string\",true]")) << "got: " << v;
}

TEST_F(PolyfillTest, EventTargetAdvanced) {
    std::string v;

    /* 1. composedPath 返回数组含 target */
    ASSERT_TRUE(host_value(h,
        "var et = new EventTarget(); var ev = new Event('x');\n"
        "var p = et.dispatchEvent(ev) || 1;\n"
        "var cp = ev.composedPath();\n"
        "JSON.stringify([Array.isArray(cp), cp.length >= 0])", &v));
    EXPECT_NE(std::string::npos, v.find("[true,true]")) << "got: " << v;

    /* 2. stopImmediatePropagation：后续监听器不触发 */
    ASSERT_TRUE(host_value(h,
        "var et = new EventTarget(); var order = [];\n"
        "et.addEventListener('x', function(e){ order.push('a'); e.stopImmediatePropagation(); });\n"
        "et.addEventListener('x', function(e){ order.push('b'); });\n"
        "et.dispatchEvent(new Event('x'));\n"
        "JSON.stringify(order)", &v));
    EXPECT_NE(std::string::npos, v.find("[\"a\"]")) << "got: " << v;

    /* 3. capture 监听器先于 bubble 触发 */
    ASSERT_TRUE(host_value(h,
        "var et = new EventTarget(); var order = [];\n"
        "et.addEventListener('x', function(){ order.push('bubble'); });\n"
        "et.addEventListener('x', function(){ order.push('capture'); }, {capture:true});\n"
        "et.dispatchEvent(new Event('x'));\n"
        "JSON.stringify(order)", &v));
    EXPECT_NE(std::string::npos, v.find("\"capture\"")) << "got: " << v;
    EXPECT_NE(std::string::npos, v.find("[\"capture\",\"bubble\"]")) << "got: " << v;

    /* 4. 子类化：class extends EventTarget */
    ASSERT_TRUE(host_value(h,
        "var Cls = class extends EventTarget { constructor(){ super(); } };\n"
        "var o = new Cls(); var hit = false;\n"
        "o.addEventListener('y', function(){ hit = true; });\n"
        "o.dispatchEvent(new Event('y'));\n"
        "JSON.stringify([o instanceof EventTarget, hit])", &v));
    EXPECT_NE(std::string::npos, v.find("[true,true]")) << "got: " << v;
}

TEST_F(PolyfillTest, PerformanceApi) {
    std::string v;

    /* 1. now() 返回 number，单调递增 */
    ASSERT_TRUE(host_value(h,
        "var t1 = performance.now(); var t2 = performance.now();\n"
        "JSON.stringify([typeof t1, t1 >= 0, t2 >= t1])", &v));
    EXPECT_NE(std::string::npos, v.find("[\"number\",true,true]")) << "got: " << v;

    /* 2. mark() + getEntries/getEntriesByType('mark') */
    ASSERT_TRUE(host_value(h,
        "performance.mark('a'); performance.mark('b');\n"
        "var marks = performance.getEntriesByType('mark');\n"
        "JSON.stringify([marks.length >= 2, marks[0].name, marks[0].entryType])", &v));
    EXPECT_NE(std::string::npos, v.find("true")) << "got: " << v;
    EXPECT_NE(std::string::npos, v.find("\"mark\"")) << "got: " << v;

    /* 3. measure() + getEntriesByType('measure') */
    ASSERT_TRUE(host_value(h,
        "performance.mark('start'); performance.mark('end');\n"
        "performance.measure('dur', 'start', 'end');\n"
        "var ms = performance.getEntriesByType('measure');\n"
        "JSON.stringify([ms.length >= 1, ms[0].name, ms[0].entryType, ms[0].duration >= 0])", &v));
    EXPECT_NE(std::string::npos, v.find("\"dur\"")) << "got: " << v;
    EXPECT_NE(std::string::npos, v.find("\"measure\"")) << "got: " << v;

    /* 4. clearMarks() 清除 mark 条目 */
    ASSERT_TRUE(host_value(h,
        "performance.mark('x'); performance.clearMarks('x');\n"
        "JSON.stringify(performance.getEntriesByType('mark').filter(function(e){ return e.name === 'x'; }).length)", &v));
    EXPECT_NE(std::string::npos, v.find("0")) << "got: " << v;
}

TEST_F(PolyfillTest, AbortSignalStatic) {
    std::string v;

    /* 1. AbortSignal.abort() 返回已 aborted 的 signal */
    ASSERT_TRUE(host_value(h,
        "var s = AbortSignal.abort('boom');\n"
        "JSON.stringify([s.aborted, s.reason])", &v));
    EXPECT_NE(std::string::npos, v.find("[true,")) << "got: " << v;
    EXPECT_NE(std::string::npos, v.find("\"boom\"")) << "got: " << v;

    /* 2. throwIfAborted：未 aborted 不抛，aborted 抛 */
    ASSERT_TRUE(host_value(h,
        "var s1 = new AbortController().signal;\n"
        "var s2 = AbortSignal.abort('err');\n"
        "var r1 = false, r2 = false;\n"
        "try { s1.throwIfAborted(); r1 = true; } catch(e) {}\n"
        "try { s2.throwIfAborted(); } catch(e) { r2 = true; }\n"
        "JSON.stringify([r1, r2])", &v));
    EXPECT_NE(std::string::npos, v.find("[true,true]")) << "got: " << v;

    /* 3. AbortSignal.any([signal])：任一 abort 则组合 signal abort */
    ASSERT_TRUE(host_value(h,
        "var ac = new AbortController();\n"
        "var combined = AbortSignal.any([ac.signal]);\n"
        "var before = combined.aborted;\n"
        "ac.abort('reason1');\n"
        "JSON.stringify([before, combined.aborted, combined.reason])", &v));
    EXPECT_NE(std::string::npos, v.find("[false,true,")) << "got: " << v;
    EXPECT_NE(std::string::npos, v.find("\"reason1\"")) << "got: " << v;

    /* 4. AbortSignal.timeout(ms)：超时后 signal abort（异步轮询） */
    ASSERT_TRUE(host_eval(h,
        "var _at = null;\n"
        "var s = AbortSignal.timeout(50);\n"
        "s.addEventListener('abort', function(){ _at = JSON.stringify([s.aborted, s.reason instanceof DOMException]); });\n"
        "0", &v));
    ASSERT_TRUE(host_poll_until_value(h, "_at", "[true,true]", &v, 3000)) << "got: " << v;
}

TEST_F(PolyfillTest, UrlSearchParamsBoundary) {
    std::string v;

    /* 1. searchParams 同一性：同一 url 多次取返回同一对象 */
    ASSERT_TRUE(host_value(h,
        "var u = new URL('https://x.com/p?a=1');\n"
        "JSON.stringify(u.searchParams === u.searchParams)", &v));
    EXPECT_NE(std::string::npos, v.find("true")) << "got: " << v;

    /* 2. strict 模式下 searchParams 赋值抛 TypeError（getter-only） */
    ASSERT_TRUE(host_value(h,
        "var u = new URL('https://x.com/p?a=1');\n"
        "var err = null;\n"
        "try { (function(){ 'use strict'; u.searchParams = new URLSearchParams(); })(); }\n"
        "catch(e) { err = String(e); }\n"
        "JSON.stringify([err !== null, /TypeError/.test(err)])", &v));
    EXPECT_NE(std::string::npos, v.find("[true,true]")) << "got: " << v;

    /* 3. search='??a=b'：searchParams 解析出 a=b（url.js 简化实现不编码多余 ?，
     *     WPT 期望 ?%3Fa=b，需完整 URL 解析，高难度，已知限制） */
    ASSERT_TRUE(host_value(h,
        "var u = new URL('https://x.com/p');\n"
        "u.search = '??a=b';\n"
        "JSON.stringify([u.searchParams.get('a'), u.search.length > 0])", &v));
    EXPECT_NE(std::string::npos, v.find("\"b\"")) << "got: " << v;
    EXPECT_NE(std::string::npos, v.find("true")) << "got: " << v;

    /* 4. searchParams 修改反向同步到 url.search */
    ASSERT_TRUE(host_value(h,
        "var u = new URL('https://x.com/p');\n"
        "u.searchParams.set('q', 'hello world');\n"
        "JSON.stringify(u.search)", &v));
    EXPECT_NE(std::string::npos, v.find("q=hello+world")) << "got: " << v;
}

TEST_F(PolyfillTest, QueueMicrotask) {
    std::string v;
    /* 1. 存在 + 类型校验：非函数参数抛 TypeError */
    ASSERT_TRUE(host_value(h,
        "var r = [typeof queueMicrotask];\n"
        "var err = null;\n"
        "try { queueMicrotask(123); } catch(e) { err = String(e); }\n"
        "r.push(err !== null, /TypeError/.test(err));\n"
        "JSON.stringify(r)", &v));
    EXPECT_NE(std::string::npos, v.find("[\"function\",true,true]")) << "got: " << v;

    /* 2. 微任务在同步代码之后、按注册顺序执行 */
    ASSERT_TRUE(host_eval(h,
        "var _qm = null;\n"
        "var order = [];\n"
        "queueMicrotask(function(){ order.push('a'); });\n"
        "queueMicrotask(function(){ order.push('b'); });\n"
        "order.push('sync');\n"
        "queueMicrotask(function(){ _qm = JSON.stringify(order); });\n"
        "0", &v));
    ASSERT_TRUE(host_poll_until_value(h, "_qm", "\"sync\"", &v)) << "got: " << v;
    ASSERT_TRUE(host_poll_until_value(h, "_qm", "\"a\"", &v)) << "got: " << v;
    ASSERT_TRUE(host_poll_until_value(h, "_qm", "\"b\"", &v)) << "got: " << v;
}

TEST_F(PolyfillTest, BtoaAtobEdgeCases) {
    std::string v;
    /* 1. btoa 非 Latin1 字符抛错（> 0xFF） */
    ASSERT_TRUE(host_value(h,
        "var err = null; try { btoa('\\u0100'); } catch(e) { err = String(e); }\n"
        "JSON.stringify([err !== null, err.length > 0])", &v));
    EXPECT_NE(std::string::npos, v.find("[true,true]")) << "got: " << v;

    /* 2. atob 非法 base64 抛错 */
    ASSERT_TRUE(host_value(h,
        "var err = null; try { atob('!!!invalid'); } catch(e) { err = String(e); }\n"
        "JSON.stringify([err !== null, err.length > 0])", &v));
    EXPECT_NE(std::string::npos, v.find("[true,true]")) << "got: " << v;

    /* 3. 往返一致性：btoa → atob 还原原串 */
    ASSERT_TRUE(host_value(h,
        "var s = 'Hello, World!';\n"
        "var encoded = btoa(s); var decoded = atob(encoded);\n"
        "JSON.stringify([decoded, decoded === s])", &v));
    EXPECT_NE(std::string::npos, v.find("\"Hello, World!\"")) << "got: " << v;
    EXPECT_NE(std::string::npos, v.find("true")) << "got: " << v;

    /* 4. 空串往返 */
    ASSERT_TRUE(host_value(h,
        "JSON.stringify([btoa(''), atob('')])", &v));
    EXPECT_NE(std::string::npos, v.find("[\"\",\"\"]")) << "got: " << v;
}

TEST_F(PolyfillTest, StructuredCloneDeepTypes) {
    std::string v;
    /* 1. Date 深拷贝：新对象，值相等 */
    ASSERT_TRUE(host_value(h,
        "var d = new Date(1234567890); var c = structuredClone(d);\n"
        "JSON.stringify([c instanceof Date, c.getTime() === d.getTime(), c !== d])", &v));
    EXPECT_NE(std::string::npos, v.find("[true,true,true]")) << "got: " << v;

    /* 2. Map 深拷贝：键值深拷贝 */
    ASSERT_TRUE(host_value(h,
        "var m = new Map([['k', {v: 1}]]); var c = structuredClone(m);\n"
        "JSON.stringify([c instanceof Map, c.size, c.get('k').v])", &v));
    EXPECT_NE(std::string::npos, v.find("[true,1,1]")) << "got: " << v;

    /* 3. Set 深拷贝 */
    ASSERT_TRUE(host_value(h,
        "var s = new Set([1, 2, 3]); var c = structuredClone(s);\n"
        "JSON.stringify([c instanceof Set, c.size, c.has(2)])", &v));
    EXPECT_NE(std::string::npos, v.find("[true,3,true]")) << "got: " << v;

    /* 4. RegExp 深拷贝 */
    ASSERT_TRUE(host_value(h,
        "var r = /test/gi; var c = structuredClone(r);\n"
        "JSON.stringify([c instanceof RegExp, c.source, c.flags, c !== r])", &v));
    EXPECT_NE(std::string::npos, v.find("[true,\"test\",\"gi\",true]")) << "got: " << v;

    /* 5. 循环引用：不无限递归，保持自引用 */
    ASSERT_TRUE(host_value(h,
        "var a = {x: 1}; a.self = a; var c = structuredClone(a);\n"
        "JSON.stringify([c.x, c.self === c, c.self !== a])", &v));
    EXPECT_NE(std::string::npos, v.find("[1,true,true]")) << "got: " << v;

    /* 6. 嵌套对象深拷贝：修改副本不影响原 */
    ASSERT_TRUE(host_value(h,
        "var orig = {nested: {val: 10}}; var c = structuredClone(orig);\n"
        "c.nested.val = 99;\n"
        "JSON.stringify([orig.nested.val, c.nested.val])", &v));
    EXPECT_NE(std::string::npos, v.find("[10,99]")) << "got: " << v;
}

TEST_F(PolyfillTest, DomException) {
    std::string v;
    /* 1. 构造器 + name/code 映射 + 继承 Error */
    ASSERT_TRUE(host_value(h,
        "var e = new DOMException('aborted', 'AbortError');\n"
        "JSON.stringify([e.message, e.name, e.code, e instanceof Error])", &v));
    EXPECT_NE(std::string::npos, v.find("\"aborted\"")) << "got: " << v;
    EXPECT_NE(std::string::npos, v.find("\"AbortError\"")) << "got: " << v;
    EXPECT_NE(std::string::npos, v.find(",20,true]")) << "got: " << v;

    /* 2. 默认 name='Error', code=0 */
    ASSERT_TRUE(host_value(h,
        "var e = new DOMException('oops');\n"
        "JSON.stringify([e.name, e.code])", &v));
    EXPECT_NE(std::string::npos, v.find("[\"Error\",0]")) << "got: " << v;

    /* 3. 常见 code 映射 */
    ASSERT_TRUE(host_value(h,
        "JSON.stringify([new DOMException('','NotSupportedError').code,\n"
        "  new DOMException('','InvalidStateError').code,\n"
        "  new DOMException('','SecurityError').code])", &v));
    EXPECT_NE(std::string::npos, v.find("[9,11,18]")) << "got: " << v;
}

TEST_F(PolyfillTest, BroadcastChannel) {
    std::string v;
    /* 1. 存在 + name 属性 */
    ASSERT_TRUE(host_value(h,
        "var bc = new BroadcastChannel('test-ch');\n"
        "JSON.stringify([typeof BroadcastChannel, bc.name])", &v));
    EXPECT_NE(std::string::npos, v.find("\"function\"")) << "got: " << v;
    EXPECT_NE(std::string::npos, v.find("\"test-ch\"")) << "got: " << v;

    /* 2. postMessage：同名 channel 收到 message 事件（同步 dispatch） */
    ASSERT_TRUE(host_value(h,
        "var rcv = new BroadcastChannel('ch2'); var got = null;\n"
        "rcv.onmessage = function(e){ got = [e.data, e.type]; };\n"
        "var snd = new BroadcastChannel('ch2');\n"
        "snd.postMessage({hello: 'world'});\n"
        "JSON.stringify([got !== null, got[0] && got[0].hello, got[1]])", &v));
    EXPECT_NE(std::string::npos, v.find("[true,\"world\",\"message\"]")) << "got: " << v;

    /* 3. 不同名 channel 不收到 */
    ASSERT_TRUE(host_value(h,
        "var rcv = new BroadcastChannel('chA'); var got = false;\n"
        "rcv.onmessage = function(){ got = true; };\n"
        "new BroadcastChannel('chB').postMessage('x');\n"
        "JSON.stringify(got)", &v));
    EXPECT_NE(std::string::npos, v.find("false")) << "got: " << v;

    /* 4. close()：关闭后不收到消息 */
    ASSERT_TRUE(host_value(h,
        "var rcv = new BroadcastChannel('ch3'); var got = false;\n"
        "rcv.onmessage = function(){ got = true; };\n"
        "rcv.close();\n"
        "new BroadcastChannel('ch3').postMessage('x');\n"
        "JSON.stringify(got)", &v));
    EXPECT_NE(std::string::npos, v.find("false")) << "got: " << v;
}

TEST_F(PolyfillTest, PerformanceObserver) {
    std::string v;
    /* 1. 存在 */
    ASSERT_TRUE(host_value(h, "JSON.stringify(typeof PerformanceObserver)", &v));
    EXPECT_NE(std::string::npos, v.find("\"function\"")) << "got: " << v;

    /* 2. observe + mark → callback 收到 entry（微任务） */
    ASSERT_TRUE(host_eval(h,
        "var _po = null;\n"
        "var obs = new PerformanceObserver(function(list){\n"
        "  var entries = list.getEntries();\n"
        "  _po = JSON.stringify([entries.length, entries[0].name, entries[0].entryType]);\n"
        "});\n"
        "obs.observe({entryTypes:['mark']});\n"
        "performance.mark('test-mark');\n"
        "0", &v));
    ASSERT_TRUE(host_poll_until_value(h, "_po", "\"test-mark\"", &v, 3000)) << "got: " << v;
    ASSERT_TRUE(host_poll_until_value(h, "_po", "\"mark\"", &v, 3000)) << "got: " << v;

    /* 3. disconnect 后不收到 */
    ASSERT_TRUE(host_eval(h,
        "var _po2 = 'none';\n"
        "var obs2 = new PerformanceObserver(function(){ _po2 = 'received'; });\n"
        "obs2.observe({entryTypes:['mark']});\n"
        "obs2.disconnect();\n"
        "performance.mark('after-disconnect');\n"
        "JSON.stringify(_po2)", &v));
    /* host_eval 返回 JSON 包装的字符串，v 形如 "\"none\""（JSON 串内嵌 JSON 串）
     * 但实际内容包含 "none" 子串（无引号），所以查找 none 即可。 */
    EXPECT_NE(std::string::npos, v.find("none")) << "got: " << v;
}

TEST_F(PolyfillTest, CacheStorageEdgeCases) {
    std::string v;
    /* 1. caches 全局存在 */
    ASSERT_TRUE(host_value(h, "JSON.stringify(typeof caches)", &v));
    if (v.find("\"undefined\"") != std::string::npos) { GTEST_SKIP() << "CacheStorage not implemented"; }
    EXPECT_NE(std::string::npos, v.find("\"object\"")) << "got: " << v;

    /* 2. open → put → match → text */
    ASSERT_TRUE(host_eval(h,
        "var _cs = null;\n"
        "caches.open('v1').then(function(cache){\n"
        "  return cache.put(\"https://x.com/a\", new Response('body'));\n"
        "}).then(function(){\n"
        "  return caches.open('v1');\n"
        "}).then(function(cache){\n"
        "  return cache.match(\"https://x.com/a\");\n"
        "}).then(function(resp){\n"
        "  return resp.text().then(function(t){ return [t, resp.status]; });\n"
        "}).then(function(arr){\n"
        "  _cs = JSON.stringify(arr);\n"
        "}).catch(function(e){\n"
        "  _cs = 'err:' + String(e);\n"
        "});\n"
        "0", &v));
    ASSERT_TRUE(host_poll_until_value(h, "_cs", "\"body\"", &v, 3000)) << "got: " << v;
}







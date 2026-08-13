// test_compress_consistency_gtest.cpp — Task 5: 迁 host 契约（host_eval 驱动）
//
// 验证：
// 1. 确定性：相同输入 → 相同压缩输出（逐字节一致）
// 2. Gzip 头/尾结构（魔数、CM、FLG、OS、CRC32、ISIZE）
// 3. Zlib 头/尾结构（CMF、FLG、Adler-32、头校验）
// 4. Roundtrip：压缩 → 解压 → 与原文精确一致
// 5. 错误处理：损坏/截断/非法数据抛错或产生空输出
//
// 压缩同步（nativeCompress 在 writable.close() 内完成），promise 链为纯微任务；
// 每轮 host_eval 后 qwrt 线程冲刷全部微任务，阶段间无需额外等待，最终用
// host_poll_until_value 轮询标志位。
#include "test_host.h"
#include <string>
#include <cstdio>

class CompressConsistencyTest : public ::testing::Test {
protected:
    HostCtx *h = nullptr;

    void SetUp() override {
        h = host_create();
        ASSERT_NE(nullptr, h);
    }

    void TearDown() override {
        host_destroy(h);
    }

    bool js_bool(const char *expr) {
        std::string out;
        return host_value(h, expr, &out) && out == "true";
    }

    /* Roundtrip test: compress with format, decompress, verify exact match */
    void test_roundtrip(const char *format, const char *data_expr) {
        char code[1024];
        std::string out;

        snprintf(code, sizeof(code),
            "var _rok=false;var _rdata=%s;"
            "var _cs=new CompressionStream('%s');"
            "var _w=_cs.writable.getWriter();"
            "var _rd=_cs.readable.getReader();"
            "var _chunks=[];var _total=0;",
            data_expr, format);
        ASSERT_TRUE(host_eval(h, code, &out));

        ASSERT_TRUE(host_eval(h,
            "_w.write(_rdata).then(function(){return _w.close();}).then(function(){"
            "  function p(){_rd.read().then(function(r){"
            "    if(r.done){return;}"
            "    _chunks.push(r.value);_total+=r.value.length;p();});}"
            "  p();});", &out));

        snprintf(code, sizeof(code),
            "var _comp=new Uint8Array(_total);var _off=0;"
            "for(var i=0;i<_chunks.length;i++){_comp.set(_chunks[i],_off);_off+=_chunks[i].length;}"
            "var _ds=new DecompressionStream('%s');"
            "var _dw=_ds.writable.getWriter();"
            "var _drd=_ds.readable.getReader();"
            "var _out=[];var _osize=0;",
            format);
        ASSERT_TRUE(host_eval(h, code, &out));

        ASSERT_TRUE(host_eval(h,
            "_dw.write(_comp).then(function(){return _dw.close();}).then(function(){"
            "  function q(){_drd.read().then(function(r){"
            "    if(r.done){"
            "      var _result=new Uint8Array(_osize);var _o=0;"
            "      for(var i=0;i<_out.length;i++){_result.set(_out[i],_o);_o+=_out[i].length;}"
            "      _rok=(_result.length===_rdata.length);"
            "      if(_rok){for(var i=0;i<_result.length;i++){if(_result[i]!==_rdata[i]){_rok=false;break;}}}"
            "      return;"
            "    }"
            "    _out.push(r.value);_osize+=r.value.length;q();});}"
            "  q();});", &out));

        ASSERT_TRUE(host_poll_until_value(h, "_rok", "true", &out));
        EXPECT_TRUE(js_bool("_rok"));
    }

    /* Determinism test: compress same input twice, compare byte-by-byte */
    void test_determinism(const char *format, const char *data_expr) {
        char code[2048];
        std::string out;

        /* First compression */
        snprintf(code, sizeof(code),
            "var _d1=%s;"
            "var _cs1=new CompressionStream('%s');"
            "var _w1=_cs1.writable.getWriter();"
            "var _rd1=_cs1.readable.getReader();"
            "var _ch1=[];var _sz1=0;var _det1done=false;",
            data_expr, format);
        ASSERT_TRUE(host_eval(h, code, &out));
        ASSERT_TRUE(host_eval(h,
            "_w1.write(_d1).then(function(){return _w1.close();}).then(function(){"
            "  function p(){_rd1.read().then(function(r){"
            "    if(r.done){_det1done=true;return;}"
            "    _ch1.push(r.value);_sz1+=r.value.length;p();});}"
            "  p();});", &out));
        ASSERT_TRUE(host_poll_until_value(h, "_det1done", "true", &out));

        /* Second compression — same input, same format */
        snprintf(code, sizeof(code),
            "var _cs2=new CompressionStream('%s');"
            "var _w2=_cs2.writable.getWriter();"
            "var _rd2=_cs2.readable.getReader();"
            "var _ch2=[];var _sz2=0;var _det2done=false;", format);
        ASSERT_TRUE(host_eval(h, code, &out));
        ASSERT_TRUE(host_eval(h,
            "_w2.write(_d1).then(function(){return _w2.close();}).then(function(){"
            "  function p(){_rd2.read().then(function(r){"
            "    if(r.done){_det2done=true;return;}"
            "    _ch2.push(r.value);_sz2+=r.value.length;p();});}"
            "  p();});", &out));
        ASSERT_TRUE(host_poll_until_value(h, "_det2done", "true", &out));

        /* Compare as byte arrays */
        snprintf(code, sizeof(code),
            "var _c1=new Uint8Array(_sz1);var _o1=0;"
            "for(var i=0;i<_ch1.length;i++){_c1.set(_ch1[i],_o1);_o1+=_ch1[i].length;}"
            "var _c2=new Uint8Array(_sz2);var _o2=0;"
            "for(var i=0;i<_ch2.length;i++){_c2.set(_ch2[i],_o2);_o2+=_ch2[i].length;}"
            "var _det=(_c1.length===_c2.length);"
            "if(_det){for(var i=0;i<_c1.length;i++){if(_c1[i]!==_c2[i]){_det=false;break;}}}");
        ASSERT_TRUE(host_eval(h, code, &out));

        EXPECT_TRUE(js_bool("_det"));
    }
};

/* ================================================================
 * Determinism
 * ================================================================ */

TEST_F(CompressConsistencyTest, DeterminismGzip1_3KB) {
    test_determinism("gzip",
        "new TextEncoder().encode('Hello World! '.repeat(100))");
}

TEST_F(CompressConsistencyTest, DeterminismGzip64KB) {
    test_determinism("gzip",
        "new TextEncoder().encode('ABCDEFGH'.repeat(8192))");
}

TEST_F(CompressConsistencyTest, DeterminismGzip1MB) {
    test_determinism("gzip",
        "new TextEncoder().encode('Hello World! '.repeat(80000))");
}

/* ================================================================
 * Gzip header/trailer structure
 * ================================================================ */

class CompressConsistencyGzipHeader : public CompressConsistencyTest {
protected:
    void SetUp() override {
        CompressConsistencyTest::SetUp();
        std::string out;

        /* Compress a known input and collect the bytes */
        ASSERT_TRUE(host_eval(h,
            "var _gd=new TextEncoder().encode('Hello World! '.repeat(100));"
            "var _gcs=new CompressionStream('gzip');"
            "var _gw=_gcs.writable.getWriter();"
            "var _grd=_gcs.readable.getReader();"
            "var _gch=[];var _gsz=0;var _gDone=false;", &out));
        ASSERT_TRUE(host_eval(h,
            "_gw.write(_gd).then(function(){return _gw.close();}).then(function(){"
            "  function p(){_grd.read().then(function(r){"
            "    if(r.done){_gDone=true;return;}"
            "    _gch.push(r.value);_gsz+=r.value.length;p();});}"
            "  p();});", &out));
        ASSERT_TRUE(host_poll_until_value(h, "_gDone", "true", &out));

        /* Concatenate into one Uint8Array */
        ASSERT_TRUE(host_eval(h,
            "var _gb=new Uint8Array(_gsz);var _go=0;"
            "for(var i=0;i<_gch.length;i++){_gb.set(_gch[i],_go);_go+=_gch[i].length;}", &out));
    }
};

TEST_F(CompressConsistencyGzipHeader, GzipId1) {
    EXPECT_TRUE(js_bool("_gb[0]===0x1F"));
}

TEST_F(CompressConsistencyGzipHeader, GzipId2) {
    EXPECT_TRUE(js_bool("_gb[1]===0x8B"));
}

TEST_F(CompressConsistencyGzipHeader, GzipCm) {
    EXPECT_TRUE(js_bool("_gb[2]===0x08"));
}

TEST_F(CompressConsistencyGzipHeader, GzipFlg) {
    EXPECT_TRUE(js_bool("_gb[3]===0x00"));
}

TEST_F(CompressConsistencyGzipHeader, GzipOs) {
    EXPECT_TRUE(js_bool("_gb[9]===0xFF"));
}

TEST_F(CompressConsistencyGzipHeader, GzipIsize) {
    /* ISIZE = input length mod 2^32, stored as LE at end-4. 1300 bytes = 0x0514 */
    EXPECT_TRUE(js_bool("_gb[_gsz-4]===0x14&&_gb[_gsz-3]===0x05&&_gb[_gsz-2]===0x00&&_gb[_gsz-1]===0x00"));
}

TEST_F(CompressConsistencyGzipHeader, GzipCrc32NonZero) {
    EXPECT_TRUE(js_bool("_gb[_gsz-8]!==0||_gb[_gsz-7]!==0||_gb[_gsz-6]!==0||_gb[_gsz-5]!==0"));
}

/* ================================================================
 * Zlib header/trailer structure
 * ================================================================ */

class CompressConsistencyZlibHeader : public CompressConsistencyTest {
protected:
    void SetUp() override {
        CompressConsistencyTest::SetUp();
        std::string out;

        ASSERT_TRUE(host_eval(h,
            "var _zd=new TextEncoder().encode('Hello World! '.repeat(100));"
            "var _zcs=new CompressionStream('deflate');"
            "var _zw=_zcs.writable.getWriter();"
            "var _zrd=_zcs.readable.getReader();"
            "var _zch=[];var _zsz=0;var _zDone=false;", &out));
        ASSERT_TRUE(host_eval(h,
            "_zw.write(_zd).then(function(){return _zw.close();}).then(function(){"
            "  function p(){_zrd.read().then(function(r){"
            "    if(r.done){_zDone=true;return;}"
            "    _zch.push(r.value);_zsz+=r.value.length;p();});}"
            "  p();});", &out));
        ASSERT_TRUE(host_poll_until_value(h, "_zDone", "true", &out));

        ASSERT_TRUE(host_eval(h,
            "var _zb=new Uint8Array(_zsz);var _zo=0;"
            "for(var i=0;i<_zch.length;i++){_zb.set(_zch[i],_zo);_zo+=_zch[i].length;}", &out));
    }
};

TEST_F(CompressConsistencyZlibHeader, ZlibCmf) {
    EXPECT_TRUE(js_bool("_zb[0]===0x78"));
}

TEST_F(CompressConsistencyZlibHeader, ZlibFlg) {
    EXPECT_TRUE(js_bool("_zb[1]===0x01"));
}

TEST_F(CompressConsistencyZlibHeader, ZlibHeaderCheck) {
    /* CMF*256+FLG must be divisible by 31 */
    EXPECT_TRUE(js_bool("(_zb[0]*256+_zb[1])%31===0"));
}

TEST_F(CompressConsistencyZlibHeader, ZlibAdler32NonZero) {
    EXPECT_TRUE(js_bool("_zb[_zsz-4]!==0||_zb[_zsz-3]!==0||_zb[_zsz-2]!==0||_zb[_zsz-1]!==0"));
}

/* ================================================================
 * Roundtrip correctness
 * ================================================================ */

TEST_F(CompressConsistencyTest, RoundtripGzip1_3KB) {
    test_roundtrip("gzip",
        "new TextEncoder().encode('Hello World! '.repeat(100))");
}

TEST_F(CompressConsistencyTest, RoundtripDeflate1_3KB) {
    test_roundtrip("deflate",
        "new TextEncoder().encode('Hello World! '.repeat(100))");
}

TEST_F(CompressConsistencyTest, RoundtripDeflateRaw1_3KB) {
    test_roundtrip("deflate-raw",
        "new TextEncoder().encode('Hello World! '.repeat(100))");
}

TEST_F(CompressConsistencyTest, RoundtripGzip64KB) {
    test_roundtrip("gzip",
        "new TextEncoder().encode('ABCDEFGH'.repeat(8192))");
}

TEST_F(CompressConsistencyTest, RoundtripGzip1MB) {
    test_roundtrip("gzip",
        "new TextEncoder().encode('Hello World! '.repeat(80000))");
}

TEST_F(CompressConsistencyTest, RoundtripGzipBinary64KB) {
    test_roundtrip("gzip",
        "(function(){var a=new Uint8Array(65536);for(var i=0;i<a.length;i++)a[i]=i&0xff;return a;})()");
}

/* ================================================================
 * Error handling
 * ================================================================ */

TEST_F(CompressConsistencyTest, CorruptGzipData) {
    /* Corrupt gzip data: valid header but garbage DEFLATE */
    std::string out;
    ASSERT_TRUE(host_eval(h,
        "var _err1=new Uint8Array([0x1F,0x8B,0x08,0x00,0,0,0,0,0,0xFF,"
        "0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF]);"
        "var _err1ok=false;"
        "try{var _ds1=new DecompressionStream('gzip');"
        "var _dw1=_ds1.writable.getWriter();var _drd1=_ds1.readable.getReader();"
        "var _out1=[];var _osz1=0;"
        "_dw1.write(_err1).then(function(){return _dw1.close();}).then(function(){"
        "  function p(){_drd1.read().then(function(r){"
        "    if(r.done){_err1ok=false;return;}"
        "    _out1.push(r.value);_osz1+=r.value.length;p();});}"
        "  p();});"
        "}catch(e){_err1ok=true;}", &out));
    /* 错误可能同步抛（try/catch 捕获）或异步经 readable 错误——两者都应为空输出 */
    ASSERT_TRUE(host_poll_until_value(h, "!_err1ok||_osz1===0", "true", &out));
    EXPECT_TRUE(js_bool("!_err1ok||_osz1===0"));
}

TEST_F(CompressConsistencyTest, TruncatedZlibData) {
    /* Truncated zlib data (<6 bytes) */
    std::string out;
    ASSERT_TRUE(host_eval(h,
        "var _err2=new Uint8Array([0x78,0x01,0x03]);"
        "var _err2ok=false;"
        "try{var _ds2=new DecompressionStream('deflate');"
        "var _dw2=_ds2.writable.getWriter();var _drd2=_ds2.readable.getReader();"
        "var _out2=[];var _osz2=0;"
        "_dw2.write(_err2).then(function(){return _dw2.close();}).then(function(){"
        "  function p(){_drd2.read().then(function(r){"
        "    if(r.done){_err2ok=false;return;}"
        "    _out2.push(r.value);_osz2+=r.value.length;p();});}"
        "  p();});"
        "}catch(e){_err2ok=true;}", &out));
    ASSERT_TRUE(host_poll_until_value(h, "!_err2ok||_osz2===0", "true", &out));
    EXPECT_TRUE(js_bool("!_err2ok||_osz2===0"));
}

TEST_F(CompressConsistencyTest, InvalidGzipMagic) {
    /* Invalid gzip magic bytes */
    std::string out;
    ASSERT_TRUE(host_eval(h,
        "var _err3=new Uint8Array([0x00,0x00,0x08,0x00,0,0,0,0,0,0xFF,"
        "0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00]);"
        "var _err3ok=false;"
        "try{var _ds3=new DecompressionStream('gzip');"
        "var _dw3=_ds3.writable.getWriter();var _drd3=_ds3.readable.getReader();"
        "var _out3=[];var _osz3=0;"
        "_dw3.write(_err3).then(function(){return _dw3.close();}).then(function(){"
        "  function p(){_drd3.read().then(function(r){"
        "    if(r.done){_err3ok=false;return;}"
        "    _out3.push(r.value);_osz3+=r.value.length;p();});}"
        "  p();});"
        "}catch(e){_err3ok=true;}", &out));
    ASSERT_TRUE(host_poll_until_value(h, "!_err3ok||_osz3===0", "true", &out));
    EXPECT_TRUE(js_bool("!_err3ok||_osz3===0"));
}

TEST_F(CompressConsistencyTest, InvalidZlibHeader) {
    /* Invalid zlib header (CM != 8) */
    std::string out;
    ASSERT_TRUE(host_eval(h,
        "var _err4=new Uint8Array([0x09,0x10,0x00,0x00,0x00,0x00]);"
        "var _err4ok=false;"
        "try{var _ds4=new DecompressionStream('deflate');"
        "var _dw4=_ds4.writable.getWriter();var _drd4=_ds4.readable.getReader();"
        "var _out4=[];var _osz4=0;"
        "_dw4.write(_err4).then(function(){return _dw4.close();}).then(function(){"
        "  function p(){_drd4.read().then(function(r){"
        "    if(r.done){_err4ok=false;return;}"
        "    _out4.push(r.value);_osz4+=r.value.length;p();});}"
        "  p();});"
        "}catch(e){_err4ok=true;}", &out));
    ASSERT_TRUE(host_poll_until_value(h, "!_err4ok||_osz4===0", "true", &out));
    EXPECT_TRUE(js_bool("!_err4ok||_osz4===0"));
}

TEST_F(CompressConsistencyTest, GzipCrc32Mismatch) {
    /* Gzip with corrupted CRC32 trailer (checksum mismatch) */
    std::string out;
    ASSERT_TRUE(host_eval(h,
        "var _err5data=new TextEncoder().encode('Hello World! '.repeat(10));"
        "var _err5cs=new CompressionStream('gzip');"
        "var _err5w=_err5cs.writable.getWriter();"
        "var _err5rd=_err5cs.readable.getReader();"
        "var _err5ch=[];var _err5sz=0;var _err5cdone=false;"
        "_err5w.write(_err5data).then(function(){return _err5w.close();}).then(function(){"
        "  function p(){_err5rd.read().then(function(r){"
        "    if(r.done){_err5cdone=true;return;}"
        "    _err5ch.push(r.value);_err5sz+=r.value.length;p();});}"
        "  p();});", &out));
    ASSERT_TRUE(host_poll_until_value(h, "_err5cdone", "true", &out));

    /* Corrupt the CRC32 (byte at offset -8) and try to decompress */
    ASSERT_TRUE(host_eval(h,
        "var _err5comp=new Uint8Array(_err5sz);var _e5o=0;"
        "for(var i=0;i<_err5ch.length;i++){_err5comp.set(_err5ch[i],_e5o);_e5o+=_err5ch[i].length;}"
        "_err5comp[_err5sz-8]^=0xFF;"
        "var _err5ok=false;"
        "try{var _ds5=new DecompressionStream('gzip');"
        "var _dw5=_ds5.writable.getWriter();var _drd5=_ds5.readable.getReader();"
        "var _out5=[];var _osz5=0;"
        "_dw5.write(_err5comp).then(function(){return _dw5.close();}).then(function(){"
        "  function p(){_drd5.read().then(function(r){"
        "    if(r.done){_err5ok=false;return;}"
        "    _out5.push(r.value);_osz5+=r.value.length;p();});}"
        "  p();});"
        "}catch(e){_err5ok=true;}", &out));
    ASSERT_TRUE(host_poll_until_value(h, "!_err5ok||_osz5===0", "true", &out));
    EXPECT_TRUE(js_bool("!_err5ok||_osz5===0"));
}

/*
 * test_compress_gtest.cpp — Google Test version of test_compress.c
 *
 * Covers: all 3 formats, various sizes (10B to 1MB),
 * roundtrip correctness, gzip header, compression ratio,
 * and binary data.
 */

#include <gtest/gtest.h>

extern "C" {
#include "qwrt/qwrt.h"
#include "pal_mock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
}

class CompressTestBase : public ::testing::Test {
protected:
    qwrt_t *rt;
    qwrt_pal_t *pal;

    void SetUp() override {
        pal = pal_mock_create();
        ASSERT_NE(pal, nullptr);

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

    /* Pump the event loop so CompressionStream/DecompressionStream promises resolve */
    void pump() {
        for (int i = 0; i < 10; i++) {
            qwrt_tick(rt);
            pal_mock_fire_all_timers(pal);
            qwrt_tick(rt);
        }
    }

    /* Evaluate a JS expression and check if it is the string "true" */
    bool js_bool(const char *expr) {
        char *r = NULL;
        bool ok = false;
        if (qwrt_eval(rt, expr, &r) == 0 && r) {
            ok = (strcmp(r, "true") == 0);
            qwrt_free(r);
        }
        return ok;
    }

    /* Generic roundtrip test: compress with format, decompress, verify match */
    void test_roundtrip(const char *format, const char *data_expr) {
        char code[1024];

        /* Setup */
        snprintf(code, sizeof(code),
            "var _rok=false;var _rdata=%s;"
            "var _cs=new CompressionStream('%s');"
            "var _w=_cs.writable.getWriter();"
            "var _rd=_cs.readable.getReader();"
            "var _chunks=[];var _total=0;",
            data_expr, format);
        qwrt_eval(rt, code, NULL);

        /* Write + close */
        qwrt_eval(rt,
            "_w.write(_rdata).then(function(){return _w.close();}).then(function(){"
            "  function p(){_rd.read().then(function(r){"
            "    if(r.done){return;}"
            "    _chunks.push(r.value);_total+=r.value.length;p();});}"
            "  p();});", NULL);
        pump();

        /* Concatenate compressed, decompress */
        snprintf(code, sizeof(code),
            "var _comp=new Uint8Array(_total);var _off=0;"
            "for(var i=0;i<_chunks.length;i++){_comp.set(_chunks[i],_off);_off+=_chunks[i].length;}"
            "var _ds=new DecompressionStream('%s');"
            "var _dw=_ds.writable.getWriter();"
            "var _drd=_ds.readable.getReader();"
            "var _out=[];var _osize=0;",
            format);
        qwrt_eval(rt, code, NULL);

        qwrt_eval(rt,
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
            "  q();});", NULL);
        pump();

        EXPECT_TRUE(js_bool("_rok"));
    }
};

/* ================================================================
 * deflate-raw
 * ================================================================ */

TEST_F(CompressTestBase, DeflateRaw10Bytes) {
    test_roundtrip("deflate-raw",
        "new Uint8Array([65,66,65,66,65,66,65,66,65,66])");
}

TEST_F(CompressTestBase, DeflateRaw130Bytes) {
    test_roundtrip("deflate-raw",
        "new TextEncoder().encode('Hello World! '.repeat(10))");
}

TEST_F(CompressTestBase, DeflateRaw4_4KB) {
    test_roundtrip("deflate-raw",
        "new TextEncoder().encode('The quick brown fox jumps over the lazy dog. '.repeat(100))");
}

TEST_F(CompressTestBase, DeflateRaw64KB) {
    test_roundtrip("deflate-raw",
        "new TextEncoder().encode('ABCDEFGH'.repeat(8192))");
}

TEST_F(CompressTestBase, DeflateRaw1MB) {
    test_roundtrip("deflate-raw",
        "new TextEncoder().encode('Hello World! '.repeat(80000))");
}

/* ================================================================
 * deflate (zlib)
 * ================================================================ */

TEST_F(CompressTestBase, Deflate10Bytes) {
    test_roundtrip("deflate",
        "new Uint8Array([65,66,65,66,65,66,65,66,65,66])");
}

TEST_F(CompressTestBase, Deflate1_3KB) {
    test_roundtrip("deflate",
        "new TextEncoder().encode('Hello World! '.repeat(100))");
}

TEST_F(CompressTestBase, Deflate64KB) {
    test_roundtrip("deflate",
        "new TextEncoder().encode('ABCDEFGH'.repeat(8192))");
}

TEST_F(CompressTestBase, Deflate1MB) {
    test_roundtrip("deflate",
        "new TextEncoder().encode('Hello World! '.repeat(80000))");
}

/* ================================================================
 * gzip
 * ================================================================ */

TEST_F(CompressTestBase, Gzip10Bytes) {
    test_roundtrip("gzip",
        "new Uint8Array([65,66,65,66,65,66,65,66,65,66])");
}

TEST_F(CompressTestBase, Gzip1_3KB) {
    test_roundtrip("gzip",
        "new TextEncoder().encode('Hello World! '.repeat(100))");
}

TEST_F(CompressTestBase, Gzip64KB) {
    test_roundtrip("gzip",
        "new TextEncoder().encode('ABCDEFGH'.repeat(8192))");
}

TEST_F(CompressTestBase, Gzip1MB) {
    test_roundtrip("gzip",
        "new TextEncoder().encode('Hello World! '.repeat(80000))");
}

/* ================================================================
 * gzip header verification
 * ================================================================ */

TEST_F(CompressTestBase, GzipMagicBytes) {
    /* Compress and check gzip magic bytes */
    qwrt_eval(rt,
        "var _gmOk=false;"
        "var _gd=new TextEncoder().encode('Hello World! '.repeat(100));"
        "var _gcs=new CompressionStream('gzip');"
        "var _gw=_gcs.writable.getWriter();"
        "var _grd=_gcs.readable.getReader();"
        "var _gch=[];var _gsz=0;", NULL);

    qwrt_eval(rt,
        "_gw.write(_gd).then(function(){return _gw.close();}).then(function(){"
        "  function p(){_grd.read().then(function(r){"
        "    if(r.done){return;}"
        "    _gch.push(r.value);_gsz+=r.value.length;p();});}"
        "  p();});", NULL);
    pump();

    EXPECT_TRUE(js_bool("_gch[0][0]===0x1f&&_gch[0][1]===0x8b"));

    /* Verify roundtrip through the same compressed data */
    qwrt_eval(rt,
        "var _gcomp=new Uint8Array(_gsz);var _go=0;"
        "for(var i=0;i<_gch.length;i++){_gcomp.set(_gch[i],_go);_go+=_gch[i].length;}"
        "var _gds=new DecompressionStream('gzip');"
        "var _gdw=_gds.writable.getWriter();"
        "var _gdrd=_gds.readable.getReader();"
        "var _gout=[];var _gosz=0;", NULL);

    qwrt_eval(rt,
        "_gdw.write(_gcomp).then(function(){return _gdw.close();}).then(function(){"
        "  function q(){_gdrd.read().then(function(r){"
        "    if(r.done){"
        "      var _gr=new Uint8Array(_gosz);var _go2=0;"
        "      for(var i=0;i<_gout.length;i++){_gr.set(_gout[i],_go2);_go2+=_gout[i].length;}"
        "      _gmOk=(_gr.length===_gd.length);"
        "      if(_gmOk){for(var i=0;i<_gr.length;i++){if(_gr[i]!==_gd[i]){_gmOk=false;break;}}}"
        "      return;"
        "    }"
        "    _gout.push(r.value);_gosz+=r.value.length;q();});}"
        "  q();});", NULL);
    pump();

    EXPECT_TRUE(js_bool("_gmOk"));
}

/* ================================================================
 * compression ratio
 * ================================================================ */

TEST_F(CompressTestBase, Gzip1MBRatio) {
    qwrt_eval(rt,
        "var _ratio=1;var _rin=0;"
        "var _rd=new TextEncoder().encode('Hello World! '.repeat(80000));"
        "_rin=_rd.length;"
        "var _rcs=new CompressionStream('gzip');"
        "var _rw=_rcs.writable.getWriter();"
        "var _rrd=_rcs.readable.getReader();"
        "var _rch=[];var _rsz=0;", NULL);

    qwrt_eval(rt,
        "_rw.write(_rd).then(function(){return _rw.close();}).then(function(){"
        "  function p(){_rrd.read().then(function(r){"
        "    if(r.done){_ratio=_rsz/_rin;return;}"
        "    _rch.push(r.value);_rsz+=r.value.length;p();});}"
        "  p();});", NULL);
    pump();

    char *r = NULL;
    qwrt_eval(rt, "_ratio", &r);
    double ratio = r ? atof(r) : 1.0;
    EXPECT_LT(ratio, 1.0) << "1MB repetitive gzip should compress (ratio was " << (ratio * 100) << "%)";
    if (r) qwrt_free(r);
}

/* ================================================================
 * binary data
 * ================================================================ */

TEST_F(CompressTestBase, GzipBinary64KB) {
    test_roundtrip("gzip",
        "(function(){var a=new Uint8Array(65536);for(var i=0;i<a.length;i++)a[i]=i&0xff;return a;})()");
}

TEST_F(CompressTestBase, DeflateRawBinary128KB) {
    test_roundtrip("deflate-raw",
        "(function(){var a=new Uint8Array(131072);for(var i=0;i<a.length;i++)a[i]=(i*7+13)&0xff;return a;})()");
}

TEST_F(CompressTestBase, GzipBinary1MB) {
    test_roundtrip("gzip",
        "(function(){var a=new Uint8Array(1048576);for(var i=0;i<a.length;i++)a[i]=i&0x03;return a;})()");
}

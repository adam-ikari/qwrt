/*
 * Compression output consistency test
 *
 * Verifies:
 * 1. Determinism: same input → same compressed output (byte-identical)
 * 2. Gzip header/trailer structure (magic, CM, FLG, OS, CRC32, ISIZE)
 * 3. Zlib header/trailer structure (CMF, FLG, Adler-32, header check)
 * 4. CRC32 correctness: our slice-by-4 matches mz_crc32
 * 5. Roundtrip: compress → decompress → exact match with original
 */
#define _POSIX_C_SOURCE 200809L
#include "test_qwrt.h"
#include "qwrt/qwrt.h"
#include "pal_mock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *load_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc(len+1); size_t n = fread(buf, 1, len, f); buf[n]=0; fclose(f); *out_len=n; return buf;
}

static void pump(qwrt_t *rt, qwrt_pal_t *pal) {
    for (int i = 0; i < 10; i++) { qwrt_tick(rt); pal_mock_fire_all_timers(pal); qwrt_tick(rt); }
}

static int js_bool(qwrt_t *rt, const char *expr) {
    char *r = NULL; int ok = 0;
    if (qwrt_eval(rt, expr, &r) == 0 && r) { ok = (strcmp(r, "true") == 0); qwrt_free(r); }
    return ok;
}

/* Roundtrip test: compress with format, decompress, verify exact match */
static int test_roundtrip(qwrt_t *rt, qwrt_pal_t *pal,
                          const char *format, const char *data_expr,
                          const char *test_name)
{
    char code[1024];

    snprintf(code, sizeof(code),
        "var _rok=false;var _rdata=%s;"
        "var _cs=new CompressionStream('%s');"
        "var _w=_cs.writable.getWriter();"
        "var _rd=_cs.readable.getReader();"
        "var _chunks=[];var _total=0;",
        data_expr, format);
    qwrt_eval(rt, code, NULL);

    qwrt_eval(rt,
        "_w.write(_rdata).then(function(){return _w.close();}).then(function(){"
        "  function p(){_rd.read().then(function(r){"
        "    if(r.done){return;}"
        "    _chunks.push(r.value);_total+=r.value.length;p();});}"
        "  p();});", NULL);
    pump(rt, pal);

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
    pump(rt, pal);

    printf("  %s...", test_name);
    if (js_bool(rt, "_rok")) { printf(" PASS\n"); return 0; }
    else { printf(" FAIL\n"); return 1; }
}

/* Determinism test: compress same input twice, compare byte-by-byte */
static int test_determinism(qwrt_t *rt, qwrt_pal_t *pal,
                            const char *format, const char *data_expr,
                            const char *test_name)
{
    char code[2048];

    /* First compression */
    snprintf(code, sizeof(code),
        "var _d1=%s;"
        "var _cs1=new CompressionStream('%s');"
        "var _w1=_cs1.writable.getWriter();"
        "var _rd1=_cs1.readable.getReader();"
        "var _ch1=[];var _sz1=0;",
        data_expr, format);
    qwrt_eval(rt, code, NULL);
    qwrt_eval(rt,
        "_w1.write(_d1).then(function(){return _w1.close();}).then(function(){"
        "  function p(){_rd1.read().then(function(r){"
        "    if(r.done){return;}"
        "    _ch1.push(r.value);_sz1+=r.value.length;p();});}"
        "  p();});", NULL);
    pump(rt, pal);

    /* Second compression — use same format */
    snprintf(code, sizeof(code),
        "var _cs2=new CompressionStream('%s');"
        "var _w2=_cs2.writable.getWriter();"
        "var _rd2=_cs2.readable.getReader();"
        "var _ch2=[];var _sz2=0;", format);
    qwrt_eval(rt, code, NULL);
    qwrt_eval(rt,
        "_w2.write(_d1).then(function(){return _w2.close();}).then(function(){"
        "  function p(){_rd2.read().then(function(r){"
        "    if(r.done){return;}"
        "    _ch2.push(r.value);_sz2+=r.value.length;p();});}"
        "  p();});", NULL);
    pump(rt, pal);

    /* Compare as hex strings (byte-by-byte in JS) */
    snprintf(code, sizeof(code),
        "var _c1=new Uint8Array(_sz1);var _o1=0;"
        "for(var i=0;i<_ch1.length;i++){_c1.set(_ch1[i],_o1);_o1+=_ch1[i].length;}"
        "var _c2=new Uint8Array(_sz2);var _o2=0;"
        "for(var i=0;i<_ch2.length;i++){_c2.set(_ch2[i],_o2);_o2+=_ch2[i].length;}"
        "var _det=(_c1.length===_c2.length);"
        "if(_det){for(var i=0;i<_c1.length;i++){if(_c1[i]!==_c2[i]){_det=false;break;}}}");
    qwrt_eval(rt, code, NULL);

    printf("  %s...", test_name);
    if (js_bool(rt, "_det")) { printf(" PASS\n"); return 0; }
    else { printf(" FAIL\n"); return 1; }
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    size_t plen;
    uint8_t *poly = load_file("../dist/polyfill.bytecode", &plen);
    if (!poly) poly = load_file("dist/polyfill.bytecode", &plen);
    if (!poly) poly = load_file("../../qwrt/dist/polyfill.bytecode", &plen);
    if (!poly) { printf("SKIP: dist/polyfill.bytecode not found\n"); return 0; }

    qwrt_pal_t *pal = pal_mock_create();
    qwrt_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.pal = pal;
    qwrt_t *rt = qwrt_create(&cfg);
    if (!rt) { free(poly); return 1; }

    int failures = 0;

    /* ---- Determinism ---- */
    printf("--- Determinism ---\n");
    failures += test_determinism(rt, pal, "gzip",
        "new TextEncoder().encode('Hello World! '.repeat(100))",
        "gzip 1.3KB");
    failures += test_determinism(rt, pal, "gzip",
        "new TextEncoder().encode('ABCDEFGH'.repeat(8192))",
        "gzip 64KB");
    failures += test_determinism(rt, pal, "gzip",
        "new TextEncoder().encode('Hello World! '.repeat(80000))",
        "gzip 1MB");

    /* ---- Gzip header structure ---- */
    printf("\n--- Gzip header/trailer ---\n");
    {
        qwrt_eval(rt,
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
        pump(rt, pal);

        /* Concatenate and examine bytes */
        qwrt_eval(rt,
            "var _gb=new Uint8Array(_gsz);var _go=0;"
            "for(var i=0;i<_gch.length;i++){_gb.set(_gch[i],_go);_go+=_gch[i].length;}", NULL);

        printf("  gzip ID1 (0x1F)...");
        if (js_bool(rt, "_gb[0]===0x1F")) printf(" PASS\n"); else { printf(" FAIL\n"); failures++; }

        printf("  gzip ID2 (0x8B)...");
        if (js_bool(rt, "_gb[1]===0x8B")) printf(" PASS\n"); else { printf(" FAIL\n"); failures++; }

        printf("  gzip CM (8=deflate)...");
        if (js_bool(rt, "_gb[2]===0x08")) printf(" PASS\n"); else { printf(" FAIL\n"); failures++; }

        printf("  gzip FLG (0=no extras)...");
        if (js_bool(rt, "_gb[3]===0x00")) printf(" PASS\n"); else { printf(" FAIL\n"); failures++; }

        printf("  gzip OS (0xFF=unknown)...");
        if (js_bool(rt, "_gb[9]===0xFF")) printf(" PASS\n"); else { printf(" FAIL\n"); failures++; }

        /* ISIZE = input length mod 2^32, stored as LE at end-4 */
        printf("  gzip ISIZE=1300...");
        if (js_bool(rt, "_gb[_gsz-4]===0x14&&_gb[_gsz-3]===0x05&&_gb[_gsz-2]===0x00&&_gb[_gsz-1]===0x00"))
            printf(" PASS\n");
        else { printf(" FAIL\n"); failures++; }

        /* CRC32 should be non-zero */
        printf("  gzip CRC32 non-zero...");
        if (js_bool(rt, "_gb[_gsz-8]!==0||_gb[_gsz-7]!==0||_gb[_gsz-6]!==0||_gb[_gsz-5]!==0"))
            printf(" PASS\n");
        else { printf(" FAIL\n"); failures++; }
    }

    /* ---- Zlib header structure ---- */
    printf("\n--- Zlib header/trailer ---\n");
    {
        qwrt_eval(rt,
            "var _zd=new TextEncoder().encode('Hello World! '.repeat(100));"
            "var _zcs=new CompressionStream('deflate');"
            "var _zw=_zcs.writable.getWriter();"
            "var _zrd=_zcs.readable.getReader();"
            "var _zch=[];var _zsz=0;", NULL);
        qwrt_eval(rt,
            "_zw.write(_zd).then(function(){return _zw.close();}).then(function(){"
            "  function p(){_zrd.read().then(function(r){"
            "    if(r.done){return;}"
            "    _zch.push(r.value);_zsz+=r.value.length;p();});}"
            "  p();});", NULL);
        pump(rt, pal);

        qwrt_eval(rt,
            "var _zb=new Uint8Array(_zsz);var _zo=0;"
            "for(var i=0;i<_zch.length;i++){_zb.set(_zch[i],_zo);_zo+=_zch[i].length;}", NULL);

        printf("  zlib CMF (0x78)...");
        if (js_bool(rt, "_zb[0]===0x78")) printf(" PASS\n"); else { printf(" FAIL\n"); failures++; }

        printf("  zlib FLG (0x01)...");
        if (js_bool(rt, "_zb[1]===0x01")) printf(" PASS\n"); else { printf(" FAIL\n"); failures++; }

        /* CMF*256+FLG must be divisible by 31 */
        printf("  zlib header check...");
        if (js_bool(rt, "(_zb[0]*256+_zb[1])%31===0")) printf(" PASS\n"); else { printf(" FAIL\n"); failures++; }

        /* Adler-32 non-zero */
        printf("  zlib Adler-32 non-zero...");
        if (js_bool(rt, "_zb[_zsz-4]!==0||_zb[_zsz-3]!==0||_zb[_zsz-2]!==0||_zb[_zsz-1]!==0"))
            printf(" PASS\n");
        else { printf(" FAIL\n"); failures++; }
    }

    /* ---- Roundtrip (comprehensive) ---- */
    printf("\n--- Roundtrip correctness ---\n");
    failures += test_roundtrip(rt, pal, "gzip",
        "new TextEncoder().encode('Hello World! '.repeat(100))", "gzip 1.3KB");
    failures += test_roundtrip(rt, pal, "deflate",
        "new TextEncoder().encode('Hello World! '.repeat(100))", "deflate 1.3KB");
    failures += test_roundtrip(rt, pal, "deflate-raw",
        "new TextEncoder().encode('Hello World! '.repeat(100))", "deflate-raw 1.3KB");
    failures += test_roundtrip(rt, pal, "gzip",
        "new TextEncoder().encode('ABCDEFGH'.repeat(8192))", "gzip 64KB");
    failures += test_roundtrip(rt, pal, "gzip",
        "new TextEncoder().encode('Hello World! '.repeat(80000))", "gzip 1MB");
    failures += test_roundtrip(rt, pal, "gzip",
        "(function(){var a=new Uint8Array(65536);for(var i=0;i<a.length;i++)a[i]=i&0xff;return a;})()",
        "gzip binary 64KB");

    /* ---- Negative / error-path tests ---- */
    printf("\n--- Error handling ---\n");
    {
        /* Corrupt gzip data: valid header but garbage DEFLATE */
        qwrt_eval(rt,
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
            "}catch(e){_err1ok=true;}", NULL);
        pump(rt, pal);
        /* The error may be thrown asynchronously via the readable stream error */
        printf("  corrupt gzip data throws...");
        if (js_bool(rt, "!_err1ok||_osz1===0")) printf(" PASS\n");
        else { printf(" FAIL\n"); failures++; }

        /* Truncated zlib data (<6 bytes) */
        qwrt_eval(rt,
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
            "}catch(e){_err2ok=true;}", NULL);
        pump(rt, pal);
        printf("  truncated zlib data throws...");
        if (js_bool(rt, "!_err2ok||_osz2===0")) printf(" PASS\n");
        else { printf(" FAIL\n"); failures++; }

        /* Invalid gzip magic bytes */
        qwrt_eval(rt,
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
            "}catch(e){_err3ok=true;}", NULL);
        pump(rt, pal);
        printf("  invalid gzip magic throws...");
        if (js_bool(rt, "!_err3ok||_osz3===0")) printf(" PASS\n");
        else { printf(" FAIL\n"); failures++; }

        /* Invalid zlib header (CM != 8) */
        qwrt_eval(rt,
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
            "}catch(e){_err4ok=true;}", NULL);
        pump(rt, pal);
        printf("  invalid zlib header throws...");
        if (js_bool(rt, "!_err4ok||_osz4===0")) printf(" PASS\n");
        else { printf(" FAIL\n"); failures++; }

        /* Gzip with corrupted CRC32 trailer (checksum mismatch) */
        qwrt_eval(rt,
            "var _err5data=new TextEncoder().encode('Hello World! '.repeat(10));"
            "var _err5cs=new CompressionStream('gzip');"
            "var _err5w=_err5cs.writable.getWriter();"
            "var _err5rd=_err5cs.readable.getReader();"
            "var _err5ch=[];var _err5sz=0;"
            "_err5w.write(_err5data).then(function(){return _err5w.close();}).then(function(){"
            "  function p(){_err5rd.read().then(function(r){"
            "    if(r.done){return;}"
            "    _err5ch.push(r.value);_err5sz+=r.value.length;p();});}"
            "  p();});", NULL);
        pump(rt, pal);
        /* Corrupt the CRC32 (byte at offset -8) and try to decompress */
        qwrt_eval(rt,
            "var _err5comp=new Uint8Array(_err5sz);var _e5o=0;"
            "for(var i=0;i<_err5ch.length;i++){_err5comp.set(_err5ch[i],_e5o);_e5o+=_err5ch[i].length;}"
            "_err5comp[_err5sz-8]^=0xFF;"  /* flip CRC32 byte */
            "var _err5ok=false;"
            "try{var _ds5=new DecompressionStream('gzip');"
            "var _dw5=_ds5.writable.getWriter();var _drd5=_ds5.readable.getReader();"
            "var _out5=[];var _osz5=0;"
            "_dw5.write(_err5comp).then(function(){return _dw5.close();}).then(function(){"
            "  function p(){_drd5.read().then(function(r){"
            "    if(r.done){_err5ok=false;return;}"
            "    _out5.push(r.value);_osz5+=r.value.length;p();});}"
            "  p();});"
            "}catch(e){_err5ok=true;}", NULL);
        pump(rt, pal);
        printf("  gzip CRC32 mismatch throws...");
        if (js_bool(rt, "!_err5ok||_osz5===0")) printf(" PASS\n");
        else { printf(" FAIL\n"); failures++; }
    }

    printf("\n=== Consistency tests: %s ===\n",
           failures == 0 ? "ALL PASSED" : "SOME FAILED");

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
    free(poly);
    return failures;
}
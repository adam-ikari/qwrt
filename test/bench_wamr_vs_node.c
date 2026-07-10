/*
 * WAMR vs Node.js Performance Comparison Benchmark
 *
 * Compares pure JS compute performance between:
 *   1. qwrt (QuickJS) — with and without WAMR extension
 *   2. Node.js — same workloads in pure JS
 *
 * WAMR extension currently provides JS API surface (stubs).
 * When WAMR is linked, WASM compute benchmarks will execute.
 * For now, measures JS-level overhead of the WAMR extension
 * and compares QuickJS compute vs Node.js.
 *
 * Usage:
 *   Build: cmake .. -DQWRT_BUILD_TESTS=ON && make bench_wamr_vs_node
 *   Run:   ./test/bench_wamr_vs_node
 *   Compare: node test/bench_wamr_vs_node.js
 */

#define _POSIX_C_SOURCE 200809L

#include "qwrt/qwrt.h"
#include "pal_mock.h"
#include "qwrt/ext_wamr.h"
#include "qwrt/ext_wasm3.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ================================================================
 * Helpers
 * ================================================================ */

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static uint8_t *load_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    if (len < 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);
    if (len == 0) { fclose(f); *out_len = 0; return (uint8_t *)calloc(1, 1); }
    uint8_t *buf = (uint8_t *)malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t nread = fread(buf, 1, (size_t)len, f);
    buf[nread] = 0;
    fclose(f);
    *out_len = nread;
    return buf;
}

/* Result tracking */
#define MAX_RESULTS 64
typedef struct {
    char name[64];
    double value;
    char unit[8];
} bench_result_t;

static bench_result_t g_results[MAX_RESULTS];
static int g_result_count = 0;

static void record(const char *name, double value, const char *unit) {
    if (g_result_count >= MAX_RESULTS) return;
    snprintf(g_results[g_result_count].name, sizeof(g_results[g_result_count].name), "%s", name);
    g_results[g_result_count].value = value;
    snprintf(g_results[g_result_count].unit, sizeof(g_results[g_result_count].unit), "%s", unit);
    g_result_count++;
}

/* Run a JS expression N times, return ops/sec */
static double bench_js(qwrt_t *rt, const char *code, int iterations) {
    double t0 = now_ms();
    for (int i = 0; i < iterations; i++) {
        char *r = NULL;
        qwrt_eval(rt, code, &r);
        qwrt_free(r);
    }
    double elapsed = now_ms() - t0;
    return iterations / (elapsed / 1000.0);
}

/* Print and record a benchmark */
static void run_bench(qwrt_t *rt, const char *name, const char *code, int iterations) {
    double ops = bench_js(rt, code, iterations);
    double elapsed = iterations / ops * 1000.0;
    printf("  %-45s %10.0f ops/s  (%.1f ms)\n", name, ops, elapsed);
    record(name, ops, "ops/s");
}

/* ================================================================
 * Minimal polyfill for benchmark (just enough for compute)
 * ================================================================ */

static const char bench_polyfill[] =
    "(function(pal) {\n"
    "  globalThis.console = { log: function() {} };\n"
    "  globalThis.performance = { now: function() { return pal.timeNow(); } };\n"
    "})(__pal_inject__);";

/* ================================================================
 * 1. Compute Benchmarks (pure JS — QuickJS vs Node.js)
 * ================================================================ */

static void bench_compute(qwrt_t *rt) {
    printf("\n=== 1. Compute (Pure JS) ===\n");

    /* Arithmetic */
    run_bench(rt, "compute.arithmetic_simple", "1+2+3+4+5", 100000);
    run_bench(rt, "compute.arithmetic_mul", "12345*67890", 100000);
    run_bench(rt, "compute.arithmetic_float", "3.14159*2.71828", 100000);

    /* Fibonacci */
    qwrt_eval(rt, "function fib(n){return n<2?n:fib(n-1)+fib(n-2)}", NULL);
    run_bench(rt, "compute.fib_20", "fib(20)", 1000);
    run_bench(rt, "compute.fib_30", "fib(30)", 50);

    /* Loop throughput */
    run_bench(rt, "compute.loop_1M",
        "(function(){var s=0;for(var i=0;i<1000000;i++){s+=i}return s})()", 100);

    /* Nested loop */
    run_bench(rt, "compute.nested_1Kx1K",
        "(function(){var s=0;for(var i=0;i<1000;i++){for(var j=0;j<1000;j++){s++}}return s})()", 10);

    /* Mandelbrot */
    qwrt_eval(rt,
        "function mandelbrot(size){"
        "  var sum=0,iter=50;"
        "  for(var y=0;y<size;y++){"
        "    for(var x=0;x<size;x++){"
        "      var zr=0,zi=0,cr=(x-size/2)/size*3,ci=(y-size/2)/size*3;"
        "      var n=0;"
        "      while(n<iter&&zr*zr+zi*zi<4){var t=zr*zr-zi*zi+cr;zi=2*zr*zi+ci;zr=t;n++}"
        "      sum+=n"
        "    }"
        "  }"
        "  return sum"
        "}", NULL);
    run_bench(rt, "compute.mandelbrot_40", "mandelbrot(40)", 200);
    run_bench(rt, "compute.mandelbrot_80", "mandelbrot(80)", 20);

    /* Prime sieve */
    qwrt_eval(rt,
        "function sieve(n){"
        "  var a=new Uint8Array(n+1);var count=0;"
        "  for(var i=2;i<=n;i++){"
        "    if(!a[i]){count++;for(var j=i*i;j<=n;j+=i)a[j]=1}"
        "  }"
        "  return count"
        "}", NULL);
    run_bench(rt, "compute.prime_sieve_100K", "sieve(100000)", 100);

    /* Matrix multiply (small) */
    qwrt_eval(rt,
        "function matmul(n){"
        "  var a=[],b=[],c=[];"
        "  for(var i=0;i<n;i++){a[i]=[];b[i]=[];c[i]=[];"
        "    for(var j=0;j<n;j++){a[i][j]=i+j;b[i][j]=i*j;c[i][j]=0}}"
        "  for(var i=0;i<n;i++)for(var j=0;j<n;j++)for(var k=0;k<n;k++)c[i][j]+=a[i][k]*b[k][j];"
        "  return c[0][0]"
        "}", NULL);
    run_bench(rt, "compute.matmul_32", "matmul(32)", 100);
    run_bench(rt, "compute.matmul_64", "matmul(64)", 10);
}

/* ================================================================
 * 2. String Benchmarks
 * ================================================================ */

static void bench_string(qwrt_t *rt) {
    printf("\n=== 2. String ===\n");

    run_bench(rt, "string.concat", "'hello'+' '+'world'", 100000);
    run_bench(rt, "string.concat_100",
        "(function(){var s='';for(var i=0;i<100;i++)s+='x';return s.length})()", 10000);
    run_bench(rt, "string.split", "'a,b,c,d,e,f,g,h,i,j'.split(',')", 100000);
    run_bench(rt, "string.indexOf", "'abcdefghijklmnopqrstuvwxyz'.indexOf('m')", 100000);
    run_bench(rt, "string.replace", "'hello world hello'.replace('hello','hi')", 100000);
    run_bench(rt, "string.repeat", "'ab'.repeat(100)", 100000);
    run_bench(rt, "string.toUpperCase", "'hello world'.toUpperCase()", 100000);

    /* Regex */
    run_bench(rt, "string.regex_test", "/[a-z]+/.test('hello123')", 100000);
    run_bench(rt, "string.regex_exec", "/[a-z]+/g.exec('hello123world456')", 50000);

    /* JSON */
    qwrt_eval(rt, "var _jobj={name:'test',values:[1,2,3],nested:{a:true,b:null}};", NULL);
    run_bench(rt, "string.json_stringify", "JSON.stringify(_jobj)", 100000);
    qwrt_eval(rt, "var _jstr=JSON.stringify(_jobj);", NULL);
    run_bench(rt, "string.json_parse", "JSON.parse(_jstr)", 100000);
}

/* ================================================================
 * 3. Array / Object Benchmarks
 * ================================================================ */

static void bench_array_object(qwrt_t *rt) {
    printf("\n=== 3. Array / Object ===\n");

    qwrt_eval(rt, "var _a100=[];for(var i=0;i<100;i++)_a100.push(i);", NULL);
    qwrt_eval(rt, "var _a1K=[];for(var i=0;i<1000;i++)_a1K.push(i);", NULL);

    run_bench(rt, "array.map_100", "_a100.map(function(x){return x*2})", 50000);
    run_bench(rt, "array.filter_100", "_a100.filter(function(x){return x%2===0})", 50000);
    run_bench(rt, "array.reduce_100", "_a100.reduce(function(a,b){return a+b},0)", 50000);
    run_bench(rt, "array.sort_1K",
        "(function(){var a=[];for(var i=0;i<1000;i++)a.push(Math.random());return a.sort().length})()", 2000);

    /* TypedArray */
    qwrt_eval(rt, "var _u8=new Uint8Array(1024);for(var i=0;i<1024;i++)_u8[i]=i&0xff;", NULL);
    run_bench(rt, "typedarray.create_1K", "new Uint8Array(1024)", 100000);
    run_bench(rt, "typedarray.read", "_u8[512]", 100000);

    /* Object */
    run_bench(rt, "object.create", "({a:1,b:2,c:3})", 100000);
    qwrt_eval(rt, "var _obj={a:1,b:2,c:3,d:4,e:5};", NULL);
    run_bench(rt, "object.read", "_obj.c", 100000);
    run_bench(rt, "object.keys", "Object.keys(_obj)", 100000);

    /* Map/Set */
    run_bench(rt, "map.churn_1K",
        "(function(){var m=new Map();for(var i=0;i<1000;i++){m.set(i,'v'+i)};for(var i=0;i<1000;i++){m.delete(i)}})()", 5000);
}

/* ================================================================
 * 4. WAMR Extension Overhead
 * ================================================================ */

static void bench_wamr_overhead(qwrt_t *rt) {
    printf("\n=== 4. WAMR Extension Overhead ===\n");

    /* Measure cost of accessing WebAssembly object (stub) */
    run_bench(rt, "wamr.global_typeof", "typeof WebAssembly", 100000);
    run_bench(rt, "wamr.global_access", "WebAssembly", 100000);

    /* Measure cost of calling stub methods (they throw) */
    /* We catch the error to avoid unhandled exception */
    run_bench(rt, "wamr.validate_call",
        "(function(){try{WebAssembly.validate(new Uint8Array(0))}catch(e){return e.message}})()", 50000);
    run_bench(rt, "wamr.compile_call",
        "(function(){try{WebAssembly.compile(new Uint8Array(0))}catch(e){return e.message}})()", 50000);
    run_bench(rt, "wamr.instantiate_call",
        "(function(){try{WebAssembly.instantiate(new Uint8Array(0))}catch(e){return e.message}})()", 50000);

    /* Module constructor overhead */
    run_bench(rt, "wamr.module_ctor",
        "(function(){try{new WebAssembly.Module(new Uint8Array(0))}catch(e){return e.message}})()", 50000);

    /* Instance constructor overhead */
    run_bench(rt, "wamr.instance_ctor",
        "(function(){try{new WebAssembly.Instance({})}catch(e){return e.message}})()", 50000);

    /* Memory constructor overhead */
    run_bench(rt, "wamr.memory_ctor",
        "(function(){try{new WebAssembly.Memory({initial:1})}catch(e){return e.message}})()", 50000);
}

/* ================================================================
 * 5. Startup / Context Switch Overhead
 * ================================================================ */

static void bench_startup(void) {
    printf("\n=== 5. Startup ===\n");

    qwrt_pal_t *pal = pal_mock_create();

    /* qwrt create/destroy (no extension) */
    double t0 = now_ms();
    for (int i = 0; i < 100; i++) {
        qwrt_config_t cfg; memset(&cfg, 0, sizeof(cfg));
        cfg.pal = pal;
        qwrt_t *rt = qwrt_create(&cfg);
        qwrt_destroy(rt);
    }
    double bare = (now_ms() - t0) / 100.0;
    printf("  %-45s %10.2f ms\n", "startup.create_destroy (no ext)", bare);
    record("startup.no_ext", bare, "ms");

    /* qwrt create/destroy (with the default extension set, which includes the
     * build-time-selected WASM engine - wamr or wasm3). The per-engine
     * comparison that used config.extensions is gone (extensions are now
     * compile-time fixed); rebuild with the other engine to compare. */
    t0 = now_ms();
    for (int i = 0; i < 100; i++) {
        qwrt_config_t cfg; memset(&cfg, 0, sizeof(cfg));
        cfg.pal = pal;
        qwrt_t *rt = qwrt_create(&cfg);
        qwrt_destroy(rt);
    }
    double with_ext = (now_ms() - t0) / 100.0;
    printf("  %-45s %10.2f ms\n", "startup.create_destroy (default ext set)", with_ext);
    record("startup.with_ext", with_ext, "ms");

    /* Overhead */
    printf("  %-45s %10.2f ms  (%.1fx)\n", "ext overhead", with_ext - bare,
           with_ext / bare);

    /* qwrt_reset overhead */
    qwrt_config_t cfg; memset(&cfg, 0, sizeof(cfg));
    cfg.pal = pal;
    qwrt_t *rt = qwrt_create(&cfg);

    t0 = now_ms();
    for (int i = 0; i < 100; i++) {
        qwrt_reset(rt, &cfg);
    }
    double reset_time = (now_ms() - t0) / 100.0;
    printf("  %-45s %10.2f ms\n", "startup.qwrt_reset", reset_time);
    record("startup.reset", reset_time, "ms");

    /* Multi-context spawn overhead */
    t0 = now_ms();
    for (int i = 0; i < 100; i++) {
        int ctx_id = qwrt_spawn(rt, &cfg);
        qwrt_destroy_ctx(rt, ctx_id);
    }
    double spawn_time = (now_ms() - t0) / 100.0;
    printf("  %-45s %10.2f ms\n", "startup.spawn_destroy_ctx", spawn_time);
    record("startup.spawn_destroy", spawn_time, "ms");

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

/* ================================================================
 * 6. Multi-Context Switch Overhead
 * ================================================================ */

static void bench_context_switch(void) {
    printf("\n=== 6. Context Switch ===\n");

    qwrt_pal_t *pal = pal_mock_create();
    qwrt_config_t cfg; memset(&cfg, 0, sizeof(cfg));
    cfg.pal = pal;
    qwrt_t *rt = qwrt_create(&cfg);

    /* Spawn several contexts */
    int ctx_ids[10];
    for (int i = 0; i < 10; i++) {
        ctx_ids[i] = qwrt_spawn(rt, &cfg);
    }

    /* Resume back and forth */
    double t0 = now_ms();
    for (int i = 0; i < 10000; i++) {
        int target = ctx_ids[i % 10];
        qwrt_resume(rt, target);
    }
    double elapsed = now_ms() - t0;
    double ops = 10000 / (elapsed / 1000.0);
    printf("  %-45s %10.0f ops/s  (%.1f ms)\n", "ctx.resume_10ctx", ops, elapsed);
    record("ctx.resume_10ctx", ops, "ops/s");

    /* Suspend/resume cycle */
    qwrt_resume(rt, 0);
    t0 = now_ms();
    for (int i = 0; i < 10000; i++) {
        qwrt_suspend(rt);
        qwrt_resume(rt, ctx_ids[i % 10]);
    }
    elapsed = now_ms() - t0;
    ops = 10000 / (elapsed / 1000.0);
    printf("  %-45s %10.0f ops/s  (%.1f ms)\n", "ctx.suspend_resume_cycle", ops, elapsed);
    record("ctx.suspend_resume", ops, "ops/s");

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
}

/* ================================================================
 * Output JSON
 * ================================================================ */

static void output_json(void) {
    printf("\n=== JSON Results (qwrt) ===\n");
    printf("[\n");
    for (int i = 0; i < g_result_count; i++) {
        printf("  {\"name\": \"%s\", \"value\": %.2f, \"unit\": \"%s\"}%s\n",
               g_results[i].name, g_results[i].value, g_results[i].unit,
               i < g_result_count - 1 ? "," : "");
    }
    printf("]\n");
}

/* ================================================================
 * Main
 * ================================================================ */

int main(void) {
    printf("=== WAMR vs Node.js Performance Comparison ===\n");
    printf("Engine: qwrt (QuickJS-NG) + WAMR extension (stub)\n\n");

    /* 5. Startup benchmarks (create their own runtimes) */
    bench_startup();

    /* Create runtime with the default extension set (WAMR or wasm3, whichever
     * is compiled in) for compute benchmarks. */
    qwrt_pal_t *pal = pal_mock_create();

    qwrt_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.pal = pal;

    qwrt_t *rt = qwrt_create(&cfg);
    if (!rt) {
        printf("FATAL: could not create runtime\n");
        pal_mock_destroy(pal);
        return 1;
    }

    /* Verify WAMR extension loaded */
    char *result = NULL;
    int rc = qwrt_eval(rt, "typeof WebAssembly", &result);
    if (rc == 0 && result) {
        printf("WebAssembly global: %s\n", result);
        qwrt_free(result);
    }

    /* Run benchmarks */
    bench_compute(rt);
    bench_string(rt);
    bench_array_object(rt);
    bench_wamr_overhead(rt);

    /* Also run without extension for comparison */
    printf("\n--- Without WAMR Extension ---\n");
    qwrt_config_t cfg_noext;
    memset(&cfg_noext, 0, sizeof(cfg_noext));
    cfg_noext.pal = pal;

    qwrt_reset(rt, &cfg_noext);
    printf("\n=== Compute (No Extension) ===\n");
    qwrt_eval(rt, "function fib(n){return n<2?n:fib(n-1)+fib(n-2)}", NULL);
    run_bench(rt, "compute_noext.fib_20", "fib(20)", 1000);
    run_bench(rt, "compute_noext.loop_1M",
        "(function(){var s=0;for(var i=0;i<1000000;i++){s+=i}return s})()", 100);
    qwrt_eval(rt,
        "function mandelbrot(size){"
        "  var sum=0,iter=50;"
        "  for(var y=0;y<size;y++){"
        "    for(var x=0;x<size;x++){"
        "      var zr=0,zi=0,cr=(x-size/2)/size*3,ci=(y-size/2)/size*3;"
        "      var n=0;"
        "      while(n<iter&&zr*zr+zi*zi<4){var t=zr*zr-zi*zi+cr;zi=2*zr*zi+ci;zr=t;n++}"
        "      sum+=n"
        "    }"
        "  }"
        "  return sum"
        "}", NULL);
    run_bench(rt, "compute_noext.mandelbrot_40", "mandelbrot(40)", 200);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);

    /* 6. Context switch benchmarks */
    bench_context_switch();

    /* JSON output */
    output_json();

    return 0;
}

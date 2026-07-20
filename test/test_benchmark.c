/*
 * qwrt Performance Benchmark Suite
 *
 * Based on standard microbenchmark patterns from:
 *   - Octane (Google V8 benchmark): crypto, deltablue, richards patterns
 *   - JetStream 2 (Webkit): array/object/string workloads
 *   - QuickJS test suite: startup and eval benchmarks
 *   - Areweweb yet (WinterTC): API compliance + perf
 *
 * Benchmark categories:
 *   1. Startup     — Runtime creation + polyfill loading
 *   2. Compute     — Arithmetic, recursion, loop throughput
 *   3. String      — Manipulation, regex, encoding
 *   4. Object      — Property access, creation, prototype chain
 *   5. Array       — Map/reduce/sort, typed arrays
 *   6. Async       — Promise, microtask, timer throughput
 *   7. WinterTC    — URL, TextEncoder, btoa, crypto, fetch
 *   8. GC          — Allocation pressure, garbage collection
 *
 * Results are printed in JSON for automated tracking.
 */

#define _POSIX_C_SOURCE 200809L

#include "qwrt/qwrt.h"
#include "pal_mock.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ================================================================
 * Globals
 * ================================================================ */

static const uint8_t *g_polyfill;
static size_t g_polyfill_len;

/* Benchmark result tracking */
#define MAX_RESULTS 64
typedef struct {
    char name[64];
    double value;        /* ops/sec or ms */
    char unit[8];        /* "ops/s" or "ms" */
} bench_result_t;

static bench_result_t g_results[MAX_RESULTS];
static int g_result_count = 0;

/* ================================================================
 * Helpers
 * ================================================================ */

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

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static void record(const char *name, double value, const char *unit) {
    if (g_result_count >= MAX_RESULTS) return;
    snprintf(g_results[g_result_count].name, sizeof(g_results[g_result_count].name), "%s", name);
    g_results[g_result_count].value = value;
    snprintf(g_results[g_result_count].unit, sizeof(g_results[g_result_count].unit), "%s", unit);
    g_result_count++;
}

/* Run a JS expr N times and record ops/sec */
static void bench_js_ops(qwrt_t *rt, const char *name, const char *code, int iterations) {
    double t0 = now_ms();
    for (int i = 0; i < iterations; i++) {
        char *r = NULL;
        qwrt_eval(rt, code, &r);
        qwrt_free(r);
    }
    double elapsed = now_ms() - t0;
    double ops_sec = iterations / (elapsed / 1000.0);
    printf("  %-40s %10.0f ops/s  (%.1f ms total)\n", name, ops_sec, elapsed);
    record(name, ops_sec, "ops/s");
}

/* Run a JS setup + measure loop */
static void bench_js_loop(qwrt_t *rt, const char *name,
                          const char *setup, const char *loop_body,
                          int iterations) {
    if (setup) {
        qwrt_eval(rt, setup, NULL);
    }
    char code[512];
    double t0 = now_ms();
    for (int i = 0; i < iterations; i++) {
        snprintf(code, sizeof(code), "%s", loop_body);
        char *r = NULL;
        qwrt_eval(rt, code, &r);
        qwrt_free(r);
    }
    double elapsed = now_ms() - t0;
    double ops_sec = iterations / (elapsed / 1000.0);
    printf("  %-40s %10.0f ops/s  (%.1f ms total)\n", name, ops_sec, elapsed);
    record(name, ops_sec, "ops/s");
}

/* ================================================================
 * 1. Startup Benchmarks
 * ================================================================ */

static void bench_startup(void) {
    printf("\n=== 1. Startup ===\n");

    /* Cold start: bytecode polyfill */
    double t0 = now_ms();
    for (int i = 0; i < 50; i++) {
        qwrt_pal_t *pal = pal_mock_create();
        qwrt_config_t cfg; memset(&cfg, 0, sizeof(cfg));
        qwrt_t *rt = qwrt_create(&cfg);
        qwrt_destroy(rt); pal_mock_destroy(pal);
    }
    double bc_time = (now_ms() - t0) / 50.0;
    printf("  %-40s %10.2f ms\n", "Runtime create (bytecode)", bc_time);
    record("startup.bytecode_polyfill", bc_time, "ms");

    /* Cold start: no polyfill */
    t0 = now_ms();
    for (int i = 0; i < 50; i++) {
        qwrt_pal_t *pal = pal_mock_create();
        qwrt_config_t cfg; memset(&cfg, 0, sizeof(cfg));
        cfg.pal = pal;
        qwrt_t *rt = qwrt_create(&cfg);
        qwrt_destroy(rt); pal_mock_destroy(pal);
    }
    double bare_time = (now_ms() - t0) / 50.0;
    printf("  %-40s %10.2f ms\n", "Runtime create (no polyfill)", bare_time);
    record("startup.no_polyfill", bare_time, "ms");

    /* Eval cold start */
    qwrt_pal_t *pal = pal_mock_create();
    qwrt_config_t cfg; memset(&cfg, 0, sizeof(cfg));
    qwrt_t *rt = qwrt_create(&cfg);

    t0 = now_ms();
    for (int i = 0; i < 1000; i++) {
        char *r = NULL;
        qwrt_eval(rt, "1+1", &r);
        qwrt_free(r);
    }
    double eval_cold = (now_ms() - t0);
    printf("  %-40s %10.2f ms  (1000 evals)\n", "First eval throughput", eval_cold);
    record("startup.eval_throughput", 1000 / (eval_cold / 1000.0), "ops/s");

    pal_mock_destroy(pal);
    qwrt_destroy(rt);
}

/* ================================================================
 * 2. Compute Benchmarks
 * ================================================================ */

static void bench_compute(qwrt_t *rt) {
    printf("\n=== 2. Compute ===\n");

    /* Arithmetic — based on Octane richards-like patterns */
    bench_js_ops(rt, "arithmetic.simple", "1+2+3+4+5", 10000);
    bench_js_ops(rt, "arithmetic.multiplication", "12345*67890", 10000);
    bench_js_ops(rt, "arithmetic.float", "3.14159*2.71828", 10000);
    bench_js_ops(rt, "arithmetic.modulo", "1234567%997", 10000);

    /* Fibonacci — classic recursive benchmark */
    qwrt_eval(rt, "function fib(n){return n<2?n:fib(n-1)+fib(n-2)}", NULL);
    bench_js_ops(rt, "compute.fib(20)", "fib(20)", 100);
    bench_js_ops(rt, "compute.fib(25)", "fib(25)", 20);

    /* Loop throughput */
    bench_js_ops(rt, "compute.loop_1M",
        "(function(){var s=0;for(var i=0;i<1000000;i++){s+=i}return s})()", 10);

    /* Nested loop */
    bench_js_ops(rt, "compute.nested_loop_1Kx1K",
        "(function(){var s=0;for(var i=0;i<1000;i++){for(var j=0;j<1000;j++){s++}}return s})()", 5);

    /* Mandelbrot (simplified) — from JetStream */
    qwrt_eval(rt,
        "function mandelbrot(size){"
        "  var sum=0;var iter=50;"
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
    bench_js_ops(rt, "compute.mandelbrot(40)", "mandelbrot(40)", 50);
}

/* ================================================================
 * 3. String Benchmarks
 * ================================================================ */

static void bench_string(qwrt_t *rt) {
    printf("\n=== 3. String ===\n");

    /* String concatenation */
    qwrt_eval(rt, "var _sp='x';", NULL);
    bench_js_ops(rt, "string.concat_small", "'hello'+' '+'world'", 10000);
    bench_js_ops(rt, "string.concat_100",
        "(function(){var s='';for(var i=0;i<100;i++)s+='x';return s.length})()", 1000);

    /* String methods */
    bench_js_ops(rt, "string.split", "'a,b,c,d,e,f,g,h,i,j'.split(',')", 10000);
    bench_js_ops(rt, "string.indexOf", "'abcdefghijklmnopqrstuvwxyz'.indexOf('m')", 10000);
    bench_js_ops(rt, "string.slice", "'abcdefghijklmnopqrstuvwxyz'.slice(5,15)", 10000);
    bench_js_ops(rt, "string.replace", "'hello world hello'.replace('hello','hi')", 10000);
    bench_js_ops(rt, "string.replaceAll", "'aaa bbb aaa'.replaceAll('aaa','ccc')", 10000);
    bench_js_ops(rt, "string.repeat", "'ab'.repeat(100)", 10000);
    bench_js_ops(rt, "string.trim", "'   hello world   '.trim()", 10000);
    bench_js_ops(rt, "string.toUpperCase", "'hello world'.toUpperCase()", 10000);

    /* Regex */
    qwrt_eval(rt, "var _re=/[a-z]+/g;", NULL);
    bench_js_ops(rt, "string.regex_test", "/[a-z]+/.test('hello123')", 10000);
    bench_js_ops(rt, "string.regex_exec", "/[a-z]+/g.exec('hello123world456')", 10000);
    bench_js_ops(rt, "string.matchAll",
        "Array.from('abc123def456'.matchAll(/[a-z]+/g)).length", 5000);

    /* Template literals */
    bench_js_ops(rt, "string.template", "(function(){var a=1,b=2;return `${a}+${b}=${a+b}`})()", 10000);
}

/* ================================================================
 * 4. Object Benchmarks
 * ================================================================ */

static void bench_object(qwrt_t *rt) {
    printf("\n=== 4. Object ===\n");

    bench_js_ops(rt, "object.create_empty", "({})", 10000);
    bench_js_ops(rt, "object.create_props", "({a:1,b:2,c:3})", 10000);

    qwrt_eval(rt, "var _obj={a:1,b:2,c:3,d:4,e:5};", NULL);
    bench_js_ops(rt, "object.property_read", "_obj.c", 10000);
    bench_js_ops(rt, "object.property_write",
        "(function(){var o={x:0};o.x=42;return o.x})()", 10000);

    bench_js_ops(rt, "object.keys", "Object.keys({a:1,b:2,c:3,d:4,e:5})", 10000);
    bench_js_ops(rt, "object.values", "Object.values({a:1,b:2,c:3})", 10000);
    bench_js_ops(rt, "object.entries", "Object.entries({a:1,b:2,c:3})", 10000);
    bench_js_ops(rt, "object.assign", "Object.assign({a:1},{b:2},{c:3})", 10000);
    bench_js_ops(rt, "object.hasOwn", "Object.hasOwn({a:1},'a')", 10000);
    bench_js_ops(rt, "object.freeze", "Object.freeze({a:1,b:2})", 5000);

    /* Prototype chain */
    qwrt_eval(rt,
        "function Animal(name){this.name=name} "
        "Animal.prototype.speak=function(){return this.name+' speaks'}; "
        "var _dog=new Animal('dog')", NULL);
    bench_js_ops(rt, "object.prototype_method", "_dog.speak()", 10000);
    bench_js_ops(rt, "object.instanceof", "_dog instanceof Animal", 10000);

    /* Deep clone */
    bench_js_ops(rt, "object.structuredClone", "structuredClone({a:{b:{c:1}}})", 5000);
}

/* ================================================================
 * 5. Array Benchmarks
 * ================================================================ */

static void bench_array(qwrt_t *rt) {
    printf("\n=== 5. Array ===\n");

    qwrt_eval(rt, "var _arr100=[];for(var i=0;i<100;i++)_arr100.push(i);", NULL);
    qwrt_eval(rt, "var _arr1K=[];for(var i=0;i<1000;i++)_arr1K.push(i);", NULL);
    qwrt_eval(rt, "var _arr10K=[];for(var i=0;i<10000;i++)_arr10K.push(i);", NULL);

    bench_js_ops(rt, "array.create_100", "(function(){var a=[];for(var i=0;i<100;i++)a.push(i);return a.length})()", 5000);
    bench_js_ops(rt, "array.map_100", "_arr100.map(function(x){return x*2})", 10000);
    bench_js_ops(rt, "array.filter_100", "_arr100.filter(function(x){return x%2===0})", 10000);
    bench_js_ops(rt, "array.reduce_100", "_arr100.reduce(function(a,b){return a+b},0)", 10000);
    bench_js_ops(rt, "array.sort_100", "(function(){return [3,1,4,1,5,9,2,6,5,3,5,8,9,7,9].sort(function(a,b){return a-b})})()", 10000);
    bench_js_ops(rt, "array.find_100", "_arr100.find(function(x){return x>50})", 10000);
    bench_js_ops(rt, "array.findLast_100", "_arr100.findLast(function(x){return x>50})", 10000);
    bench_js_ops(rt, "array.includes_100", "_arr100.includes(99)", 10000);
    bench_js_ops(rt, "array.flat_100", "[[1,2],[3,4],[5,6]].flat()", 10000);
    bench_js_ops(rt, "array.toSorted_100", "_arr100.toSorted(function(a,b){return b-a})", 10000);

    /* Large array operations */
    bench_js_ops(rt, "array.map_1K", "_arr1K.map(function(x){return x*2})", 5000);
    bench_js_ops(rt, "array.reduce_1K", "_arr1K.reduce(function(a,b){return a+b},0)", 5000);
    bench_js_ops(rt, "array.sort_1K",
        "(function(){var a=[];for(var i=0;i<1000;i++)a.push(Math.random());return a.sort().length})()", 500);

    /* TypedArray */
    qwrt_eval(rt, "var _u8=new Uint8Array(1024);for(var i=0;i<1024;i++)_u8[i]=i&0xff;", NULL);
    bench_js_ops(rt, "typedarray.create_1K", "new Uint8Array(1024)", 10000);
    bench_js_ops(rt, "typedarray.read", "_u8[512]", 10000);
    bench_js_ops(rt, "typedarray.slice_1K", "_u8.slice(0,512)", 10000);

    /* Spread / destructuring */
    bench_js_ops(rt, "array.spread", "[..._arr100, ..._arr100].length", 5000);
}

/* ================================================================
 * 6. Async Benchmarks
 * ================================================================ */

static void bench_async(qwrt_t *rt) {
    printf("\n=== 6. Async ===\n");

    /* Promise resolution throughput */
    double t0 = now_ms();
    int N = 5000;
    qwrt_eval(rt, "var _pcount=0;", NULL);
    for (int i = 0; i < N; i++) {
        qwrt_eval(rt, "Promise.resolve(1).then(function(v){_pcount++})", NULL);
        qwrt_tick(rt, 100);
    }
    double elapsed = now_ms() - t0;
    double ops = N / (elapsed / 1000.0);
    printf("  %-40s %10.0f ops/s  (%.1f ms total)\n", "async.promise_resolve", ops, elapsed);
    record("async.promise_resolve", ops, "ops/s");

    /* queueMicrotask throughput */
    qwrt_eval(rt, "var _mcount=0;", NULL);
    t0 = now_ms();
    for (int i = 0; i < N; i++) {
        qwrt_eval(rt, "queueMicrotask(function(){_mcount++})", NULL);
        qwrt_tick(rt, 100);
    }
    elapsed = now_ms() - t0;
    ops = N / (elapsed / 1000.0);
    printf("  %-40s %10.0f ops/s  (%.1f ms total)\n", "async.queueMicrotask", ops, elapsed);
    record("async.queueMicrotask", ops, "ops/s");

    /* Tick overhead (no pending) */
    t0 = now_ms();
    for (int i = 0; i < 100000; i++) qwrt_tick(rt, 100);
    elapsed = now_ms() - t0;
    ops = 100000 / (elapsed / 1000.0);
    printf("  %-40s %10.0f ops/s  (%.1f ms total)\n", "async.tick_empty", ops, elapsed);
    record("async.tick_empty", ops, "ops/s");

    /* qwrt_call throughput */
    qwrt_eval(rt, "function _add(a,b){return a+b}", NULL);
    t0 = now_ms();
    for (int i = 0; i < 5000; i++) {
        char *r = NULL;
        qwrt_call(rt, "_add", "[1,2]", &r);
        qwrt_free(r);
    }
    elapsed = now_ms() - t0;
    ops = 5000 / (elapsed / 1000.0);
    printf("  %-40s %10.0f ops/s  (%.1f ms total)\n", "async.qwrt_call", ops, elapsed);
    record("async.qwrt_call", ops, "ops/s");
}

/* ================================================================
 * 7. WinterTC API Benchmarks
 * ================================================================ */

static void bench_wintercg(qwrt_t *rt) {
    printf("\n=== 7. WinterTC APIs ===\n");

    /* URL parsing */
    bench_js_ops(rt, "wintercg.url_parse",
        "new URL('https://example.com/path?q=1#frag')", 5000);
    bench_js_ops(rt, "wintercg.url_searchParams",
        "new URL('https://example.com/?a=1&b=2&c=3').searchParams.get('b')", 5000);
    bench_js_ops(rt, "wintercg.urlParams_parse",
        "new URLSearchParams('a=1&b=2&c=3&d=4')", 5000);
    bench_js_ops(rt, "wintercg.urlParams_toString",
        "new URLSearchParams({a:'1',b:'2'}).toString()", 5000);

    /* TextEncoder/Decoder */
    bench_js_ops(rt, "wintercg.textEncoder_encode",
        "new TextEncoder().encode('Hello, World! This is a test string.')", 10000);
    qwrt_eval(rt, "var _te=new TextEncoder().encode('Hello, World! This is a test string.');", NULL);
    bench_js_ops(rt, "wintercg.textDecoder_decode",
        "new TextDecoder().decode(_te)", 10000);

    /* btoa/atob */
    bench_js_ops(rt, "wintercg.btoa", "btoa('Hello, World! This is a test string.')", 10000);
    qwrt_eval(rt, "var _b64=btoa('Hello, World! This is a test string.');", NULL);
    bench_js_ops(rt, "wintercg.atob", "atob(_b64)", 10000);

    /* crypto.getRandomValues */
    bench_js_ops(rt, "wintercg.crypto_random",
        "crypto.getRandomValues(new Uint8Array(16))", 10000);

    /* performance.now */
    bench_js_ops(rt, "wintercg.performance_now", "performance.now()", 10000);

    /* console.log (through PAL) */
    bench_js_ops(rt, "wintercg.console_log", "console.log('benchmark')", 10000);

    /* JSON */
    qwrt_eval(rt, "var _jobj={name:'test',values:[1,2,3],nested:{a:true,b:null}};", NULL);
    bench_js_ops(rt, "wintercg.json_stringify",
        "JSON.stringify(_jobj)", 10000);
    qwrt_eval(rt, "var _jstr=JSON.stringify(_jobj);", NULL);
    bench_js_ops(rt, "wintercg.json_parse",
        "JSON.parse(_jstr)", 10000);

    /* Date */
    bench_js_ops(rt, "wintercg.date_now", "Date.now()", 10000);
    bench_js_ops(rt, "wintercg.date_format",
        "new Date().toISOString()", 5000);

    /* Math */
    bench_js_ops(rt, "wintercg.math_random", "Math.random()", 10000);
    bench_js_ops(rt, "wintercg.math_sqrt", "Math.sqrt(12345.6789)", 10000);
}

/* ================================================================
 * 8. GC / Memory Benchmarks
 * ================================================================ */

static void bench_gc(qwrt_t *rt) {
    printf("\n=== 8. GC / Memory ===\n");

    /* Object creation + discard (GC pressure) */
    bench_js_ops(rt, "gc.object_create_discard_1K",
        "(function(){for(var i=0;i<1000;i++){var o={a:i,b:'str'+i,c:[i]}}})()", 100);

    /* Array creation + discard */
    bench_js_ops(rt, "gc.array_create_discard_1K",
        "(function(){for(var i=0;i<1000;i++){var a=new Array(100);a[0]=i}})()", 100);

    /* String creation + discard */
    bench_js_ops(rt, "gc.string_create_discard_1K",
        "(function(){for(var i=0;i<1000;i++){var s='x'.repeat(100)}})()", 100);

    /* Closure creation */
    bench_js_ops(rt, "gc.closure_create_1K",
        "(function(){for(var i=0;i<1000;i++){var f=function(x){return x+i};f(i)}})()", 100);

    /* Map/Set churn */
    bench_js_ops(rt, "gc.map_churn_1K",
        "(function(){var m=new Map();for(var i=0;i<1000;i++){m.set(i,'v'+i)};for(var i=0;i<1000;i++){m.delete(i)}})()", 100);
    bench_js_ops(rt, "gc.set_churn_1K",
        "(function(){var s=new Set();for(var i=0;i<1000;i++){s.add(i)};for(var i=0;i<1000;i++){s.delete(i)}})()", 100);

    /* Deep object tree */
    bench_js_ops(rt, "gc.deep_object_100",
        "(function(){var o={};var c=o;for(var i=0;i<100;i++){c.child={value:i};c=c.child}return o})()", 1000);
}

/* ================================================================
 * Output JSON results
 * ================================================================ */

static void output_json(void) {
    printf("\n=== JSON Results ===\n");
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
    /* Polyfill is auto-injected by qwrt_create (qwrt_default_polyfill). */
    printf("=== qwrt Performance Benchmark Suite ===\n\n");

    /* 1. Startup */
    bench_startup();

    /* Create runtime for remaining benchmarks */
    qwrt_pal_t *pal = pal_mock_create();
    qwrt_config_t cfg; memset(&cfg, 0, sizeof(cfg));
    cfg.pal = pal;
    qwrt_t *rt = qwrt_create(&cfg);
    if (!rt) {
        printf("FATAL: could not create runtime\n");
        return 1;
    }

    /* 2-8. Run benchmarks */
    bench_compute(rt);
    bench_string(rt);
    bench_object(rt);
    bench_array(rt);
    bench_async(rt);
    bench_wintercg(rt);
    bench_gc(rt);

    /* Cleanup */
    qwrt_destroy(rt);
    pal_mock_destroy(pal);

    /* JSON output for tracking */
    output_json();

    return 0;
}

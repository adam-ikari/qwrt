/*
 * Node.js benchmark counterpart to bench_wamr_vs_node.c
 *
 * Run: node test/bench_wamr_vs_node.js
 *
 * Uses the same compute workloads for direct comparison with qwrt.
 */

'use strict';

const { performance } = require('perf_hooks');

/* Result tracking */
const results = [];
function record(name, value, unit) {
    results.push({ name, value, unit });
}

function bench_fn(name, fn, iterations) {
    // Warmup
    for (let i = 0; i < Math.min(iterations, 10); i++) fn();

    const t0 = performance.now();
    for (let i = 0; i < iterations; i++) fn();
    const elapsed = performance.now() - t0;
    const ops = iterations / (elapsed / 1000);
    const perOp = elapsed / iterations;
    console.log(`  ${name.padEnd(45)} ${ops.toFixed(0).padStart(10)} ops/s  (${perOp.toFixed(1)} ms)`);
    record(name, ops, 'ops/s');
}

/* ================================================================
 * 1. Compute
 * ================================================================ */

function bench_compute() {
    console.log('\n=== 1. Compute (Pure JS) ===');

    bench_fn('compute.arithmetic_simple', () => 1+2+3+4+5, 100000);
    bench_fn('compute.arithmetic_mul', () => 12345*67890, 100000);
    bench_fn('compute.arithmetic_float', () => 3.14159*2.71828, 100000);

    function fib(n) { return n < 2 ? n : fib(n-1) + fib(n-2); }
    bench_fn('compute.fib_20', () => fib(20), 1000);
    bench_fn('compute.fib_30', () => fib(30), 50);

    bench_fn('compute.loop_1M', () => { let s=0; for(let i=0;i<1000000;i++) s+=i; return s; }, 100);
    bench_fn('compute.nested_1Kx1K', () => { let s=0; for(let i=0;i<1000;i++) for(let j=0;j<1000;j++) s++; return s; }, 10);

    function mandelbrot(size) {
        let sum = 0, iter = 50;
        for (let y = 0; y < size; y++) {
            for (let x = 0; x < size; x++) {
                let zr = 0, zi = 0;
                const cr = (x - size/2) / size * 3;
                const ci = (y - size/2) / size * 3;
                let n = 0;
                while (n < iter && zr*zr + zi*zi < 4) {
                    const t = zr*zr - zi*zi + cr;
                    zi = 2*zr*zi + ci;
                    zr = t;
                    n++;
                }
                sum += n;
            }
        }
        return sum;
    }
    bench_fn('compute.mandelbrot_40', () => mandelbrot(40), 200);
    bench_fn('compute.mandelbrot_80', () => mandelbrot(80), 20);

    function sieve(n) {
        const a = new Uint8Array(n + 1);
        let count = 0;
        for (let i = 2; i <= n; i++) {
            if (!a[i]) { count++; for (let j = i*i; j <= n; j += i) a[j] = 1; }
        }
        return count;
    }
    bench_fn('compute.prime_sieve_100K', () => sieve(100000), 100);

    function matmul(n) {
        const a = [], b = [], c = [];
        for (let i = 0; i < n; i++) { a[i]=[]; b[i]=[]; c[i]=[];
            for (let j = 0; j < n; j++) { a[i][j]=i+j; b[i][j]=i*j; c[i][j]=0; }
        }
        for (let i = 0; i < n; i++)
            for (let j = 0; j < n; j++)
                for (let k = 0; k < n; k++) c[i][j] += a[i][k] * b[k][j];
        return c[0][0];
    }
    bench_fn('compute.matmul_32', () => matmul(32), 100);
    bench_fn('compute.matmul_64', () => matmul(64), 10);
}

/* ================================================================
 * 2. String
 * ================================================================ */

function bench_string() {
    console.log('\n=== 2. String ===');

    bench_fn('string.concat', () => 'hello' + ' ' + 'world', 100000);
    bench_fn('string.concat_100', () => { let s=''; for(let i=0;i<100;i++) s+='x'; return s.length; }, 10000);
    bench_fn('string.split', () => 'a,b,c,d,e,f,g,h,i,j'.split(','), 100000);
    bench_fn('string.indexOf', () => 'abcdefghijklmnopqrstuvwxyz'.indexOf('m'), 100000);
    bench_fn('string.replace', () => 'hello world hello'.replace('hello','hi'), 100000);
    bench_fn('string.repeat', () => 'ab'.repeat(100), 100000);
    bench_fn('string.toUpperCase', () => 'hello world'.toUpperCase(), 100000);

    bench_fn('string.regex_test', () => /[a-z]+/.test('hello123'), 100000);
    bench_fn('string.regex_exec', () => /[a-z]+/g.exec('hello123world456'), 50000);

    const _jobj = {name:'test',values:[1,2,3],nested:{a:true,b:null}};
    bench_fn('string.json_stringify', () => JSON.stringify(_jobj), 100000);
    const _jstr = JSON.stringify(_jobj);
    bench_fn('string.json_parse', () => JSON.parse(_jstr), 100000);
}

/* ================================================================
 * 3. Array / Object
 * ================================================================ */

function bench_array_object() {
    console.log('\n=== 3. Array / Object ===');

    const _a100 = Array.from({length: 100}, (_, i) => i);
    const _a1K = Array.from({length: 1000}, (_, i) => i);

    bench_fn('array.map_100', () => _a100.map(x => x*2), 50000);
    bench_fn('array.filter_100', () => _a100.filter(x => x%2===0), 50000);
    bench_fn('array.reduce_100', () => _a100.reduce((a,b) => a+b, 0), 50000);
    bench_fn('array.sort_1K', () => { const a=[]; for(let i=0;i<1000;i++) a.push(Math.random()); return a.sort().length; }, 2000);

    const _u8 = new Uint8Array(1024); for(let i=0;i<1024;i++) _u8[i] = i & 0xff;
    bench_fn('typedarray.create_1K', () => new Uint8Array(1024), 100000);
    bench_fn('typedarray.read', () => _u8[512], 100000);

    bench_fn('object.create', () => ({a:1,b:2,c:3}), 100000);
    const _obj = {a:1,b:2,c:3,d:4,e:5};
    bench_fn('object.read', () => _obj.c, 100000);
    bench_fn('object.keys', () => Object.keys(_obj), 100000);

    bench_fn('map.churn_1K', () => { const m=new Map(); for(let i=0;i<1000;i++) m.set(i,'v'+i); for(let i=0;i<1000;i++) m.delete(i); }, 5000);
}

/* ================================================================
 * 4. Startup
 * ================================================================ */

async function bench_startup() {
    console.log('\n=== 5. Startup ===');

    // Node.js startup time (measure process startup)
    // We can't directly measure node startup from inside node,
    // but we can measure vm.createContext as a comparison
    const vm = require('vm');

    const t0 = performance.now();
    for (let i = 0; i < 100; i++) {
        const ctx = vm.createContext({ console, setTimeout, JSON });
        vm.runInContext('1+1', ctx);
    }
    const elapsed = (performance.now() - t0) / 100;
    console.log(`  ${'startup.vm_createContext'.padEnd(45)} ${elapsed.toFixed(2).padStart(10)} ms`);
    record('startup.vm_createContext', elapsed, 'ms');

    // Module compilation
    const t1 = performance.now();
    for (let i = 0; i < 100; i++) {
        new vm.Script('function fib(n){return n<2?n:fib(n-1)+fib(n-2)} fib(20);');
    }
    const compile_time = (performance.now() - t1) / 100;
    console.log(`  ${'startup.script_compile'.padEnd(45)} ${compile_time.toFixed(2).padStart(10)} ms`);
    record('startup.script_compile', compile_time, 'ms');
}

/* ================================================================
 * 5. WebAssembly (Node.js native)
 * ================================================================ */

function bench_wasm() {
    console.log('\n=== 4. WebAssembly (Node.js native) ===');

    // Minimal WASM module: exports an add function
    const wasmBytes = new Uint8Array([
        0x00,0x61,0x73,0x6d, 0x01,0x00,0x00,0x00, // header
        0x01,0x07,0x01,0x60,0x02,0x7f,0x7f,0x01,0x7f, // func type: (i32,i32)->i32
        0x03,0x02,0x01,0x00, // function section
        0x07,0x07,0x01,0x03,0x61,0x64,0x64,0x00,0x00, // export "add"
        0x0a,0x09,0x01,0x07,0x00,0x20,0x00,0x20,0x01,0x6a,0x0b // code: local.get 0 + local.get 1 + i32.add
    ]);

    // Validate
    bench_fn('wasm.validate', () => WebAssembly.validate(wasmBytes), 50000);

    // Compile
    bench_fn('wasm.compile', () => WebAssembly.compile(wasmBytes), 5000);

    // Instantiate
    const module = new WebAssembly.Module(wasmBytes);
    bench_fn('wasm.instantiate', () => new WebAssembly.Instance(module), 10000);

    // Call exported function
    const instance = new WebAssembly.Instance(module);
    bench_fn('wasm.call_add', () => instance.exports.add(1, 2), 100000);

    // Mandelbrot in WASM
    // (Using JS fallback — real WASM mandelbrot would need a .wasm file)
    // For now, measure the overhead of calling a WASM function in a loop
    bench_fn('wasm.call_add_1M', () => { let s=0; for(let i=0;i<1000000;i++) s=instance.exports.add(s,i); return s; }, 10);

    // Memory
    bench_fn('wasm.memory_create', () => new WebAssembly.Memory({initial:1}), 10000);
    const memory = new WebAssembly.Memory({initial: 1});
    bench_fn('wasm.memory_write', () => { new Uint8Array(memory.buffer)[0] = 42; }, 100000);
    bench_fn('wasm.memory_read', () => new Uint8Array(memory.buffer)[0], 100000);
}

/* ================================================================
 * Output JSON
 * ================================================================ */

function output_json() {
    console.log('\n=== JSON Results (nodejs) ===');
    console.log(JSON.stringify(results, null, 2));
}

/* ================================================================
 * Main
 * ================================================================ */

(async function main() {
    console.log('=== WAMR vs Node.js Performance Comparison ===');
    console.log(`Engine: Node.js ${process.version}\n`);

    bench_compute();
    bench_string();
    bench_array_object();
    bench_wasm();
    await bench_startup();

    output_json();
})();

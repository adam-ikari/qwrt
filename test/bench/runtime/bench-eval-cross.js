/* qwrt runtime-perf harness — cross-runtime R6 eval throughput (integer add
 * / closure call / string concat). Shared verbatim by qwrt / node / bun /
 * tjs so every runtime runs identical code; driven by
 * test/bench_cross_runtime.py; prints one JSON line.
 *
 * Interpreter vs JIT is exactly the positioning difference under test, so
 * the iteration/sample schedule (warmup 3 + samples 5, median M ops/s) is
 * generous enough to fully warm V8/JSC-style JITs without making qwrt/tjs
 * runs pathological.
 *
 * Args are read via each runtime's own convention: process.argv for
 * node/bun, globalThis.arguments for qwrt/tjs.
 *
 * Usage: <bin> test/bench/runtime/bench-eval-cross.js [iterations] [samples]
 */
var ARGV;
if (typeof tjs !== 'undefined' && tjs && tjs.args) {
  /* txiki.js: tjs.args = full argv incl. binary + subcommand. Slice after
   * this script's own path so the harness can pass iters/samples the same
   * way for every runtime. */
  ARGV = [];
  var a = tjs.args;
  for (var i = 0; i < a.length; i++) {
    if (/bench-eval-cross\.js$/.test(a[i])) { ARGV = a.slice(i + 1); break; }
  }
} else if (typeof process !== 'undefined' && process && process.argv) {
  ARGV = process.argv.slice(2);
} else {
  ARGV = globalThis.arguments || [];
}
var ITERS = ARGV[0] ? parseInt(ARGV[0], 10) : 1000000;
var SAMPLES = ARGV[1] ? parseInt(ARGV[1], 10) : 5;
var WARMUP = 3;
var keepalive = setInterval(function () {}, 50);

function median(a) {
  var s = a.slice().sort(function (x, y) { return x - y; });
  var n = s.length;
  return n % 2 ? s[(n - 1) / 2] : (s[n / 2 - 1] + s[n / 2]) / 2;
}

function benchOp(fn) {
  var i, s;
  for (i = 0; i < WARMUP; i++) fn();
  var mops = [];
  for (s = 0; s < SAMPLES; s++) {
    var t0 = performance.now();
    fn();
    var dt = performance.now() - t0;
    if (dt <= 0) dt = 1e-9;
    mops.push((ITERS / (dt / 1000)) / 1e6);
  }
  return median(mops);
}

function benchInt() {
  var s = 0;
  for (var i = 0; i < ITERS; i++) s += i;
  return s;
}

function benchClosure() {
  var f = (function () { var base = 1; return function (x) { return base + x; }; })();
  var s = 0;
  for (var i = 0; i < ITERS; i++) s += f(i);
  return s;
}

function benchStr() {
  var s = '';
  for (var i = 0; i < ITERS; i++) s += 'x';
  return s;
}

console.log(JSON.stringify({
  mode: 'r6',
  iters: ITERS,
  samples: SAMPLES,
  int_mops: benchOp(benchInt),
  closure_mops: benchOp(benchClosure),
  str_mops: benchOp(benchStr),
}));
clearInterval(keepalive);

/* qwrt runtime-perf harness — R6 eval throughput (integer add / closure call
 * / string concat). Driven by test/bench_runtime.py; prints one JSON line.
 *
 * Pure CPU micro-benchmarks: CPU-dense work is backend-independent by the
 * multi-process model's judgment table §1.4, so a single backend suffices.
 * Each op: warmup 3 discarded runs + 5 sampled runs, median M ops/s.
 *
 * Usage: qwrt test/bench/runtime/bench-eval.js [iterations] [samples]
 */
var ITERS = globalThis.arguments[0] ? parseInt(globalThis.arguments[0], 10) : 1000000;
var SAMPLES = globalThis.arguments[1] ? parseInt(globalThis.arguments[1], 10) : 5;
var WARMUP = 3;
var keepalive = setInterval(function () {}, 50);

function median(a) {
  var s = a.slice().sort(function (x, y) { return x - y; });
  var n = s.length;
  return n % 2 ? s[(n - 1) / 2] : (s[n / 2 - 1] + s[n / 2]) / 2;
}

function benchOp(fn, tag) {
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
  int_mops: benchOp(benchInt, 'int'),
  closure_mops: benchOp(benchClosure, 'closure'),
  str_mops: benchOp(benchStr, 'str'),
}));
clearInterval(keepalive);

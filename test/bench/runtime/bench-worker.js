/* qwrt runtime-perf harness — worker subsystem metrics (R2/R2b/R3/R4/R5).
 *
 * Driven by test/bench_runtime.py (single JSON line on stdout as the last
 * output line — the bench_httpserver.py CI convention). Worker fixture URLs
 * are built from env QWRT_BENCH_DIR (set by the driver, default
 * 'test/bench/runtime') so the harness is cwd-independent.
 *
 * Usage:  qwrt test/bench/runtime/bench-worker.js <mode> [params...]
 *   r2 [warmup] [samples]        — spawn (R2) + terminate (R2b) latency, µs
 *   r3 [warmup] [samples] [payloadBytes] — postMessage echo round-trip, µs
 *   r4 [runs] [n] [payloadBytes] — N-message echo throughput, msg/s
 *   r5                           — worker VmHWM (KB) self-report
 *
 * Timing source: performance.now() (pal.hrtime → uv_hrtime, ns precision).
 * Sample policy per design §3.3: warmup (discarded) → sample → median (+p95
 * for R3/R4).
 *
 * NOTE on slot reuse: QWRT_MAX_WORKERS=16. The THREAD backend never releases
 * a worker slot until runtime teardown (qwrt_worker_terminate is fire-and-
 * forget; only the PROCESS backend reaps on pipe EOF, asynchronously). Hence
 * R2 must keep warmup+samples <= 16, and every R2 iteration yields to the
 * event loop after terminate() so PROCESS reap callbacks can run. This is a
 * documented deviation from design §2's "warmup 5 + sample 20".
 */
var BENCH_DIR = (globalThis.env && globalThis.env.QWRT_BENCH_DIR) || 'test/bench/runtime';
var keepalive = setInterval(function () {}, 50);

function workerUrl(name) { return 'file://' + BENCH_DIR + '/' + name; }

function yieldLoop() {
  return new Promise(function (resolve) { setTimeout(resolve, 0); });
}

function median(a) {
  if (!a.length) return NaN;
  var s = a.slice().sort(function (x, y) { return x - y; });
  var n = s.length;
  return n % 2 ? s[(n - 1) / 2] : (s[n / 2 - 1] + s[n / 2]) / 2;
}

function p95(a) {
  if (!a.length) return NaN;
  var s = a.slice().sort(function (x, y) { return x - y; });
  return s[Math.min(s.length - 1, Math.floor(s.length * 0.95))];
}
/* ── R2/R2b: time-to-first-message + terminate latency ──
 * Unified "worker usable" semantics: the ready moment is when the worker
 * answers its first postMessage echo (not the backend's own handshake).
 * The two backends define "ready" differently at the C level — THREAD's
 * spawnWorker blocks until the worker thread finished polyfill injection +
 * script eval; PROCESS's handshake returns before the child even loads the
 * polyfill — so raw spawn times are not comparable. Measuring to the first
 * echo makes both measure the same thing: time until a message can round-
 * trip. The raw new Worker() time is still reported (spawn_raw_*) to expose
 * the backend ready-semantics difference.
 *
 * Each iteration also times w.terminate() (R2b, record-only). Uses
 * worker-echo.js (needs an onmessage handler to answer the ping). Yields
 * twice after terminate so the PROCESS backend's reap callback frees the
 * slot; THREAD never releases slots (warmup+samples must stay <= 16). */
async function benchR2(warmup, samples) {
  var url = workerUrl('worker-echo.js');
  var readyUs = [], spawnUs = [], termUs = [];
  var total = warmup + samples;
  for (var i = 0; i < total; i++) {
    var t0 = performance.now();
    var w = new Worker(url);
    var spawnDt = (performance.now() - t0) * 1000;
    await oneEcho(w, null);
    var readyDt = (performance.now() - t0) * 1000;
    if (i >= warmup) {
      spawnUs.push(spawnDt);
      readyUs.push(readyDt);
    }
    var t1 = performance.now();
    w.terminate();
    var termDt = (performance.now() - t1) * 1000;
    if (i >= warmup) termUs.push(termDt);
    await yieldLoop();
    await yieldLoop();
  }
  return {
    mode: 'r2',
    ready_median_us: median(readyUs),
    ready_p95_us: p95(readyUs),
    spawn_raw_median_us: median(spawnUs),
    terminate_median_us: median(termUs),
    terminate_p95_us: p95(termUs),
    n: readyUs.length,
  };
}

/* single echo round-trip: set onmessage → postMessage → resolve on echo */
function oneEcho(w, buf) {
  return new Promise(function (resolve) {
    w.onmessage = function () { resolve(); };
    w.postMessage(buf);
  });
}

/* ── R3: postMessage round-trip latency, per-op sampling ──
 * payload is a preallocated ArrayBuffer (cloned per postMessage, never
 * detached — no in-loop allocation noise). One worker, reused for all ops.
 * Raw per-op samples (samples_us) are also emitted so the Python driver can
 * pool median/p95 across worker respawns: a PROCESS worker drops out after
 * ~100 sustained messages (HEAD bug), so the driver splits R3 into batches
 * fit in one worker and pools the raw samples. */
async function benchR3(warmup, samples, payloadBytes) {
  var w = new Worker(workerUrl('worker-echo.js'));
  var buf = new ArrayBuffer(payloadBytes);
  var i;
  for (i = 0; i < warmup; i++) await oneEcho(w, buf);
  var us = [];
  for (i = 0; i < samples; i++) {
    var t0 = performance.now();
    await oneEcho(w, buf);
    us.push((performance.now() - t0) * 1000);
  }
  w.terminate();
  await yieldLoop();
  await yieldLoop();
  return {
    mode: 'r3',
    payload: payloadBytes,
    median_us: median(us),
    p95_us: p95(us),
    n: us.length,
    samples_us: us.map(function (x) { return Math.round(x * 100) / 100; }),
  };
}

/* one throughput run: push n messages synchronously, stop when n echoes in */
function oneThroughput(w, buf, n) {
  return new Promise(function (resolve) {
    var t0 = performance.now();
    var rcvd = 0;
    w.onmessage = function () {
      rcvd++;
      if (rcvd === n) {
        var dt = performance.now() - t0;
        resolve(n / (dt / 1000));
      }
    };
    for (var i = 0; i < n; i++) w.postMessage(buf);
  });
}

/* ── R4: postMessage throughput, repeated runs → median + p95 msg/s ── */
async function benchR4(runs, n, payloadBytes) {
  var w = new Worker(workerUrl('worker-echo.js'));
  var buf = new ArrayBuffer(payloadBytes);
  var rate = [];
  for (var r = 0; r < runs; r++) rate.push(await oneThroughput(w, buf, n));
  w.terminate();
  await yieldLoop();
  await yieldLoop();
  return {
    mode: 'r4',
    median_msgps: median(rate),
    p95_msgps: p95(rate),
    n: n,
    runs: runs,
  };
}

/* ── R5: worker peak RSS (VmHWM) self-report, KB ──
 * Worker reads its own /proc/self/status (async pal.fsRead — fsReadSync
 * sizes via fstat which returns 0 for procfs, so it cannot read /proc).
 * After reporting, hold the main process alive ~200ms so the Python driver
 * can sample the parent's own VmHWM from /proc/<pid>/status. */
async function benchR5() {
  var w = new Worker(workerUrl('worker-rss.js'));
  var vmhwm = await new Promise(function (resolve) {
    w.onmessage = function (ev) {
      resolve(ev && ev.data ? ev.data.vmhwm_kb : -1);
    };
    w.postMessage('rss?');
  });
  w.terminate();
  await yieldLoop();
  await yieldLoop();
  await new Promise(function (resolve) { setTimeout(resolve, 200); });
  return { mode: 'r5', worker_vmhwm_kb: vmhwm };
}

function parseIntArg(i, dflt) {
  var v = globalThis.arguments[i];
  if (v === undefined || v === '') return dflt;
  var n = parseInt(v, 10);
  return isNaN(n) ? dflt : n;
}

async function main() {
  var mode = globalThis.arguments[0];
  var out;
  if (mode === 'r2') {
    out = await benchR2(parseIntArg(1, 3), parseIntArg(2, 12));
  } else if (mode === 'r3') {
    out = await benchR3(parseIntArg(1, 50), parseIntArg(2, 500),
                        parseIntArg(3, 0));
  } else if (mode === 'r4') {
    out = await benchR4(parseIntArg(1, 5), parseIntArg(2, 10000),
                        parseIntArg(3, 64));
  } else if (mode === 'r5') {
    out = await benchR5();
  } else {
    out = { mode: mode, error: 'unknown mode' };
  }
  console.log(JSON.stringify(out));
  clearInterval(keepalive);
}

main();

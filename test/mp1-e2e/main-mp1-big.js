/* M-P1 process-backend e2e — phase 1b: >64KB payload round-trip.
 *
 * A payload this size makes the IPC frame (payload + 4-byte length + 40-byte
 * envelope) exceed one pipe read chunk (QZ_IPC_READ_BUF_SIZE, = libuv's
 * UV__IO_MAX_BYTES), so the receiving side must accumulate a partial frame and
 * wait for the rest. Regression guard: rt_main.c process_rx lost its
 * "not enough bytes yet" check in M-P4 (446d7ea3) — the byte counter then
 * underflowed on the partial read, memmove'd past the accumulator and
 * corrupted the heap (64KB worker round-trip hang). */
var WORKER_URL = 'file:///home/gem/project/qzjs/test/mp1-e2e/worker_echo.js';
var keepalive = setInterval(function () {}, 50);
var N = 128 * 1024;                     /* frame = N + 44 > 64KB chunk */
var buf = new ArrayBuffer(N);
var view = new Uint8Array(buf);
view[0] = 7;
view[N - 1] = 9;

var w = new Worker(WORKER_URL);
w.onmessage = function (ev) {
  var back = new Uint8Array(ev.data);
  console.log('big:' + back.length + ':' + back[0] + ':' + back[N - 1]);
  w.terminate();
  setTimeout(function () {
    console.log('DONE');
    clearInterval(keepalive);
  }, 100);
};
w.postMessage(buf);

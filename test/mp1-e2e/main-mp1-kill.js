/* M-P1 process-backend e2e — phase 2: hard-kill the child mid-flight.
 * The shell SIGKILLs the qwrt-rt child after READY. JS gets no death event
 * (crash notification is M-P4), so we re-spawn on a timer: if the crashed
 * worker's slot was NOT released (I1), `new Worker` would throw once all 16
 * slots leak — here it must succeed. Also proves the parent survives the dead
 * peer (C2: writes use MSG_NOSIGNAL, no SIGPIPE abort). */
var WORKER_URL = 'file:///home/gem/project/qwrt/test/mp1-e2e/worker_echo.js';
var keepalive = setInterval(function () {}, 50);

var w = new Worker(WORKER_URL);
console.log('READY');

/* Write to the (soon-to-be-dead) worker: exercises the EPIPE path. Either the
 * slot is still live (write → EPIPE → returns false, no crash) or already
 * reaped (workerPost throws) — both are caught. */
setTimeout(function () {
  try { w.postMessage('after-kill'); } catch (e) { /* slot released */ }
}, 800);

setTimeout(function () {
  var w2 = new Worker(WORKER_URL);   /* must succeed: slot was recycled (I1) */
  console.log('RESPAWNED');
  w2.postMessage('ping2');
  w2.onmessage = function (ev) {
    console.log('echo2:' + ev.data);
    w2.terminate();
    console.log('DONE');
    clearInterval(keepalive);
  };
}, 2000);

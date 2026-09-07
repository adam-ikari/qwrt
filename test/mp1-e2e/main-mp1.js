/* M-P1 process-backend e2e — phase 1: spawn a PROCESS Worker, do a
 * structured-clone postMessage round-trip across the real process boundary,
 * then gracefully terminate (3-tier §9.2, tier-1 CONTROL{shutdown}).
 * Proves fork+exec qwrt-rt → handshake → bidirectional message flow →
 * graceful child exit → parent loop returns to idle. */
var WORKER_URL = 'file:///home/gem/project/qwrt/test/mp1-e2e/worker_echo.js';
var keepalive = setInterval(function () {}, 50);

var w = new Worker(WORKER_URL);
w.onmessage = function (ev) {
  console.log('echo:' + ev.data);
  w.terminate();
  setTimeout(function () {
    console.log('DONE');
    clearInterval(keepalive);
  }, 100);
};
w.postMessage('ping');

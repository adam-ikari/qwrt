/* M-P4 fixture：worker 崩溃 → 主RT dispatch error 事件（§9.3），主RT 继续存活
 * 并能 spawn 新 worker（无 stale 状态）。 */
var keep = setInterval(function () {}, 50);
var w = new Worker('file://__ROOT__/test/mp4-e2e/worker_keepalive.js');
w.onerror = function (e) {
  console.log('ONERROR:' + (e && e.message));
  /* 崩溃后主RT 仍健全：新 worker 正常往返 */
  var w2 = new Worker('file://__ROOT__/test/mp4-e2e/worker_echo.js');
  w2.onmessage = function (e2) {
    console.log('after-crash:' + e2.data);
    if (String(e2.data).indexOf('echo:') === 0) {
      console.log('CRASH-DONE');
      clearInterval(keep);
    }
  };
  w2.postMessage('ping');
};
w.onmessage = function () { console.log('WORKER-READY'); };

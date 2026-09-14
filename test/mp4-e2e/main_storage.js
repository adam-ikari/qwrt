/* M-P4 fixture：跨进程 storage e2e（主RT 所有者 + PROCESS worker 代理）。 */
var keep = setInterval(function () {}, 50);
localStorage.clear();
localStorage.setItem('k1', 'v1');
var w = new Worker('file://__ROOT__/test/mp4-e2e/worker_storage.js');
w.onmessage = function (e) {
  console.log('xproc:' + e.data);
  console.log('main-sees:' + localStorage.getItem('wkey'));
  console.log('main-len:' + localStorage.length);
  console.log('main-removed:' + localStorage.getItem('k1'));
  console.log('STORAGE-DONE');
  clearInterval(keep);
};
w.postMessage('go');

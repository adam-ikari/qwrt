/* 嵌套 spawn e2e：主RT 脚本。起 worker（子），子起孙 worker，往返消息。
 * 期望 stdout：nested:child-grand:ping → DONE。 */
var WORKER_URL = 'file:///home/gem/project/amoib/test/nested-e2e/worker_spawn_child.js';
var keepalive = setInterval(function () {}, 50);

var w = new Worker(WORKER_URL);
w.onmessage = function (ev) {
  console.log('nested:' + ev.data);
  w.terminate();
  setTimeout(function () {
    console.log('DONE');
    clearInterval(keepalive);
  }, 150);
};
w.onerror = function (ev) {
  console.log('ERROR:' + (ev && ev.message ? ev.message : ev));
  clearInterval(keepalive);
};
w.postMessage('ping');

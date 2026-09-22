/* 嵌套 spawn e2e：worker（子进程）在自身进程内 new Worker 起孙 worker 进程，
 * 把从父收到的消息转发给孙，再把孙的回复上行给父。证明 worker 进程具备
 * spawn 能力（§1.1 树形拓扑：任意 worker 亦可 spawn 子 worker）。 */
var WORKER_URL = 'file:///home/gem/project/amoib/test/nested-e2e/worker_grand_echo.js';

var g = new Worker(WORKER_URL);
g.onmessage = function (ev) {
  self.postMessage('child-' + ev.data);
};
self.onmessage = function (ev) {
  g.postMessage(ev.data);
};

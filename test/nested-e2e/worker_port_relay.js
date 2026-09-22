/* 嵌套 MessagePort e2e：worker（子）。把从父转移来的 port 再转移给孙 worker
 * （§8.2 多跳转移 → 子进程登记路由表项，对端仍按旧端点发来的帧被改指转发）。 */
var g = new Worker('file:///home/gem/project/qzjs/test/nested-e2e/worker_grand_port_echo.js');
g.onmessage = function (e) {
  if (e.data === 'reforwarded') self.postMessage('reforwarded');
};
onmessage = function (e) {
  var p = e.ports && e.ports[0];
  if (!p) { self.postMessage({ err: 'relay: no port' }); return; }
  g.postMessage('take', [p]);
};

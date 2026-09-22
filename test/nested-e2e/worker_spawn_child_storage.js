/* 嵌套 storage e2e：worker（子）。起孙 worker；孙的 localStorage 请求经本进程
 * 中继上行到主RT 所有者（§10.2 单所有者代理的嵌套延伸）。 */
var g = new Worker('file:///home/gem/project/amoib/test/nested-e2e/worker_grand_storage.js');
g.onmessage = function (e) { self.postMessage('grand:' + e.data); };
onmessage = function () { g.postMessage('go'); };

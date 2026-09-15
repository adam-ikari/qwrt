/* 嵌套 spawn e2e：worker（子），起孙 worker 并常驻——死亡级联用例的中间层。 */
var g = new Worker('file:///home/gem/project/qwrt/test/nested-e2e/worker_grand_echo.js');
self.onmessage = function (e) { g.postMessage(e.data); };
setInterval(function () {}, 100);

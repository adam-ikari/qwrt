/* 嵌套 spawn e2e：孙 worker（worker 的子进程）。echo 语义加前缀，便于区分层级。 */
self.onmessage = function (ev) {
  self.postMessage('grand:' + ev.data);
};

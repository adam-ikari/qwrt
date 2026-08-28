// A2 fixture: 运行中(定时器回调)抛错。self.onerror 捕获 reportError 派发的
// ErrorEvent 并回报父;随后 worker 继续处理消息(存活验证)。
self.onerror = function (e) {
  postMessage({ workerErr: e.message });
};
onmessage = function (e) {
  if (e.data === 'boom-later') {
    setTimeout(function () { throw new Error('async-boom'); }, 10);
    postMessage('scheduled');
  } else {
    postMessage('alive:' + e.data);
  }
};

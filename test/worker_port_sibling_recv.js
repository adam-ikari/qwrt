// M-P3 fixture: 收到父转移来的 MessagePort 后监听其消息，结果转报父进程
// （gtest/e2e 据此断言跨进程 sibling 接力成功）。
onmessage = function (e) {
  var p = e.ports && e.ports[0];
  if (!p) { postMessage({ err: 'no port in event.ports' }); return; }
  p.onmessage = function (e2) { postMessage('recv:' + e2.data); };
};

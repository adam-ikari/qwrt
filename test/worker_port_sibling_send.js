// M-P3 fixture: 收到父转移来的 MessagePort 后，经它发一条消息。
// 对端可能在另一个 worker 进程 —— 消息须由主RT（LCA）按帧头 dest 端点接力转发。
onmessage = function (e) {
  var p = e.ports && e.ports[0];
  if (!p) { postMessage({ err: 'no port in event.ports' }); return; }
  p.postMessage('sib-hello');
};

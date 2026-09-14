// M-P3 fixture（id 撞车回归）：worker 自建 MessageChannel 的同时还持有父转移来的
// port。两个独立进程各自从 1 起分配本地 id，id 会撞 —— 只有 (owner,id) 组合身份
// 能区分，仅按 id 建表会让自建 channel 覆盖转移来的 port 表项（echo 丢失）。
onmessage = function (e) {
  var own = new MessageChannel();
  var ownGot = [];
  own.port2.onmessage = function (e2) { ownGot.push(String(e2.data)); };
  own.port1.postMessage('self');          // 本地通道自测：两者都须可用
  var p = e.ports && e.ports[0];
  if (!p) { postMessage({ err: 'no port in event.ports' }); return; }
  p.onmessage = function (e2) {
    p.postMessage('echo:' + e2.data + '|own=' + ownGot.join(','));
  };
  p.postMessage('ready');
};

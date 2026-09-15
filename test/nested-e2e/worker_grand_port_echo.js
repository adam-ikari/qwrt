/* 嵌套 MessagePort e2e：孙 worker。收下父再转移来的 port，监听并回 echo。 */
onmessage = function (e) {
  var p = e.ports && e.ports[0];
  if (!p) { postMessage({ err: 'no port in event.ports' }); return; }
  p.onmessage = function (e2) { p.postMessage('echo:' + e2.data); };
  postMessage('reforwarded');
};

/* 嵌套 MessagePort e2e：主RT。port2 留本地、port1 经 worker 再转移到孙，
 * 消息 main↔孙 跨两级（中继 = worker，改指转发），期望 G2M:echo:hello。 */
var keep = setInterval(function () {}, 50);
var ch = new MessageChannel();
var w = new Worker('file:///home/gem/project/amoib/test/nested-e2e/worker_port_relay.js');
ch.port2.onmessage = function (e) {
  console.log('G2M:' + e.data);
  if (String(e.data).indexOf('echo:') === 0) {
    console.log('DONE');
    w.terminate();
    clearInterval(keep);
  }
};
w.onmessage = function (e) {
  if (e.data === 'reforwarded') ch.port2.postMessage('hello');
};
w.postMessage('init', [ch.port1]);

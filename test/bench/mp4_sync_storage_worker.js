/* 4MB sync storage RPC 基准：worker 侧计时 setItem 往返，postMessage 结果 */
onmessage = function () {
  var size = (self.benchSize || 4 * 1024 * 1024);
  var s = 'x'.repeat(size);
  var t0 = Date.now();
  localStorage.setItem('bench', s);
  var t1 = Date.now();
  var chk = localStorage.getItem('bench');
  var t2 = Date.now();
  var v = localStorage.getItem('bench');
  var ok = v === s;
  localStorage.removeItem('bench');
  postMessage('set=' + (t1 - t0) + 'ms get=' + (t2 - t1) + 'ms ok=' + ok);
};
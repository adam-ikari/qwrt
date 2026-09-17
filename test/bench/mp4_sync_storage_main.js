/* 主RT 侧：spawn worker 跑 sync storage 基准，打印总耗时。
 * 注意：主RT 必须先 init storage（否则 worker 首个 storage RPC 挂起——预存
 * 行为，见 brain 记录）。 */
var keep = setInterval(function () {}, 50);
localStorage.setItem('__bench_init', '1');
var w = new Worker('file://__ROOT__/test/bench/mp4_sync_storage_worker.js');
var t0 = Date.now();
w.onmessage = function (e) {
  console.log('wall=' + (Date.now() - t0) + 'ms ' + e.data);
  clearInterval(keep);
  w.terminate();
};
w.postMessage('go');
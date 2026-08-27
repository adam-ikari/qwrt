/* qwrt example: worker 编排（真线程并行计算）
 *
 * 演示在宿主脚本里用多个真线程 Web Worker 并行计算：
 *
 *   1. 并行 — 同时创建 3 个 worker，各自独立线程互不阻塞
 *   2. 分发 — 每个 worker 收到一个质数计数任务（区间不同）
 *   3. 聚合 — 全部 worker 回包后，父线程汇总结果并输出
 *
 * 运行（仓库根，worker 脚本路径按相对 cwd 解析）：
 *   ./build-ws/qwrt examples/worker-orchestrate/orchestrate.js
 *
 * 依赖的能力（qwrt 内置）：
 *   new Worker('file://...')   — 真线程 worker（仅支持 file://）
 *   w.postMessage / w.onmessage — 双向消息（结构化克隆）
 *   w.terminate()              — 显式回收 worker 线程
 *
 * 注意：qwrt CLI 在无待处理异步工作（libuv 句柄）时退出进程
 * （wait_idle 语义），因此示例用保底 setTimeout 让事件循环保持
 * 活跃，直至全部 worker 回包完成。
 */

var WORKER_URL = 'file://examples/worker-orchestrate/task-worker.js';
var workers = [];          /* 所有存活的 worker，聚合完成后统一回收 */
var results = [];          /* 每个任务的回包结果（按分发序号） */
var pending;               /* 未回包任务数 */

/* 三个并行任务：统计 [lo, hi] 区间内质数个数（worker 侧埃氏筛） */
var tasks = [
  { type: 'countPrimes', lo: 2,      hi: 50000 },
  { type: 'countPrimes', lo: 50001,  hi: 100000 },
  { type: 'countPrimes', lo: 100001, hi: 150000 },
];
function aggregate() {
  clearTimeout(guardTimer);          /* 聚合完成，撤掉保底 timer */
  var total = 0;
  for (var i = 0; i < results.length; i++) {
    var r = results[i];
    console.log('[host] worker#' + (i + 1) + '  [' + r.lo + ', ' + r.hi + '] -> ' +
                r.count + ' 个质数');
    total += r.count;
  }
  console.log('[host] 聚合结果：2..150000 共 ' + total + ' 个质数');
  for (var j = 0; j < workers.length; j++) workers[j].terminate();
}

console.log('[host] 并行分发 ' + tasks.length + ' 个任务到独立 worker...');

/* 每任务一个 worker：回包即记结果，最后一个回包触发聚合 */
tasks.forEach(function (task, idx) {
  var w = new Worker(WORKER_URL);
  workers.push(w);
  w.onmessage = function (e) {
    if (e.data.type === 'error') {
      console.log('[host] worker#' + (idx + 1) + ' 出错: ' + e.data.error);
      return;
    }
    results[idx] = e.data;
    if (--pending === 0) aggregate();
  };
  w.onerror = function (err) {
    console.log('[host] worker#' + (idx + 1) + ' 异常: ' + err.message);
  };
  w.postMessage(task);
});
pending = tasks.length;

/* 保底 timer：保持事件循环活跃等待 worker 回包；超时则报错退出。
 * 聚合完成后由 aggregate() 清除，进程随即正常退出。 */
var guardTimer = setTimeout(function () {
  if (pending > 0) {
    console.log('[host] 超时：' + pending + ' 个任务未回包');
  }
  for (var i = 0; i < workers.length; i++) workers[i].terminate();
}, 10000);

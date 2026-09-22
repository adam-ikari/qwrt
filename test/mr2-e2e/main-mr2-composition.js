/* M-R2 多 RT 组合模型 e2e（PROCESS 后端）—— §14.3 场景在真实进程 worker 上回归：
 *   · contexts ×2（qzContext.spawn）与 workers ×2（进程后端）正交并存
 *   · ctx suspend/resume 与 worker postMessage 交错：挂起期间照常收发，恢复后
 *     两轴结果都对（无死锁：脚本跑到底；消息一条不丢）
 *   · ctx destroy 后 worker 消息照常派发
 *   · worker 归 rt：terminate 后优雅退出，无残留子进程
 * 输出契约（test/test_mr2_composition_e2e.sh 逐字比对）：
 *   echoes:6
 *   DONE
 * 任一断言失败 → FAIL:<原因> 且不打印 DONE。
 */
var DIR = 'file:///home/gem/project/qzjs/test/mr2-e2e/';
var STATE = '/tmp/qzjs-mr2-state.bin';
var keepalive = setInterval(function () {}, 50);
var seen = Object.create(null);
var order = Object.create(null);   /* 各 worker 的到达顺序（FIFO 复核） */
var failed = false;

function fail(msg) {
  if (failed) return;
  failed = true;
  console.log('FAIL:' + msg);
  clearInterval(keepalive);
  if (typeof w1 !== 'undefined') w1.terminate();
  if (typeof w2 !== 'undefined') w2.terminate();
}

function note(slot, data) {
  var k = slot + ':' + data;
  seen[k] = 1;
  (order[slot] = order[slot] || []).push(String(data));
}

var w1 = new Worker(DIR + 'worker_echo.js');
var w2 = new Worker(DIR + 'worker_echo.js');
w1.onmessage = function (e) { note('w1', e.data); };
w1.onerror = function (e) { fail('w1 error ' + JSON.stringify(e.data)); };
w2.onmessage = function (e) { note('w2', e.data); };
w2.onerror = function (e) { fail('w2 error ' + JSON.stringify(e.data)); };

var c1 = qzContext.spawn("globalThis.tag = 'c1';");
var c2 = qzContext.spawn("globalThis.tag = 'c2';");
if (c1 !== 1 || c2 !== 2) fail('spawn ids ' + c1 + ',' + c2);

/* 交错：worker 消息与 ctx 挂起/恢复/销毁穿插（同一同步段内完成）
 *   a1/b1 — 基线：两个 worker 的消息域各自独立
 *   suspend(c1) 期间的 a2 — ctx 挂起不波及 worker（消息照常收发）
 *   resume(c1) 后的 b2    — 恢复不改 worker 语义
 *   destroy(c2) 后的 a3/b3 — ctx 销毁后 worker 消息照常派发（§14.3） */
w1.postMessage('a1');
w2.postMessage('b1');
qzContext.suspend(c1, STATE);
w1.postMessage('a2');
qzContext.resume(c1, '', STATE);
w2.postMessage('b2');
qzContext.destroy(c2);
w1.postMessage('a3');
w2.postMessage('b3');

var want = ['w1:a1', 'w2:b1', 'w1:a2', 'w2:b2', 'w1:a3', 'w2:b3'];
var wantOrder = {w1: ['a1', 'a2', 'a3'], w2: ['b1', 'b2', 'b3']};
var waited = 0;

var poll = setInterval(function () {
  waited += 100;
  var missing = [];
  for (var i = 0; i < want.length; i++) if (!seen[want[i]]) missing.push(want[i]);
  if (!missing.length) { finish(); return; }
  if (waited >= 10000) fail('missing ' + missing.join(','));
}, 100);

function finish() {
  clearInterval(poll);
  /* 每 worker 内的消息 FIFO：到达顺序必须等于 postMessage 顺序 */
  var slots = ['w1', 'w2'];
  for (var s = 0; s < slots.length; s++) {
    var got = (order[slots[s]] || []).join(',');
    var exp = wantOrder[slots[s]].join(',');
    if (got !== exp) { fail(slots[s] + ' order ' + got + ' != ' + exp); return; }
  }
  console.log('echoes:' + want.length);
  w1.terminate();
  w2.terminate();
  setTimeout(function () {
    console.log('DONE');
    clearInterval(keepalive);
  }, 100);
}

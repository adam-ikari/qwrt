/* 嵌套 spawn e2e：主RT 脚本，起 worker→孙 且监听 worker 的 error（§9.3）。
 * 用于死亡级联用例：worker 被杀时主RT 须 dispatch onerror 且自身存活。 */
var keep = setInterval(function () {}, 100);
var w = new Worker('file:///home/gem/project/qwrt/test/nested-e2e/worker_spawn_child_hold.js');
w.onerror = function (e) {
  console.log('MAINRT-ONERROR:' + (e && e.message));
};
console.log('READY');

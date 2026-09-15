/* 嵌套 storage e2e：主RT（owner）。 */
var keep = setInterval(function () {}, 50);
localStorage.clear();
localStorage.setItem('gk1', 'gv1');
var w = new Worker('file:///home/gem/project/qwrt/test/nested-e2e/worker_spawn_child_storage.js');
w.onmessage = function (e) {
  console.log(e.data);
  console.log('main-sees-gkey:' + localStorage.getItem('gkey'));
  console.log('NESTED-STORAGE-DONE');
  clearInterval(keep);
  w.terminate();
};
w.postMessage('go');

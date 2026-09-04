/* SW-0 e2e — 4 场景顺序覆盖：①register→activated ②install→activate 顺序
 * ③postMessage 往返 ④同 URL 重注册替换（旧 redundant + controllerchange）。
 * keepalive interval 必需：CLI eval 返回后 wait_idle 需要 loop 上有活动
 * handle，纯 promise 链会被判 idle 提前 teardown。 */
var SW_URL = 'file:///home/gem/project/qwrt/test/sw-e2e/sw.js';
var keepalive = setInterval(function () {}, 50);
var ccCount = 0;
navigator.serviceWorker.addEventListener('controllerchange', function () {
  ccCount++;
});

navigator.serviceWorker.register(SW_URL).then(function () {
  return navigator.serviceWorker.ready;
}).then(function (reg) {
  var c = navigator.serviceWorker.controller;
  console.log('p1: state=' + c.state + ' scope=' + reg.scope);
  return new Promise(function (resolve) {
    c.onmessage = function (ev) {
      console.log('p3: ' + ev.data);
      resolve();
    };
    c.postMessage('ping');
  });
}).then(function () {
  var first = navigator.serviceWorker.controller;
  return navigator.serviceWorker.register(SW_URL).then(function () {
    return new Promise(function (resolve) {
      var tries = 0;
      var poll = setInterval(function () {
        tries++;
        var cur = navigator.serviceWorker.controller;
        if ((cur && cur !== first) || tries > 100) {
          clearInterval(poll);
          console.log('p4: old=' + first.state + ' new=' + (cur ? cur.state : 'null') + ' cc=' + ccCount);
          console.log('DONE');
          clearInterval(keepalive);
        }
      }, 10);
    });
  });
}).catch(function (e) {
  console.log('FAIL: ' + (e && e.message));
  clearInterval(keepalive);
});

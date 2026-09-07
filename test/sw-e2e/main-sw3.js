/* SW-3 e2e — 更新机制三验证门：
 * ①同 URL 字节未变 → update() 不触发 install，返回同一 registration、controller 不变
 * ②改写脚本（字节变化）→ update() 走 install→activate→替换旧 SW
 * ③新 SW install/activating 期间旧 SW 仍拦截 fetch（无控制真空）
 * 字节变化通过 qwrt.fs.writeFile 改写 SW 脚本文件制造；版本 B 的 activate
 * 用 waitUntil 延迟 200ms，提供观察窗口。
 * keepalive interval 必需：CLI eval 返回后 wait_idle 需要 loop 上有活动 handle。 */
var SW_FILE = '/home/gem/project/qwrt/test/sw-e2e/sw-sw3.js';
var SW_URL = 'file://' + SW_FILE;
var SW_B_FILE = '/home/gem/project/qwrt/test/sw-e2e/sw-sw3-b.js';
var keepalive = setInterval(function () {}, 50);
var ccCount = 0;
navigator.serviceWorker.addEventListener('controllerchange', function () {
  ccCount++;
});

function waitCC(target) {
  return new Promise(function (resolve) {
    if (ccCount >= target) { resolve(); return; }
    var poll = setInterval(function () {
      if (ccCount >= target) { clearInterval(poll); resolve(); }
    }, 10);
  });
}

function fetchWho() {
  return fetch('http://127.0.0.1:18433/who').then(function (res) {
    return res.text();
  });
}

navigator.serviceWorker.register(SW_URL).then(function (reg1) {
  return navigator.serviceWorker.ready.then(function () { return reg1; });
}).then(function (reg1) {
  var c = navigator.serviceWorker.controller;
  console.log('p1: state=' + c.state + ' cc=' + ccCount);
  /* 门1：字节未变 → update() 不 install、同一 registration、controller 不变 */
  var before = navigator.serviceWorker.controller;
  return reg1.update().then(function (r2) {
    var after = navigator.serviceWorker.controller;
    console.log('p2: sameReg=' + (r2 === reg1) + ' sameCtrl=' + (before === after) + ' cc=' + ccCount);
    /* 改写脚本 → 字节变化 */
    return qwrt.fs.readFile(SW_B_FILE).then(function (code) {
      return qwrt.fs.writeFile(SW_FILE, code);
    }).then(function () {
      return reg1.update();
    }).then(function () {
      /* 门3：B install 已 done、activate 未完成（200ms waitUntil）→ 旧 SW(A) 仍拦截 */
      return fetchWho().then(function (body) {
        var cur = navigator.serviceWorker.controller;
        console.log('p3: during=' + body + ' ctrl=' + cur.state);
      });
    });
  });
}).then(function () {
  /* 门2：等 B activate 完成替换 → 新 SW 拦截 */
  return waitCC(2).then(function () {
    var c = navigator.serviceWorker.controller;
    return fetchWho().then(function (body) {
      console.log('p4: after=' + body + ' ctrl=' + c.state + ' cc=' + ccCount);
      /* B 已激活：字节未变 → update 不再 install */
      var before = navigator.serviceWorker.controller;
      return navigator.serviceWorker.getRegistration().then(function (reg) {
        return reg.update().then(function (r3) {
          var after = navigator.serviceWorker.controller;
          console.log('p5: sameReg=' + (r3 === reg) + ' sameCtrl=' + (before === after) + ' cc=' + ccCount);
          console.log('DONE');
          clearInterval(keepalive);
        });
      });
    });
  });
}).catch(function (e) {
  console.log('FAIL: ' + (e && e.message));
  clearInterval(keepalive);
});

/* SW-3 e2e — 更新机制三验证门：
 * ①同 URL 字节未变 → update() 不触发 install，返回同一 registration、controller 不变
 * ②改写脚本（字节变化）→ update() 走 install→activate→替换旧 SW
 * ③新 SW install/activating 期间旧 SW 仍拦截 fetch（无控制真空）
 * 字节变化通过 qwrt.fs.writeFile 改写 SW 脚本文件制造；版本 B 的 activate 顶部
 * postMessage('B-activate') 给主线程显式信号（I4），waitUntil 挂起到主线程回
 * 'go'——观察窗口由信号驱动而非墙钟，CI 负载无关。
 * 脚本文件路径由 wrapper 以参数传入（arguments[0]=临时副本，arguments[1]=fixture
 * 目录）：测试对工作树零污染、并行安全（I3）。
 * keepalive interval 必需：CLI eval 返回后 wait_idle 需要 loop 上有活动 handle。 */
var SW_FILE = globalThis.arguments[0];
var SW_URL = 'file://' + SW_FILE;
var SW_B_FILE = globalThis.arguments[1] + '/sw-sw3-b.js';
var keepalive = setInterval(function () {}, 50);
var ccCount = 0;
var ctrlA = null;
var bsw = null;
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

function waitMsg(sw, data) {
  return new Promise(function (resolve) {
    sw.addEventListener('message', function onMsg(ev) {
      if (ev.data === data) { sw.removeEventListener('message', onMsg); resolve(); }
    });
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
  ctrlA = navigator.serviceWorker.controller;
  console.log('p1: state=' + ctrlA.state + ' cc=' + ccCount);
  /* 门1：字节未变 → update() 不 install、同一 registration、controller 不变 */
  var before = navigator.serviceWorker.controller;
  return reg1.update().then(function (r2) {
    var after = navigator.serviceWorker.controller;
    console.log('p2: sameReg=' + (r2 === reg1) + ' sameCtrl=' + (before === after) + ' cc=' + ccCount);
    /* 改写脚本 → 字节变化 */
    return qwrt.fs.readFile(SW_B_FILE).then(function (code) {
      return qwrt.fs.writeFile(SW_FILE, code);
    }).then(function () {
      var upd = reg1.update();
      bsw = reg1.installing;
      upd.then(function () {}, function () {});
      /* 门3（I4 显式信号）：等 B activate 顶部信号 → B 处于 activating、
       * 旧 SW(A) 仍 controller，观察窗口由信号而非墙钟界定 */
      return waitMsg(bsw, 'B-activate').then(function () {
        return fetchWho().then(function (body) {
          var cur = navigator.serviceWorker.controller;
          console.log('p3: during=' + body + ' ctrlA=' + (cur === ctrlA));
          bsw.postMessage('go');   /* 放行 B 完成 activate */
        });
      });
    });
  });
}).then(function () {
  /* 门2：等 B activate 完成替换 → 新 SW 拦截 */
  return waitCC(2).then(function () {
    var c = navigator.serviceWorker.controller;
    return fetchWho().then(function (body) {
      console.log('p4: after=' + body + ' ctrlB=' + (c === bsw) + ' cc=' + ccCount);
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

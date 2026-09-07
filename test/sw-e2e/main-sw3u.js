/* SW-3 更新机制回归 e2e — 评审探针固化的两个场景：
 * ①revert-during-install（C1）：A active → 装 Bslow（install waitUntil 挂起，
 *   postMessage('Bslow-install') 显式信号）→ 文件回写 A 字节 → update() skip
 *   resolve，在途 Bslow 被 supersede 取消、controller 仍 A、B 永不 activate
 * ②overlapping-activation（C2）：A → B（activate 信号门控）→ C，B 激活中途被
 *   C 更新 supersede → 无孤儿中间 worker（A、B 均 redundant）、最终 controller
 *   是最后版本 C（引用相等 ===）。
 * 脚本路径由 wrapper 以参数传入（arguments[0]=临时副本，arguments[1]=fixture 目录）。
 * keepalive interval 必需：CLI eval 返回后 wait_idle 需要 loop 上有活动 handle。 */
var SW_FILE = globalThis.arguments[0];
var SW_URL = 'file://' + SW_FILE;
var DIR = globalThis.arguments[1];
var A_FILE = DIR + '/sw-sw3-a.js';
var BSLOW_FILE = DIR + '/sw-sw3u-bslow.js';
var B_FILE = DIR + '/sw-sw3-b.js';
var C_FILE = DIR + '/sw-sw3u-c.js';
var keepalive = setInterval(function () {}, 50);
var ccCount = 0;
var reg = null;
var ctrlA = null;
var bsw = null;
var csw = null;
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

function copyFile(src) {
  return qwrt.fs.readFile(src).then(function (code) {
    return qwrt.fs.writeFile(SW_FILE, code);
  });
}

function sleep(ms) {
  return new Promise(function (resolve) { setTimeout(resolve, ms); });
}

function fetchWho() {
  return fetch('http://127.0.0.1:18433/who').then(function (res) {
    return res.text();
  });
}

/* —— 场景⓪：并发同 URL register（I2）—— 首版 install 未完成时第二次
 *   register（同字节）必须 resolve 同一 registration，不得 reject 首个调用方。
 *   旧代码：第二次对在途 installing 调 failSW('superseded') → 首个 promise reject。 */
var regA = navigator.serviceWorker.register(SW_URL);
var regB = navigator.serviceWorker.register(SW_URL);
Promise.all([regA, regB]).then(function (rs) {
  console.log('p0: same=' + (rs[0] === rs[1]));
  reg = rs[0];
  return navigator.serviceWorker.ready;
}).then(function () {
  ctrlA = navigator.serviceWorker.controller;
  console.log('p1: state=' + ctrlA.state + ' cc=' + ccCount);
  /* 改写为 Bslow → 慢 install 开始 */
  return copyFile(BSLOW_FILE);
}).then(function () {
  var first = reg.update();
  bsw = reg.installing;
  first.then(function () {}, function () {});
  /* 等 Bslow install 已启动（显式信号）→ 回滚：写回 A 字节 */
  return waitMsg(bsw, 'Bslow-install').then(function () {
    return copyFile(A_FILE);
  }).then(function () {
    /* update() 命中回滚 skip：在途 Bslow 被取消、沿用 active A */
    return reg.update().then(function (r) {
      console.log('p2: skipResolve=' + (r === reg) +
        ' installingNull=' + (reg.installing === null) +
        ' ctrlA=' + (navigator.serviceWorker.controller === ctrlA));
      return fetchWho().then(function (body) {
        console.log('p3: during=' + body);
      });
    });
  });
}).then(function () {
  /* Bslow 已被取消：等 1000ms（> Bslow 500ms 慢 install）确认其从未 activate、
   * controller 仍 A——旧代码下 Bslow 会在 500ms 后 install_done→activate 顶掉 A */
  return sleep(1000).then(function () {
    var cur = navigator.serviceWorker.controller;
    return fetchWho().then(function (body) {
      console.log('p4: ctrlA=' + (cur === ctrlA) + ' during=' + body + ' cc=' + ccCount);
    });
  });
})
/* —— 场景②：overlapping-activation —— */
.then(function () {
  /* 版本 B（activate 信号门控）：B 进入 activating 后等待主线程信号 */
  return copyFile(B_FILE);
}).then(function () {
  var upd = reg.update();
  bsw = reg.installing;
  upd.then(function () {}, function () {});
  /* 等 B activate 顶部信号 → B 正处于 activating（C2：仍占 _waiting 槽） */
  return waitMsg(bsw, 'B-activate').then(function () {
    /* B 激活中途 → 改成 C 再 update → B 必须被 supersede（不产生孤儿） */
    return copyFile(C_FILE);
  }).then(function () {
    var upd2 = reg.update();
    csw = reg.installing;
    upd2.then(function () {}, function () {});
    /* C activate 完成 → controller 替换为 C（cc=2） */
    return waitCC(2).then(function () {
      var cur = navigator.serviceWorker.controller;
      return fetchWho().then(function (body) {
        console.log('p5: ctrlC=' + (cur === csw) + ' during=' + body +
          ' A=' + ctrlA.state + ' B=' + bsw.state + ' C=' + csw.state +
          ' waitNull=' + (reg.waiting === null) + ' instNull=' + (reg.installing === null));
        console.log('DONE');
        clearInterval(keepalive);
      });
    });
  });
}).catch(function (e) {
  console.log('FAIL: ' + (e && e.message));
  clearInterval(keepalive);
});

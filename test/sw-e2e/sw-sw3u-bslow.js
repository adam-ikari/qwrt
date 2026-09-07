/* SW-3 更新回归 service worker — 慢 install 版本 B'：install 顶部 postMessage
 * ('Bslow-install') 显式信号，随后 waitUntil 挂起 500ms 才 install_done。
 * 回滚 update() 若未取消在途安装（C1 bug），这 500ms 后的 install_done 会驱动
 * Bslow 激活顶掉 A；修复后 Bslow 在 install_done 前即被 supersede 终止，绝不 activate。 */
self.addEventListener('install', function (event) {
  console.log('SW3U-Bslow install');
  postMessage('Bslow-install');
  event.waitUntil(new Promise(function (resolve) { setTimeout(resolve, 500); }));
});
self.addEventListener('activate', function () {
  console.log('SW3U-Bslow activate');
});
self.addEventListener('fetch', function (event) {
  if (event.request.url.indexOf('/who') !== -1) {
    event.respondWith(new Response('B', { status: 200 }));
  }
});

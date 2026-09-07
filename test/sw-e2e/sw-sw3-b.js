/* SW-3 e2e service worker — 版本 B（更新后）：/who → 'B'。
 * activate 顶部 postMessage('B-activate') 给主线程显式信号（I4），waitUntil 挂起
 * 到主线程回 'go' 才放行 activate_done——观察窗口由信号驱动而非墙钟，CI 负载无关。 */
self.addEventListener('install', function () {
  console.log('SW3-B install');
});
self.addEventListener('activate', function (event) {
  console.log('SW3-B activate');
  postMessage('B-activate');
  event.waitUntil(new Promise(function (resolve) {
    self.addEventListener('message', function onGo(ev) {
      if (ev.data === 'go') { self.removeEventListener('message', onGo); resolve(); }
    });
  }));
});
self.addEventListener('fetch', function (event) {
  if (event.request.url.indexOf('/who') !== -1) {
    event.respondWith(new Response('B', { status: 200 }));
  }
});

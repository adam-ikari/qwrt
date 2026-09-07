/* SW-3 e2e service worker — 版本 B（更新后）：/who → 'B'；activate 用 waitUntil
 * 延迟 200ms，制造"新 SW install/activating 期间旧 SW 仍拦截"的观察窗口。 */
self.addEventListener('install', function () {
  console.log('SW3-B install');
});
self.addEventListener('activate', function (event) {
  console.log('SW3-B activate');
  event.waitUntil(new Promise(function (resolve) { setTimeout(resolve, 200); }));
});
self.addEventListener('fetch', function (event) {
  if (event.request.url.indexOf('/who') !== -1) {
    event.respondWith(new Response('B', { status: 200 }));
  }
});

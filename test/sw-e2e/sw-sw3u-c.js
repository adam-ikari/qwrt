/* SW-3 更新回归 service worker — 版本 C（最后版本）：/who → 'C'，install/activate 即刻完成 */
self.addEventListener('install', function () {
  console.log('SW3U-C install');
});
self.addEventListener('activate', function () {
  console.log('SW3U-C activate');
});
self.addEventListener('fetch', function (event) {
  if (event.request.url.indexOf('/who') !== -1) {
    event.respondWith(new Response('C', { status: 200 }));
  }
});

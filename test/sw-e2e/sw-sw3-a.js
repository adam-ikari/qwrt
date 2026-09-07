/* SW-3 e2e service worker — 版本 A（首次注册）：/who → 'A'，install/activate 即刻完成 */
self.addEventListener('install', function () {
  console.log('SW3-A install');
});
self.addEventListener('activate', function () {
  console.log('SW3-A activate');
});
self.addEventListener('fetch', function (event) {
  if (event.request.url.indexOf('/who') !== -1) {
    event.respondWith(new Response('A', { status: 200 }));
  }
});

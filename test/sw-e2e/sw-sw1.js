/* SW-1 e2e service worker — fetch 拦截三场景：
 * /sw-intercept   → 合成 Response（respondWith）
 * /sw-passthrough → fetch(event.request) 透传（SW 内 fetch 不递归拦截，
 *                    走真实网络到主线程 serve()）
 * 其它            → 不调用 respondWith → 主线程回退网络 */
self.addEventListener('install', function () {
  console.log('SW1 install');
});
self.addEventListener('activate', function () {
  console.log('SW1 activate');
});
self.addEventListener('fetch', function (event) {
  var url = event.request.url;
  if (url.indexOf('/sw-intercept') !== -1) {
    event.respondWith(new Response('intercepted:' + event.request.method, {
      status: 201,
      statusText: 'Made Up',
      headers: { 'x-sw': '1' },
    }));
    return;
  }
  if (url.indexOf('/sw-passthrough') !== -1) {
    event.respondWith(fetch(event.request));
    return;
  }
  /* fallthrough：无 respondWith → 网络回退 */
});

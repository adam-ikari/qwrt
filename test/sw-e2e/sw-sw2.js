/* SW-2 e2e service worker — Cache API 集成：
 * install → addAll pre-cache；fetch → cache-first（命中回缓存，未命中透传
 * 网络并 put 回填，put 落定后才回话——无回填竞态）。
 * /net-count 放行回退网络，用于证明其余请求确实未打网络。 */
var BASE = 'http://127.0.0.1:18432';

self.addEventListener('install', function (event) {
  console.log('SW2 install');
  event.waitUntil(
    caches.open('v1').then(function (cache) {
      return cache.addAll([BASE + '/pre-cached']);
    })
  );
});

self.addEventListener('activate', function () {
  console.log('SW2 activate');
});

self.addEventListener('fetch', function (event) {
  var url = event.request.url;
  if (url.indexOf('/net-count') !== -1) {
    /* fallthrough：无 respondWith → 主线程回退网络 */
    return;
  }
  event.respondWith(
    caches.match(event.request).then(function (cached) {
      if (cached) return cached;
      return fetch(event.request).then(function (res) {
        var copy = res.clone(); /* SW-2: 流式 tee clone */
        return caches.open('v1').then(function (cache) {
          return cache.put(event.request, copy);
        }).then(function () {
          return res; /* put 落定后才回话 */
        });
      });
    })
  );
});

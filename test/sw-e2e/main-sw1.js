/* SW-1 e2e — fetch 拦截三场景：①SW 合成响应 ②SW 内 fetch 透传
 * （防重入 + 真实网络往返 serve()）③未 respondWith 回退网络。
 * keepalive interval 必需：CLI eval 返回后 wait_idle 需要 loop 上有活动
 * handle，纯 promise 链会被判 idle 提前 teardown（与 SW-0 main.js 同）。 */
var SW_URL = 'file:///home/gem/project/qwrt/test/sw-e2e/sw-sw1.js';
var PORT = 18431;
var keepalive = setInterval(function () {}, 50);

var server = serve({ port: PORT, hostname: '127.0.0.1' }, function (req) {
  return new Response('served:' + req.url.split('/').pop(), {
    status: 200,
    headers: { 'x-served': 'yes' },
  });
});

navigator.serviceWorker.register(SW_URL).then(function () {
  return navigator.serviceWorker.ready;
}).then(function () {
  var base = 'http://127.0.0.1:' + PORT;

  /* ① 拦截：SW 合成 201 + 自定义头 + body */
  return fetch(base + '/sw-intercept').then(function (res) {
    return res.text().then(function (body) {
      console.log('p1: ' + res.status + ' ' + res.headers.get('x-sw') + ' ' + body);
    });
  }).then(function () {
    /* ② SW 内 fetch(event.request) 透传 → 真实 serve() 响应
     * （若递归拦截会死循环/超时，直接失败） */
    return fetch(base + '/sw-passthrough').then(function (res) {
      return res.text().then(function (body) {
        console.log('p2: ' + res.status + ' ' + res.headers.get('x-served') + ' ' + body);
      });
    });
  }).then(function () {
    /* ③ SW 不 respondWith → 主线程回退网络 */
    return fetch(base + '/sw-plain').then(function (res) {
      return res.text().then(function (body) {
        console.log('p3: ' + res.status + ' ' + body);
      });
    });
  });
}).then(function () {
  console.log('DONE');
  clearInterval(keepalive);
}).catch(function (e) {
  console.log('FAIL: ' + (e && e.message));
  clearInterval(keepalive);
});

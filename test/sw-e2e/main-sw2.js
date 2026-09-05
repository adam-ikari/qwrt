/* SW-2 e2e — Cache 集成四场景：①install pre-cache 命中 ②未命中网络回填
 * ③回填后缓存命中（serve 计数不涨）④放行请求证明计数（③确未走网络）。
 * keepalive interval 必需：CLI eval 返回后 wait_idle 需要 loop 上有活动
 * handle，纯 promise 链会被判 idle 提前 teardown（与 SW-0/1 main 同）。 */
var SW_URL = 'file:///home/gem/project/qwrt/test/sw-e2e/sw-sw2.js';
var PORT = 18432;
var keepalive = setInterval(function () {}, 50);

var hits = 0;
var server = serve({ port: PORT, hostname: '127.0.0.1' }, function (req) {
  hits++;
  return new Response('served:' + hits, { status: 200 });
});

function p(label, url) {
  return fetch(url).then(function (res) {
    return res.text().then(function (body) {
      console.log(label + ': ' + res.status + ' ' + body);
    });
  });
}

navigator.serviceWorker.register(SW_URL).then(function () {
  return navigator.serviceWorker.ready;
}).then(function () {
  var base = 'http://127.0.0.1:' + PORT;
  return p('p1', base + '/pre-cached')
    .then(function () { return p('p2', base + '/cache-fill'); })
    .then(function () { return p('p3', base + '/cache-fill'); })
    .then(function () { return p('p4', base + '/net-count'); });
}).then(function () {
  console.log('DONE');
  clearInterval(keepalive);
}).catch(function (e) {
  console.log('FAIL: ' + (e && e.message));
  clearInterval(keepalive);
});

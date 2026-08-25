/* qwrt example http server — 前端小脚本（演示静态文件服务） */
(function () {
  'use strict';

  var out = document.getElementById('out');
  var btnHello = document.getElementById('btn-hello');
  var btnEcho = document.getElementById('btn-echo');
  var btnCache = document.getElementById('btn-cache');

  function show(label, data) {
    out.textContent = label + '\n' + data;
  }

  function request(url, opts) {
    opts = opts || {};
    var method = opts.method || 'GET';
    var body = opts.body || null;
    return fetch(url, {
      method: method,
      headers: body ? { 'Content-Type': 'text/plain' } : {},
      body: body,
    }).then(function (res) {
      return res.text();
    });
  }

  btnHello.addEventListener('click', function () {
    request('/api/hello').then(function (t) { show('GET /api/hello', t); });
  });

  btnEcho.addEventListener('click', function () {
    request('/api/echo', { method: 'POST', body: 'hi from browser' })
      .then(function (t) { show('POST /api/echo', t); });
  });

  btnCache.addEventListener('click', function () {
    request('/api/cache').then(function (t) { show('GET /api/cache', t); });
  });
})();

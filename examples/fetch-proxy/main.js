/* qwrt example: fetch 出站代理（forward proxy）
 *
 * 演示 qwrt fetch() 的 HTTP(S)_PROXY / NO_PROXY 环境变量出站代理能力
 * （C 层透明实现，协议仍在 JS——本例不写一行 C）：
 *
 *   客户端进程 ──HTTP_PROXY──▶ 转发代理 ──直连──▶ 目标源站
 *                                    ▲
 *                    qwrt fetch 本身也由 JS 实现（serve + fetch），
 *                    代理即一个"解析绝对式 URL 再转发"的 serve() handler
 *
 *   终端 2（客户端 + 本地源站，带代理环境变量）：
 *     HTTP_PROXY=http://127.0.0.1:18082 NO_PROXY=127.0.0.1,example.com \
 *       qwrt examples/fetch-proxy/main.js client
 *
 * 演示的三个行为：
 *   1. NO_PROXY 未覆盖 localhost  → 请求经代理（代理能看到绝对式请求行
 *      GET http://localhost:18081/hello，RFC 7230 §5.3.2），响应带
 *      x-proxied-by 头证明走了代理
 *   2. NO_PROXY=127.0.0.1         → 127.0.0.1 的请求绕过代理直连源站
 *      （响应无 x-proxied-by 头）
 *   3. NO_PROXY=…,example.com     → 域名后缀匹配，外网请求同样直连
 *      （https 经代理需 CONNECT 隧道，极简转发代理不实现，见 README）
 *
 * 依赖的能力（qwrt 内置，无需任何配置）：
 *   HTTP_PROXY / HTTPS_PROXY / NO_PROXY 环境变量 — fetch 出站代理（C 层）
 *   serve({port}, handler)                       — HTTP 监听 + 回复
 *   fetch / Response / URL                       — 客户端与 URL 解析
 *   globalThis.env / globalThis.arguments        — CLI 注入的环境与参数
 */

var PROXY_PORT = arguments[1] ? Number(arguments[1]) : 18082;
var ORIGIN_PORT = 18081;

if (arguments[0] === 'proxy') {
  /* ── 代理进程：绝对式 URL → 换成相对路径向源站直连转发 ── */
  serve({ port: PROXY_PORT, hostname: '127.0.0.1' }, async function (req) {
    var u = new URL(req.url);           // 代理视角收到的是绝对 URL
    var target = 'http://127.0.0.1:' + ORIGIN_PORT + u.pathname + u.search;
    console.log('[proxy] ' + req.method + ' ' + req.url + '  ->  ' + target);
    try {
      var up = await fetch(target);     // 代理进程自身无 HTTP_PROXY → 直连
      var body = await up.arrayBuffer();
      var hdrs = { 'x-proxied-by': 'qwrt-fetch-proxy-example' };
      var ct = up.headers.get('content-type');
      if (ct) hdrs['Content-Type'] = ct;
      return new Response(body, { status: up.status, headers: hdrs });
    } catch (e) {
      return new Response('proxy upstream error: ' + e, { status: 502 });
    }
  });

} else {
  /* ── 客户端 + 本地源站进程 ── */

  /* 源站：任何路径都回 origin-hello */
  var srv = serve({ port: ORIGIN_PORT, hostname: '0.0.0.0' }, function (req) {
    console.log('[origin] ' + req.method + ' ' + req.url);
    return new Response('origin-hello', {
      status: 200,
      headers: { 'Content-Type': 'text/plain; charset=utf-8' },
    });
  });

  (async function () {
    /* 1) localhost 不在 NO_PROXY 里 → 走代理 */
    var r = await fetch('http://localhost:' + ORIGIN_PORT + '/hello');
    console.log('[client] via-proxy : status=' + r.status +
                '  x-proxied-by=' + r.headers.get('x-proxied-by') +
                '  body=' + (await r.text()));

    /* 2) 127.0.0.1 被 NO_PROXY 排除 → 直连，不经过代理 */
    var r2 = await fetch('http://127.0.0.1:' + ORIGIN_PORT + '/direct');
    console.log('[client] direct    : status=' + r2.status +
                '  x-proxied-by=' + r2.headers.get('x-proxied-by') +
                '  body=' + (await r2.text()));

    /* 3) example.com 被 NO_PROXY 后缀匹配排除 → 外网直连（不走代理）。
     *    说明：https 经代理需 CONNECT 隧道，本例的极简转发代理只演示
     *    plain-http 转发；真实代理（squid 等）配 HTTPS_PROXY 即可隧道。
     *    外网也用 plain-http（https://example.com 亦可，仅体积更大）。 */
    try {
      var r3 = await fetch('http://example.com/');
      console.log('[client] internet  : status=' + r3.status +
                  '  len=' + (await r3.text()).length + '  (NO_PROXY 直连)');
    } catch (e) {
      console.log('[client] internet  : 不可达（离线环境正常）: ' + e);
    }
    srv.close();
    console.log('[client] done');
  })();
}

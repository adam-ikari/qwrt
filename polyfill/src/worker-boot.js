/**
 * qwrt worker boot shim — 独立 IIFE,由 build.js 用 qjsc 编译成字节码,
 * 注入到每个 worker 线程执行(不再用 C 内嵌源码字符串 eval)。
 *
 * 依赖:worker 线程已注入完整 polyfill(上下文提供 __qwrt_serialize__ /
 * __qwrt_deserialize__ / __qwrt_lookup_port__ / __qwrt_port_from_ref__ /
 * __qwrt_deliver_port_msg__ / MessageEvent / MessagePort / dispatchEvent),
 * 且 __native__(worker 侧 pal)已由 polyfill 注入路径保留。
 *
 * 作用:覆盖 postMessage / __qwrt_dispatch__ / close / importScripts,
 * 使 worker 脚本里的 postMessage()/onmessage/close() 按 worker 语义工作。
 *
 * Service Worker 模式(SW-1,见 service-worker.js):父线程首条消息
 * {__qwrt_sw__:'enter',url,scope} 进入 SW 模式——注入 self.registration /
 * self.skipWaiting / self.clients;后续 {__qwrt_sw_lifecycle__:'install'|
 * 'activate'} 触发 ExtendableEvent,waitUntil 的 promise 全部 settle 后回发
 * {__sw_event__:true,phase:'install_done'|'activate_done'}。SW 侧控制消息
 * SW-1 fetch 拦截：父线程 {__qwrt_sw_fetch__:{fetchId,request:{url,method,
 * headers,body}}} → FetchEvent（Request 由扁平字段重建）→ fetch 监听器；
 * respondWith(promise<Response>) settle 后回发 {__sw_event__,phase:
 * 'fetch_response'|'fetch_fallback',fetchId, response:{status,statusText,
 * headers,body}}。SW 内 fetch() 自带防重入标志，不递归拦截。
 */
(function(pal){

  /* SW 控制通道：直发序列化控制消息给父线程（不经用户 postMessage） */
  function swEmit(obj){
    pal.postMessage(__qwrt_serialize__(obj));
  }

  /* ExtendableEvent：install/activate 事件基类，waitUntil 延迟生命周期 */
  /* Event 是 class，不能用 call 复用——直接原型继承包装 */
  function makeExtendable(type){
    var ev = new Event(type);
    var promises = [];
    ev.waitUntil = function(p){
      if (p && typeof p.then === 'function') promises.push(Promise.resolve(p));
    };
    ev._promises = promises;
    return ev;
  }
  /* 派发 install/activate 生命周期事件；handler 抛错（经全局 error 事件
   * 上报，dispatchEvent 内部吞掉异常）或 waitUntil reject → 带 error 回发 */
  function swDispatchLifecycle(phase){
    var ev = makeExtendable(phase);
    var failed = null;
    function onErr(e){
      if (failed) return;
      failed = (e && ((e.error && (e.error.message || e.error)) || e.message)) || 'lifecycle handler error';
    }
    self.addEventListener('error', onErr);
    try {
      self.dispatchEvent(ev);
    } catch (err) {
      self.removeEventListener('error', onErr);
      swEmit({ __sw_event__: true, phase: phase + '_done', error: String(err && err.message || err) });
      return;
    }
    self.removeEventListener('error', onErr);
    if (failed !== null) {
      swEmit({ __sw_event__: true, phase: phase + '_done', error: String(failed) });
      return;
    }
    var ps = ev._promises;
    if (!ps.length) {
      swEmit({ __sw_event__: true, phase: phase + '_done' });
      return;
    }
    Promise.all(ps.map(function(p){
      return p.then(function(){ return null; }, function(err){
        return String(err && (err.message || err) || 'waitUntil rejected');
      });
    })).then(function(errs){
      for (var i = 0; i < errs.length; i++) {
        if (errs[i] !== null) {
          swEmit({ __sw_event__: true, phase: phase + '_done', error: errs[i] });
          return;
        }
      }
      swEmit({ __sw_event__: true, phase: phase + '_done' });
    });
  }
  /* 进入 SW 模式：注入 ServiceWorkerGlobalScope 专属全局。
   * __qwrt_sw_mode__ 标志让 SW 线程内的 fetch() 绕过拦截（防自我递归，
   * 设计 §7.2）；qwrt 单客户端（主线程），clients 按设计 §3.2 返回常量。 */
  function swEnter(msg){
    globalThis.__qwrt_sw_mode__ = true;
    self.registration = {
      scope: msg.scope != null ? msg.scope : '/',
      scriptURL: msg.url,
    };
    self.skipWaiting = function(){
      swEmit({ __sw_event__: true, phase: 'skipWaiting' });
      return Promise.resolve();
    };
    var mainClient = { id: 'main', type: 'window', url: '' };
    self.clients = {
      claim: function(){ return Promise.resolve(); },
      matchAll: function(){ return Promise.resolve([mainClient]); },
      get: function(id){ return Promise.resolve(id === 'main' ? mainClient : null); },
    };
    mainClient.postMessage = function(data){
      swEmit(data);
    };
  }

  /* ---- SW-1：FetchEvent 派发 ----
   * 父线程 {__qwrt_sw_fetch__:{fetchId,request:{url,method,headers,body}}}：
   * 重建 Request → FetchEvent → fetch 监听器。respondWith(promise<Response>)
   * settle 后回发 fetch_response；reject / 非序列化 / 未调用 respondWith →
   * fetch_fallback（主线程回退网络）。无浏览器端 30s 超时——超时在主线程侧。 */
  function serializeResponse(resp){
    if (!resp || typeof resp !== 'object' || typeof resp.arrayBuffer !== 'function') {
      return Promise.resolve(null);
    }
    return resp.arrayBuffer().then(function(buf){
      var headers = {};
      resp.headers.forEach(function(v, n){ headers[n] = v; });
      return {
        status: resp.status,
        statusText: resp.statusText || '',
        headers: headers,
        body: buf ? new Uint8Array(buf) : null,
      };
    }, function(){
      return null; /* body 读取失败 → 回退网络 */
    });
  }

  function swDispatchFetch(payload){
    var fetchId = payload.fetchId;
    var info = payload.request;
    var req;
    try {
      req = new Request(info.url, {
        method: info.method || 'GET',
        headers: info.headers || {},
        body: info.body != null ? info.body : undefined,
      });
    } catch (err) {
      swEmit({ __sw_event__: true, phase: 'fetch_fallback', fetchId: fetchId });
      return;
    }
    var ev = makeExtendable('fetch');
    ev.request = req;
    ev.clientId = 'main';
    var responded = null;
    ev.respondWith = function(p){
      if (responded) throw new TypeError('FetchEvent.respondWith: already called');
      responded = Promise.resolve(p).then(serializeResponse);
    };
    self.dispatchEvent(ev);
    var onX = self['onfetch'];
    if (typeof onX === 'function') {
      try { onX.call(self, ev); } catch (e) { /* 与 dispatchEvent 同：吞掉 */ }
    }
    if (!responded) {
      swEmit({ __sw_event__: true, phase: 'fetch_fallback', fetchId: fetchId });
      return;
    }
    responded.then(function(serialized){
      if (serialized) {
        swEmit({ __sw_event__: true, phase: 'fetch_response', fetchId: fetchId, response: serialized });
      } else {
        swEmit({ __sw_event__: true, phase: 'fetch_fallback', fetchId: fetchId });
      }
    });
  }

  globalThis.postMessage = function(v, transfer){
    var ports = [];
    var abT;
    if (transfer && transfer.length) {
      abT = [];
      for (var i = 0; i < transfer.length; i++) {
        var t = transfer[i];
        if (typeof MessagePort !== 'undefined' && t instanceof MessagePort) {
          /* ref.peerThread = 被转移 port 的对端所在线程(从接收方视角)。多跳
           * (worker 转发从父收到的 port)时对端在父(t._peerThread='parent'),
           * 必须保留而不是写死本 workerId;对端在本 worker('local')才用本
           * workerId(worker 侧对端只可能在本 worker 或父线程)。 */
          ports.push({ id: t._id, peerId: t._peerId, peerThread: (t._peerThread === 'local' ? pal.workerId() : t._peerThread) });
          t._detached = true;
          var peer = globalThis.__qwrt_lookup_port__(t._peerId);
          if (peer) peer._peerThread = 'parent';
        } else { abT.push(t); }
      }
      if (!abT.length) abT = undefined;
    }
    var db = __qwrt_serialize__(v, abT);
    if (ports.length) {
      pal.postMessage(__qwrt_serialize__({ __qwrt_ports: ports, __qwrt_payload: db }));
    } else {
      pal.postMessage(db);
    }
  };
  globalThis.__qwrt_dispatch__ = function(data, source){
    var o = __qwrt_deserialize__(data);
    /* SW 控制消息（父线程 → SW 线程），不进用户消息流 */
    if (o && typeof o === 'object' && o.__qwrt_sw__ === 'enter') {
      swEnter(o);
      return;
    }
    if (o && typeof o === 'object' && o.__qwrt_sw_lifecycle__) {
      swDispatchLifecycle(String(o.__qwrt_sw_lifecycle__));
      return;
    }
    if (o && typeof o === 'object' && o.__qwrt_sw_fetch__) {
      swDispatchFetch(o.__qwrt_sw_fetch__);
      return;
    }
    if (globalThis.__qwrt_deliver_port_msg__ &&
        globalThis.__qwrt_deliver_port_msg__(o)) return;
    if (o && typeof o === 'object' && o.__qwrt_ports) {
      var ports = [];
      for (var i = 0; i < o.__qwrt_ports.length; i++) {
        ports.push(globalThis.__qwrt_port_from_ref__(o.__qwrt_ports[i]));
      }
      var inner = __qwrt_deserialize__(o.__qwrt_payload);
      globalThis.dispatchEvent(new MessageEvent('message', {data: inner, ports: ports}));
    } else {
      globalThis.dispatchEvent(new MessageEvent('message', {data: o}));
    }
  };
  globalThis.close = function(){ pal.workerClose(); };
  globalThis.importScripts = function(){
    for (var i = 0; i < arguments.length; i++) {
      var url = String(arguments[i]);
      if (url.indexOf('file://') !== 0)
        throw new Error('importScripts: only file:// URLs');
      var code = pal.fsReadSync(url.slice(7));
      (0, eval)(code);
    }
  };
})(globalThis.__native__);

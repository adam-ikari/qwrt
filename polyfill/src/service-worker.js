/**
 * qwrt polyfill: Service Worker — SW-1（注册 / 生命周期 / 消息 + fetch 拦截）
 *
 * SW 脚本跑在独立 worker 线程（复用 Worker）。主线程状态机由 worker 侧
 * 控制消息驱动（协议见 worker-boot.js）：
 *   register → new Worker(url)（同步 spawn + 脚本顶层 eval）
 *     → {__qwrt_sw__:'enter'} → {__qwrt_sw_lifecycle__:'install'} → installing
 *   ← {__sw_event__,phase:'install_done'} → installed →（零等待）activating
 *     → {__qwrt_sw_lifecycle__:'activate'}
 *   ← {__sw_event__,phase:'activate_done'} → activated → controller + controllerchange
 *   带 error 的 *_done / worker 'error' 事件 → redundant。
 * 与浏览器差异（设计已拍板）：全局唯一注册；scope 接受但忽略；
 * install 完成即自动 skipWaiting + 激活；register() 在 install 成功即 resolve。
 */

export function setupServiceWorker(pal) {
  var self = globalThis;

  var currentRegistration = null;
  var controller = null;
  var readyResolve = null;
  var readyPromise = new Promise(function (resolve) { readyResolve = resolve; });

  /* ---- SW-1：fetch 拦截状态 ----
   * fetchSeq/pendingFetches：主线程 fetch() 被 SW 接管的在途表。
   * entry.resolve/reject = fetch promise 的 settle 钩子；onFallback(bytes)
   * = SW 回退/超时/SW 终止时续走网络（bytes 复用，流式 body 不可重读）；
   * onSettle = 成功响应后移除 abort 监听（回退路径保持监听交回网络）。 */
  var fetchSeq = 0;
  var pendingFetches = new Map();

  /* dispatchEvent + onX 属性（EventTarget 类不调 onX，这里统一补） */
  function fire(target, event) {
    target.dispatchEvent(event);
    var h = target['on' + event.type];
    if (typeof h === 'function') h.call(target, event);
  }

  /* ---- ServiceWorker ---- */
  class ServiceWorker extends EventTarget {
    constructor(url) {
      super();
      this._url = url;
      this._state = 'parsed';
      this._worker = null;    /* worker.js 的真 Worker 实例 */
      this._settled = false;  /* activated / redundant 即终态 */
      this._onok = null;      /* register() resolve/reject 钩子 */
      this._onfail = null;
    }
    get state() { return this._state; }
    get scriptURL() { return this._url; }
    postMessage(data, transfer) {
      if (!this._worker) throw new TypeError('ServiceWorker.postMessage: worker is not running');
      this._worker.postMessage(data, transfer);
    }
    _setState(state) {
      if (this._state === state) return;
      this._state = state;
      if (state === 'activated' || state === 'redundant') this._settled = true;
      var ev = new Event('statechange');
      ev.state = state;
      fire(this, ev);
    }
    _kill() {
      if (!this._worker) return;
      var w = this._worker;
      this._worker = null;
      try { w.terminate(); } catch (err) { /* 已终止 */ }
    }
  }

  /* ---- ServiceWorkerRegistration ---- */
  class ServiceWorkerRegistration extends EventTarget {
    constructor(url, scope) {
      super();
      this._url = url;
      this._scope = scope;
      this._installing = null;
      this._waiting = null;
      this._active = null;
    }
    get installing() { return this._installing; }
    get waiting() { return this._waiting; }
    get active() { return this._active; }
    get scope() { return this._scope; }
    /* SW-0：无字节 diff，直接重走 install 流程 */
    update() { return container.register(this._url, { scope: this._scope }); }
    unregister() {
      var slots = [this._installing, this._waiting, this._active];
      for (var i = 0; i < slots.length; i++) {
        var s = slots[i];
        if (!s) continue;
        s._kill();
        if (s._state !== 'redundant') s._setState('redundant');
      }
      this._installing = this._waiting = this._active = null;
      if (controller && controller._state === 'redundant') {
        controller = null;
        fire(container, new Event('controllerchange'));
      }
      if (currentRegistration === this) currentRegistration = null;
      return Promise.resolve(true);
    }
  }

  /* ---- 状态机 ---- */
  function failSW(sw, registration, reason) {
    if (sw._settled) return;
    sw._kill();
    if (registration._installing === sw) registration._installing = null;
    if (registration._waiting === sw) registration._waiting = null;
    if (registration._active === sw) registration._active = null;
    sw._setState('redundant');
    var f = sw._onfail;
    sw._onok = sw._onfail = null;
    if (f) f(reason instanceof Error ? reason : new Error(String(reason)));
    /* SW 挂了：在途被拦截 fetch 全部回退网络（设计 §7.2 预期行为） */
    flushPendingFetches();
  }

  /* SW 终止/替换时把所有在途 fetch 回退网络（计时器一并清掉） */
  function flushPendingFetches() {
    if (!pendingFetches.size) return;
    var entries = Array.from(pendingFetches.values());
    pendingFetches.clear();
    for (var i = 0; i < entries.length; i++) {
      if (entries[i].timer) clearTimeout(entries[i].timer);
      entries[i].onFallback(entries[i].bytes);
    }
  }

  /* install 完成 → 零等待激活（旧 SW 的替换等 activate_done，保证
   * await ready 后旧 SW 已 redundant、controllerchange 已派发） */
  function activateSW(sw, registration) {
    sw._previous = (controller !== sw) ? controller : null;
    registration._waiting = null;
    registration._active = sw;
    controller = sw;
    sw._setState('activating');
    try {
      sw._worker.postMessage({ __qwrt_sw_lifecycle__: 'activate' });
    } catch (err) {
      failSW(sw, registration, err);
    }
  }

  function handleControl(sw, registration, d) {
    var failed = typeof d.error === 'string' && d.error !== '';
    if (d.phase === 'install_done') {
      if (failed) { failSW(sw, registration, new Error('install failed: ' + d.error)); return; }
      registration._installing = null;
      registration._waiting = sw;
      sw._setState('installed');
      var ok = sw._onok;
      sw._onok = null;
      if (ok) ok(registration);
      activateSW(sw, registration);
    } else if (d.phase === 'activate_done') {
      if (failed) { failSW(sw, registration, new Error('activate failed: ' + d.error)); return; }
      sw._setState('activated');
      var previous = sw._previous;
      if (previous && previous !== sw) {
        previous._kill();
        if (previous._state !== 'redundant') previous._setState('redundant');
        /* 旧 SW 已终止：其未回话的在途 fetch 全部回退网络（新 SW 不认旧 id） */
        flushPendingFetches();
      }
      fire(container, new Event('controllerchange'));
      if (readyResolve) {
        var r = readyResolve;
        readyResolve = null;
        r(registration);
      }
    } else if (d.phase === 'fetch_response' || d.phase === 'fetch_fallback') {
      var entry = pendingFetches.get(d.fetchId);
      if (!entry) return;
      pendingFetches.delete(d.fetchId);
      if (entry.timer) { clearTimeout(entry.timer); entry.timer = null; }
      if (d.phase === 'fetch_response' && d.response) {
        /* SW 回话：Response 重建（body 为对端 structuredClone 的 Uint8Array）。
         * onSettle 移除 abort 监听（promise 已 settle，不再需要）。 */
        var res = new Response(d.response.body != null ? d.response.body : null, {
          status: d.response.status || 200,
          statusText: d.response.statusText || '',
          headers: d.response.headers,
        });
        res._url = entry.url;
        if (entry.onSettle) entry.onSettle();
        entry.resolve(res);
      } else {
        /* SW 回退（无监听器/respondWith reject/serialize 失败）：
         * abort 监听保持（交回网络路径），续走网络。 */
        entry.onFallback(entry.bytes);
      }
    }
    /* phase === 'skipWaiting'：qwrt 本就零等待，忽略 */
  }

  /* ---- navigator.serviceWorker ---- */
  var container = new EventTarget();

  Object.defineProperty(container, 'controller', {
    get: function () {
      return controller && controller._state === 'activated' ? controller : null;
    },
    configurable: true, enumerable: true,
  });
  Object.defineProperty(container, 'ready', {
    get: function () { return readyPromise; },
    configurable: true, enumerable: true,
  });
  Object.defineProperty(container, 'oncontrollerchange', {
    get: function () { return this._oncc || null; },
    set: function (fn) { this._oncc = fn; },
    configurable: true, enumerable: true,
  });

  /* ---- SW-1：fetch.js 网络路径前的拦截入口 ----
   * 返回 true = 已派发 FetchEvent 到 SW 线程（fetch promise 由回话消息驱动）；
   * 返回 false = 无 activated 控制器，fetch.js 直接走网络。
   * 30s 超时回退（设计 §5 SW-1，与浏览器一致）。 */
  container.__qwrt_sw_intercept__ = function (request, bytes, resolve, reject, onFallback, onSettle) {
    var sw = controller;
    if (!sw || sw._state !== 'activated' || !sw._worker) return false;
    var fetchId = ++fetchSeq;
    var headers = {};
    request.headers.forEach(function (value, name) { headers[name] = value; });
    var entry = {
      resolve: resolve,
      onFallback: onFallback,
      onSettle: onSettle,
      bytes: bytes,
      url: request.url,
      timer: null,
    };
    pendingFetches.set(fetchId, entry);
    entry.timer = setTimeout(function () {
      if (!pendingFetches.has(fetchId)) return;
      pendingFetches.delete(fetchId);
      entry.timer = null;
      onFallback(bytes);
    }, 30000);
    try {
      sw._worker.postMessage({
        __qwrt_sw_fetch__: {
          fetchId: fetchId,
          request: { url: request.url, method: request.method, headers: headers, body: bytes || null },
        },
      });
    } catch (err) {
      if (entry.timer) clearTimeout(entry.timer);
      pendingFetches.delete(fetchId);
      return false;
    }
    return true;
  };

  container.register = function (url, options) {
    if (typeof url !== 'string' || url.indexOf('file://') !== 0) {
      return Promise.reject(new Error(
        'serviceWorker.register: only file:// script URLs are supported in SW-1'));
    }
    var scope = (options && options.scope != null) ? String(options.scope) : '/';

    var registration = (currentRegistration && currentRegistration._url === url)
      ? currentRegistration
      : new ServiceWorkerRegistration(url, scope);
    registration._scope = scope;

    var sw = new ServiceWorker(url);
    var promise = new Promise(function (resolve, reject) {
      sw._onok = resolve;
      sw._onfail = reject;
    });

    /* 同一 registration 上未完成的前一次安装尝试直接作废 */
    if (registration._installing) failSW(registration._installing, registration, new Error('superseded'));
    if (registration._waiting) failSW(registration._waiting, registration, new Error('superseded'));
    registration._installing = sw;
    currentRegistration = registration;

    var worker;
    try {
      /* 同步：spawn worker 线程 + 跑完 SW 脚本顶层（install listener 就位） */
      worker = new self.Worker(url);
    } catch (err) {
      registration._installing = null;
      sw._setState('redundant');
      return Promise.reject(err);
    }
    sw._worker = worker;

    worker.addEventListener('message', function (ev) {
      if (ev.data && typeof ev.data === 'object' && ev.data.__sw_event__ === true) {
        handleControl(sw, registration, ev.data);
      } else {
        fire(sw, new MessageEvent('message', { data: ev.data }));
      }
    });
    /* worker.js 把 C 侧 {type:'error'} 转成 ErrorEvent('error') */
    worker.addEventListener('error', function (ev) {
      failSW(sw, registration, new Error(ev && ev.message ? ev.message : 'service worker script error'));
    });

    sw._setState('installing');
    try {
      /* 1) 进入 SW 模式  2) 派发 install（见 worker-boot.js） */
      worker.postMessage({ __qwrt_sw__: 'enter', url: url, scope: scope });
      worker.postMessage({ __qwrt_sw_lifecycle__: 'install' });
    } catch (err) {
      failSW(sw, registration, err);
    }
    return promise;
  };

  container.getRegistration = function () {
    return Promise.resolve(currentRegistration || null);
  };
  container.getRegistrations = function () {
    return Promise.resolve(currentRegistration ? [currentRegistration] : []);
  };

  if (!self.navigator) self.navigator = {};
  self.navigator.serviceWorker = container;
}

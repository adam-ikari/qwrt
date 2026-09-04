/**
 * qwrt polyfill: Web Worker (parent side)
 *
 * W3C-style Worker class backed by a real qwrt runtime thread (execution
 * model A — each worker is its own qwrt_t with its own thread/loop/JSRuntime).
 *
 *   - new Worker(url)           — load script synchronously (file:// only in v1,
 *                                 via pal.fsReadSync), spawn worker thread
 *                                 (pal.spawnWorker blocks until the worker is
 *                                 ready), returns instance keyed by worker id.
 *   - w.postMessage(value)      — structured-clone bytes → pal.workerPost.
 *   - w.terminate()             — pal.workerTerminate (async stop + join at
 *                                 parent teardown).
 *   - w.onmessage               — fires with MessageEvent whose data is
 *                                 deserialized from the worker's bytes.
 *   - w.onerror                 — fires when the worker script throws at the
 *                                 top level; the event's data is
 *                                 {type:'error', error:<message>}.
 *
 * Inbound routing: __qwrt_dispatch__(data, source). source 0 = host (delegates
 * to host-messaging's handler verbatim); source > 0 = worker id (bytes).
 *
 * Depends on: MessageEvent (message-channel.js), __qwrt_serialize__ /
 * __qwrt_deserialize__ (structured-clone.js), host-messaging.js (invoked after
 * it in index.js so the host handler can be captured for delegation).
 */

export function setupWorker(pal) {
  var self = globalThis;
  var workers = new Map();   /* worker id -> Worker 实例 */

  // Synchronous script load. v1: file:// only.
  function loadScript(url) {
    if (typeof url !== 'string' || url.indexOf('file://') !== 0) {
      throw new Error('Worker: only file:// URLs are supported in v1');
    }
    return pal.fsReadSync(url.slice('file://'.length));
  }

  function Worker(url) {
    var code = loadScript(url);
    var id = pal.spawnWorker(code);   /* 同步阻塞直到 worker ready；失败抛 Error */
    this._id = id;
    this._onmsg = null;
    this._onerror = null;
    this._onmsgErr = null;
    this._listeners = new Map();      /* type -> [{callback, once}] (EventTarget 支持) */
    workers.set(id, this);
    var w = this;
    Object.defineProperty(this, 'onmessage', {
      get: function () { return w._onmsg; },
      set: function (fn) { w._onmsg = fn; },
      configurable: true,
    });
    Object.defineProperty(this, 'onerror', {
      get: function () { return w._onerror; },
      set: function (fn) { w._onerror = fn; },
      configurable: true,
    });
    Object.defineProperty(this, 'onmessageerror', {
      get: function () { return w._onmsgErr; },
      set: function (fn) { w._onmsgErr = fn; },
      configurable: true,
    });
  }

  Worker.prototype.addEventListener = function (type, callback, options) {
    if (typeof type !== 'string') return;
    if (typeof callback !== 'function' &&
        !(callback && typeof callback.handleEvent === 'function')) return;
    if (!this._listeners.has(type)) this._listeners.set(type, []);
    var list = this._listeners.get(type);
    for (var i = 0; i < list.length; i++) {
      if (list[i].callback === callback) return;
    }
    list.push({ callback: callback, once: !!(options && options.once) });
  };

  Worker.prototype.removeEventListener = function (type, callback) {
    if (typeof type !== 'string') return;
    var list = this._listeners.get(type);
    if (!list) return;
    for (var i = 0; i < list.length; i++) {
      if (list[i].callback === callback) { list.splice(i, 1); return; }
    }
  };

  Worker.prototype.dispatchEvent = function (event) {
    var type = event && event.type;
    if (typeof type !== 'string') return false;
    var handlers = [];
    var list = this._listeners.get(type);
    if (list) handlers = handlers.concat(list.slice());
    if (type === 'message' && typeof this._onmsg === 'function') handlers.push(this._onmsg);
    if (type === 'error' && typeof this._onerror === 'function') handlers.push(this._onerror);
    if (type === 'messageerror' && typeof this._onmsgErr === 'function') handlers.push(this._onmsgErr);
    for (var i = 0; i < handlers.length; i++) {
      var entry = handlers[i];
      try {
        /* entry 两种形态：裸函数（onmessage 等内建 handler）或
         * {callback, once}（addEventListener 存的对象，callback 可为
         * 函数或带 handleEvent 的对象） */
        var target = typeof entry === 'function' ? entry : entry.callback;
        var cb = typeof target === 'function'
          ? target
          : (target && typeof target.handleEvent === 'function' ? target.handleEvent : undefined);
        if (typeof cb === 'function') cb.call(this, event);
      } catch (err) {
        if (typeof globalThis.reportError === 'function') globalThis.reportError(err);
        else if (globalThis.console) console.error('Error in worker event listener:', err);
      }
      if (entry && entry.once && list) {
        var idx = list.indexOf(entry);
        if (idx >= 0) list.splice(idx, 1);
      }
    }
    return true;
  };

  /* C 侧 worker 错误通知（wire format {type:'error', error:<msg>}）→
   * ErrorEvent('error')，并保留 e.data 兼容历史 onerror 契约。 */
  Worker.prototype._deliverError = function (msg) {
    var ev;
    try {
      ev = new ErrorEvent('error', { message: String(msg), error: new Error(String(msg)), cancelable: true });
    } catch (err) {
      try { ev = new Event('error'); } catch (e2) { ev = { type: 'error' }; }
      ev.message = String(msg);
    }
    ev.data = { type: 'error', error: String(msg) };
    this.dispatchEvent(ev);
  };

  Worker.prototype.postMessage = function (value, transfer) {
    /* 拆出 transfer 列表里的 MessagePort（其余 ArrayBuffer 照常序列化），
     * 并把它们编码成 {__qwrt_ports:[{id,peerId,peerThread}], __qwrt_payload}。
     * 转移语义：原 port 标记 detached；父侧对端 port 的 _peerThread 指向 worker。 */
    var ports = [];
    var abTransfer;
    if (transfer && transfer.length) {
      abTransfer = [];
      for (var i = 0; i < transfer.length; i++) {
        var t = transfer[i];
        if (typeof MessagePort !== 'undefined' && t instanceof MessagePort) {
          /* ref.peerThread = 被转移 port 的对端所在线程（从接收方视角）。父侧对端
           * 只可能在本线程（'local'→对端留在父，ref 用 'parent'）或某 worker
           * （workerId→保持不变）。写死 'parent' 会在父把从 worker 收到的代理 port
           * 再转移时路由错线程。 */
          ports.push({ id: t._id, peerId: t._peerId, peerThread: (t._peerThread === 'local' ? 'parent' : t._peerThread) });
          t._detached = true;   /* 原 port 已转移，不再可用 */
          var peer = globalThis.__qwrt_lookup_port__(t._peerId);
          if (peer) peer._peerThread = this._id;  /* 对端现在在 worker */
        } else {
          abTransfer.push(t);
        }
      }
      if (!abTransfer.length) abTransfer = undefined;
    }
    var dataBytes = __qwrt_serialize__(value, abTransfer);
    if (ports.length) {
      var wrapped = __qwrt_serialize__(
        { __qwrt_ports: ports, __qwrt_payload: dataBytes });
      pal.workerPost(this._id, wrapped);
    } else {
      pal.workerPost(this._id, dataBytes);
    }
  };

  Worker.prototype.terminate = function () {
    pal.workerTerminate(this._id);
    workers.delete(this._id);
  };

  /* 判断是否为 C 侧 worker 错误通知：{type:'error', error:<string>}。
   * 要求 error 为字符串以尽量排除用户消息误路由；彻底区分需 C 侧 wire marker
   * （见报告）。 */
  function isWorkerError(d) {
    return d && typeof d === 'object' &&
           d.type === 'error' && typeof d.error === 'string';
  }

  globalThis.Worker = Worker;

  // Route inbound messages: source 0 = host JSON, > 0 = worker bytes.
  var hostDispatch = self.__qwrt_dispatch__;
  globalThis.__qwrt_dispatch__ = function (data, source) {
    if (source === 0) {
      hostDispatch(data, source);
      return;
    }
    var d;
    try { d = __qwrt_deserialize__(data); }
    catch (err) {
      /* 反序列化失败：按规范触发 worker 的 messageerror 事件 */
      var we = workers.get(source);
      if (we) {
        var errEv;
        try { errEv = new MessageEvent('messageerror'); }
        catch (e2) { errEv = new Event('messageerror'); }
        we.dispatchEvent(errEv);
      } else {
        reportError(err);
      }
      return;
    }
    /* 跨线程 port 消息：路由到本地 port 对象，不是 Worker 实例的 onmessage */
    if (globalThis.__qwrt_deliver_port_msg__ &&
        globalThis.__qwrt_deliver_port_msg__(d)) return;
    var w = workers.get(source);
    if (!w) return;
    /* 带 MessagePort 转移的 worker 消息：解包 ports + payload */
    if (d && typeof d === 'object' && d.__qwrt_ports) {
      var ports = [];
      try {
        for (var i = 0; i < d.__qwrt_ports.length; i++) {
          ports.push(globalThis.__qwrt_port_from_ref__(d.__qwrt_ports[i]));
        }
      } catch (err) { reportError(err); return; }
      var inner;
      try { inner = __qwrt_deserialize__(d.__qwrt_payload); }
      catch (err) { reportError(err); return; }
      if (isWorkerError(inner)) { w._deliverError(inner.error); return; }
      var ev2;
      try { ev2 = new MessageEvent('message', { data: inner, ports: ports }); }
      catch (err) { reportError(err); return; }
      w.dispatchEvent(ev2);
      return;
    }
    if (isWorkerError(d)) { w._deliverError(d.error); return; }
    var e;
    try {
      e = new MessageEvent('message', { data: d });
    } catch (err) {
      reportError(err);
      return;
    }
    w.dispatchEvent(e);
  };
}

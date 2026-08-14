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
  }

  Worker.prototype.postMessage = function (value) {
    var bytes = __qwrt_serialize__(value);
    pal.workerPost(this._id, bytes);
  };

  Worker.prototype.terminate = function () {
    pal.workerTerminate(this._id);
    workers.delete(this._id);
  };

  globalThis.Worker = Worker;

  // Route inbound messages: source 0 = host JSON, > 0 = worker bytes.
  var hostDispatch = self.__qwrt_dispatch__;
  globalThis.__qwrt_dispatch__ = function (data, source) {
    if (source === 0) {
      hostDispatch(data, source);
      return;
    }
    var w = workers.get(source);
    if (!w) return;
    var d;
    try { d = __qwrt_deserialize__(data); }
    catch (err) { reportError(err); return; }
    var handler = (d && d.type === 'error') ? w._onerror : w._onmsg;
    if (!handler) return;
    var e;
    try {
      e = new MessageEvent('message', { data: d });
    } catch (err) {
      reportError(err);
      return;
    }
    try { handler.call(self, e); }
    catch (err) { reportError(err); }
  };
}

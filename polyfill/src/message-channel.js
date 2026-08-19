/**
 * qwrt polyfill: MessageChannel, MessagePort, MessageEvent
 *
 * TC55/ECMA-429 requires MessageChannel for structured communication
 * between execution contexts. MessagePort extends EventTarget.
 *
 * Cross-thread MessagePort transfer (transferable):
 *   - Every port gets a globally-unique id (pal.portCreate, atomically
 *     allocated in C). A MessageChannel holds one entangled pair (id1/id2).
 *   - Same-thread messaging stays a direct JS object reference
 *     (_entangledPort). Cross-thread messaging (after a port is transferred
 *     to a Worker) goes through the existing worker byte channels
 *     (pal.workerPost parent→worker, pal.postMessage worker→parent) wrapped
 *     as {__port_msg: {target: <peerId>, payload: <serialized bytes>}}.
 *   - Every thread keeps a local registry (id → MessagePort). Inbound
 *     dispatch (worker.js / worker boot shim) recognizes {__port_msg} and
 *     routes to the local port.
 *
 * Depends on: EventTarget (must be loaded after event-target.js).
 */

export function setupMessageChannel(pal) {
  if (typeof globalThis.EventTarget !== 'function') {
    throw new Error('MessagePort requires EventTarget to be loaded first');
  }

  /* Local port registry: id → MessagePort. Each thread has its own copy of
   * this module (separate JSRuntime), so the Map is per-thread. */
  var portRegistry = new Map();
  /* True inside a worker runtime: its pal has workerClose but not workerPost. */
  var inWorker = typeof pal.workerClose === 'function';

  function registerPort(p) { if (p._id) portRegistry.set(p._id, p); }
  function lookupPort(id) { return portRegistry.get(id); }

  /* ================================================================
   * MessageEvent
   * ================================================================ */
  class MessageEvent extends Event {
    constructor(type, options) {
      super(type, options);
      this._data = options?.data ?? null;
      this._origin = options?.origin ?? '';
      this._lastEventId = options?.lastEventId ?? '';
      this._source = options?.source ?? null;
      this._ports = options?.ports ?? [];
    }

    get data() { return this._data; }
    get origin() { return this._origin; }
    get lastEventId() { return this._lastEventId; }
    get source() { return this._source; }
    get ports() { return this._ports; }
  }

  /* ================================================================
   * MessagePort
   * ================================================================ */
  class MessagePort extends EventTarget {
    constructor(id, peerId) {
      super();
      this._id = id;            /* 全局唯一（pal.portCreate） */
      this._peerId = peerId;    /* 纠缠对端 id */
      this._entangledPort = null;   /* 同线程直接引用 */
      this._peerThread = 'local';   /* 'local' | 'parent' | workerId(>0) */
      this._detached = false;   /* 已转移/关闭 */
      this._started = false;
      this._messageQueue = [];
      this._onmessage = null;
      this._onmessageerror = null;
    }

    get onmessage() { return this._onmessage; }
    set onmessage(fn) {
      if (this._onmessage) {
        this.removeEventListener('message', this._onmessage);
      }
      this._onmessage = fn;
      if (fn) {
        this.addEventListener('message', fn);
      }
      this._start();
    }

    get onmessageerror() { return this._onmessageerror; }
    set onmessageerror(fn) {
      if (this._onmessageerror) {
        this.removeEventListener('messageerror', this._onmessageerror);
      }
      this._onmessageerror = fn;
      if (fn) {
        this.addEventListener('messageerror', fn);
      }
    }

    /* 跨线程发送：消息字节 → 包装 {__qwrt_port_msg} → 经现有通道投递到对端线程 */
    _sendRemote(payloadBytes) {
      var wrapped = __qwrt_serialize__(
        { __qwrt_port_msg: { target: this._peerId, payload: payloadBytes } });
      if (inWorker) {
        /* worker → 父：worker 的 pal.postMessage（克隆字节 → 父入站） */
        pal.postMessage(wrapped);
      } else if (this._peerThread > 0) {
        /* 父 → worker：pal.workerPost(workerId, bytes) */
        pal.workerPost(this._peerThread, wrapped);
      } else {
        /* 对端在父线程但本线程是父（理论不达） */
        throw new Error('MessagePort: cannot route to parent from parent');
      }
    }

    postMessage(message, transfer) {
      if (this._detached) {
        throw new Error('MessagePort: port is detached');
      }
      if (this._peerThread === 'local') {
        if (!this._entangledPort) return;
        // Structured clone the message data
        var data;
        try {
          data = typeof globalThis.structuredClone === 'function'
            ? globalThis.structuredClone(message, transfer ? { transfer: transfer } : undefined)
            : JSON.parse(JSON.stringify(message));
        } catch (e) {
          // If structured clone fails, send a messageerror
          var errorEvent = new MessageEvent('messageerror', { data: e });
          this._entangledPort.dispatchEvent(errorEvent);
          return;
        }

        var event = new MessageEvent('message', { data: data, ports: [] });

        if (this._entangledPort._started) {
          this._entangledPort.dispatchEvent(event);
        } else {
          this._entangledPort._messageQueue.push(event);
        }
      } else {
        /* 跨线程：序列化消息（含 transfer）→ 包装 → 发送 */
        var bytes = __qwrt_serialize__(message, transfer);
        this._sendRemote(bytes);
      }
    }

    /* 入站：接收跨线程 port 消息（payload 是序列化字节） */
    _deliverRemote(payloadBytes) {
      var v;
      try { v = __qwrt_deserialize__(payloadBytes); }
      catch (err) {
        var errEvent = new MessageEvent('messageerror', { data: err });
        this.dispatchEvent(errEvent);
        return;
      }
      var event = new MessageEvent('message', { data: v, ports: [] });
      if (this._started) {
        this.dispatchEvent(event);
      } else {
        this._messageQueue.push(event);
      }
    }

    start() {
      this._start();
    }

    _start() {
      if (this._started) return;
      this._started = true;

      // Flush queued messages
      for (var i = 0; i < this._messageQueue.length; i++) {
        this.dispatchEvent(this._messageQueue[i]);
      }
      this._messageQueue = [];
    }

    close() {
      this._detached = true;
      this._entangledPort = null;
      this._started = false;
      this._messageQueue = [];
    }
  }

  /* ================================================================
   * MessageChannel
   * ================================================================ */
  class MessageChannel {
    constructor() {
      var ids = pal.portCreate();
      this._port1 = new MessagePort(ids.id1, ids.id2);
      this._port2 = new MessagePort(ids.id2, ids.id1);
      this._port1._entangledPort = this._port2;
      this._port2._entangledPort = this._port1;
      registerPort(this._port1);
      registerPort(this._port2);
    }

    get port1() { return this._port1; }
    get port2() { return this._port2; }
  }

  globalThis.MessageChannel = MessageChannel;
  globalThis.MessagePort = MessagePort;
  globalThis.MessageEvent = MessageEvent;

  /* 供 worker.js / boot shim 查询本地 port（transfer 时更新对端线程） */
  globalThis.__qwrt_lookup_port__ = lookupPort;

  /* ================================================================
   * Cross-thread routing helpers (used by worker.js / boot shim dispatch)
   * ================================================================ */

  /* 把一条跨线程 port 消息路由到本地 port 对象并投递。返回 true 表示已消费
   * （该消息是 port 消息，不应再当作普通 worker 消息处理）。 */
  globalThis.__qwrt_deliver_port_msg__ = function (msg) {
    if (!msg || typeof msg !== 'object' || !msg.__qwrt_port_msg) return false;
    var pm = msg.__qwrt_port_msg;
    var target = (pm && typeof pm === 'object') ? pm.target : null;
    var payload = (pm && typeof pm === 'object') ? pm.payload : null;
    var port = (target !== null && target !== undefined) ? lookupPort(target) : null;
    if (!port) return false;
    port._deliverRemote(payload);
    return true;
  };

  /* 反序列化 MessagePort 引用时由 structured-clone 调用：
   * info = {id, peerId, peerThread, detached} → 返回一个新的可用 MessagePort
   * 代理（转移后原对象已 detached，新引用总是新对象；同一 id 的本地表项
   * 被覆盖为新代理）。
   *
   * 纠缠关系重建：若对端（lookupPort(info.peerId)）已在本地——多跳转移把
   * port 送回它的出生线程时（父→worker→父）——重建同线程纠缠（双方
   * _peerThread='local' + _entangledPort 互指），此后两 port 直接同线程分发；
   * 否则对端在别的线程，按 info.peerThread 走远程路由（单跳路径不受影响）。 */
  globalThis.__qwrt_port_from_ref__ = function (info) {
    if (!info || info.id === undefined || info.id === null) {
      throw new DOMException('invalid MessagePort reference', 'DataCloneError');
    }
    var p = new MessagePort(info.id, info.peerId);
    p._detached = false;
    var peer = lookupPort(info.peerId);
    if (peer && peer !== p) {
      p._peerThread = 'local';
      p._entangledPort = peer;
      peer._peerThread = 'local';
      peer._entangledPort = p;
    } else {
      p._peerThread = info.peerThread || 'parent';
    }
    registerPort(p);
    return p;
  };
}

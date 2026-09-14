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
  if (typeof globalThis.structuredClone !== 'function') {
    throw new Error('MessagePort requires structuredClone to be loaded first');
  }

  /* Local port registry: (owner, id) → MessagePort. Each runtime (JSRuntime)
   * has its own copy of this module, so the Map is per-runtime.
   *
   * M-P3 endpoint identity: a port is identified by (owner, id), owner being
   * the endpoint that created it — 0 = mainRT/root, >0 = the worker id the
   * direct parent assigned at spawn (§8.2 "直接父本地分配": every runtime
   * allocates ids locally, so two isolated processes both hand out 1,2 — only
   * the owner disambiguates). Under THREAD the C allocator is process-global
   * and ids never collide, but the composite key stays unique, so behaviour is
   * unchanged there. */
  var portRegistry = new Map();
  /* True inside a worker runtime: its pal has workerClose but not workerPost. */
  var inWorker = typeof pal.workerClose === 'function';
  /* This runtime's own endpoint id. */
  var ownerSelf = inWorker ? pal.workerId() : 0;

  function portKey(owner, id) { return owner + ':' + id; }
  function registerPort(p) {
    if (p._id) portRegistry.set(portKey(p._owner, p._id), p);
  }
  function lookupPort(id, owner) {
    return portRegistry.get(portKey(owner === undefined ? ownerSelf : owner, id));
  }

  /* ── M-P3 port frames ──
   * A PORT_TRANSFER envelope payload is a 16-byte LE routing header followed by
   * opaque structured-clone bytes (ipc_envelope.h): op, dest_owner, key_owner,
   * key_port. The header lets a node decide deliver-locally vs forward without
   * decoding the payload (§7.2); the flat topology routes it here in JS, a
   * future nested-spawn relay reuses the same header unchanged. */
  var OP_PORT_MSG = 1, OP_PORT_XFER = 2, PORT_HDR = 16;

  function toU8(b) { return b instanceof Uint8Array ? b : new Uint8Array(b); }
  function rdU32(u8, off) {
    return (u8[off] | (u8[off + 1] << 8) | (u8[off + 2] << 16) |
            (u8[off + 3] << 24)) >>> 0;
  }
  function wrU32(u8, off, v) {
    u8[off] = v & 0xff; u8[off + 1] = (v >>> 8) & 0xff;
    u8[off + 2] = (v >>> 16) & 0xff; u8[off + 3] = (v >>> 24) & 0xff;
  }
  function portFrame(op, dest, keyOwner, keyPort, body) {
    var b = toU8(body);
    var u8 = new Uint8Array(PORT_HDR + b.length);
    wrU32(u8, 0, op); wrU32(u8, 4, dest);
    wrU32(u8, 8, keyOwner); wrU32(u8, 12, keyPort);
    u8.set(b, PORT_HDR);
    return u8;
  }
  /* op=2（port 转移列表）帧：投给直连对端通道，头里的 dest/key 不参与路由
   * （接收方按到达的通道确定发送者），全 0。worker.js / boot shim 构造转移帧
   * 时用它，避免在别处重复头布局。 */
  function portXferFrame(body) { return portFrame(OP_PORT_XFER, 0, 0, 0, body); }
  /* 取 PORT_TRANSFER 帧的路由头之后的不透明 SC 字节（op=2 解包用）。 */
  function portFrameBody(bytes) { return toU8(bytes).subarray(PORT_HDR); }

  /* 目标端点是否由本 runtime 投递：本端点自身，或本进程持有的根端点代理
   * （worker 侧持有主RT 转移来的 port，其归属仍是根）。 */
  function localEndpoint(owner) {
    return owner === ownerSelf || (inWorker && owner === 0);
  }

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
    constructor(id, peerId, owner) {
      super();
      this._id = id;            /* owner 本地分配（pal.portCreate） */
      this._peerId = peerId;    /* 纠缠对端的本地 id（与本 port 同 owner） */
      this._owner = owner === undefined ? ownerSelf : owner;  /* 出生端点；转移不变 */
      this._entangledPort = null;   /* 同 runtime 直接引用 */
      this._peerThread = 'local';   /* 对端当前所在：'local' | 'parent' | workerId(>0) */
      this._detached = false;   /* 已转移/关闭 */
      this._peerGone = false;   /* 对端端点已死亡（§8.2 失败语义） */
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

    /* 跨 runtime 发送：SC 消息字节 → PORT_TRANSFER 帧（kind=1）→ 对端端点通道。
     * dest = 对端当前所在端点；key = (owner, peerId) 目标 port 身份。对端端点
     * 已死 → 静默丢弃（与 terminate 后 postMessage 静默的规范语义一致）。 */
    _sendRemote(payloadBytes) {
      if (this._peerGone) return;
      var dest = this._peerThread === 'parent' ? 0 : this._peerThread;
      var frame = portFrame(OP_PORT_MSG, dest, this._owner, this._peerId,
                            payloadBytes);
      if (inWorker) {
        /* worker → 父：单通道上行，父按帧头 dest 接力 */
        pal.postMessage(frame, 1);
      } else if (globalThis.__qwrt_worker_post__) {
        /* 主RT → 目标 worker（THREAD: pal.workerPost；PROCESS: processPost）。
         * worker 已 terminate/不存在时静默丢弃（不抛）——与 C 侧
         * qwrt_worker_post 对 shutting_down worker 的优雅失败语义一致。 */
        globalThis.__qwrt_worker_post__(dest, frame, 1);
      }
    }

    postMessage(message, transfer) {
      if (this._detached) {
        throw new Error('MessagePort: port is detached');
      }
      if (this._peerThread === 'local') {
        if (!this._entangledPort) return;
        // Structured clone the message data. Clone failure must rethrow to
        // the postMessage caller (WHATWG HTML §message-port-post-steps:
        // "failed to serialize" → throw a "DataCloneError" DOMException).
        // messageerror is only for receive-side deserialization failures.
        var data = globalThis.structuredClone(
          message, transfer ? { transfer: transfer } : undefined);

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
      portRegistry.delete(portKey(this._owner, this._id));   /* 摘表：路由不再命中 */
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
  globalThis.__qwrt_port_xfer_frame__ = portXferFrame;
  globalThis.__qwrt_port_frame_body__ = portFrameBody;
  globalThis.MessageChannel = MessageChannel;
  globalThis.MessagePort = MessagePort;
  globalThis.MessageEvent = MessageEvent;

  /* 供 worker.js / boot shim 查询本地 port（transfer 时更新对端端点） */
  globalThis.__qwrt_lookup_port__ = lookupPort;

  /* ================================================================
   * Cross-process routing helpers (used by worker.js / boot shim dispatch)
   * ================================================================ */
  /* PORT_TRANSFER 帧的 op（0 = 不是 port 帧）。接收侧先看 kind=1 + op 再分流：
   * op=1 走端点路由，op=2 是 port 转移列表。 */
  globalThis.__qwrt_port_frame_op__ = function (bytes) {
    var u8;
    try { u8 = toU8(bytes); } catch (e) { return 0; }
    if (u8.length < PORT_HDR) return 0;
    return rdU32(u8, 0);
  };

  /* op=1：按帧头把 port 消息投到本 runtime 的 port，或按 dest 端点接力转发
   * （主RT → 目标 worker 句柄；worker → 父，由父继续按 dest 转发）。返回 true
   * 表示已消费。目标端点已死/无此 port → 静默丢弃（不抛）。 */
  globalThis.__qwrt_route_port_message__ = function (bytes) {
    var u8 = toU8(bytes);
    if (u8.length < PORT_HDR) return false;
    var dest = rdU32(u8, 4);
    var keyOwner = rdU32(u8, 8);
    var keyPort = rdU32(u8, 12);
    var body = u8.subarray(PORT_HDR);
    if (localEndpoint(dest)) {
      var port = portRegistry.get(portKey(keyOwner, keyPort));
      if (port) port._deliverRemote(body);
      return true;
    }
    if (inWorker) {
      /* 本进程只有一条上行通道：交父（主RT）按 dest 接力 */
      pal.postMessage(bytes, 1);
    } else if (globalThis.__qwrt_worker_post__) {
      globalThis.__qwrt_worker_post__(dest, bytes, 1);
    }
    return true;
  };

  /* 端点死亡（fd EOF / terminate）→ 清路由表（§8.2 失败语义）：
   *   - 归属该端点的代理 port：本体已随进程消失 → 摘表并置 peerGone；
   *   - 对端在该端点的本地 port：派发 'error' 事件（对端不可达），后续
   *     postMessage 静默丢弃。幂等（重复死亡通知无副作用）。 */
  globalThis.__qwrt_endpoint_dead__ = function (owner) {
    var dead = [];
    portRegistry.forEach(function (p, k) {
      if (p._owner === owner) dead.push([k, p, 'own']);
      else if (p._peerThread === owner) dead.push([k, p, 'peer']);
    });
    for (var i = 0; i < dead.length; i++) {
      var k = dead[i][0], p = dead[i][1], why = dead[i][2];
      /* 一律只置 _peerGone（postMessage 静默丢弃），**不**置 _detached——后者
       * 会让 postMessage 抛错，而 terminate 后 postMessage 的规范语义是静默。 */
      p._peerGone = true;
      if (why === 'own') {
        p._entangledPort = null;
        portRegistry.delete(k);   /* 摘表：路由不再命中该端点的 port */
      } else {
        var ev;
        try {
          ev = new Event('error');
          ev.message = 'MessagePort: peer endpoint ' + owner + ' is gone';
        } catch (e) { ev = { type: 'error' }; }
        try { p.dispatchEvent(ev); } catch (e2) { /* 监听器异常不影响清理 */ }
      }
    }
  };

  /* 反序列化 MessagePort 引用时由 structured-clone 调用：
   * info = {id, peerId, owner, peerThread} → 返回一个新的可用 MessagePort
   * 代理（转移后原对象已 detached，新引用总是新对象；同一 (owner,id) 的本地
   * 表项被覆盖为新代理）。
   *
   * 纠缠关系重建：若对端（lookupPort(info.peerId, info.owner)）已在本地——
   * 多跳转移把 port 送回它的出生 runtime 时（父→worker→父）——重建同 runtime
   * 纠缠（双方 _peerThread='local' + _entangledPort 互指），此后两 port 直接
   * 本地分发；否则对端在别的端点，按 info.peerThread 走远程路由。 */
  globalThis.__qwrt_port_from_ref__ = function (info) {
    if (!info || info.id === undefined || info.id === null) {
      throw new DOMException('invalid MessagePort reference', 'DataCloneError');
    }
    var owner = info.owner === undefined ? ownerSelf : info.owner;
    var p = new MessagePort(info.id, info.peerId, owner);
    p._detached = false;
    var peer = lookupPort(info.peerId, owner);
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

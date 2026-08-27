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
 */
(function(pal){
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

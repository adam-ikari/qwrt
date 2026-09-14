/**
 * qwrt Polyfill - localStorage (Web Storage, Storage interface)
 *
 * 双形态（M-P4 §10.2 单所有者代理）：
 *
 *   A. 所有者（主RT runtime，pal.workerId 不存在）——同进程语义不变：
 *      - path: pal.localStoragePath() — env QWRT_LOCALSTORAGE_FILE, else
 *        ~/.qwrt/localstorage.json (HOME unset → .qwrt-localstorage.json in cwd)
 *      - loaded synchronously at setup (pal.fsReadSync; missing/corrupt → empty)
 *      - every setItem/removeItem/clear writes back atomically (pal.fsWriteSync:
 *        temp file + rename)
 *      - quota: 5 MiB (key + value UTF-16 code units) → DOMException
 *        'QuotaExceededError'
 *      - 额外注册 __qwrt_storage_dispatch__：处理 PROCESS worker 经
 *        kind=STORAGE 信封路由来的 store 操作（payload = structured clone
 *        {op,key,value?,storageDomain}），对同一 map/persist 执行后经
 *        __qwrt_worker_post__(source, replyBytes, kind=4) 回发结果。
 *
 *   B. PROCESS worker（pal.workerId 存在 且 pal.workerBackend()==='process'）
 *      ——同步代理：每个 getItem/setItem/removeItem/clear/key/length 经
 *      pal.storageSync（C 层同步 RPC：发 kind=STORAGE 信封 → 阻塞等待主RT
 *      回复，期间不派发任何 JS）路由到所有者执行，API 语义（同步、异常
 *      形状）与同进程完全一致。所有者死亡 → fd EOF → storageSync 抛错 →
 *      worker 按 §9.4 连锁自杀。
 *
 * THREAD worker（pal.workerId 存在 且 backend==='thread'）不挂 localStorage
 * （Web Storage 保守默认：worker 无 DOM 场景；THREAD 基线零改动）。
 *
 * 与 qwrt.storage（async in-memory extension API）共存。
 */

/* 组装 Storage 接口实例（方法/访问器不可枚举）并挂到 globalThis.localStorage。 */
function buildStorageObject(impl) {
  var storage = {};
  Object.defineProperties(storage, {
    length: { get: impl.length, enumerable: false, configurable: true },
    key: { value: impl.key, writable: true, enumerable: false, configurable: true },
    getItem: { value: impl.getItem, writable: true, enumerable: false, configurable: true },
    setItem: { value: impl.setItem, writable: true, enumerable: false, configurable: true },
    removeItem: { value: impl.removeItem, writable: true, enumerable: false, configurable: true },
    clear: { value: impl.clear, writable: true, enumerable: false, configurable: true },
  });
  return storage;
}

function mountGlobal(storage) {
  try {
    Object.defineProperty(globalThis, 'localStorage', {
      value: storage, writable: false, enumerable: true, configurable: true,
    });
  } catch (e) {
    /* 已存在且不可重新定义：保持现状（与旧实现一致） */
  }
}

export function setupLocalStorage(pal) {
  var isWorker = typeof pal.workerId === 'function';
  var backend;
  try { backend = pal.workerBackend(); } catch (e) { backend = 'thread'; }
  /* THREAD worker：不挂（基线）。接住 localStoragePath/workerBackend 抛错的
   * 非标准环境（如 service worker 线程）与现状一致：静默跳过。 */
  if (isWorker && backend !== 'process') return;
  if (typeof globalThis.localStorage !== 'undefined') return;

  /* ================= PROCESS worker：同步代理（M-P4 §10.2） =================
   * 每个操作序列化为 {op,key,value?,storageDomain}（structured clone 字节，
   * 复用现有序列化，不给 storage 另设 fb 表）→ pal.storageSync 同步往返主RT
   * 所有者 → 结果/异常按原语义返回。storageDomain 恒 'localStorage'（§4.1
   * payload 契约字段，为将来 sessionStorage 等留位，所有者单域处理）。 */
  if (isWorker) {
    function request(op, key, value) {
      var req = __qwrt_serialize__({ op: op, key: key, value: value,
                                     storageDomain: 'localStorage' });
      var rep = pal.storageSync(req);   /* Uint8Array；所有者死亡 → 抛 InternalError */
      var r = __qwrt_deserialize__(rep);
      if (r && r.e) {
        var name = String(r.e.name || 'Error');
        var msg = String(r.e.message != null ? r.e.message : r.e);
        var ex;
        try { ex = new DOMException(msg, name); }
        catch (err) { ex = new Error(msg); ex.name = name; }
        throw ex;
      }
      return r ? r.v : undefined;
    }
    mountGlobal(buildStorageObject({
      length: function () { return request('length'); },
      key: function (i) { return request('key', i >>> 0); },
      getItem: function (k) { return request('get', String(k)); },
      setItem: function (k, v) { request('set', String(k), String(v)); },
      removeItem: function (k) { request('remove', String(k)); },
      clear: function () { request('clear'); },
    }));
    return;
  }

  /* ================= 所有者（主RT）：文件持久化 Storage 实现 ================= */
  var path;
  try {
    path = pal.localStoragePath();
  } catch (e) {
    return;
  }

  /* Storage area. map: null prototype so keys like '__proto__' can't pollute;
   * keys: insertion order (spec key(n) order; updating an existing key does
   * not reorder); total: sum of key.length + value.length (quota accounting). */
  var map = Object.create(null);
  var keys = [];
  var total = 0;
  var QUOTA = 5 * 1024 * 1024;   /* Web Storage default, in code units */

  function has(key) {
    return Object.prototype.hasOwnProperty.call(map, key);
  }

  function load() {
    var raw, obj, ks, i;
    try { raw = pal.fsReadSync(path); } catch (e) { return; }  /* missing → empty */
    try { obj = JSON.parse(raw); } catch (e) { return; }       /* corrupt → empty */
    if (!obj || typeof obj !== 'object' || Array.isArray(obj)) return;
    ks = Object.keys(obj);
    for (i = 0; i < ks.length; i++) {
      if (!Object.prototype.hasOwnProperty.call(obj, ks[i])) continue;
      map[ks[i]] = String(obj[ks[i]]);
      keys.push(ks[i]);
      total += ks[i].length + map[ks[i]].length;
    }
  }

  function persist() {
    pal.fsWriteSync(path, JSON.stringify(map));
  }

  function getItem(key) {
    key = String(key);
    return has(key) ? map[key] : null;
  }

  function setItem(key, value) {
    key = String(key);
    value = String(value);
    var existed = has(key);
    var oldValue = existed ? map[key] : null;
    var add = existed ? (value.length - oldValue.length) : (key.length + value.length);
    if (total + add > QUOTA) {
      throw new DOMException(
        "Failed to execute 'setItem' on 'Storage': setting the value of '" +
        key + "' exceeded the quota.",
        'QuotaExceededError');
    }
    if (!existed) keys.push(key);
    map[key] = value;
    total += add;
    try {
      persist();
    } catch (e) {
      /* 落盘失败：回滚内存态，保持与持久化文件一致 */
      if (existed) {
        map[key] = oldValue;
      } else {
        delete map[key];
        keys.pop();
      }
      total -= add;
      throw e;
    }
  }

  function removeItem(key) {
    key = String(key);
    if (!has(key)) return;
    var oldValue = map[key];
    var idx = keys.indexOf(key);
    if (idx >= 0) keys.splice(idx, 1);
    total -= key.length + oldValue.length;
    delete map[key];
    try {
      persist();
    } catch (e) {
      if (idx >= 0) keys.splice(idx, 0, key);
      map[key] = oldValue;
      total += key.length + oldValue.length;
      throw e;
    }
  }

  function clear() {
    var oldMap = map, oldKeys = keys, oldTotal = total;
    map = Object.create(null);
    keys = [];
    total = 0;
    try {
      persist();
    } catch (e) {
      map = oldMap;
      keys = oldKeys;
      total = oldTotal;
      throw e;
    }
  }

  function key(index) {
    index = index >>> 0;   /* WebIDL unsigned long */
    return index < keys.length ? keys[index] : null;
  }

  load();

  /* M-P4 §10.2（所有者侧）：处理 PROCESS worker 的 storage 请求帧。C 层只
   * 透传信封（qwrt_storage_dispatch），op 编排在这里——对同一 map/persist
   * 执行，异常（配额/落盘）原样封装成 {e:{name,message}} 回发，代理侧据此
   * 重建 DOMException，同步 API 语义跨进程保持一致。 */
  globalThis.__qwrt_storage_dispatch__ = function (bytes, source) {
    var o;
    try { o = __qwrt_deserialize__(bytes); } catch (err) { return; }
    if (!o || typeof o !== 'object') return;
    var reply;
    try {
      switch (o.op) {
        case 'get':    reply = { v: getItem(o.key) }; break;
        case 'set':    setItem(o.key, o.value); reply = { v: null }; break;
        case 'remove': removeItem(o.key); reply = { v: null }; break;
        case 'clear':  clear(); reply = { v: null }; break;
        case 'key':    reply = { v: key(o.key) }; break;
        case 'length': reply = { v: keys.length }; break;
        default:       reply = { e: { name: 'Error',
                                      message: 'unknown storage op: ' + o.op } };
      }
    } catch (err) {
      reply = { e: { name: (err && err.name) || 'Error',
                     message: (err && err.message != null)
                                ? String(err.message) : String(err) } };
    }
    var rep;
    try { rep = __qwrt_serialize__(reply); } catch (err) { return; }
    if (typeof globalThis.__qwrt_worker_post__ === 'function')
      globalThis.__qwrt_worker_post__(source, rep, 4);   /* kind=STORAGE */
  };

  /* Storage 接口实例：方法/访问器均不可枚举（Object.keys(localStorage) 为空）。 */
  mountGlobal(buildStorageObject({
    length: function () { return keys.length; },
    key: key,
    getItem: getItem,
    setItem: setItem,
    removeItem: removeItem,
    clear: clear,
  }));
}

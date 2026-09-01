/**
 * qwrt Polyfill - localStorage (Web Storage, Storage interface)
 *
 * Synchronous key-value storage persisted across restarts to a JSON file:
 *   - path: pal.localStoragePath() — env QWRT_LOCALSTORAGE_FILE, else
 *     ~/.qwrt/localstorage.json (HOME unset → .qwrt-localstorage.json in cwd)
 *   - loaded synchronously at setup (pal.fsReadSync; missing/corrupt → empty)
 *   - every setItem/removeItem/clear writes back atomically (pal.fsWriteSync:
 *     temp file + rename)
 *   - quota: 5 MiB (key + value UTF-16 code units) → DOMException
 *     'QuotaExceededError'
 *
 * Worker contexts do NOT get localStorage (pal.workerId present → skip); the
 * parent runtime is the only place the Web Storage API is mounted. Coexists
 * with qwrt.storage (async in-memory extension API).
 */

export function setupLocalStorage(pal) {
  /* Worker thread: no DOM / Web-Storage scenario — parent only. */
  if (typeof pal.workerId === 'function') return;
  if (typeof globalThis.localStorage !== 'undefined') return;

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

  /* Storage 接口实例：方法/访问器均不可枚举（Object.keys(localStorage) 为空）。 */
  var storage = {};
  Object.defineProperties(storage, {
    length: { get: function () { return keys.length; },
              enumerable: false, configurable: true },
    key: { value: key, writable: true, enumerable: false, configurable: true },
    getItem: { value: getItem, writable: true, enumerable: false, configurable: true },
    setItem: { value: setItem, writable: true, enumerable: false, configurable: true },
    removeItem: { value: removeItem, writable: true, enumerable: false, configurable: true },
    clear: { value: clear, writable: true, enumerable: false, configurable: true },
  });

  load();
  try {
    Object.defineProperty(globalThis, 'localStorage', {
      value: storage, writable: false, enumerable: true, configurable: true,
    });
  } catch (e) {
    /* 已存在/不可定义：保持现状 */
  }
}

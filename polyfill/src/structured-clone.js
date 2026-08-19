/**
 * qwrt polyfill: structuredClone (enhanced)
 *
 * Replaces the basic JSON.parse(JSON.stringify()) implementation
 * with proper structured clone that handles:
 *   - TypedArrays (all types)
 *   - ArrayBuffer / DataView
 *   - Blob / File
 *   - Error (and subtypes)
 *   - Map / Set
 *   - Date
 *   - RegExp
 *   - Circular references
 *   - Infinity / NaN / -0
 *
 * TC55/ECMA-429 requires structuredClone to handle these types.
 *
 * Pure JS - no PAL primitives needed.
 */

export function setupStructuredClone() {

  /**
   * structuredClone(value, options)
   *
   * Deep clones a value using the structured clone algorithm.
   * Handles circular references and special JS types.
   */
  globalThis.structuredClone = function structuredClone(value, options) {
    /* transfer 列表校验（v1 只支持 ArrayBuffer） */
    var transferSet = null;
    if (options && options.transfer !== undefined && options.transfer !== null) {
      if (!Array.isArray(options.transfer))
        throw new DOMException('transfer must be a sequence', 'DataCloneError');
      transferSet = new Set();
      for (var i = 0; i < options.transfer.length; i++) {
        var t = options.transfer[i];
        if (transferSet.has(t))
          throw new DOMException('duplicate transferable', 'DataCloneError');
        /* transferable: ArrayBuffer, 或 MessagePort（v1 只这两种） */
        var isPort = typeof globalThis.MessagePort === 'function' &&
                     t instanceof globalThis.MessagePort;
        if (!(t instanceof ArrayBuffer) && !isPort)
          throw new DOMException('object is not transferable', 'DataCloneError');
        transferSet.add(t);
      }
    }
    /* 把 transferSet 挂到私有字段，随 clone 递归自动传递（不改用户对象） */
    if (options && typeof options === 'object') {
      options = { transfer: options.transfer, _qwrtTransfer: transferSet };
    } else {
      options = { _qwrtTransfer: transferSet };
    }
    var seen = new Map();
    var result = clone(value, seen, options);
    /* 未被消息引用的 transfer 对象也要 detach（ArrayBuffer: transfer()；
     * MessagePort: 标记 detached——同线程转移返回原引用，仅表达不可再用） */
    if (transferSet) {
      transferSet.forEach(function (t) {
        if (t instanceof ArrayBuffer) {
          if (!t.detached) t.transfer();
        } else if (typeof t._detached === 'boolean') {
          t._detached = true;
        }
      });
    }
    return result;
  };

  function clone(value, seen, options) {
    // Primitives: return as-is (handles null, undefined, boolean, number, string, bigint, symbol)
    if (value === null || value === undefined) return value;
    var type = typeof value;
    if (type === 'boolean' || type === 'number' || type === 'string' || type === 'bigint') {
      return value;
    }
    if (type === 'symbol') {
      throw new DOMException('Symbols cannot be cloned', 'DataCloneError');
    }

    // Check for circular reference
    if (typeof value === 'object' || typeof value === 'function') {
      if (seen.has(value)) {
        return seen.get(value);
      }
    }

    // Handle functions — cannot be cloned
    if (typeof value === 'function') {
      throw new DOMException('Functions cannot be cloned', 'DataCloneError');
    }

    // Date
    if (value instanceof Date) {
      return new Date(value.getTime());
    }

    // RegExp
    if (value instanceof RegExp) {
      return new RegExp(value.source, value.flags);
    }

    // Error types
    if (value instanceof Error) {
      var Ctor = value.constructor;
      if (Ctor === Error || Ctor === TypeError || Ctor === RangeError ||
          Ctor === SyntaxError || Ctor === URIError || Ctor === ReferenceError ||
          Ctor === EvalError) {
        var err = new Ctor(value.message);
        err.stack = value.stack;
        return err;
      }
      // DOMException
      if (typeof DOMException === 'function' && value instanceof DOMException) {
        return new DOMException(value.message, value.name);
      }
      // Generic Error
      var err = new Error(value.message);
      err.name = value.name;
      err.stack = value.stack;
      return err;
    }

    // Map
    if (value instanceof Map) {
      var result = new Map();
      seen.set(value, result);
      value.forEach(function(v, k) {
        result.set(clone(k, seen, options), clone(v, seen, options));
      });
      return result;
    }

    // Set
    if (value instanceof Set) {
      var result = new Set();
      seen.set(value, result);
      value.forEach(function(v) {
        result.add(clone(v, seen, options));
      });
      return result;
    }

    // MessagePort：只可转移（transfer 列表），不可克隆。
    if (typeof globalThis.MessagePort === 'function' &&
        value instanceof globalThis.MessagePort) {
      var ts = options && options._qwrtTransfer;
      if (ts && ts.has(value)) {
        ts.delete(value);
        /* 转移：原 port detached；返回一个新的可用代理（同线程下与原
         * 对端纠缠）。复用 message-channel 的 __qwrt_port_from_ref__。 */
        if (globalThis.__qwrt_port_from_ref__) {
          var peer = value._entangledPort;
          value._detached = true;
          var pr = globalThis.__qwrt_port_from_ref__(
            { id: value._id, peerId: value._peerId, peerThread: 'local' });
          if (peer && pr) pr._entangledPort = peer;
          seen.set(value, pr);
          return pr;
        }
        seen.set(value, value);
        return value;
      }
      throw new DOMException('MessagePort cannot be cloned (use transfer)', 'DataCloneError');
    }

    // ArrayBuffer
    if (value instanceof ArrayBuffer) {
      var ts = options && options._qwrtTransfer;
      var result;
      if (ts && ts.has(value)) {
        ts.delete(value);
        result = value.transfer();   /* 内容转移到新 buffer，原 buffer detached */
      } else {
        result = value.slice(0);
      }
      seen.set(value, result);
      return result;
    }

    // DataView
    if (value instanceof DataView) {
      var buf = clone(value.buffer, seen, options);
      return new DataView(buf, value.byteOffset, value.byteLength);
    }

    // TypedArrays
    if (value instanceof Int8Array) return cloneTypedArray(value, Int8Array, seen);
    if (value instanceof Uint8Array) return cloneTypedArray(value, Uint8Array, seen);
    if (value instanceof Uint8ClampedArray) return cloneTypedArray(value, Uint8ClampedArray, seen);
    if (value instanceof Int16Array) return cloneTypedArray(value, Int16Array, seen);
    if (value instanceof Uint16Array) return cloneTypedArray(value, Uint16Array, seen);
    if (value instanceof Int32Array) return cloneTypedArray(value, Int32Array, seen);
    if (value instanceof Uint32Array) return cloneTypedArray(value, Uint32Array, seen);
    if (value instanceof Float32Array) return cloneTypedArray(value, Float32Array, seen);
    if (value instanceof Float64Array) return cloneTypedArray(value, Float64Array, seen);
    if (typeof BigInt64Array !== 'undefined' && value instanceof BigInt64Array)
      return cloneTypedArray(value, BigInt64Array, seen);
    if (typeof BigUint64Array !== 'undefined' && value instanceof BigUint64Array)
      return cloneTypedArray(value, BigUint64Array, seen);

    // Blob
    if (typeof Blob !== 'undefined' && value instanceof Blob) {
      return new Blob([value], { type: value.type });
    }

    // File
    if (typeof File !== 'undefined' && value instanceof File) {
      return new File([value], value.name, { type: value.type, lastModified: value.lastModified });
    }

    // Array
    if (Array.isArray(value)) {
      var result = [];
      seen.set(value, result);
      for (var i = 0; i < value.length; i++) {
        result[i] = clone(value[i], seen, options);
      }
      return result;
    }

    // Plain object
    if (value.constructor === Object || !value.constructor) {
      var result = {};
      seen.set(value, result);
      var keys = Object.keys(value);
      for (var i = 0; i < keys.length; i++) {
        result[keys[i]] = clone(value[keys[i]], seen, options);
      }
      return result;
    }

    // Objects with custom constructor — try to clone as plain object
    // (structured clone spec: only certain types are cloneable)
    var result = {};
    seen.set(value, result);
    try {
      var keys = Object.keys(value);
      for (var i = 0; i < keys.length; i++) {
        result[keys[i]] = clone(value[keys[i]], seen, options);
      }
    } catch (e) {
      // If we can't enumerate, just return empty
    }
    return result;
  }

  function cloneTypedArray(value, Ctor, seen) {
    var result = new Ctor(value);
    seen.set(value, result);
    return result;
  }

  /* ================================================================
   * 字节序列化（worker 跨线程传输 / 挂起恢复共用）
   *
   * serializeToBytes(value) -> ArrayBuffer，deserializeFromBytes(buf) -> value。
   * 自定 tag 流（LE 字节序），支持循环引用与 TypedArray/ArrayBuffer/Blob 等；
   * 函数/符号 → DataCloneError。v1 无 transferables。
   * 挂为 globalThis.__qwrt_serialize__ / __qwrt_deserialize__。
   * ================================================================ */

  var TA_CTORS = [Int8Array, Uint8Array, Uint8ClampedArray, Int16Array, Uint16Array,
                  Int32Array, Uint32Array, Float32Array, Float64Array];
  if (typeof BigInt64Array !== 'undefined') TA_CTORS.push(BigInt64Array, BigUint64Array);

  function utf8Encode(s) {
    var out = [];
    for (var i = 0; i < s.length; i++) {
      var c = s.charCodeAt(i);
      if (c < 0x80) {
        out.push(c);
      } else if (c < 0x800) {
        out.push(0xc0 | (c >> 6), 0x80 | (c & 0x3f));
      } else if (c < 0xd800 || c >= 0xe000) {
        out.push(0xe0 | (c >> 12), 0x80 | ((c >> 6) & 0x3f), 0x80 | (c & 0x3f));
      } else {
        var c2 = s.charCodeAt(++i);
        var cp = 0x10000 + ((c & 0x3ff) << 10) + (c2 & 0x3ff);
        out.push(0xf0 | (cp >> 18), 0x80 | ((cp >> 12) & 0x3f),
                 0x80 | ((cp >> 6) & 0x3f), 0x80 | (cp & 0x3f));
      }
    }
    return out;
  }

  function utf8Decode(u8, start, len) {
    var out = '';
    var i = start, end = start + len;
    while (i < end) {
      var b = u8[i];
      if (b < 0x80) {
        out += String.fromCharCode(b); i += 1;
      } else if (b < 0xe0) {
        out += String.fromCharCode(((b & 0x1f) << 6) | (u8[i + 1] & 0x3f)); i += 2;
      } else if (b < 0xf0) {
        out += String.fromCharCode(((b & 0x0f) << 12) | ((u8[i + 1] & 0x3f) << 6) |
                                   (u8[i + 2] & 0x3f)); i += 3;
      } else {
        var cp = ((b & 0x07) << 18) | ((u8[i + 1] & 0x3f) << 12) |
                 ((u8[i + 2] & 0x3f) << 6) | (u8[i + 3] & 0x3f);
        var u = cp - 0x10000;
        out += String.fromCharCode(0xd800 + (u >> 10)) + String.fromCharCode(0xdc00 + (u & 0x3ff));
        i += 4;
      }
    }
    return out;
  }

  function ByteWriter() {
    var bytes = [];
    return {
      u8: function (b) { bytes.push(b & 0xff); },
      u32: function (v) {
        bytes.push(v & 0xff, (v >>> 8) & 0xff, (v >>> 16) & 0xff, (v >>> 24) & 0xff);
      },
      f64: function (v) {
        var ab = new ArrayBuffer(8), f = new Float64Array(ab), u = new Uint8Array(ab);
        f[0] = v;
        for (var i = 0; i < 8; i++) bytes.push(u[i]);
      },
      raw: function (u8) {
        for (var i = 0; i < u8.length; i++) bytes.push(u8[i]);
      },
      done: function () { return new Uint8Array(bytes).buffer; },
    };
  }

  function encodeString(bytes, s) {
    var u = utf8Encode(String(s));
    bytes.u32(u.length);
    for (var i = 0; i < u.length; i++) bytes.u8(u[i]);
  }

  function serializeToBytes(value, transfer) {
    /* transfer 列表校验：v1 只支持 ArrayBuffer；重复/不可转移 → DataCloneError */
    var transferSet = null;
    if (transfer !== undefined && transfer !== null) {
      if (!Array.isArray(transfer))
        throw new DOMException('transfer must be a sequence', 'DataCloneError');
      transferSet = new Set();
      for (var i = 0; i < transfer.length; i++) {
        var t = transfer[i];
        if (transferSet.has(t))
          throw new DOMException('duplicate transferable', 'DataCloneError');
        var isPort = typeof globalThis.MessagePort === 'function' &&
                     t instanceof globalThis.MessagePort;
        if (!(t instanceof ArrayBuffer) && !isPort)
          throw new DOMException('object is not transferable', 'DataCloneError');
        transferSet.add(t);
      }
    }
    var refs = new Map();     /* object -> 索引（首次出现分配） */
    var next = 0;
    var bytes = ByteWriter();

    function w(v) {
      if (v === null) { bytes.u8(0x01); return; }
      if (v === undefined) { bytes.u8(0x02); return; }
      var t = typeof v;
      if (t === 'boolean') { bytes.u8(v ? 0x03 : 0x04); return; }
      if (t === 'number') {
        if (Number.isInteger(v) && !Object.is(v, -0) &&
            v >= -2147483648 && v <= 2147483647) {
          bytes.u8(0x05); bytes.u32(v >>> 0);
        } else {
          bytes.u8(0x06); bytes.f64(v);
        }
        return;
      }
      if (t === 'string') { bytes.u8(0x07); encodeString(bytes, v); return; }
      if (t === 'bigint') { bytes.u8(0x1F); encodeString(bytes, v.toString()); return; }
      if (t === 'symbol') throw new DOMException('Symbols cannot be cloned', 'DataCloneError');
      if (t === 'function') throw new DOMException('Functions cannot be cloned', 'DataCloneError');

      if (refs.has(v)) { bytes.u8(0x1E); bytes.u32(refs.get(v)); return; }

      if (v instanceof Date) { refs.set(v, next++); bytes.u8(0x08); bytes.f64(v.getTime()); return; }
      if (v instanceof RegExp) {
        refs.set(v, next++); bytes.u8(0x09);
        encodeString(bytes, v.source); encodeString(bytes, v.flags);
        return;
      }
      if (v instanceof Error) {
        refs.set(v, next++); bytes.u8(0x0A);
        encodeString(bytes, v.name || 'Error'); encodeString(bytes, v.message || '');
        return;
      }
      if (typeof File !== 'undefined' && v instanceof File) {
        refs.set(v, next++); bytes.u8(0x1B);
        encodeString(bytes, v.name); encodeString(bytes, v.type || '');
        bytes.f64(v.lastModified);
        var fbytes = (typeof v._getBytes === 'function') ? v._getBytes() : null;
        if (!fbytes) throw new DOMException('File cannot be cloned', 'DataCloneError');
        bytes.u32(fbytes.length); bytes.raw(fbytes);
        return;
      }
      if (typeof Blob !== 'undefined' && v instanceof Blob) {
        refs.set(v, next++); bytes.u8(0x1A);
        encodeString(bytes, v.type || '');
        var bbytes = (typeof v._getBytes === 'function') ? v._getBytes() : null;
        if (!bbytes) throw new DOMException('Blob cannot be cloned', 'DataCloneError');
        bytes.u32(bbytes.length); bytes.raw(bbytes);
        return;
      }
      if (v instanceof Map) {
        refs.set(v, next++); bytes.u8(0x0B);
        bytes.u32(v.size);
        v.forEach(function (val, key) { w(key); w(val); });
        return;
      }
      if (v instanceof Set) {
        refs.set(v, next++); bytes.u8(0x0C);
        bytes.u32(v.size);
        v.forEach(function (val) { w(val); });
        return;
      }
      /* MessagePort：transfer 列表中的 → 编码为 __qwrt_port_ref；否则不可克隆 */
      if (typeof globalThis.MessagePort === 'function' &&
          v instanceof globalThis.MessagePort) {
        if (!transferSet || !transferSet.has(v))
          throw new DOMException('MessagePort cannot be cloned (use transfer)', 'DataCloneError');
        transferSet.delete(v);
        v._detached = true;
        refs.set(v, next++); bytes.u8(0x20);
        bytes.u32(v._id || 0);
        bytes.u32(v._peerId || 0);
        encodeString(bytes, v._peerThread || 'local');
        return;
      }
      if (v instanceof ArrayBuffer) {
        refs.set(v, next++); bytes.u8(0x0D);
        var au8 = new Uint8Array(v);
        bytes.u32(au8.length); bytes.raw(au8);
        if (transferSet && transferSet.has(v)) {
          transferSet.delete(v);
          v.transfer();   /* detach 原 buffer（返回的新 buffer 丢弃） */
        }
        return;
      }
      if (v instanceof DataView) {
        refs.set(v, next++); bytes.u8(0x0E);
        w(v.buffer);                        /* 嵌套写入（占新 ref 索引） */
        bytes.u32(v.byteOffset); bytes.u32(v.byteLength);
        return;
      }
      for (var i = 0; i < TA_CTORS.length; i++) {
        if (v instanceof TA_CTORS[i]) {
          refs.set(v, next++); bytes.u8(0x0F + i);
          var tu8 = new Uint8Array(v.buffer, v.byteOffset, v.byteLength);
          bytes.u32(tu8.length); bytes.raw(tu8);
          return;
        }
      }
      if (Array.isArray(v)) {
        refs.set(v, next++); bytes.u8(0x1C);
        bytes.u32(v.length);
        for (var i = 0; i < v.length; i++) w(v[i]);
        return;
      }
      /* 普通对象或自定义构造对象——按普通对象克隆 */
      refs.set(v, next++); bytes.u8(0x1D);
      var keys;
      try { keys = Object.keys(v); } catch (e) { keys = []; }
      bytes.u32(keys.length);
      for (var i = 0; i < keys.length; i++) { encodeString(bytes, keys[i]); w(v[keys[i]]); }
    }

    w(value);
    /* 序列化完成后，detach 未被消息引用的 transfer 对象 */
    if (transferSet) {
      transferSet.forEach(function (ab) {
        if (!ab.detached) ab.transfer();
      });
    }
    return bytes.done();
  }

  function ByteReader(u8) {
    var i = 0;
    return {
      u8: function () { return u8[i++]; },
      u32: function () {
        var v = u8[i] | (u8[i + 1] << 8) | (u8[i + 2] << 16) | (u8[i + 3] << 24);
        i += 4;
        return v >>> 0;
      },
      f64: function () {
        var ab = new ArrayBuffer(8), f = new Float64Array(ab), u = new Uint8Array(ab);
        for (var j = 0; j < 8; j++) u[j] = u8[i + j];
        i += 8;
        return f[0];
      },
      str: function () {
        var n = this.u32();
        var s = utf8Decode(u8, i, n);
        i += n;
        return s;
      },
      bytes: function (n) {
        var out = new Uint8Array(n);
        for (var j = 0; j < n; j++) out[j] = u8[i + j];
        i += n;
        return out.buffer;
      },
    };
  }

  function deserializeFromBytes(buf) {
    var r = ByteReader(new Uint8Array(buf));
    var refs = [];            /* 解码时的对象表（与编码 refs 索引一一对应） */

    function rd() {
      var tag = r.u8();
      switch (tag) {
        case 0x01: return null;
        case 0x02: return undefined;
        case 0x03: return true;
        case 0x04: return false;
        case 0x05: return r.u32() | 0;
        case 0x06: return r.f64();
        case 0x07: return r.str();
        case 0x08: return new Date(r.f64());
        case 0x09: return new RegExp(r.str(), r.str());
        case 0x0A: {
          var nm = r.str(), ms = r.str();
          var e = new Error(ms);
          e.name = nm;
          refs.push(e);
          return e;
        }
        case 0x0B: {
          var n = r.u32(); var m = new Map(); refs.push(m);
          for (var i = 0; i < n; i++) m.set(rd(), rd());
          return m;
        }
        case 0x0C: {
          var n = r.u32(); var s = new Set(); refs.push(s);
          for (var i = 0; i < n; i++) s.add(rd());
          return s;
        }
        case 0x0D: {
          var n = r.u32(); var ab = r.bytes(n); refs.push(ab);
          return ab;
        }
        case 0x0E: {
          var idx = refs.length; refs.push(null);   /* 占位，保持索引对齐 */
          var b = rd();
          var off = r.u32(), len = r.u32();
          var dv = new DataView(b, off, len);
          refs[idx] = dv;
          return dv;
        }
        default: {
          if (tag >= 0x0F && tag <= 0x19) {
            var Ctor = TA_CTORS[tag - 0x0F];
            var n = r.u32();
            var ta = new Ctor(r.bytes(n));
            refs.push(ta);
            return ta;
          }
          if (tag === 0x1A) {
            var type = r.str(), n = r.u32();
            var bl = new Blob([r.bytes(n)], { type: type });
            refs.push(bl);
            return bl;
          }
          if (tag === 0x1B) {
            var nm = r.str(), type = r.str(), lm = r.f64(), n = r.u32();
            var fl = new File([r.bytes(n)], nm, { type: type, lastModified: lm });
            refs.push(fl);
            return fl;
          }
          if (tag === 0x1C) {
            var n = r.u32(); var a = []; refs.push(a);
            for (var i = 0; i < n; i++) a[i] = rd();
            return a;
          }
          if (tag === 0x1D) {
            var n = r.u32(); var o = {}; refs.push(o);
            for (var i = 0; i < n; i++) { var k = r.str(); o[k] = rd(); }
            return o;
          }
          if (tag === 0x1E) return refs[r.u32()];
          if (tag === 0x1F) return BigInt(r.str());
          if (tag === 0x20) {
            /* MessagePort 引用：__qwrt_port_from_ref__ 创建/复用本地代理 */
            var pid = r.u32(), ppeer = r.u32(), pth = r.str();
            var portRef;
            if (globalThis.__qwrt_port_from_ref__) {
              portRef = globalThis.__qwrt_port_from_ref__(
                { id: pid, peerId: ppeer, peerThread: pth });
            } else {
              throw new DOMException('MessagePort reference requires message-channel', 'DataCloneError');
            }
            refs.push(portRef);
            return portRef;
          }
          throw new DOMException('Bad serialized data', 'DataCloneError');
        }
      }
    }

    return rd();
  }

  globalThis.__qwrt_serialize__ = serializeToBytes;
  globalThis.__qwrt_deserialize__ = deserializeFromBytes;
}

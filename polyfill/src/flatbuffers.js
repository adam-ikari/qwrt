/**
 * qwrt polyfill: FlatBuffers — dynamic .fbs schema parser + binary codec.
 *
 * Scope (gRPC/HTTP2 Phase 2): a high-performance internal serialization layer,
 * exposed standalone as `globalThis.flatbuffers` and used by the gRPC client as
 * an alternative payload codec next to protobuf. Pure JS, no dependencies, no
 * `pal`, QuickJS-safe ES2020.
 *
 * Implemented: namespace, table, struct (inline, incl. fixed arrays), enum
 * (explicit/auto values), union (auto `_type` slot), scalars (bool/byte/
 * ubyte/short/ushort/int/uint/long/ulong/float/double), string, vector of
 * scalars/strings/tables/structs, field defaults, `(id: N)`, `[deprecated]`,
 * `[required]`, `[force_align: N]`, root_type.
 *
 * Deliberately NOT implemented (per task scope):
 *   - unsigned semantics: ubyte/ushort/uint/ulong are accepted but treated as
 *     their signed equivalents of the same width (values outside signed range
 *     wrap);
 *   - soffset-based roots and `file_identifier` (root offset is a plain uoffset
 *     at byte 0, no identifier);
 *   - `include` / cross-file references — schemas are single-file, all types
 *     inline;
 *   - key/hash tables, buffers, flexible tree, RPC services.
 *
 * Binary format (little-endian throughout):
 *   buffer      : uoffset root at 0
 *   table       : soffset to vtable, then inline fields
 *   vtable      : u16 size, u16 table size, u16 field offsets (0 = absent)
 *   uoffset     : forward distance from the field's own position
 *   string      : u32 length, bytes, NUL
 *   vector      : u32 length, elements
 *
 * API:
 *   parseSchema(text) -> Schema
 *     Schema.tables / structs / enums / unions   {fullName: Type}
 *     Schema.rootType, Schema.lookup(name)
 *     (type names are also direct keys on Schema)
 *   TableType.encode(obj) -> Uint8Array
 *   TableType.decode(bytes) -> plain object
 */

// ── UTF-8 (hand-rolled; runs before setupTextEncoding in the bundle) ──

function utf8Encode(s) {
  var out = [];
  for (var i = 0; i < s.length; i++) {
    var c = s.charCodeAt(i);
    if (c < 0x80) out.push(c);
    else if (c < 0x800) out.push(0xc0 | (c >> 6), 0x80 | (c & 0x3f));
    else if (c >= 0xd800 && c <= 0xdbff && i + 1 < s.length) {
      var c2 = s.charCodeAt(i + 1);
      if (c2 >= 0xdc00 && c2 <= 0xdfff) {
        var cp = 0x10000 + ((c - 0xd800) << 10) + (c2 - 0xdc00);
        i++;
        out.push(0xf0 | (cp >> 18), 0x80 | ((cp >> 12) & 0x3f),
                 0x80 | ((cp >> 6) & 0x3f), 0x80 | (cp & 0x3f));
      } else out.push(0xef, 0xbf, 0xbd);
    } else if (c >= 0xd800 && c <= 0xdfff) out.push(0xef, 0xbf, 0xbd);
    else out.push(0xe0 | (c >> 12), 0x80 | ((c >> 6) & 0x3f), 0x80 | (c & 0x3f));
  }
  return Uint8Array.from(out);
}

function utf8Decode(b, start, len) {
  var s = '';
  for (var i = start, end = start + len; i < end;) {
    var c = b[i++];
    if (c < 0x80) s += String.fromCharCode(c);
    else if (c >= 0xc0 && c < 0xe0 && i < end) s += String.fromCharCode(((c & 0x1f) << 6) | (b[i++] & 0x3f));
    else if (c >= 0xe0 && c < 0xf0 && i + 1 < end) {
      s += String.fromCharCode(((c & 0x0f) << 12) | ((b[i++] & 0x3f) << 6) | (b[i++] & 0x3f));
    } else if (c >= 0xf0 && i + 2 < end) {
      var cp = ((c & 0x07) << 18) | ((b[i++] & 0x3f) << 12) | ((b[i++] & 0x3f) << 6) | (b[i++] & 0x3f);
      cp -= 0x10000;
      s += String.fromCharCode(0xd800 + (cp >> 10), 0xdc00 + (cp & 0x3ff));
    } else s += '\ufffd';
  }
  return s;
}

// ── scalar type table (unsigned names map to signed widths, see header) ──

var SCALARS = {
  bool: { size: 1, align: 1, kind: 'bool' },
  byte: { size: 1, align: 1, kind: 'i8' },
  ubyte: { size: 1, align: 1, kind: 'i8' },
  short: { size: 2, align: 2, kind: 'i16' },
  ushort: { size: 2, align: 2, kind: 'i16' },
  int: { size: 4, align: 4, kind: 'i32' },
  uint: { size: 4, align: 4, kind: 'i32' },
  float: { size: 4, align: 4, kind: 'f32' },
  long: { size: 8, align: 8, kind: 'i64' },
  ulong: { size: 8, align: 8, kind: 'i64' },
  double: { size: 8, align: 8, kind: 'f64' },
  int8: { size: 1, align: 1, kind: 'i8' },
  int16: { size: 2, align: 2, kind: 'i16' },
  int32: { size: 4, align: 4, kind: 'i32' },
  int64: { size: 8, align: 8, kind: 'i64' },
  uint8: { size: 1, align: 1, kind: 'i8' },
  uint16: { size: 2, align: 2, kind: 'i16' },
  uint32: { size: 4, align: 4, kind: 'i32' },
  uint64: { size: 8, align: 8, kind: 'i64' },
  float32: { size: 4, align: 4, kind: 'f32' },
  float64: { size: 8, align: 8, kind: 'f64' },
};

var I64 = { i64: 1 };

function toU8(d) {
  if (d == null) return new Uint8Array(0);
  if (d instanceof Uint8Array) return d;
  if (d instanceof ArrayBuffer) return new Uint8Array(d);
  if (ArrayBuffer.isView(d)) return new Uint8Array(d.buffer, d.byteOffset, d.byteLength);
  return utf8Encode(String(d));
}

function asBigInt(v, what) {
  if (typeof v === 'bigint') return v;
  if (typeof v === 'number') {
    if (!Number.isFinite(v)) throw new Error('flatbuffers: non-finite ' + what);
    return BigInt(Math.trunc(v));
  }
  if (typeof v === 'string') {
    if (!/^[+-]?\d+$/.test(v)) throw new Error('flatbuffers: bad integer string for ' + what);
    return BigInt(v);
  }
  throw new Error('flatbuffers: expected integer for ' + what + ', got ' + typeof v);
}

/* ── writer: builds the buffer back-to-front, like the reference C++ builder ── */

function Builder(initialSize) {
  this.buf = new Uint8Array(initialSize || 1024);
  this.space = this.buf.length;
  this.minalign = 1;
  this.vtables = [];
  this.fieldTable = [];
  this.start = 0;
  this.nested = false;
  this._dv = null;
}
Builder.prototype.offset = function () { return this.buf.length - this.space; };
Builder.prototype._view = function () {
  if (!this._dv || this._dv.buffer !== this.buf.buffer) {
    this._dv = new DataView(this.buf.buffer);
  }
  return this._dv;
};
Builder.prototype._ensure = function (n) {
  if (this.space >= n) return;
  var written = this.buf.length - this.space;
  var cap = this.buf.length;
  while (cap < written + n) cap *= 2;
  var nb = new Uint8Array(cap);
  nb.set(this.buf.subarray(this.space), cap - written);
  this.buf = nb;
  this.space = cap - written;
  this._dv = null;
};
Builder.prototype._pad = function (n) { this._ensure(n); this.space -= n; };
Builder.prototype.preAlign = function (size, add) {
  if (size > this.minalign) this.minalign = size;
  var d = (this.offset() + add) % size;
  if (d) this._pad(size - d);
};
Builder.prototype.pushU8 = function (v) { this._ensure(1); this.buf[--this.space] = v & 0xff; };
Builder.prototype.pushU16 = function (v) {
  this._ensure(2); this.space -= 2;
  this.buf[this.space] = v & 0xff; this.buf[this.space + 1] = (v >>> 8) & 0xff;
};
Builder.prototype.pushU32 = function (v) {
  this._ensure(4); this.space -= 4;
  this._view().setUint32(this.space, v >>> 0, true);
};
Builder.prototype.pushI32 = function (v) {
  this._ensure(4); this.space -= 4;
  this._view().setInt32(this.space, v | 0, true);
};
Builder.prototype.pushF32 = function (v) {
  this._ensure(4); this.space -= 4;
  this._view().setFloat32(this.space, v, true);
};
Builder.prototype.pushI64 = function (v) {
  this._ensure(8); this.space -= 8;
  this._view().setBigInt64(this.space, BigInt.asIntN(64, v), true);
};
Builder.prototype.pushF64 = function (v) {
  this._ensure(8); this.space -= 8;
  this._view().setFloat64(this.space, v, true);
};
/* Place `u8` at the next lower addresses, in order (u8[0] lowest). */
Builder.prototype.pushBytes = function (u8) {
  this._ensure(u8.length);
  this.space -= u8.length;
  this.buf.set(u8, this.space);
};
Builder.prototype._patchI32 = function (tailOffset, value) {
  this._view().setInt32(this.buf.length - tailOffset, value | 0, true);
};
Builder.prototype._notNested = function () {
  if (this.nested) throw new Error('flatbuffers: object construction while a table is open');
};

Builder.prototype.createString = function (s) {
  var b = typeof s === 'string' ? utf8Encode(s) : toU8(s);
  this._notNested();
  this.preAlign(4, b.length + 1);
  this.pushU8(0);
  this.pushBytes(b);
  this.pushU32(b.length);
  return this.offset();
};

/* elements pushed in reverse; `push` writes one element's bytes. */
/* elements pushed in reverse; `push` writes one element's bytes. Mirrors the
 * reference StartVector: Prep(4, data) then Prep(elemAlign, data). */
Builder.prototype.createVector = function (arr, elemSize, elemAlign, push) {
  this._notNested();
  var data = arr.length * elemSize;
  this.preAlign(4, data);
  this.preAlign(elemAlign, data);
  this.nested = true;
  for (var i = arr.length - 1; i >= 0; i--) push(arr[i]);
  this.nested = false;
  this.pushU32(arr.length);
  return this.offset();
};

Builder.prototype.startTable = function () {
  this._notNested();
  this.nested = true;
  this.fieldTable = [];
  this.start = this.offset();
};
Builder.prototype._track = function (slot, vloc) {
  if (slot >= this.fieldTable.length) {
    while (this.fieldTable.length <= slot) this.fieldTable.push(0);
  }
  this.fieldTable[slot] = vloc;
};
/* align may exceed the byte width via (force_align: N). */
Builder.prototype.addFieldScalar = function (slot, align, byteSize, push, value) {
  if (!this.nested) throw new Error('flatbuffers: addField outside startTable');
  this.preAlign(align, byteSize);
  push(value);
  this._track(slot, this.offset());
};
Builder.prototype.addFieldOffset = function (slot, off) {
  if (!this.nested) throw new Error('flatbuffers: addField outside startTable');
  this.preAlign(4, 4);
  this.pushU32(this.offset() + 4 - off);
  this._track(slot, this.offset());
};
Builder.prototype.addFieldInline = function (slot, bytes, align) {
  if (!this.nested) throw new Error('flatbuffers: addField outside startTable');
  this.preAlign(align, bytes.length);
  this.pushBytes(bytes);
  this._track(slot, this.offset());
};
Builder.prototype.endTable = function () {
  if (!this.nested) throw new Error('flatbuffers: endTable without startTable');
  this.nested = false;
  var numSlots = this.fieldTable.length;
  this.preAlign(4, 0);
  var T = this.offset() + 4;
  this.pushU32(0);                       // soffset placeholder, patched below
  var tableEnd = this.offset();
  var tableSize = tableEnd - this.start;

  var vt = new Array(numSlots);
  for (var i = 0; i < numSlots; i++) {
    vt[i] = this.fieldTable[i] ? (tableEnd - this.fieldTable[i]) : 0;
  }
  var vsize = 4 + 2 * numSlots;
  var key = tableSize + '|' + vt.join(',');
  for (var d = 0; d < this.vtables.length; d++) {
    if (this.vtables[d].key === key) { this._patchI32(T, this.vtables[d].v - T); return tableEnd; }
  }
  this.preAlign(2, vsize);
  for (var j = numSlots - 1; j >= 0; j--) this.pushU16(vt[j]);
  this.pushU16(tableSize);
  this.pushU16(vsize);
  var V = this.offset();
  this.vtables.push({ key: key, v: V });
  this._patchI32(T, V - T);
  return tableEnd;
};
Builder.prototype.finish = function (rootOff) {
  this._notNested();
  this.preAlign(Math.max(this.minalign, 4), 4);
  this.pushU32(this.offset() + 4 - rootOff);
  return this.buf.subarray(this.space);
};

/* ── reader ── */

function Reader(bytes) {
  this.u8 = bytes instanceof Uint8Array ? bytes : toU8(bytes);
  this.dv = new DataView(this.u8.buffer, this.u8.byteOffset, this.u8.byteLength);
}
Reader.prototype.u8at = function (p) { return this.u8[p]; };
Reader.prototype.i8 = function (p) { return this.dv.getInt8(p); };
Reader.prototype.i16 = function (p) { return this.dv.getInt16(p, true); };
Reader.prototype.u16 = function (p) { return this.dv.getUint16(p, true); };
Reader.prototype.i32 = function (p) { return this.dv.getInt32(p, true); };
Reader.prototype.u32 = function (p) { return this.dv.getUint32(p, true); };
Reader.prototype.f32 = function (p) { return this.dv.getFloat32(p, true); };
Reader.prototype.f64 = function (p) { return this.dv.getFloat64(p, true); };
Reader.prototype.i64 = function (p) { return this.dv.getBigInt64(p, true); };
Reader.prototype.indirect = function (p) { return p + this.u32(p); };
Reader.prototype.string = function (p) {
  var s = this.indirect(p), len = this.u32(s);
  return utf8Decode(this.u8, s + 4, len);
};
Reader.prototype.vectorLen = function (p) { return this.u32(this.indirect(p)); };
Reader.prototype.vectorAt = function (p) { return this.indirect(p) + 4; };

/* ── type objects ── */

function EnumType(name, base, values) {
  this.name = name;
  this.kind = 'enum';
  this.base = base;                 // scalar descriptor
  this.values = values;             // {NAME: number}
  this.byNumber = {};
  for (var k in values) this.byNumber[values[k]] = k;
}
EnumType.prototype.number = function (v) {
  if (typeof v === 'number') return v | 0;
  if (typeof v === 'string') {
    if (!(v in this.values)) throw new Error('flatbuffers: unknown enum value ' + v + ' for ' + this.name);
    return this.values[v];
  }
  throw new Error('flatbuffers: bad enum value for ' + this.name);
};

function UnionType(name, members) {
  this.name = name;
  this.kind = 'union';
  this.members = members;           // {NAME: TableType}
  this.byNumber = {};
  for (var i = 0; i < members.__order.length; i++) {
    this.byNumber[i + 1] = members.__order[i];
  }
}

function StructType(name, fields) {
  this.name = name;
  this.kind = 'struct';
  this.fields = fields;
  // layout: fields in order, each aligned to its own alignment
  var off = 0, align = 1;
  for (var i = 0; i < fields.length; i++) {
    var f = fields[i];
    var a = f.align, rep = f.rep || 1;
    if (a > align) align = a;
    if (off % a) off += a - (off % a);
    f.offset = off;
    off += f.size * rep;
  }
  if (off % align) off += align - (off % align);
  this.size = off;
  this.align = align;
}

function TableType(name, fields) {
  this.name = name;
  this.kind = 'table';
  this.fields = fields;             // resolved field descriptors
  this.bySlot = {};
  this.byName = {};
  for (var i = 0; i < fields.length; i++) {
    var f = fields[i];
    this.bySlot[f.slot] = f;
    this.byName[f.name] = f;
  }
}

/* ── encode ── */

function pushScalar(b, sc, v) {
  switch (sc.kind) {
    case 'bool': b.pushU8(v ? 1 : 0); break;
    case 'i8': b.pushU8(v | 0); break;
    case 'i16': b.pushU16(v | 0); break;
    case 'i32': b.pushI32(v); break;
    case 'f32': b.pushF32(Number(v)); break;
    case 'f64': b.pushF64(Number(v)); break;
    case 'i64': b.pushI64(asBigInt(v, 'int64')); break;
    default: throw new Error('flatbuffers: unhandled scalar kind ' + sc.kind);
  }
}

function writeScalarBytes(out, off, sc, v, dv) {
  switch (sc.kind) {
    case 'bool': out[off] = v ? 1 : 0; break;
    case 'i8': dv.setInt8(off, v | 0); break;
    case 'i16': dv.setInt16(off, v | 0, true); break;
    case 'i32': dv.setInt32(off, v | 0, true); break;
    case 'f32': dv.setFloat32(off, Number(v), true); break;
    case 'f64': dv.setFloat64(off, Number(v), true); break;
    case 'i64': dv.setBigInt64(off, BigInt.asIntN(64, asBigInt(v, 'int64')), true); break;
    default: throw new Error('flatbuffers: unhandled scalar kind ' + sc.kind);
  }
}

function readScalarAt(rd, p, sc) {
  switch (sc.kind) {
    case 'bool': return rd.u8at(p) !== 0;
    case 'i8': return rd.i8(p);
    case 'i16': return rd.i16(p);
    case 'i32': return rd.i32(p);
    case 'f32': return rd.f32(p);
    case 'f64': return rd.f64(p);
    case 'i64': return rd.i64(p);
    default: throw new Error('flatbuffers: unhandled scalar kind ' + sc.kind);
  }
}

function isDefaultVal(f, v) {
  if (v === undefined || v === null) return true;
  var d = f.def;
  if (typeof d === 'bigint') {
    if (typeof v === 'bigint') return v === d;
    try { return asBigInt(v, f.name) === d; } catch (e) { return false; }
  }
  if (typeof d === 'number') {
    if (typeof v === 'bigint') return Number(v) === d;
    return Number(v) === d;
  }
  if (typeof d === 'boolean') return !!v === d;
  return v === d;
}

/* Serialize a struct (or a fixed array inside one) into a fresh byte array. */
function encodeStructBytes(type, obj, b) {
  var out = new Uint8Array(type.size);
  var dv = new DataView(out.buffer);
  for (var i = 0; i < type.fields.length; i++) {
    var f = type.fields[i];
    var v = obj ? obj[f.name] : undefined;
    if (f.rep) {
      if (v == null) v = [];
      if (!Array.isArray(v)) throw new Error('flatbuffers: struct array field ' + f.name + ' expects array');
      for (var j = 0; j < f.rep; j++) {
        writeScalarBytes(out, f.offset + j * f.elemSize, f.scalar, v[j] === undefined ? 0 : v[j], dv);
      }
    } else if (f.structType) {
      var sub = encodeStructBytes(f.structType, v, b);
      for (var k = 0; k < sub.length; k++) out[f.offset + k] = sub[k];
    } else {
      var sv = v === undefined ? f.defNum : v;
      writeScalarBytes(out, f.offset, f.scalar, sv, dv);
    }
  }
  return out;
}

/* Build any child objects for field `f` and return a descriptor to apply once
 * the table is open. flatbuffers requires children to be created BEFORE the
 * object is started, so encoding a table is two-pass. Returns null when the
 * value equals the field default and the slot can be omitted. */
function prepareField(b, f, v) {
  switch (f.type) {
    case 'scalar':
      return { kind: 'scalar', slot: f.slot, sc: f.scalar, val: v };
    case 'enum': {
      var ev = f.enumType.number(v);
      if (ev === f.defNum) return null;
      return { kind: 'scalar', slot: f.slot, sc: f.enumType.base, val: ev };
    }
    case 'string':
      return { kind: 'off', slot: f.slot, off: b.createString(v) };
    case 'struct':
      return { kind: 'inline', slot: f.slot, bytes: encodeStructBytes(f.structType, v),
               align: f.structType.align };
    case 'table':
      return { kind: 'off', slot: f.slot, off: encodeTableTo(b, f.tableType, v) };
    case 'union': {
      var t = f.unionType, tn = v && v.type, member = null, idx = 0;
      for (var q = 0; q < t.members.__order.length; q++) {
        if (t.members.__order[q] === tn) { member = t.members[tn]; idx = q + 1; }
      }
      if (!member) {
        throw new Error('flatbuffers: union field ' + f.name + ' expects {type: <MemberName>, value: {...}}' +
          (tn === undefined ? ', got no type' : ', unknown member ' + tn));
      }
      return { kind: 'union', slot: f.slot, idx: idx, off: encodeTableTo(b, member, v.value) };
    }
    case 'vector':
      return { kind: 'off', slot: f.slot, off: createVectorField(b, f, v) };
    default:
      throw new Error('flatbuffers: unhandled field type ' + f.type + ' for ' + f.name);
  }
}

function createVectorField(b, f, v) {
  if (!Array.isArray(v)) throw new Error('flatbuffers: vector field ' + f.name + ' expects array');
  var e = f.elem, i;
  if (e.kind === 'string') {
    var so = new Array(v.length);
    for (i = 0; i < v.length; i++) so[i] = b.createString(v[i] == null ? '' : String(v[i]));
    return b.createVector(so, 4, 4, function (o) { b.pushU32(b.offset() + 4 - o); });
  }
  if (e.kind === 'table') {
    var to = new Array(v.length);
    for (i = 0; i < v.length; i++) to[i] = encodeTableTo(b, e.tableType, v[i]);
    return b.createVector(to, 4, 4, function (o) { b.pushU32(b.offset() + 4 - o); });
  }
  if (e.kind === 'struct') {
    var st = e.structType, sb = new Array(v.length);
    for (i = 0; i < v.length; i++) sb[i] = encodeStructBytes(st, v[i]);
    return b.createVector(sb, st.size, st.align, function (bytes) { b.pushBytes(bytes); });
  }
  var sc = e.scalar || e.enumType.base;
  return b.createVector(v, sc.size, sc.align, function (x) { pushScalar(b, sc, x); });
}

function applyField(b, d) {
  switch (d.kind) {
    case 'off': b.addFieldOffset(d.slot, d.off); break;
    case 'inline': b.addFieldInline(d.slot, d.bytes, d.align); break;
    case 'scalar': {
      var sc = d.sc;
      b.addFieldScalar(d.slot, sc.align, sc.size, function (x) { pushScalar(b, sc, x); }, d.val);
      break;
    }
    case 'union': {
      var idx = d.idx;
      b.addFieldScalar(d.slot, 1, 1, function () { b.pushU8(idx); }, idx);
      b.addFieldOffset(d.slot + 1, d.off);
      break;
    }
  }
}

/* Encode `obj` as a table into builder `b`, returning its tail offset. */
function encodeTableTo(b, type, obj) {
  if (obj == null) obj = {};
  if (typeof obj !== 'object') throw new Error('flatbuffers: encode expects object for ' + type.name);
  var pending = [];
  for (var i = 0; i < type.fields.length; i++) {
    var f = type.fields[i];
    if (f.deprecated) continue;
    var v = obj[f.name];
    if (v === undefined || v === null) continue;
    if ((f.type === 'scalar' || f.type === 'enum') && isDefaultVal(f, v)) continue;
    var d = prepareField(b, f, v);
    if (d) pending.push(d);
  }
  // The reference (flatc-generated code) fills slots in descending order, so
  // that inline field offsets ascend by slot. Matching it byte-for-byte keeps
  // buffers comparable across implementations and minimises padding.
  pending.sort(function (x, y) { return y.slot - x.slot; });
  b.startTable();
  for (var p = 0; p < pending.length; p++) applyField(b, pending[p]);
  return b.endTable();
}

TableType.prototype.encode = function (obj) {
  var b = new Builder(1024);
  var root = encodeTableTo(b, this, obj);
  return b.finish(root).slice();
};

/* ── decode ── */

TableType.prototype.decode = function (bytes) {
  var rd = new Reader(bytes);
  return decodeTable(rd, rd.indirect(0), this);
};

function slotOf(rd, pos, slot) {
  var vt = pos - rd.i32(pos);
  var vsize = rd.u16(vt);
  var at = vt + 4 + slot * 2;
  if (at >= vt + vsize) return 0;
  return rd.u16(at);
}

function decodeTable(rd, pos, type) {
  var obj = {};
  for (var i = 0; i < type.fields.length; i++) {
    var f = type.fields[i];
    var off = slotOf(rd, pos, f.slot);
    if (off === 0) {
      if (f.required) throw new Error('flatbuffers: required field ' + f.name + ' missing in ' + type.name);
      if (f.type === 'vector') obj[f.name] = [];
      else if (f.type === 'string') obj[f.name] = f.def;
      else if (f.type === 'table' || f.type === 'struct' || f.type === 'union') obj[f.name] = undefined;
      else obj[f.name] = f.def;
      continue;
    }
    var p = pos + off;
    switch (f.type) {
      case 'scalar': obj[f.name] = readScalarAt(rd, p, f.scalar); break;
      case 'enum': obj[f.name] = readScalarAt(rd, p, f.enumType.base); break;
      case 'string': obj[f.name] = rd.string(p); break;
      case 'table': obj[f.name] = decodeTable(rd, rd.indirect(p), f.tableType); break;
      case 'struct': obj[f.name] = decodeStruct(rd, p, f.structType); break;
      case 'union': {
        var tn = readScalarAt(rd, p, { size: 1, align: 1, kind: 'i8' });
        var name = f.unionType.byNumber[tn];
        obj[f.name] = { type: name, typeNum: tn, value: name ? decodeTable(rd, rd.indirect(pos + slotOf(rd, pos, f.slot + 1)), f.unionType.members[name]) : undefined };
        break;
      }
      case 'vector': obj[f.name] = decodeVector(rd, p, f); break;
      default: throw new Error('flatbuffers: unhandled field type ' + f.type);
    }
  }
  return obj;
}

function decodeStruct(rd, p, type) {
  var obj = {};
  for (var i = 0; i < type.fields.length; i++) {
    var f = type.fields[i], at = p + f.offset;
    if (f.rep) {
      var arr = [];
      for (var j = 0; j < f.rep; j++) arr.push(readScalarAt(rd, at + j * f.elemSize, f.scalar));
      obj[f.name] = arr;
    } else if (f.structType) obj[f.name] = decodeStruct(rd, at, f.structType);
    else obj[f.name] = readScalarAt(rd, at, f.scalar);
  }
  return obj;
}

function decodeVector(rd, p, f) {
  var base = rd.vectorAt(p), n = rd.vectorLen(p), out = new Array(n);
  var e = f.elem;
  for (var i = 0; i < n; i++) {
    if (e.kind === 'string') out[i] = rd.string(base + i * 4);
    else if (e.kind === 'table') out[i] = decodeTable(rd, rd.indirect(base + i * 4), e.tableType);
    else if (e.kind === 'struct') out[i] = decodeStruct(rd, base + i * e.structType.size, e.structType);
    else out[i] = readScalarAt(rd, base + i * (e.scalar || e.enumType.base).size, e.scalar || e.enumType.base);
  }
  return out;
}

/* ── .fbs tokenizer ── */

function tokenize(src) {
  var toks = [], i = 0, n = src.length;
  while (i < n) {
    var c = src.charCodeAt(i);
    if (c === 0x20 || c === 0x09 || c === 0x0a || c === 0x0d) { i++; continue; }
    if (c === 0x2f && i + 1 < n && src.charCodeAt(i + 1) === 0x2f) {
      while (i < n && src.charCodeAt(i) !== 0x0a) i++;
      continue;
    }
    if (c === 0x2f && i + 1 < n && src.charCodeAt(i + 1) === 0x2a) {
      i += 2;
      while (i + 1 < n && !(src.charCodeAt(i) === 0x2a && src.charCodeAt(i + 1) === 0x2f)) i++;
      i += 2;
      continue;
    }
    if (c === 0x22 || c === 0x27) {
      var q = c, s = '';
      i++;
      while (i < n && src.charCodeAt(i) !== q) {
        if (src.charCodeAt(i) === 0x5c && i + 1 < n) {
          var e = src[i + 1];
          s += e === 'n' ? '\n' : e === 't' ? '\t' : e === 'r' ? '\r' : e;
          i += 2;
        } else s += src[i++];
      }
      i++;
      toks.push({ t: 'str', v: s });
      continue;
    }
    if ((c >= 0x30 && c <= 0x39) || ((c === 0x2d || c === 0x2b) && i + 1 < n &&
        ((src.charCodeAt(i + 1) >= 0x30 && src.charCodeAt(i + 1) <= 0x39) || src.charCodeAt(i + 1) === 0x2e))) {
      var st = i;
      if (src.charCodeAt(i) === 0x2d || src.charCodeAt(i) === 0x2b) i++;
      if (src.charCodeAt(i) === 0x30 && (src.charCodeAt(i + 1) === 0x78 || src.charCodeAt(i + 1) === 0x58)) {
        i += 2; while (i < n && /[0-9a-fA-F]/.test(src[i])) i++;
      } else {
        while (i < n && src.charCodeAt(i) >= 0x30 && src.charCodeAt(i) <= 0x39) i++;
        if (i < n && src.charCodeAt(i) === 0x2e) { i++; while (i < n && src.charCodeAt(i) >= 0x30 && src.charCodeAt(i) <= 0x39) i++; }
        if (i < n && (src.charCodeAt(i) === 0x65 || src.charCodeAt(i) === 0x45)) {
          i++;
          if (i < n && (src.charCodeAt(i) === 0x2b || src.charCodeAt(i) === 0x2d)) i++;
          while (i < n && src.charCodeAt(i) >= 0x30 && src.charCodeAt(i) <= 0x39) i++;
        }
      }
      toks.push({ t: 'num', v: src.slice(st, i) });
      continue;
    }
    if ((c >= 0x41 && c <= 0x5a) || (c >= 0x61 && c <= 0x7a) || c === 0x5f) {
      var j = i;
      while (j < n && /[A-Za-z0-9_]/.test(src[j])) j++;
      toks.push({ t: 'id', v: src.slice(i, j) });
      i = j;
      continue;
    }
    toks.push({ t: 'punc', v: src[i] });
    i++;
  }
  return toks;
}

/* ── .fbs parser ── */

function FbsParser(text) {
  this.toks = tokenize(text);
  this.i = 0;
}
FbsParser.prototype._peek = function () { return this.toks[this.i]; };
FbsParser.prototype._isId = function (v) { var t = this._peek(); return t && t.t === 'id' && (v == null || t.v === v); };
FbsParser.prototype._isPunc = function (v) { var t = this._peek(); return t && t.t === 'punc' && t.v === v; };
FbsParser.prototype._eof = function () { return this.i >= this.toks.length; };
FbsParser.prototype._eatId = function (v) {
  if (!this._isId(v)) throw new Error('flatbuffers: expected ' + (v || 'identifier') + ' at token ' + this.i +
    ' (got ' + (this._peek() ? JSON.stringify(this._peek().v) : 'EOF') + ')');
  return this.toks[this.i++].v;
};
FbsParser.prototype._eatPunc = function (v) {
  if (!this._isPunc(v)) throw new Error('flatbuffers: expected "' + v + '" at token ' + this.i +
    ' (got ' + (this._peek() ? JSON.stringify(this._peek().v) : 'EOF') + ')');
  this.i++;
};
FbsParser.prototype._tryPunc = function (v) { if (this._isPunc(v)) { this.i++; return true; } return false; };
FbsParser.prototype._name = function () {
  var s = this._eatId();
  while (this._isPunc('.')) { this.i++; s += '.' + this._eatId(); }
  return s;
};
/* attribute metadata: `( attr , attr: value , ... )` */
FbsParser.prototype._metadata = function () {
  var md = {};
  if (!this._tryPunc('(')) return md;
  while (!this._isPunc(')')) {
    if (this._eof()) throw new Error('flatbuffers: unterminated attribute list');
    var k = this._eatId();
    var v = true;
    if (this._tryPunc(':')) {
      var t = this._peek();
      if (!t) throw new Error('flatbuffers: bad attribute value');
      if (t.t === 'str' || t.t === 'num') { v = t.v; this.i++; } else v = this._eatId();
    }
    md[k] = v;
    if (!this._tryPunc(',')) break;
  }
  this._eatPunc(')');
  return md;
};

FbsParser.prototype.parse = function () {
  var out = { namespace: '', tables: [], structs: [], enums: [], unions: [], services: [], rootType: null, includes: [] };
  while (!this._eof()) {
    if (this._isId('namespace')) { this.i++; out.namespace = this._name(); this._eatPunc(';'); continue; }
    if (this._isId('include')) { this.i++; out.includes.push(this._peek().v); this.i++; this._eatPunc(';'); continue; }
    if (this._isId('root_type')) { this.i++; out.rootType = this._name(); this._eatPunc(';'); continue; }
    if (this._isId('file_identifier') || this._isId('file_extension')) { this.i++; this.i++; this._eatPunc(';'); continue; }
    if (this._isId('attribute') || this._isId('rpc_call')) { this.i++; while (!this._eof() && !this._isPunc(';')) this.i++; this._eatPunc(';'); continue; }
    if (this._isId('table')) { out.tables.push(this.parseTable(out.namespace)); continue; }
    if (this._isId('struct')) { out.structs.push(this.parseStruct(out.namespace)); continue; }
    if (this._isId('enum')) { out.enums.push(this.parseEnum(out.namespace)); continue; }
    if (this._isId('union')) { out.unions.push(this.parseUnion(out.namespace)); continue; }
    if (this._isId('service')) { out.services.push(this.parseService(out.namespace)); continue; }
    throw new Error('flatbuffers: unexpected token ' + JSON.stringify(this._peek().v) + ' at top level (token ' + this.i + ')');
  }
  return out;
};

/* `service Name { Rpc (ReqType) : RespType (attrs)? ; }` — the RPC signatures
 * are what let grpc.js bind a method path to request/response tables. */
FbsParser.prototype.parseService = function (ns) {
  this._eatId('service');
  var name = this._eatId();
  var svc = { name: name, namespace: ns || '', methods: [] };
  if (this._isPunc('(')) this._metadata();
  this._eatPunc('{');
  while (!this._isPunc('}')) {
    if (this._eof()) throw new Error('flatbuffers: unterminated service ' + name);
    var mn = this._eatId();
    this._eatPunc('(');
    var req = this._isPunc(')') ? null : this._eatId();
    // `Rpc (Req) : Resp` and the older `Rpc(Req): Resp` both occur; also
    // tolerate a stream marker `stream` before a type name.
    while (this._isId('stream') || this._isId('rpc_call')) this.i++;
    this._eatPunc(')');
    this._eatPunc(':');
    if (this._isId('stream')) this.i++;
    var resp = this._eatId();
    if (this._isPunc('(')) this._metadata();
    if (this._isPunc('{')) { while (!this._isPunc('}') && !this._eof()) this.i++; this._eatPunc('}'); }
    else this._eatPunc(';');
    svc.methods.push({ name: mn, request: req, response: resp });
  }
  this._eatPunc('}');
  return svc;
};

FbsParser.prototype._field = function (full) {
  var name = this._eatId();
  this._eatPunc(':');
  var type = this._type();
  var md = {}, def = null;
  // attributes `(id: 3, deprecated)` and a default `= 80` may appear in either
  // order across real-world .fbs files.
  for (;;) {
    if (this._isPunc('(')) { Object.assign(md, this._metadata()); continue; }
    if (this._tryPunc('=')) {
      var t = this._peek();
      if (!t) throw new Error('flatbuffers: missing default value for ' + name);
      if (t.t === 'num' || t.t === 'str') { def = t.v; this.i++; } else def = this._eatId();
      continue;
    }
    break;
  }
  this._eatPunc(';');
  return { name: name, type: type, md: md, def: def, scope: full };
};

FbsParser.prototype._type = function () {
  if (this._tryPunc('[')) {
    var inner = this._type();
    if (this._tryPunc(':')) {
      var n = this._peek();
      if (!n || n.t !== 'num') throw new Error('flatbuffers: bad fixed-array length at token ' + this.i);
      this.i++;
      this._eatPunc(']');
      return { array: inner, len: parseInt(n.v, 10) };
    }
    this._eatPunc(']');
    return { vector: inner };
  }
  return this._name();
};

FbsParser.prototype.parseTable = function (ns) {
  this._eatId('table');
  var name = this._eatId();
  var md = this._metadata();
  this._eatPunc('{');
  var fields = [];
  while (!this._isPunc('}')) {
    if (this._eof()) throw new Error('flatbuffers: unterminated table ' + name);
    fields.push(this._field([ns, name]));
  }
  this._eatPunc('}');
  return { name: name, ns: ns, md: md, fields: fields };
};

FbsParser.prototype.parseStruct = function (ns) {
  this._eatId('struct');
  var name = this._eatId();
  var md = this._metadata();
  this._eatPunc('{');
  var fields = [];
  while (!this._isPunc('}')) {
    if (this._eof()) throw new Error('flatbuffers: unterminated struct ' + name);
    fields.push(this._field([ns, name]));
  }
  this._eatPunc('}');
  return { name: name, ns: ns, md: md, fields: fields };
};

FbsParser.prototype.parseEnum = function (ns) {
  this._eatId('enum');
  var name = this._eatId();
  this._eatPunc(':');
  var base = this._name();
  this._eatPunc('{');
  var values = {}, auto = 0;
  while (!this._isPunc('}')) {
    if (this._eof()) throw new Error('flatbuffers: unterminated enum ' + name);
    var vn = this._eatId();
    var v = auto;
    if (this._tryPunc('=')) {
      var neg = this._tryPunc('-');
      var num = this._peek();
      if (!num || num.t !== 'num') throw new Error('flatbuffers: bad enum value for ' + vn);
      v = parseInt(num.v, 10); if (neg) v = -v;
      this.i++;
    }
    values[vn] = v;
    auto = v + 1;
    if (this._isPunc('(')) { while (!this._isPunc(')')) this.i++; this._eatPunc(')'); }
    if (!this._tryPunc(',')) break;
  }
  this._eatPunc('}');
  return { name: name, ns: ns, base: base, values: values };
};

FbsParser.prototype.parseUnion = function (ns) {
  this._eatId('union');
  var name = this._eatId();
  if (this._isPunc('(')) { while (!this._isPunc(')')) this.i++; this._eatPunc(')'); }
  this._eatPunc('{');
  var members = [];
  while (!this._isPunc('}')) {
    if (this._eof()) throw new Error('flatbuffers: unterminated union ' + name);
    members.push(this._name());
    if (this._isPunc('(')) { while (!this._isPunc(')')) this.i++; this._eatPunc(')'); }
    if (!this._tryPunc(',')) break;
  }
  this._eatPunc('}');
  return { name: name, ns: ns, members: members };
};

/* ── schema assembly + type resolution ── */

function Schema(ns) {
  this.namespace = ns || '';
  this.tables = {};
  this.structs = {};
  this.enums = {};
  this.unions = {};
  this.services = {};
  this.rootType = null;
}
Schema.prototype.lookup = function (n) {
  return this.tables[n] || this.structs[n] || this.enums[n] || this.unions[n];
};
/* Look up a parsed service by full or short name. */
Schema.prototype.service = function (n) {
  var svc = this.services[n] || this.services[(this.namespace ? this.namespace + '.' : '') + n];
  if (svc && !svc.method) {
    svc.method = function (m) {
      var md = svc.methods[m];
      if (!md) throw new Error('flatbuffers: no method ' + m + ' in service ' + svc.fullName);
      return md;
    };
  }
  return svc;
};

function fqName(ns, name) { return (ns ? ns + '.' : '') + name; }

function resolveType(schema, typeStr, scope) {
  if (schema.tables[typeStr] || schema.structs[typeStr] || schema.enums[typeStr] || schema.unions[typeStr]) {
    return schema.lookup(typeStr);
  }
  var parts = typeStr.split('.');
  // try scope prefixes (innermost first), then namespace, then bare
  var chains = [];
  for (var i = scope.length; i >= 0; i--) chains.push(scope.slice(0, i).concat(parts).join('.'));
  if (schema.namespace) chains.push(schema.namespace + '.' + typeStr);
  for (var c = 0; c < chains.length; c++) {
    var hit = schema.lookup(chains[c]);
    if (hit) return hit;
  }
  return undefined;
}

function parseDefault(sc, def, enumType) {
  if (def === null || def === undefined) {
    if (enumType) return 0;
    if (sc.kind === 'bool') return false;
    if (sc.kind === 'i64') return 0n;
    return 0;
  }
  if (enumType) {
    if (typeof def === 'string') {
      // may be "Enum.VALUE" or bare VALUE
      var bare = def.indexOf('.') >= 0 ? def.split('.').pop() : def;
      if (!(bare in enumType.values)) throw new Error('flatbuffers: unknown enum default ' + def);
      return enumType.values[bare];
    }
    return def | 0;
  }
  if (sc.kind === 'bool') return def === true || def === 'true' || def === 1 || def === '1';
  if (sc.kind === 'i64') return asBigInt(def, 'default value');
  if (sc.kind === 'f32' || sc.kind === 'f64') {
    var fv = parseFloat(def);
    if (!Number.isFinite(fv)) throw new Error('flatbuffers: bad float default ' + JSON.stringify(String(def)));
    return fv;
  }
  if (typeof def === 'string' && !/^[+-]?\d+$/.test(def)) {
    throw new Error('flatbuffers: bad integer default ' + JSON.stringify(def));
  }
  return parseInt(def, 10) | 0;
}

function buildField(schema, raw, slot) {
  var f = { name: raw.name, slot: slot, deprecated: !!raw.md.deprecated, required: !!raw.md.required };
  var t = raw.type;
  var forceAlign = raw.md.force_align ? parseInt(raw.md.force_align, 10) : 0;

  if (t.vector) {
    f.type = 'vector';
    var it = t.vector;
    if (it === 'string') f.elem = { kind: 'string' };
    else if (SCALARS[it]) f.elem = { kind: 'scalar', scalar: SCALARS[it] };
    else {
      var r = resolveType(schema, it, []);
      if (!r) throw new Error('flatbuffers: unknown vector element type ' + it + ' for ' + f.name);
      if (r.kind === 'struct') f.elem = { kind: 'struct', structType: r };
      else if (r.kind === 'union') throw new Error('flatbuffers: vector of unions is not supported (field ' + f.name + ')');
      else if (r.kind === 'enum') f.elem = { kind: 'enum', enumType: r };
      else f.elem = { kind: 'table', tableType: r };
    }
    f.def = [];
    return f;
  }
  if (t === 'string') { f.type = 'string'; f.def = undefined; return f; }
  if (SCALARS[t]) {
    f.type = 'scalar'; f.scalar = SCALARS[t];
    if (forceAlign) f.scalar = { size: f.scalar.size, align: forceAlign, kind: f.scalar.kind };
    f.def = parseDefault(f.scalar, raw.def, null);
    f.defNum = f.def;
    return f;
  }
  var res = resolveType(schema, t, []);
  if (!res) throw new Error('flatbuffers: unknown type "' + t + '" for field ' + f.name);
  if (res.kind === 'enum') {
    f.type = 'enum'; f.enumType = res;
    f.def = parseDefault(res.base, raw.def, res); f.defNum = f.def;
    return f;
  }
  if (res.kind === 'union') { f.type = 'union'; f.unionType = res; f.def = undefined; return f; }
  if (res.kind === 'struct') { f.type = 'struct'; f.structType = res; f.def = undefined; return f; }
  if (res.kind === 'table') { f.type = 'table'; f.tableType = res; f.def = undefined; return f; }
  throw new Error('flatbuffers: unsupported field type ' + t + ' for ' + f.name);
}

function assignSlots(schema, decl, isStruct) {
  var slot = 0, out = [];
  for (var i = 0; i < decl.fields.length; i++) {
    var raw = decl.fields[i];
    var explicit = raw.md.id !== undefined ? parseInt(raw.md.id, 10) : null;
    var t = raw.type;
    var isUnion = !isStruct && !t.vector && SCALARS[t] === undefined && t !== 'string' &&
      (function () { var r = resolveType(schema, t, []); return r && r.kind === 'union'; })();

    if (isStruct) {
      var sf = buildStructField(schema, raw);
      sf.slot = explicit != null ? explicit : slot;
      out.push(sf);
      slot = sf.slot + 1;
      continue;
    }
    var s = explicit != null ? explicit : slot;
    var f = buildField(schema, raw, s);
    out.push(f);
    if (isUnion) {
      // union value occupies the NEXT slot; the type field is `s`
      f.unionValueSlot = s + 1;
      slot = s + 2;
    } else {
      slot = s + 1;
    }
  }
  return out;
}

function buildStructField(schema, raw) {
  var t = raw.type;
  var f = { name: raw.name, deprecated: false, required: false };
  if (t.array) {
    var it = t.array;
    if (it === 'string' || !SCALARS[it]) {
      var sr = resolveType(schema, it, []);
      if (!sr || sr.kind !== 'struct') {
        throw new Error('flatbuffers: only scalar/struct fixed arrays are supported (field ' + f.name + ')');
      }
      f.structType = sr; f.size = sr.size; f.align = sr.align; f.rep = t.len;
      f.elemSize = sr.size;
      return f;
    }
    f.scalar = SCALARS[it]; f.size = f.scalar.size; f.align = f.scalar.align;
    f.rep = t.len; f.elemSize = f.scalar.size;
    f.defNum = parseDefault(f.scalar, raw.def, null);
    return f;
  }
  if (t.vector) throw new Error('flatbuffers: vectors are not allowed inside structs (use [T:N], field ' + f.name + ')');
  if (t === 'string') throw new Error('flatbuffers: string not allowed in struct ' + raw.name);
  if (SCALARS[t]) {
    f.scalar = SCALARS[t]; f.size = f.scalar.size; f.align = f.scalar.align;
    f.defNum = parseDefault(f.scalar, raw.def, null);
    return f;
  }
  var r = resolveType(schema, t, []);
  if (r && r.kind === 'struct') { f.structType = r; f.size = r.size; f.align = r.align; return f; }
  if (r && r.kind === 'enum') {
    f.scalar = r.base; f.size = r.base.size; f.align = r.base.align;
    f.defNum = parseDefault(r.base, raw.def, r);
    return f;
  }
  throw new Error('flatbuffers: type ' + t + ' not allowed in struct (field ' + raw.name + ')');
}

function parseSchema(text) {
  var ast = new FbsParser(String(text)).parse();
  var schema = new Schema(ast.namespace);

  // 1. enums
  for (var e = 0; e < ast.enums.length; e++) {
    var ed = ast.enums[e];
    var base = SCALARS[ed.base];
    if (!base) throw new Error('flatbuffers: bad enum base type ' + ed.base);
    schema.enums[fqName(ed.ns, ed.name)] = new EnumType(fqName(ed.ns, ed.name), base, ed.values);
  }
  // 2. struct shells (in dependency order: resolve lazily below)
  for (var s = 0; s < ast.structs.length; s++) {
    var sd = ast.structs[s];
    var sfull = fqName(sd.ns, sd.name);
    schema.structs[sfull] = new StructType(sfull, []);
    schema.structs[sfull]._decl = sd;
  }
  // structs may nest structs; build repeatedly until all resolve
  var pendingStructs = ast.structs.slice();
  var guard = pendingStructs.length * pendingStructs.length + 4;
  while (pendingStructs.length && guard-- > 0) {
    var retry = [];
    for (var ps = 0; ps < pendingStructs.length; ps++) {
      var pd = pendingStructs[ps];
      var pfull = fqName(pd.ns, pd.name);
      var st = schema.structs[pfull];
      try { st.fields = assignSlots(schema, pd, true); }
      catch (err) { retry.push(pd); continue; }
      // recompute layout
      var off = 0, align = 1;
      for (var fi = 0; fi < st.fields.length; fi++) {
        var fld = st.fields[fi];
        if (fld.align > align) align = fld.align;
        if (off % fld.align) off += fld.align - (off % fld.align);
        fld.offset = off;
        off += fld.size * (fld.rep || 1);
      }
      if (off % align) off += align - (off % align);
      st.size = off; st.align = align;
    }
    if (retry.length === pendingStructs.length) {
      // one more attempt to surface a real error
      for (var q = 0; q < retry.length; q++) assignSlots(schema, retry[q], true);
      break;
    }
    pendingStructs = retry;
  }
  // 3. table shells
  for (var ti = 0; ti < ast.tables.length; ti++) {
    var td = ast.tables[ti];
    var tfull = fqName(td.ns, td.name);
    schema.tables[tfull] = new TableType(tfull, []);
    schema.tables[tfull]._decl = td;
  }
  // 4. unions
  for (var ui = 0; ui < ast.unions.length; ui++) {
    var ud = ast.unions[ui];
    var ufull = fqName(ud.ns, ud.name);
    var members = { __order: [] };
    for (var um = 0; um < ud.members.length; um++) {
      var mt = resolveType(schema, ud.members[um], []);
      if (!mt || mt.kind !== 'table') throw new Error('flatbuffers: union member ' + ud.members[um] + ' is not a table');
      members.__order.push(ud.members[um]);
      members[ud.members[um]] = mt;
    }
    schema.unions[ufull] = new UnionType(ufull, members);
    schema.unions[ufull].values = {};
    for (var wo = 0; wo < members.__order.length; wo++) schema.unions[ufull].values[members.__order[wo]] = wo + 1;
    schema.unions[ufull].byNumber = {};
    for (var bn = 0; bn < members.__order.length; bn++) schema.unions[ufull].byNumber[bn + 1] = members.__order[bn];
  }
  // 5. fill table fields
  for (var fj = 0; fj < ast.tables.length; fj++) {
    var t2 = ast.tables[fj];
    var full2 = fqName(t2.ns, t2.name);
    var tt = schema.tables[full2];
    tt.fields = assignSlots(schema, t2, false);
    tt.bySlot = {}; tt.byName = {};
    for (var k = 0; k < tt.fields.length; k++) {
      var fx = tt.fields[k];
      tt.bySlot[fx.slot] = fx; tt.byName[fx.name] = fx;
    }
    delete tt._decl;
  }
  for (var ps2 = 0; ps2 < ast.structs.length; ps2++) delete schema.structs[fqName(ast.structs[ps2].ns, ast.structs[ps2].name)]._decl;

  if (ast.rootType) {
    var rt = resolveType(schema, ast.rootType, []);
    if (!rt || rt.kind !== 'table') throw new Error('flatbuffers: root_type ' + ast.rootType + ' is not a table');
    schema.rootType = rt;
  }

  // 6. services — bind each rpc to its request/response tables so grpc.js can
  //    encode a call straight from the schema.
  for (var sv = 0; sv < ast.services.length; sv++) {
    var sd2 = ast.services[sv];
    var sfull = fqName(sd2.namespace, sd2.name);
    var svc = { name: sd2.name, fullName: sfull, methods: {} };
    for (var sm = 0; sm < sd2.methods.length; sm++) {
      var mmd = sd2.methods[sm];
      var rqt = mmd.request ? schema.tables[mmd.request] || schema.tables[fqName(sd2.namespace, mmd.request)] : null;
      var rpt = schema.tables[mmd.response] || schema.tables[fqName(sd2.namespace, mmd.response)];
      if (mmd.request && !rqt) throw new Error('flatbuffers: unknown request type ' + mmd.request + ' for ' + sd2.name + '.' + mmd.name);
      if (!rpt) throw new Error('flatbuffers: unknown response type ' + mmd.response + ' for ' + sd2.name + '.' + mmd.name);
      svc.methods[mmd.name] = {
        name: mmd.name, service: sfull, path: '/' + sfull + '/' + mmd.name,
        requestType: rqt, responseType: rpt, serialization: 'flatbuffers',
      };
    }
    schema.services[sfull] = svc;
    if (!Object.prototype.hasOwnProperty.call(schema, sd2.name)) schema[sd2.name] = svc;
  }

  // direct keys for convenience
  var all = [schema.tables, schema.structs, schema.enums, schema.unions];
  for (var g = 0; g < all.length; g++) {
    for (var key in all[g]) {
      if (!Object.prototype.hasOwnProperty.call(schema, key)) schema[key] = all[g][key];
      var short = key.split('.').pop();
      if (!Object.prototype.hasOwnProperty.call(schema, short)) schema[short] = all[g][key];
    }
  }
  return schema;
}

export { parseSchema, Builder, Reader, utf8Encode, utf8Decode };

export function setupFlatbuffers(pal) {
  globalThis.flatbuffers = {
    parseSchema: parseSchema,
    Builder: Builder,
    Reader: Reader,
  };
}

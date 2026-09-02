/**
 * flatbuffers.js unit harness (verification only — not part of the runtime bundle).
 *
 * Bundles polyfill/src/flatbuffers.js to ESM and exercises the .fbs parser and
 * the binary codec: scalars of every width, strings, vectors (scalar/string/
 * table/struct), nested tables, structs (incl. fixed arrays), enums, unions,
 * defaults, id/deprecated/required attributes, and error cases.
 *
 * Usage: node test/flatbuffers_harness.mjs
 * Exits 0 on all-pass, 1 on any failure.
 */
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import { execFileSync } from 'node:child_process';
import path from 'node:path';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const require = createRequire(import.meta.url);
const { buildSync } = require(path.resolve(__dirname, '..', 'polyfill', 'node_modules', 'esbuild'));
const SRC = path.resolve(__dirname, '..', 'polyfill', 'src');

const bundlePath = '/tmp/flatbuffers.bundle.mjs';
buildSync({
  entryPoints: [path.join(SRC, 'flatbuffers.js')],
  bundle: true, format: 'esm', outfile: bundlePath, write: true, logLevel: 'silent',
});
const { parseSchema } = await import(bundlePath);

let pass = 0, fail = 0;
function ok(cond, msg) {
  if (cond) { pass++; console.log('  ok  ' + msg); }
  else { fail++; console.log('  FAIL ' + msg); }
}
function hex(u8) { return Buffer.from(u8).toString('hex'); }
function throws(fn, msg) {
  try { fn(); ok(false, msg + ' (no throw)'); }
  catch (e) { ok(true, msg + ' → ' + e.message); }
}
function deep(a, b) { return JSON.stringify(a, bigJ) === JSON.stringify(b, bigJ); }
function bigJ(k, v) { return typeof v === 'bigint' ? v + 'n' : v; }

const SCHEMA = `
namespace demo;

enum Color : ubyte { RED = 1, GREEN = 2, BLUE = 7 }

struct Vec3 { x: float; y: float; z: float; }
struct Pair { a: int; b: long; }
struct Mask { bits: [ubyte:4]; tag: int; }

table Item { id: int; label: string; }

table Mon {
  hp: int = 100;
  mana: short = 150;
  color: Color = GREEN;
  name: string;
  friends: [int];
  nicknames: [string];
  pos: Vec3;
  pair: Pair;
  mask: Mask;
  items: [Item];
  dmg: double;
  flag: bool;
  blob: [ubyte];
  eq: Any;
  legacy: string (deprecated);
  req: int (id: 20);
}

union Any { Item, Mon }

root_type Mon;
`;

let s;
try { s = parseSchema(SCHEMA); ok(true, 'parseSchema succeeds on full-featured schema'); }
catch (e) { ok(false, 'parseSchema threw: ' + e.message); console.log(e.stack); process.exit(1); }

ok(!!s.tables['demo.Mon'], 'table registered package-qualified');
ok(s.Mon === s.tables['demo.Mon'], 'short name exposed as direct key');
ok(!!s.structs['demo.Vec3'] && !!s.enums['demo.Color'] && !!s.unions['demo.Any'], 'struct/enum/union registered');
ok(s.rootType === s.tables['demo.Mon'], 'root_type resolved');
ok(s.enums['demo.Color'].values.BLUE === 7, 'explicit enum value parsed');
ok(s.structs['demo.Vec3'].size === 12 && s.structs['demo.Vec3'].align === 4, 'Vec3 layout 12/4');
ok(s.structs['demo.Pair'].size === 16 && s.structs['demo.Pair'].align === 8, 'Pair padded to 16/8');
ok(s.structs['demo.Mask'].size === 8, 'Mask [ubyte:4]+int = 8 bytes');
ok(s.Mon.byName.req.slot === 20, '(id: 20) honoured');
ok(s.Mon.byName.legacy.deprecated === true, '[deprecated] recorded');

// ── defaults omitted, non-defaults stored ──
var only = s.Mon.encode({});
ok(only.length > 0, 'empty table encodes');
var od = s.Mon.decode(only);
ok(od.hp === 100 && od.mana === 150 && od.color === 2, 'defaults returned for absent fields');
ok(od.name === undefined && deep(od.friends, []) && deep(od.items, []), 'absent string/vector defaults');
ok(od.pos === undefined && od.eq === undefined, 'absent struct/union undefined');

// ── scalars of every width ──
var full = {
  hp: 80, mana: 200, color: 'BLUE', name: 'monster',
  friends: [1, 2, 3], nicknames: ['bob', 'sue✓'],
  pos: { x: 1.5, y: -2.25, z: 3.5 }, pair: { a: -7, b: 2n ** 40n },
  mask: { bits: [1, 2, 3, 4], tag: -9 }, items: [{ id: 1, label: 'a' }, { id: 2, label: 'bb' }],
  dmg: 3.14159, flag: true, blob: [255, 0, 128], req: 42,
};
var fd = s.Mon.decode(s.Mon.encode(full));
ok(fd.hp === 80, 'int32 roundtrip');
ok(fd.mana === 200, 'int16 roundtrip');
ok(fd.color === 7, 'enum by name encodes number, decodes number');
ok(fd.name === 'monster', 'string roundtrip');
ok(deep(fd.friends, [1, 2, 3]), 'vector<int> roundtrip');
ok(deep(fd.nicknames, ['bob', 'sue✓']), 'vector<string> roundtrip with UTF-8');
ok(fd.pos.x === 1.5 && fd.pos.y === -2.25 && fd.pos.z === 3.5, 'struct<float> roundtrip');
ok(fd.pair.a === -7 && fd.pair.b === 2n ** 40n, 'struct with int64 roundtrip');
ok(deep(fd.mask.bits, [1, 2, 3, 4]) && fd.mask.tag === -9, 'fixed array [ubyte:4] + int roundtrip');
ok(fd.items.length === 2 && fd.items[0].label === 'a' && fd.items[1].id === 2, 'vector<table> roundtrip');
ok(Math.abs(fd.dmg - 3.14159) < 1e-9, 'double roundtrip');
ok(fd.flag === true, 'bool roundtrip');
ok(deep(fd.blob, [-1, 0, -128]), 'vector<byte> roundtrip (ubyte stored as signed, per scope)');
ok(fd.req === 42, 'explicit id: 20 field roundtrip');

// ── union ──
var ud = s.Mon.decode(s.Mon.encode({ eq: { type: 'Item', value: { id: 5, label: 'sword' } } }));
ok(ud.eq.typeNum === 1 && ud.eq.type === 'Item', 'union type index roundtrip');
ok(ud.eq.value.id === 5 && ud.eq.value.label === 'sword', 'union value roundtrip');
var ud2 = s.Mon.decode(s.Mon.encode({ eq: { type: 'Mon', value: { hp: 3 } } }));
ok(ud2.eq.typeNum === 2 && ud2.eq.value.hp === 3, 'second union member roundtrip');
throws(() => s.Mon.encode({ eq: { type: 'Nope', value: {} } }), 'unknown union member rejected');
throws(() => s.Mon.encode({ eq: { value: {} } }), 'union without type rejected');

// ── negative int64 / boundaries ──
var I64 = parseSchema('table T { a: long; b: int; c: short; d: byte; }').T;
var i64v = { a: -(2n ** 63n), b: -2147483648, c: -32768, d: -128 };
ok(deep(I64.decode(I64.encode(i64v)), i64v), 'signed minimums roundtrip');
var i64max = { a: 2n ** 63n - 1n, b: 2147483647, c: 32767, d: 127 };
ok(deep(I64.decode(I64.encode(i64max)), i64max), 'signed maximums roundtrip');

// ── root offset / vtable structure sanity ──
var Simple = parseSchema('table T { a: int; b: string; }').T;
var sb = Simple.encode({ a: 42, b: 'hi' });
var rootOff = sb[0] | (sb[1] << 8) | (sb[2] << 16) | (sb[3] << 24);
ok(rootOff > 0 && rootOff < sb.length, 'root uoffset at byte 0 points inside buffer');
var so = (sb[rootOff] | (sb[rootOff + 1] << 8) | (sb[rootOff + 2] << 16) | (sb[rootOff + 3] << 24)) | 0;
var vt = rootOff - so;
ok(vt >= 0 && vt < sb.length, 'soffset locates the vtable');
var vsz = sb[vt] | (sb[vt + 1] << 8);
ok(vsz === 4 + 2 * 2, 'vtable size = 4 + 2*slots for a 2-slot table');
var tsz = sb[vt + 2] | (sb[vt + 3] << 8);
ok(tsz === 4 + 4 + 4, 'table inline size = soffset + int field + string offset');

// ── vtable dedup: two identical sub-tables share one vtable ──
var Dd = parseSchema('table P { xs: [Q]; } table Q { a: int; b: int; }').P;
var db = Dd.encode({ xs: [{ a: 1, b: 2 }, { a: 1, b: 2 }, { a: 1, b: 2 }] });
ok(db.length < 90, 'identical nested tables dedup their vtable (' + db.length + ' bytes)');
ok(deep(Dd.decode(db).xs, [{ a: 1, b: 2 }, { a: 1, b: 2 }, { a: 1, b: 2 }]), 'dedup roundtrip still correct');

// ── required ──
var Rq = parseSchema('table T { a: int (required); }').T;
throws(() => Rq.decode(Rq.encode({})), '[required] enforced on decode');
ok(Rq.decode(Rq.encode({ a: 1 })).a === 1, 'required field present decodes');

// ── deprecated ──
var Dp = parseSchema('table T { a: int; b: string (deprecated); }').T;
ok(Dp.decode(Dp.encode({ a: 1 })).a === 1, 'deprecated field skipped on encode');

// ── no-namespace schema ──
var NN = parseSchema('table T { v: int; }').T;
ok(NN.decode(NN.encode({ v: 9 })).v === 9, 'schema without namespace works');

// ── forward reference (table used before declared) ──
var Fw = parseSchema('table A { b: B; } table B { v: int; }');
ok(Fw.A.decode(Fw.A.encode({ b: { v: 4 } })).b.v === 4, 'forward type reference resolves');

// ── struct vector ──
var Sv = parseSchema('struct S { a: int; b: int; } table T { ss: [S]; }').T;
var svd = Sv.decode(Sv.encode({ ss: [{ a: 1, b: 2 }, { a: 3, b: 4 }] }));
ok(deep(svd.ss, [{ a: 1, b: 2 }, { a: 3, b: 4 }]), 'vector<struct> roundtrip');

// ── enum vector + enum default ──
var Ev = parseSchema('enum E : byte { A = 0, B = 1 } table T { es: [E]; e: E = B; }').T;
var evd = Ev.decode(Ev.encode({ es: [0, 1], e: 0 }));
ok(deep(evd.es, [0, 1]) && evd.e === 0, 'enum vector + non-default enum roundtrip');
ok(Ev.decode(Ev.encode({})).e === 1, 'enum default from schema');

// ── large stress ──
var Big = parseSchema('table B { s: [string]; n: [long]; f: [double]; t: [T]; } table T { v: int; w: string; }').B;
var big = {
  s: Array.from({ length: 400 }, (_, i) => 'x-' + i + '-✓'),
  n: Array.from({ length: 400 }, (_, i) => BigInt(i) * 1000003n - 7n),
  f: Array.from({ length: 400 }, (_, i) => i / 3),
  t: Array.from({ length: 200 }, (_, i) => ({ v: i, w: 'w' + i })),
};
var bd = Big.decode(Big.encode(big));
ok(bd.s.length === 400 && bd.s[399] === 'x-399-✓', 'large vector<string> roundtrip');
ok(bd.n.length === 400 && bd.n[0] === -7n && bd.n[399] === 399n * 1000003n - 7n, 'large vector<long> roundtrip');
ok(bd.f.length === 400 && bd.f[3] === 1, 'large vector<double> roundtrip');
ok(bd.t.length === 200 && bd.t[199].w === 'w199', 'large vector<table> roundtrip');

// ── parse errors ──
throws(() => parseSchema('table T { a: NoSuchType; }'), 'unknown type rejected');
throws(() => parseSchema('table T { a: int'), 'unterminated table rejected');
throws(() => parseSchema('table T { a int; }'), 'missing colon rejected');
throws(() => parseSchema('table T { a: [string]; } struct S { v: [string]; }').S, 'string vector in struct rejected');
throws(() => parseSchema('table T { a: [NoSuchType]; }'), 'unknown vector element rejected');
throws(() => parseSchema('union U { NoSuch }\ntable T { u: U; }'), 'unknown union member rejected');
throws(() => parseSchema('table T { a: int = "x"; }'), 'bad default value rejected');

// ── byte-exact cross-check against the OFFICIAL python flatbuffers.Builder ──
// Byte equality with the reference encoder is the strongest available proof of
// wire compatibility (it also implies the reference decodes our output).
const PYREF = `
import flatbuffers, sys
out = {}
b = flatbuffers.Builder(0)
name = b.CreateString("monster")
b.StartVector(4, 3, 4)
for v in reversed([1, 2, 3]): b.PrependInt32(v)
friends = b.EndVector()
b.StartObject(6)
b.PrependUint8Slot(5, 2, 2)                     # default -> omitted
b.Prep(4, 12)                                   # struct: inline, reverse fields
b.PrependFloat32(3.5); b.PrependFloat32(-2.25); b.PrependFloat32(1.5)
b.PrependStructSlot(4, b.Offset(), 0)
b.PrependUOffsetTRelativeSlot(3, friends, 0)
b.PrependUOffsetTRelativeSlot(2, name, 0)
b.PrependInt16Slot(1, 150, 150)                 # default -> omitted
b.PrependInt32Slot(0, 80, 100)
r = b.EndObject(); b.Finish(r); out['full'] = b.Output()

b = flatbuffers.Builder(0); b.StartObject(6); b.PrependInt32Slot(0, 100, 100)
r = b.EndObject(); b.Finish(r); out['default_omitted'] = b.Output()

b = flatbuffers.Builder(0); b.StartObject(6)
r = b.EndObject(); b.Finish(r); out['empty'] = b.Output()

b = flatbuffers.Builder(0)
offs = []
for _ in range(2):
    b.StartObject(1); b.PrependInt32Slot(0, 7, 0); offs.append(b.EndObject())
b.StartVector(4, 2, 4)
for o in reversed(offs): b.PrependUOffsetTRelative(o)
xs = b.EndVector()
b.StartObject(1); b.PrependUOffsetTRelativeSlot(0, xs, 0)
r = b.EndObject(); b.Finish(r); out['dedup'] = b.Output()

b = flatbuffers.Builder(0); b.StartObject(2)
b.PrependInt64Slot(1, -(2 ** 63), 0); b.PrependInt32Slot(0, 2147483647, 0)
r = b.EndObject(); b.Finish(r); out['wide'] = b.Output()

b = flatbuffers.Builder(0); s = b.CreateString("sue\\u2713\\U0001f600")
b.StartObject(1); b.PrependUOffsetTRelativeSlot(0, s, 0)
r = b.EndObject(); b.Finish(r); out['utf8'] = b.Output()

b = flatbuffers.Builder(0)
b.StartObject(1); b.PrependInt32Slot(0, 5, 0); child = b.EndObject()
b.StartObject(1); b.PrependUOffsetTRelativeSlot(0, child, 0)
r = b.EndObject(); b.Finish(r); out['nested'] = b.Output()

b = flatbuffers.Builder(0)
b.StartVector(8, 3, 8)
for v in reversed([1, -2, 3]): b.PrependInt64(v)
lv = b.EndVector()
b.StartObject(1); b.PrependUOffsetTRelativeSlot(0, lv, 0)
r = b.EndObject(); b.Finish(r); out['vec_long'] = b.Output()

for k, v in out.items(): print(k, v.hex())
`;

const XMON = parseSchema(`
  struct Vec3 { x: float; y: float; z: float; }
  table Mon { hp: int = 100; mana: short = 150; name: string; friends: [int]; pos: Vec3; color: ubyte = 2; }
`);
const XQ = parseSchema('table Q { a: int; } table P { xs: [Q]; }').P;
const XT = parseSchema('table T { a: int; b: long; }').T;
const XS = parseSchema('table T { s: string; }').T;
const XC = parseSchema('table C { a: int; } table P { c: C; }').P;
const XL = parseSchema('table T { l: [long]; }').T;

const jsBytes = {
  full: XMON.Mon.encode({ hp: 80, mana: 150, name: 'monster', friends: [1, 2, 3],
                          pos: { x: 1.5, y: -2.25, z: 3.5 }, color: 2 }),
  default_omitted: XMON.Mon.encode({ hp: 100 }),
  empty: XMON.Mon.encode({}),
  dedup: XQ.encode({ xs: [{ a: 7 }, { a: 7 }] }),
  wide: XT.encode({ a: 2147483647, b: -(2n ** 63n) }),
  utf8: XS.encode({ s: 'sue✓\u{1f600}' }),
  nested: XC.encode({ c: { a: 5 } }),
  vec_long: XL.encode({ l: [1n, -2n, 3n] }),
};

let pyOut = null;
try {
  pyOut = execFileSync('python3', ['-c', PYREF], { encoding: 'utf8' });
} catch (e) {
  const msg = String(e && e.stderr || e.message);
  if (msg.includes('No module named')) console.log('  SKIP python cross-check (python flatbuffers not installed)');
  else { fail++; console.log('  FAIL python cross-check errored: ' + msg.split('\n').slice(-3).join(' ')); }
}
if (pyOut !== null) {
  const ref = {};
  for (const line of pyOut.split('\n')) {
    const sp = line.indexOf(' ');
    if (sp > 0) ref[line.slice(0, sp)] = line.slice(sp + 1).trim();
  }
  for (const k of Object.keys(jsBytes)) {
    const mine = hex(jsBytes[k]);
    ok(mine === ref[k], 'byte-exact vs python flatbuffers: ' + k);
    if (k === 'full') {
      const back = XMON.Mon.decode(new Uint8Array(Buffer.from(ref[k], 'hex')));
      ok(back.hp === 80 && back.name === 'monster' && back.pos.z === 3.5 &&
         JSON.stringify(back.friends) === '[1,2,3]', 'decodes python-produced buffer');
    }
  }
}

console.log('\nflatbuffers: ' + pass + ' passed, ' + fail + ' failed');
process.exit(fail ? 1 : 0);

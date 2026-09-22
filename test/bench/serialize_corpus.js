/* __am_serialize__/__am_deserialize__ 字节等价性语料测试：
 * 新旧 polyfill 对同一语料库应产出完全相同字节（hex），且往返一致。
 * 用法：./build/amoib test/bench/serialize_corpus.js
 */
function hex(buf) {
  var u = new Uint8Array(buf);
  var s = '';
  for (var i = 0; i < u.length; i++) s += (u[i] < 16 ? '0' : '') + u[i].toString(16);
  return s;
}

var corpus = [
  null, undefined, true, false,
  0, -0, 1, -1, 2147483647, -2147483648, 2147483648, -2147483649,
  3.14, -0.5, NaN, Infinity, -Infinity, 1e21,
  '', 'a', 'hello world', 'x'.repeat(100000),
  '中文测试', '日本語', '한국어', 'emoji: \uD83D\uDE00\uD83C\uDF89',
  '\uD800x', 'x\uDC00', '\uD800', '\uDC00', 'a\uD800\uDC00b',
  'mixed: 中文 abc 123 \uD83D\uDE00 end',
  [], [1, 2, 3], ['a', 'b', [1, [2]]], [null, undefined, NaN],
  {}, { a: 1 }, { a: { b: { c: 'deep' } } }, { '中文键': '值' },
  { arr: [1, 'two', { three: 3 }], n: null, b: true },
  new Date(0), new Date(1234567890123),
  /abc/gi, /^\d+$/m,
  new Map([[1, 'one'], ['two', 2]]),
  new Set([1, 2, 3]),
  new Error('boom'), (function () { var e = new TypeError('t'); e.name = 'TypeError'; return e; })(),
  new ArrayBuffer(0), new ArrayBuffer(16),
  new Uint8Array([1, 2, 3, 255]),
  new Int16Array([-1, 0, 32767]),
  new Float64Array([1.5, -2.25]),
  new DataView(new ArrayBuffer(8), 2, 4),
  new Uint8Array(100000).fill(7),
  { t: new Uint8Array([9, 8, 7]), d: new Date(42), s: new Set(['x', 'y']) },
  { cyc: null }
];
corpus[corpus.length - 1].cyc = corpus[corpus.length - 1];

var lines = [];
for (var i = 0; i < corpus.length; i++) {
  try {
    var bytes = __am_serialize__(corpus[i]);
    var back = __am_deserialize__(bytes);
    lines.push(i + ': ' + hex(bytes));
  } catch (e) {
    lines.push(i + ': ERR ' + e.name + ':' + e.message);
  }
}
console.log(lines.join('\n'));
console.log('CORPUS-END');
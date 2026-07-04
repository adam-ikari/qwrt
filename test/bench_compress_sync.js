/**
 * Compression Benchmark — qwrt vs Node.js (sync native only)
 *
 * Measures pal.nativeCompress / pal.nativeDecompress throughput
 * on qwrt (native zlib) and compares with Node.js zlib.
 *
 * Run on Node.js:  node bench_compress_sync.js
 * Run on qwrt:     via test_compress_bench
 */

function now_ns() {
  /* Use pal.hrtime() for nanosecond precision when available */
  if (typeof globalThis !== 'undefined' && typeof globalThis.__pal__ !== 'undefined' &&
      typeof globalThis.__pal__.hrtime === 'function') {
    return globalThis.__pal__.hrtime();
  }
  /* Use performance.now() if it returns real sub-ms values */
  if (typeof performance !== 'undefined') {
    var t = performance.now();
    if (t > 0) return t * 1e6;  /* ms → ns */
  }
  /* Fallback: Date.now() with ms precision */
  return Date.now() * 1e6;
}

function makeRepetitive(size) {
  var pattern = 'Hello World! ';
  var s = '';
  while (s.length < size) s += pattern;
  return new TextEncoder().encode(s.slice(0, size));
}

function makeRandom(size) {
  var arr = new Uint8Array(size);
  var chunkSize = 65536;
  for (var off = 0; off < size; off += chunkSize) {
    var len = Math.min(chunkSize, size - off);
    crypto.getRandomValues(arr.subarray(off, off + len));
  }
  return arr;
}

function makeSparse(size) {
  var arr = new Uint8Array(size);
  for (var i = 0; i < size; i++) arr[i] = i & 0x3;
  return arr;
}

var hasNative = (typeof globalThis !== 'undefined' && typeof globalThis.__pal__ !== 'undefined' && typeof globalThis.__pal__.nativeCompress === 'function');
var isNode = (typeof process !== 'undefined' && typeof process.versions !== 'undefined' && typeof process.versions.node !== 'undefined');

/* Node.js fallback: use zlib module */
var zlibSync = null;
if (isNode && !hasNative) {
  var zlib = require('zlib');
  zlibSync = {
    compress: function(data, format) {
      if (format === 'deflate-raw') return zlib.deflateRawSync(data);
      if (format === 'deflate') return zlib.deflateSync(data);
      if (format === 'gzip') return zlib.gzipSync(data);
    },
    decompress: function(data, format) {
      if (format === 'deflate-raw') return zlib.inflateRawSync(data);
      if (format === 'deflate') return zlib.inflateSync(data);
      if (format === 'gzip') return zlib.gunzipSync(data);
    }
  };
}

function nativeCompress(data, format) {
  if (hasNative) return globalThis.__pal__.nativeCompress(data, format);
  if (zlibSync) return zlibSync.compress(data, format);
  throw new Error('No native compression available');
}

function nativeDecompress(data, format) {
  if (hasNative) return globalThis.__pal__.nativeDecompress(data, format);
  if (zlibSync) return zlibSync.decompress(data, format);
  throw new Error('No native decompression available');
}

var runtime = 'unknown';
if (isNode) runtime = 'Node.js ' + process.version + ' (zlib)';
else if (hasNative) runtime = 'qwrt (zlib)';

var formats = ['deflate-raw', 'deflate', 'gzip'];
var sizes = [
  { label: '1KB',   size: 1024,        iters: 1000 },
  { label: '10KB',  size: 10 * 1024,   iters: 500 },
  { label: '100KB', size: 100 * 1024,  iters: 100 },
  { label: '1MB',   size: 1024 * 1024, iters: 20 },
];
var patterns = [
  { name: 'repetitive', gen: makeRepetitive },
  { name: 'random',     gen: makeRandom },
  { name: 'sparse',     gen: makeSparse },
];

function main() {
  console.log('=== Compression Benchmark: ' + runtime + ' ===\n');

  var results = [];

  for (var fi = 0; fi < formats.length; fi++) {
    var fmt = formats[fi];

    /* Compress */
    console.log('[' + fmt + ' compress]');
    for (var si = 0; si < sizes.length; si++) {
      var s = sizes[si];
      for (var pi = 0; pi < patterns.length; pi++) {
        var pat = patterns[pi];
        var data = pat.gen(s.size);
        var name = 'compress.' + pat.name + '.' + s.label + '.' + fmt;
        nativeCompress(data, fmt); /* warmup */
        var t0 = now_ns();
        for (var i = 0; i < s.iters; i++) {
          nativeCompress(data, fmt);
        }
        var elapsed_ns = now_ns() - t0;
        var elapsed_ms = elapsed_ns / 1e6;
        var mb_per_sec = (data.length * s.iters) / (elapsed_ns / 1e9) / (1024 * 1024);
        console.log('  ' + name.padEnd(45) + mb_per_sec.toFixed(1).padStart(8) + ' MB/s  (' + elapsed_ms.toFixed(1) + ' ms)');
        results.push({ name: name, mb_per_sec: +mb_per_sec.toFixed(1), size: s.size, format: fmt, mode: 'compress' });
      }
    }

    /* Decompress */
    console.log('\n[' + fmt + ' decompress]');
    for (var si = 0; si < sizes.length; si++) {
      var s = sizes[si];
      for (var pi = 0; pi < patterns.length; pi++) {
        var pat = patterns[pi];
        var data = pat.gen(s.size);
        var compressed = nativeCompress(data, fmt);
        var name = 'decompress.' + pat.name + '.' + s.label + '.' + fmt;
        nativeDecompress(compressed, fmt); /* warmup */
        var t0 = now_ns();
        for (var i = 0; i < s.iters; i++) {
          nativeDecompress(compressed, fmt);
        }
        var elapsed_ns = now_ns() - t0;
        var elapsed_ms = elapsed_ns / 1e6;
        var mb_per_sec = (data.length * s.iters) / (elapsed_ns / 1e9) / (1024 * 1024);
        console.log('  ' + name.padEnd(45) + mb_per_sec.toFixed(1).padStart(8) + ' MB/s  (' + elapsed_ms.toFixed(1) + ' ms)');
        results.push({ name: name, mb_per_sec: +mb_per_sec.toFixed(1), size: s.size, format: fmt, mode: 'decompress' });
      }
    }
    console.log('');
  }

  /* Compression ratio */
  console.log('=== Compression Ratio ===');
  for (var fi = 0; fi < formats.length; fi++) {
    var fmt = formats[fi];
    for (var pi = 0; pi < patterns.length; pi++) {
      var pat = patterns[pi];
      var data = pat.gen(1024 * 1024);
      var compressed = nativeCompress(data, fmt);
      var ratio = (compressed.length / data.length * 100).toFixed(1);
      console.log('  ' + (pat.name + '.1MB.' + fmt).padEnd(30) + ratio + '%');
      results.push({ name: 'ratio.' + pat.name + '.1MB.' + fmt, ratio: +ratio, format: fmt, mode: 'ratio' });
    }
  }

  console.log('\n=== JSON ===');
  console.log(JSON.stringify(results));
}

main();

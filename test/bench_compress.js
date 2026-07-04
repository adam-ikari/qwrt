/**
 * Compression Benchmark — qwrt vs Node.js
 *
 * Run on Node.js:  node bench_compress.js
 * Run on qwrt:     via C test harness (test_compress_bench)
 *
 * Two measurement modes:
 *   1. Sync (pal.nativeCompress/nativeDecompress) — pure zlib throughput
 *   2. Stream (CompressionStream/DecompressionStream) — real API including async overhead
 *
 * Tests deflate-raw, deflate, gzip across multiple data sizes and patterns.
 */

function now_ms() {
  if (typeof performance !== 'undefined') return performance.now();
  return Date.now();
}

/* Generate test data */
function makeRepetitive(size) {
  var pattern = 'Hello World! ';
  var s = '';
  while (s.length < size) s += pattern;
  return new TextEncoder().encode(s.slice(0, size));
}

function makeRandom(size) {
  var arr = new Uint8Array(size);
  /* crypto.getRandomValues has a 65536 byte limit in some implementations */
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

/* Compress via CompressionStream (streaming API) */
function compressStream(data, format) {
  return new Promise(function(resolve, reject) {
    var cs = new CompressionStream(format);
    var writer = cs.writable.getWriter();
    var reader = cs.readable.getReader();
    var chunks = [];
    var totalSize = 0;

    reader.read().then(function pump(result) {
      if (result.done) {
        var combined = new Uint8Array(totalSize);
        var off = 0;
        for (var i = 0; i < chunks.length; i++) {
          combined.set(chunks[i], off);
          off += chunks[i].length;
        }
        resolve(combined);
        return;
      }
      chunks.push(result.value);
      totalSize += result.value.length;
      return reader.read().then(pump);
    });

    writer.write(data);
    writer.close();
  });
}

/* Decompress via DecompressionStream */
function decompressStream(data, format) {
  return new Promise(function(resolve, reject) {
    var ds = new DecompressionStream(format);
    var writer = ds.writable.getWriter();
    var reader = ds.readable.getReader();
    var chunks = [];
    var totalSize = 0;

    reader.read().then(function pump(result) {
      if (result.done) {
        var combined = new Uint8Array(totalSize);
        var off = 0;
        for (var i = 0; i < chunks.length; i++) {
          combined.set(chunks[i], off);
          off += chunks[i].length;
        }
        resolve(combined);
        return;
      }
      chunks.push(result.value);
      totalSize += result.value.length;
      return reader.read().then(pump);
    });

    writer.write(data);
    writer.close();
  });
}

/* Check if native sync API is available (qwrt only) */
var hasNative = (typeof globalThis !== 'undefined' && typeof globalThis.__pal__ !== 'undefined' && typeof globalThis.__pal__.nativeCompress === 'function');

var runtime = 'unknown';
if (typeof process !== 'undefined') {
  runtime = 'Node.js ' + process.version;
} else if (hasNative) {
  runtime = 'qwrt (native+zlib)';
} else {
  runtime = 'qwrt (stream only)';
}

var formats = ['deflate-raw', 'deflate', 'gzip'];
var sizes = [
  { label: '1KB',   size: 1024,        iters: 500 },
  { label: '10KB',  size: 10 * 1024,   iters: 200 },
  { label: '100KB', size: 100 * 1024,  iters: 50 },
  { label: '1MB',   size: 1024 * 1024, iters: 10 },
];
var patterns = [
  { name: 'repetitive', gen: makeRepetitive },
  { name: 'random',     gen: makeRandom },
  { name: 'sparse',     gen: makeSparse },
];

var results = [];

async function main() {
  console.log('=== Compression Benchmark: ' + runtime + ' ===\n');

  /* --- Section 1: Sync native throughput (qwrt only) --- */
  if (hasNative) {
    console.log('=== 1. Native Sync Throughput (pal.nativeCompress/nativeDecompress) ===');
    var pal = globalThis.__pal__;
    for (var fi = 0; fi < formats.length; fi++) {
      var fmt = formats[fi];
      console.log('\n  [' + fmt + ' compress]');
      for (var si = 0; si < sizes.length; si++) {
        var s = sizes[si];
        for (var pi = 0; pi < patterns.length; pi++) {
          var pat = patterns[pi];
          var data = pat.gen(s.size);
          var name = 'sync_compress.' + pat.name + '.' + s.label + '.' + fmt;
          /* Warmup */
          pal.nativeCompress(data, fmt);
          var t0 = now_ms();
          for (var i = 0; i < s.iters; i++) {
            pal.nativeCompress(data, fmt);
          }
          var elapsed = now_ms() - t0;
          var mb_per_sec = (data.length * s.iters) / (elapsed / 1000.0) / (1024 * 1024);
          console.log('  ' + name.padEnd(50) + mb_per_sec.toFixed(1).padStart(8) + ' MB/s  (' + elapsed.toFixed(1) + ' ms)');
          results.push({ name: name, mb_per_sec: mb_per_sec, size: s.size, format: fmt, mode: 'sync_compress' });
        }
      }

      console.log('\n  [' + fmt + ' decompress]');
      for (var si = 0; si < sizes.length; si++) {
        var s = sizes[si];
        for (var pi = 0; pi < patterns.length; pi++) {
          var pat = patterns[pi];
          var data = pat.gen(s.size);
          var compressed = pal.nativeCompress(data, fmt);
          var name = 'sync_decompress.' + pat.name + '.' + s.label + '.' + fmt;
          /* Warmup */
          pal.nativeDecompress(compressed, fmt);
          var t0 = now_ms();
          for (var i = 0; i < s.iters; i++) {
            pal.nativeDecompress(compressed, fmt);
          }
          var elapsed = now_ms() - t0;
          var mb_per_sec = (data.length * s.iters) / (elapsed / 1000.0) / (1024 * 1024);
          console.log('  ' + name.padEnd(50) + mb_per_sec.toFixed(1).padStart(8) + ' MB/s  (' + elapsed.toFixed(1) + ' ms)');
          results.push({ name: name, mb_per_sec: mb_per_sec, size: s.size, format: fmt, mode: 'sync_decompress' });
        }
      }
    }
  }

  /* --- Section 2: CompressionStream throughput (both runtimes) --- */
  console.log('\n=== 2. CompressionStream Throughput ===');
  for (var fi = 0; fi < formats.length; fi++) {
    var fmt = formats[fi];
    for (var si = 0; si < sizes.length; si++) {
      var s = sizes[si];
      for (var pi = 0; pi < patterns.length; pi++) {
        var pat = patterns[pi];
        var data = pat.gen(s.size);

        /* Compress throughput */
        var name_c = 'stream_compress.' + pat.name + '.' + s.label + '.' + fmt;
        await compressStream(data, fmt); /* warmup */
        var t0 = now_ms();
        for (var i = 0; i < s.iters; i++) {
          await compressStream(data, fmt);
        }
        var elapsed = now_ms() - t0;
        var mb_per_sec = (data.length * s.iters) / (elapsed / 1000.0) / (1024 * 1024);
        console.log('  ' + name_c.padEnd(50) + mb_per_sec.toFixed(1).padStart(8) + ' MB/s  (' + elapsed.toFixed(1) + ' ms)');
        results.push({ name: name_c, mb_per_sec: mb_per_sec, size: s.size, format: fmt, mode: 'stream_compress' });

        /* Decompress throughput */
        var compressed = await compressStream(data, fmt);
        var name_d = 'stream_decompress.' + pat.name + '.' + s.label + '.' + fmt;
        await decompressStream(compressed, fmt); /* warmup */
        var t1 = now_ms();
        for (var i = 0; i < s.iters; i++) {
          await decompressStream(compressed, fmt);
        }
        var elapsed2 = now_ms() - t1;
        var mb_per_sec2 = (data.length * s.iters) / (elapsed2 / 1000.0) / (1024 * 1024);
        console.log('  ' + name_d.padEnd(50) + mb_per_sec2.toFixed(1).padStart(8) + ' MB/s  (' + elapsed2.toFixed(1) + ' ms)');
        results.push({ name: name_d, mb_per_sec: mb_per_sec2, size: s.size, format: fmt, mode: 'stream_decompress' });
      }
    }
  }

  /* --- Section 3: Compression ratio --- */
  console.log('\n=== 3. Compression Ratio ===');
  for (var fi = 0; fi < formats.length; fi++) {
    var fmt = formats[fi];
    for (var pi = 0; pi < patterns.length; pi++) {
      var pat = patterns[pi];
      var data = pat.gen(1024 * 1024);
      if (hasNative) {
        var compressed = globalThis.__pal__.nativeCompress(data, fmt);
        var ratio = (compressed.length / data.length * 100).toFixed(1);
        console.log('  ratio.' + pat.name + '.1MB.' + fmt + ' = ' + ratio + '%');
      } else {
        var compressed = await compressStream(data, fmt);
        var ratio = (compressed.length / data.length * 100).toFixed(1);
        console.log('  ratio.' + pat.name + '.1MB.' + fmt + ' = ' + ratio + '%');
      }
    }
  }

  console.log('\n=== JSON Results ===');
  console.log(JSON.stringify(results, null, 2));
}

main().catch(function(e) { console.error(e); });
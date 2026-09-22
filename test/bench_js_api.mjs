#!/usr/bin/env node
/**
 * qzjs JS-API performance benchmark — runs identically on qzjs / node / bun.
 *
 * Five modes (--mode streams|crypto|compress|fs|wasm|all), each printing one
 * JSON line as the last line of output:
 *
 *   streams   pipeThrough backpressure throughput + tee dual-branch drain (MB/s)
 *   crypto    AES-GCM-256 encrypt/decrypt, SHA-256, HMAC-SHA256 (MB/s)
 *   compress  CompressionStream gzip + deflate-raw encode (MB/s)
 *   fs        big-file sequential read (MB/s) + small-file batch read (files/s)
 *   wasm      precompiled module exec: fib(38) iterations/s
 *
 * Runtime adaptation:
 *   - no static imports (qzjs runs files as plain scripts)
 *   - fs: qzjs uses qzjs.fs.*; node/bun dynamically import node:fs promises
 *   - argv: process.argv (node/bun) or globalThis.arguments (qzjs)
 *
 * Usage:
 *   <runtime> test/bench_js_api.mjs --mode all [--iters N] [--dir /tmp]
 *   (--iters scales the inner workload; default is tuned for ~1s per metric)
 */

function randomFill(buf) {
  // Web Crypto getRandomValues caps at 65536 bytes per call
  const CH = 65536;
  for (let off = 0; off < buf.length; off += CH) {
    crypto.getRandomValues(buf.subarray(off, Math.min(off + CH, buf.length)));
  }
}

function parseArgs() {
  const args = { mode: 'all', iters: 1, dir: '/tmp' };
  let argv;
  if (typeof process !== 'undefined' && process.argv) argv = process.argv.slice(2);
  else argv = (typeof globalThis.arguments !== 'undefined' && globalThis.arguments) || [];
  for (let i = 0; i < argv.length; i++) {
    if (argv[i] === '--mode') args.mode = argv[++i];
    else if (argv[i] === '--iters') args.iters = parseFloat(argv[++i]);
    else if (argv[i] === '--dir') args.dir = argv[++i];
  }
  return args;
}

const MIB = 1024 * 1024;
const join = (dir, name) => dir.replace(/\/+$/, '') + '/' + name;

async function loadFs() {
  if (globalThis.qzjs && globalThis.qzjs.fs && globalThis.qzjs.fs.readFileBinary) {
    return { kind: 'qzjs', fs: globalThis.qzjs.fs, join: join };
  }
  const m = await import('node:fs');
  const p = await import('node:path');
  const fsp = m.promises || m.default.promises;
  return { kind: 'node', fs: fsp, join: p.join || join };
}

async function timeIt(fn, samples) {
  const results = [];
  for (let i = 0; i < (samples || 5); i++) results.push(await fn());
  results.sort((a, b) => a - b);
  return results[Math.floor(results.length / 2)]; // median
}

// ---------------------------------------------------------------------------
// streams: backpressure-driven pipeThrough throughput + tee dual-branch drain
// ---------------------------------------------------------------------------
async function benchStreams(iters) {
  const chunk = new Uint8Array(64 * 1024);
  randomFill(chunk);
  const total = Math.max(chunk.byteLength * 4, Math.round(64 * MIB * iters));

  async function pipeOnce() {
    const t0 = performance.now();
    let enqueued = 0;
    const src = new ReadableStream({
      pull(controller) {
        controller.enqueue(chunk);
        enqueued += chunk.byteLength;
        if (enqueued >= total) controller.close();
      },
    });
    const reader = src.pipeThrough(new TransformStream()).getReader();
    let got = 0;
    for (;;) {
      const r = await reader.read();
      if (r.done) break;
      got += r.value.byteLength;
    }
    return got / ((performance.now() - t0) / 1000) / MIB;
  }

  async function teeOnce() {
    const t0 = performance.now();
    let enqueued = 0;
    const src = new ReadableStream({
      pull(controller) {
        controller.enqueue(chunk);
        enqueued += chunk.byteLength;
        if (enqueued >= total) controller.close();
      },
    });
    const branches = src.tee();
    const drain = async (reader) => {
      let got = 0;
      for (;;) {
        const r = await reader.read();
        if (r.done) break;
        got += r.value.byteLength;
      }
      return got;
    };
    const g = await Promise.all([drain(branches[0].getReader()), drain(branches[1].getReader())]);
    const secs = (performance.now() - t0) / 1000;
    return Math.max(g[0], g[1]) / secs / MIB;
  }

  const pipe = await timeIt(pipeOnce);
  const tee = await timeIt(teeOnce);
  return { pipe_mbs: +pipe.toFixed(1), tee_mbs: +tee.toFixed(1) };
}

// ---------------------------------------------------------------------------
// crypto: AES-GCM-256 enc/dec + SHA-256 + HMAC-SHA256 over a 1 MiB buffer
// ---------------------------------------------------------------------------
async function benchCrypto(iters) {
  const size = Math.round(MIB * iters);
  const buf = new Uint8Array(size);
  randomFill(buf);
  const keyRaw = crypto.getRandomValues(new Uint8Array(32));
  const key = await crypto.subtle.importKey('raw', keyRaw, 'AES-GCM', false, ['encrypt', 'decrypt']);
  const hmacKey = await crypto.subtle.importKey('raw', keyRaw, { name: 'HMAC', hash: 'SHA-256' }, false, ['sign']);
  const iv = new Uint8Array(12);

  async function once(op) {
    const t0 = performance.now();
    if (op === 'enc') await crypto.subtle.encrypt({ name: 'AES-GCM', iv: iv }, key, buf);
    else if (op === 'dec') await crypto.subtle.decrypt({ name: 'AES-GCM', iv: iv }, key, CT);
    else if (op === 'sha') await crypto.subtle.digest('SHA-256', buf);
    else if (op === 'hmac') await crypto.subtle.sign('HMAC', hmacKey, buf);
    return size / ((performance.now() - t0) / 1000) / MIB;
  }

  const CT = await crypto.subtle.encrypt({ name: 'AES-GCM', iv: iv }, key, buf);
  const enc = await timeIt(() => once('enc'));
  const dec = await timeIt(() => once('dec'));
  const sha = await timeIt(() => once('sha'));
  const hmac = await timeIt(() => once('hmac'));
  return { aes_gcm_enc_mbs: +enc.toFixed(1), aes_gcm_dec_mbs: +dec.toFixed(1),
           sha256_mbs: +sha.toFixed(1), hmac_sha256_mbs: +hmac.toFixed(1) };
}

// ---------------------------------------------------------------------------
// compress: CompressionStream gzip / deflate-raw one-shot encode of 4 MiB
// ---------------------------------------------------------------------------
async function benchCompress(iters) {
  const size = Math.round(4 * MIB * iters);
  const buf = new Uint8Array(size);
  const block = new TextEncoder().encode(
    JSON.stringify({ k: 'v', n: 42, arr: [1, 2, 3], s: 'x'.repeat(64) }));
  for (let off = 0; off < size; off += block.length) {
    buf.set(block.subarray(0, Math.min(block.length, size - off)), off);
  }

  async function once(format) {
    const t0 = performance.now();
    const src = new ReadableStream({
      start(controller) { controller.enqueue(buf); controller.close(); },
    });
    await src.pipeThrough(new CompressionStream(format)).pipeTo(new WritableStream({
      write() {},
    }));
    return size / ((performance.now() - t0) / 1000) / MIB;
  }

  const gzip = await timeIt(() => once('gzip'));
  const deflate = await timeIt(() => once('deflate-raw'));
  return { gzip_mbs: +gzip.toFixed(1), deflate_raw_mbs: +deflate.toFixed(1), input_bytes: size };
}

// ---------------------------------------------------------------------------
// fs: big-file sequential read + small-file batch read
// ---------------------------------------------------------------------------
async function benchFs(args, iters) {
  const f = await loadFs();
  const bigPath = f.join(args.dir, 'qzjs-bench-big.bin');
  const bigSize = Math.round(32 * MIB * iters);
  // All three runtimes' writeFile accept strings; binary writes vary
  // (qzjs's fs layer is string-only), so the fixture is text. The read
  // path measures sequential throughput either way.
  const blob = 'qzjs-bench-fixture;'.repeat(64 * 1024); // ~1.2 MiB text
  let written = 0;
  while (written < bigSize) {
    await f.fs.writeFile(bigPath, blob);
    written += blob.length;
  }

  const readBig = await timeIt(async () => {
    const t0 = performance.now();
    let ab;
    if (f.kind === 'qzjs') ab = await f.fs.readFileBinary(bigPath);
    else ab = await f.fs.readFile(bigPath);
    const bytes = ab.byteLength !== undefined ? ab.byteLength : ab.length;
    return bytes / ((performance.now() - t0) / 1000) / MIB;
  });

  // small-file batch: 256 x 4 KB files, readdir + read all.
  // Flat layout in args.dir — qzjs's fs layer cannot create directories.
  const smallDir = args.dir;
  const small = new Uint8Array(4096);
  randomFill(small);
  const nFiles = 256;
  for (let i = 0; i < nFiles; i++) {
    await f.fs.writeFile(f.join(smallDir, 'qzjs-bench-f' + i + '.bin'), small);
  }
  const batch = await timeIt(async () => {
    const t0 = performance.now();
    // readdir + read only this bench's files (args.dir may hold thousands
    // of unrelated entries on a shared /tmp)
    const names = (await f.fs.readdir(smallDir))
      .filter((n) => n.indexOf('qzjs-bench-f') === 0);
    const reads = [];
    for (let i = 0; i < names.length; i++) reads.push(f.fs.readFile(f.join(smallDir, names[i])));
    await Promise.all(reads);
    return names.length / ((performance.now() - t0) / 1000);
  });

  // cleanup
  try { await f.fs.unlink(bigPath); } catch (e) {}
  try {
    const names = await f.fs.readdir(smallDir);
    for (let i = 0; i < names.length; i++) {
      if (names[i].indexOf('qzjs-bench-f') === 0) await f.fs.unlink(f.join(smallDir, names[i]));
    }
  } catch (e) {}

  return { fs_backend: f.kind, big_read_mbs: +readBig.toFixed(1),
           small_batch_files_per_s: Math.round(batch) };
}

// ---------------------------------------------------------------------------
// wasm: fib(38) via precompiled module (execution speed, not compile time)
// ---------------------------------------------------------------------------
const WASM_FIB_I32 = Uint8Array.from(atob('AGFzbQEAAAABBgFgAX8BfwMCAQAHBwEDZmliAAAKHgEcACAAQQJMBH8gAAUgAEEBaxAAIABBAmsQAGoLCw=='), (c) => c.charCodeAt(0));

async function benchWasm(iters) {
  const mod = await WebAssembly.compile(WASM_FIB_I32);
  const inst = new WebAssembly.Instance(mod);
  const sanity = inst.exports.fib(20);
  if (sanity !== 10946) throw new Error('wasm fib(20) sanity failed: ' + sanity);

  // fib(38) ≈ 100+ ms per call on JIT runtimes, seconds on interpreters.
  // Calibrate: run one timed round, then repeat so total wall ≈ 1s.
  const n = 38;
  let t0 = performance.now();
  let acc = inst.exports.fib(n);
  const per = (performance.now() - t0) / 1000;
  const rounds = Math.max(1, Math.min(20, Math.round(1.0 / per)));
  t0 = performance.now();
  acc = 0;
  for (let i = 0; i < rounds; i++) acc += inst.exports.fib(n);
  const secs = (performance.now() - t0) / 1000;
  if (acc !== rounds * 63245986) throw new Error('fib(38) accumulator mismatch: ' + acc);
  return { fib38_ops_per_s: +((rounds / secs).toFixed(2)), round_ms: +(secs * 1000 / rounds).toFixed(1) };
}

// ---------------------------------------------------------------------------
const MODES = {
  streams: (a, it) => benchStreams(it),
  crypto: (a, it) => benchCrypto(it),
  compress: (a, it) => benchCompress(it),
  fs: benchFs,
  wasm: (a, it) => benchWasm(it),
};

async function main() {
  const args = parseArgs();
  const modes = args.mode === 'all' ? Object.keys(MODES) : [args.mode];
  const out = {};
  for (let i = 0; i < modes.length; i++) {
    const m = modes[i];
    const fn = MODES[m];
    if (!fn) { console.error('unknown mode: ' + m); processExit(2); return; }
    out[m] = await fn(args, args.iters);
  }
  console.log(JSON.stringify({ js_api_bench: true, date: new Date().toISOString().slice(0, 10), ...out }));
}

function processExit(code) {
  if (typeof process !== 'undefined' && process.exit) process.exit(code);
  throw new Error('exit ' + code);
}

main().catch(function (e) { console.error('BENCH-FAIL: ' + (e && (e.stack || e.message) || JSON.stringify(e))); processExit(1); });

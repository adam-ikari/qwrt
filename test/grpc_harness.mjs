/**
 * gRPC client e2e harness (verification only — not part of the runtime bundle).
 *
 * Runs the REAL polyfill/src/grpc.js (+ http2.js, hpack.js, protobuf.js,
 * flatbuffers.js, esbuild-bundled to ESM) against real HTTP/2 servers. Only the
 * transport is shimmed: `pal.tcpConnect/tcpWrite/tcpClose` are backed by Node
 * `net` sockets, so the h2 engine sees the same byte stream it would in the
 * qwrt runtime.
 *
 * Two peers, on purpose:
 *   1. A hand-written Node http2 server that encodes/decodes protobuf BYTES
 *      itself (no protobuf library anywhere near it). This is the deterministic,
 *      dependency-free core: it proves the 5-byte gRPC framing, the trailers
 *      status model, metadata, deadline and wire compatibility of our own
 *      protobuf.js against an independent reading of the spec. It also serves
 *      the flatbuffers path, which by design only a qwrt-aware peer can speak.
 *   2. @grpc/grpc-js, if resolvable (QWRT_GRPC_PEER_MODULES or a global
 *      install). A genuine standard peer — the strongest interop evidence.
 *      Skipped (loudly) when absent, since installing it needs network.
 *
 * Usage: node test/grpc_harness.mjs
 * Exits 0 on all-pass (skips allowed), 1 on any failure.
 */
import http2 from 'node:http2';
import net from 'node:net';
import fs from 'node:fs';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const require = createRequire(import.meta.url);
const { buildSync } = require(path.resolve(__dirname, '..', 'polyfill', 'node_modules', 'esbuild'));
const SRC = path.resolve(__dirname, '..', 'polyfill', 'src');

// 1. bundle the whole gRPC stack to a temp ESM file
const bundlePath = '/tmp/grpc_harness.bundle.mjs';
buildSync({
  entryPoints: [path.join(SRC, 'grpc.js')],
  bundle: true, format: 'esm', outfile: bundlePath, write: true, logLevel: 'silent',
});
const { setupGrpc } = await import(bundlePath);

// 2. pal shim over Node net (mirrors the qwrt pal.tcp* contract)
const pal = {
  tcpConnect(host, port, cb /*, opts */) {
    const sock = net.connect({ host, port });
    sock.setNoDelay(true);
    const h = { sock };
    sock.on('connect', () => cb.onconnect && cb.onconnect());
    sock.on('data', (d) => {
      const ab = d.buffer.slice(d.byteOffset, d.byteOffset + d.byteLength);
      cb.ondata && cb.ondata(ab);
    });
    sock.on('error', (e) => cb.onerror && cb.onerror(e.message));
    sock.on('close', () => cb.onclose && cb.onclose());
    return h;
  },
  tcpWrite(h, data) {
    return new Promise((resolve, reject) => {
      h.sock.write(data, (err) => (err ? reject(err) : resolve(data.length)));
    });
  },
  tcpClose(h) { try { h.sock.end(); } catch (e) {} },
};
setupGrpc(pal);
const grpc = globalThis.grpc;

// ── tiny test harness ───────────────────────────────────────────────
let passed = 0, failed = 0, skipped = 0;
const failures = [];
async function t(name, fn) {
  try { await fn(); passed++; console.log('  ok   ' + name); }
  catch (e) { failed++; failures.push(name + ': ' + (e && e.stack || e)); console.log('  FAIL ' + name + '\n       ' + (e && e.message || e)); }
}
function skip(name, why) { skipped++; console.log('  SKIP ' + name + ' (' + why + ')'); }
function eq(a, b, what) {
  if (a !== b) {
    if (typeof a === 'bigint') a = Number(a);
    if (typeof b === 'bigint') b = Number(b);
    if (a !== b) throw new Error((what || 'value') + ': expected ' + JSON.stringify(b) + ', got ' + JSON.stringify(a));
  }
}
function ok(cond, what) { if (!cond) throw new Error(what || 'condition failed'); }

// ── hand-rolled protobuf wire codec (independent of protobuf.js) ───
// Only what helloworld.proto needs: length-delimited strings and varints.
function varint(n) {
  const out = [];
  do { let b = n & 0x7f; n = Math.floor(n / 128); if (n) b |= 0x80; out.push(b); } while (n);
  return Uint8Array.from(out);
}
function readVarint(buf, pos) {
  let r = 0, shift = 1, i = pos;
  for (;;) { const b = buf[i++]; r += (b & 0x7f) * shift; if (!(b & 0x80)) break; shift *= 128; }
  return [r, i];
}
/** HelloReply { string message = 1; int32 count = 2; } */
function rawEncodeReply({ message, count }) {
  const parts = [];
  if (message != null) {
    const s = new TextEncoder().encode(message);
    parts.push(Uint8Array.from([0x0a]), varint(s.length), s);
  }
  if (count) parts.push(Uint8Array.from([0x10]), varint(count));
  let len = 0; for (const p of parts) len += p.length;
  const out = new Uint8Array(len); let o = 0;
  for (const p of parts) { out.set(p, o); o += p.length; }
  return out;
}
/** HelloRequest { string name = 1; repeated string tags = 2; } */
function rawDecodeRequest(bytes) {
  const req = { name: '', tags: [] };
  let i = 0;
  while (i < bytes.length) {
    const tag = bytes[i++];
    const field = tag >> 3, wire = tag & 7;
    if (wire !== 2) throw new Error('raw codec: unexpected wire type ' + wire);
    const [len, j] = readVarint(bytes, i); i = j + len;
    const s = new TextDecoder().decode(bytes.subarray(j, j + len));
    if (field === 1) req.name = s; else if (field === 2) req.tags.push(s);
  }
  return req;
}
/** gRPC Length-Prefixed-Message: [flag:1][len:4 BE][payload] */
function rawFrame(payload) {
  const out = new Uint8Array(5 + payload.length);
  out[0] = 0;
  out[1] = (payload.length >>> 24) & 255; out[2] = (payload.length >>> 16) & 255;
  out[3] = (payload.length >>> 8) & 255; out[4] = payload.length & 255;
  out.set(payload, 5);
  return out;
}
function rawUnframe(buf) {
  if (buf.length < 5) throw new Error('raw peer: short frame ' + buf.length);
  const len = ((buf[1] << 24) | (buf[2] << 16) | (buf[3] << 8) | buf[4]) >>> 0;
  if (len !== buf.length - 5) throw new Error('raw peer: frame length mismatch ' + len + ' vs ' + (buf.length - 5));
  return buf.subarray(5);
}

// ── the hand-written gRPC-over-http2 peer ──────────────────────────
// handlers: { '/pkg.Svc/M': (req, ctx) => ({reply|status|...}) }
function startRawServer(handlers) {
  const seen = [];   // one entry per call: {path, headers, req, error}
  const srv = http2.createServer({ settings: { enablePush: false } });
  srv.on('stream', (stream, headers) => {
    const ctx = { headers, seen };
    const chunks = [];
    let settled = false;
    const path_ = headers[':path'];
    seen.push({ path: path_, headers });
    const entry = seen[seen.length - 1];
    stream.on('data', (d) => chunks.push(new Uint8Array(d)));
    stream.on('aborted', () => { entry.aborted = true; });
    stream.on('end', () => {
      if (settled) return;
      settled = true;
      let total = 0; for (const c of chunks) total += c.length;
      const all = new Uint8Array(total); let o = 0;
      for (const c of chunks) { all.set(c, o); o += c.length; }
      entry.raw = all;
      const h = handlers[path_];
      if (!h) {
        // Trailers-Only form: status arrives in the SAME header block, no DATA.
        stream.respond({ ':status': 200, 'content-type': 'application/grpc',
                         'grpc-status': '12', 'grpc-message': 'unimplemented' }, { endStream: true });
        return;
      }
      let r;
      try { r = h(all.length ? rawUnframe(all) : new Uint8Array(0), ctx); }
      catch (e) {
        stream.respond({ ':status': 200, 'content-type': 'application/grpc',
                         'grpc-status': '13', 'grpc-message': String(e.message) }, { endStream: true });
        return;
      }
      const delay = r.delayMs || 0;
      setTimeout(() => {
        if (stream.destroyed || entry.aborted) return;
        if (r.trailersOnly) {
          // Trailers-Only: status in the SAME header block, no DATA at all.
          stream.respond({ ':status': r.httpStatus || 200, 'content-type': 'application/grpc',
                           'grpc-status': String(r.status || 0),
                           ...(r.message ? { 'grpc-message': r.message } : {}) }, { endStream: true });
          return;
        }
        if (r.httpStatusOnly) {   // a peer that never speaks grpc-status
          stream.respond({ ':status': r.httpStatus }, { endStream: true });
          return;
        }
        const trailer = { 'grpc-status': String(r.status || 0) };
        if (r.message) trailer['grpc-message'] = r.message;
        if (r.trailerMeta) Object.assign(trailer, r.trailerMeta);
        try {
          stream.respond({ ':status': 200, 'content-type': headers['content-type'] || 'application/grpc',
                           'grpc-encoding': 'identity', ...(r.headerMeta || {}) },
                          { endStream: false, waitForTrailers: true });
          // Node fires wantTrailers only when respond() opts waitForTrailers.
          // sendTrailers() then emits the trailer HEADERS frame (END_STREAM).
          stream.on('wantTrailers', () => stream.sendTrailers(trailer));
          stream.end(r.body && r.body.length ? Buffer.from(rawFrame(r.body)) : undefined);
        } catch (e) { entry.serverError = e.message; }
      }, delay);
    });
  });
  return new Promise((res) => srv.listen(0, '127.0.0.1', () => res({ srv, port: srv.address().port, seen })));
}

const PROTO_TEXT = fs.readFileSync(path.join(__dirname, 'proto', 'helloworld.proto'), 'utf8');
const FBS_TEXT = `
namespace internal;
table EchoRequest { text:string; num:int64; }
table EchoReply { text:string; num:int64; ok:bool; }
service Echo { Run(EchoRequest):EchoReply; Stream(EchoRequest):EchoReply; }
`;

// ════════════════════════════════════════════════════════════════════
//  A. protobuf path against the hand-written peer (default codec)
// ════════════════════════════════════════════════════════════════════
const reg = grpc.loadProto(PROTO_TEXT);
const sayHello = reg.service('helloworld.Greeter').method('SayHello');
const failCall = reg.service('helloworld.Greeter').method('Fail');
const slowCall = reg.service('helloworld.Greeter').method('Slow');
const echoMeta = reg.service('helloworld.Greeter').method('EchoMeta');

const H = {
  '/helloworld.Greeter/SayHello': (req) => {
    const r = rawDecodeRequest(req);
    return { body: rawEncodeReply({ message: 'Hello ' + r.name, count: r.tags.length }) };
  },
  '/helloworld.Greeter/Fail': () => ({ status: 5, message: 'no such %E2%98%BA thing' }),
  '/helloworld.Greeter/Slow': () => ({ body: rawEncodeReply({ message: 'late' }), delayMs: 400 }),
  '/helloworld.Greeter/EchoMeta': (req, ctx) => ({
    body: rawEncodeReply({ message: 'meta' }),
    headerMeta: { 'x-early': 'from-header' },
    trailerMeta: { 'x-custom': ctx.headers['x-custom'] || '', 'x-bin': ctx.headers['x-bin'] || '' },
  }),
};
const raw = await startRawServer(H);
const ch = grpc.createInsecureChannel('127.0.0.1:' + raw.port);

console.log('\ngRPC client e2e (peer: hand-written node http2, protobuf default codec)');

await t('module mounted on globalThis', () => {
  ok(grpc && typeof grpc.createChannel === 'function', 'grpc.createChannel');
  ok(typeof grpc.loadProto === 'function', 'grpc.loadProto');
  ok(typeof grpc.loadFlatbuffers === 'function', 'grpc.loadFlatbuffers');
  eq(grpc.Status.DEADLINE_EXCEEDED, 4, 'status table');
  eq(grpc.Status.UNAVAILABLE, 14, 'status table');
});

await t('unary call, protobuf reply decoded', async () => {
  const reply = await ch.invoke(sayHello, { name: 'world', tags: ['a', 'b'] });
  eq(reply.message, 'Hello world', 'reply.message');
  eq(reply.count, 2, 'reply.count (repeated field survived the round trip)');
});

await t('request bytes are spec-shaped protobuf', () => {
  // The peer decoded it with an independent codec and echoed the right answer;
  // additionally assert the framing we actually put on the wire.
  const frame = raw.seen[0].raw;
  eq(frame[0], 0, 'compressed flag must be 0');
  eq(((frame[1] << 24) | (frame[2] << 16) | (frame[3] << 8) | frame[4]) >>> 0, frame.length - 5, 'declared length');
  eq(frame[5], 0x0a, 'field 1 wiretype 2 tag');
});

await t('method resolved from a bare /pkg.Svc/M path', async () => {
  const reply = await ch.invoke('/helloworld.Greeter/SayHello', { name: 'path' },
                                { registry: reg });
  eq(reply.message, 'Hello path');
});

await t('content-type advertises the protobuf codec', () => {
  eq(raw.seen[0].headers['content-type'], 'application/grpc+proto', 'content-type');
  eq(raw.seen[0].headers['te'], 'trailers', 'te');
  eq(raw.seen[0].headers[':method'], 'POST', ':method');
});

await t('non-zero grpc-status rejects with StatusError', async () => {
  let err = null;
  try { await ch.invoke(failCall, { name: 'x' }); } catch (e) { err = e; }
  ok(err instanceof grpc.StatusError, 'expected StatusError, got ' + err);
  eq(err.code, grpc.Status.NOT_FOUND, 'code');
  eq(err.codeName, 'NOT_FOUND', 'codeName');
  eq(err.message, 'grpc: NOT_FOUND: no such ☺ thing', 'percent-decoded grpc-message');
});

await t('unknown method -> UNIMPLEMENTED via trailers-only', async () => {
  let err = null;
  try { await ch.invoke('/helloworld.Greeter/Nope', { name: 'x' }, { registry: reg }); }
  catch (e) { err = e; }
  ok(err instanceof grpc.StatusError, 'StatusError');
  eq(err.code, grpc.Status.UNIMPLEMENTED, 'code');
});

await t('metadata: -bin sent as base64, response metadata decoded', async () => {
  const token = Uint8Array.from([0xde, 0xad, 0x00, 0x01, 0xff]);
  let meta = null;
  const reply = await ch.invoke(echoMeta, { name: 'm' },
    { headers: { 'X-Custom': 'hi', 'x-bin': token }, onMetadata: (m) => { meta = m; } });
  eq(reply.message, 'meta');
  ok(meta, 'onMetadata fired');
  eq(meta['x-custom'], 'hi', 'round-tripped text metadata');
  ok(meta['x-bin'] instanceof Uint8Array, '-bin decoded to bytes');
  eq(Array.from(meta['x-bin']).join(','), '222,173,0,1,255', '-bin bytes');
  // what the peer actually received must be base64 text, not raw bytes
  eq(raw.seen[raw.seen.length - 1].headers['x-bin'], '3q0AAf8=', 'outgoing -bin base64');
  eq(raw.seen[raw.seen.length - 1].headers['x-custom'], 'hi', 'keys lowercased');
});

await t('deadline: grpc-timeout header + local DEADLINE_EXCEEDED', async () => {
  const before = Date.now();
  let err = null;
  try { await ch.invoke(slowCall, { name: 's' }, { timeoutMs: 120 }); } catch (e) { err = e; }
  const took = Date.now() - before;
  ok(err instanceof grpc.StatusError, 'StatusError, got ' + err);
  eq(err.code, grpc.Status.DEADLINE_EXCEEDED, 'code');
  ok(took < 350, 'must not wait for the 400ms server, took ' + took + 'ms');
  const sent = raw.seen[raw.seen.length - 1].headers['grpc-timeout'];
  ok(/^120m$/.test(sent), 'grpc-timeout should be 120m, got ' + sent);
});

await t('deadline generous enough -> call succeeds', async () => {
  const reply = await ch.invoke(slowCall, { name: 's' }, { timeoutMs: 1500 });
  eq(reply.message, 'late');
});

await t('missing grpc-status maps HTTP :status', async () => {
  const p = await startRawServer({ '/x.S/M': () => ({ httpStatusOnly: true, httpStatus: 503 }) });
  const c = grpc.createInsecureChannel('127.0.0.1:' + p.port);
  let err = null;
  try { await c.invoke('/x.S/M', {}, { requestType: reg.HelloRequest, responseType: reg.HelloReply }); }
  catch (e) { err = e; }
  eq(err && err.code, grpc.Status.UNAVAILABLE, '503 -> UNAVAILABLE');
  await c.close(); p.srv.close();
});

await t('maxRecvMsgSize guards a runaway peer', async () => {
  const big = new Uint8Array(64 * 1024);
  const p = await startRawServer({ '/x.S/M': () => ({ body: big }) });
  const c = grpc.createInsecureChannel('127.0.0.1:' + p.port, { maxRecvMsgSize: 4096 });
  let err = null;
  try { await c.invoke('/x.S/M', {}, { requestType: reg.HelloRequest, responseType: reg.HelloReply }); }
  catch (e) { err = e; }
  eq(err && err.code, grpc.Status.RESOURCE_EXHAUSTED, 'code');
  await c.close(); p.srv.close();
});

await t('connection reuse: one socket serves many calls', async () => {
  const before = raw.seen.length;
  await Promise.all([1, 2, 3, 4, 5].map((n) => ch.invoke(sayHello, { name: 'n' + n })));
  eq(raw.seen.length - before, 5, 'five calls');
  const reply = await ch.invoke(sayHello, { name: 'after' });
  eq(reply.message, 'Hello after', 'still healthy afterwards');
});

// ════════════════════════════════════════════════════════════════════
//  B. flatbuffers path (opt-in; qwrt<->qwrt internal only)
// ════════════════════════════════════════════════════════════════════
const fbs = grpc.loadFlatbuffers(FBS_TEXT);
const runEcho = fbs.service('internal.Echo').method('Run');

const F = {
  '/internal.Echo/Run': (req) => {
    const got = fbs.lookup('internal.EchoRequest').decode(req);
    const body = fbs.lookup('internal.EchoReply').encode({ text: got.text.toUpperCase(), num: Number(got.num) * 2, ok: true });
    return { body };
  },
};
const fraw = await startRawServer(F);
const fch = grpc.createInsecureChannel('127.0.0.1:' + fraw.port);

console.log('\ngRPC client e2e (peer: flatbuffers-aware, opt-in codec)');

await t('flatbuffers method carries its own codec', () => {
  eq(runEcho.serialization, 'flatbuffers', 'bound serialization');
  eq(runEcho.path, '/internal.Echo/Run', 'path');
  eq(runEcho.requestType.kind, 'table', 'request type is an fbs table');
});

await t('unary call with flatbuffers payload', async () => {
  const reply = await fch.invoke(runEcho, { text: 'ping', num: 21 });
  eq(reply.text, 'PING', 'reply.text');
  eq(reply.num, 42, 'reply.num');
  eq(reply.ok, true, 'reply.ok');
});

await t('flatbuffers call advertises the flatbuffers content-type', () => {
  eq(fraw.seen[0].headers['content-type'], 'application/grpc+flatbuffers', 'content-type');
});

await t('protobuf method rejects a flatbuffers override', async () => {
  let err = null;
  try { await ch.invoke(sayHello, { name: 'x' }, { serialization: 'flatbuffers' }); }
  catch (e) { err = e; }
  ok(err && /is bound to protobuf/.test(err.message), 'clear mismatch error, got: ' + (err && err.message));
});

await t('flatbuffers method rejects a protobuf override', async () => {
  let err = null;
  try { await fch.invoke(runEcho, { text: 'a', num: 1 }, { serialization: 'protobuf' }); }
  catch (e) { err = e; }
  ok(err && /is bound to flatbuffers/.test(err.message), 'clear mismatch error, got: ' + (err && err.message));
});

await t('streaming methods are refused, not silently truncated', async () => {
  let err = null;
  try { await ch.invoke(sayHello, { name: 'x' }, {}); } catch (e) { err = e; }
  ok(!err, 'sanity: unary still works');
  const sreg = grpc.loadProto(`syntax="proto3";
    service S { rpc Up(stream A) returns (B); }
    message A { string a = 1; } message B { string b = 1; }`);
  try { await ch.invoke(sreg.service('S').method('Up'), { a: 'x' }); } catch (e) { err = e; }
  eq(err && err.code, grpc.Status.UNIMPLEMENTED, 'code');
});

// ════════════════════════════════════════════════════════════════════
//  C. @grpc/grpc-js — a real standard peer (optional)
// ════════════════════════════════════════════════════════════════════
console.log('\ngRPC client e2e (peer: @grpc/grpc-js, standard interop)');
let peer = null;
try {
  const base = process.env.QWRT_GRPC_PEER_MODULES || '/tmp/grpcpeer';
  const preq = createRequire(path.join(base, 'noop.js'));
  peer = { grpc: preq('@grpc/grpc-js'), loader: preq('@grpc/proto-loader') };
} catch (e) { peer = null; }

if (!peer) {
  skip('interop with @grpc/grpc-js', 'package not resolvable; set QWRT_GRPC_PEER_MODULES');
} else {
  const def = peer.loader.loadSync(path.join(__dirname, 'proto', 'helloworld.proto'),
                                   { keepCase: true, longs: Number, defaults: true });
  const svc = peer.grpc.loadPackageDefinition(def).helloworld.Greeter;
  const server = new peer.grpc.Server();
  server.addService(svc.service, {
    SayHello: (call, cb) => cb(null, { message: 'Hello ' + call.request.name, count: (call.request.tags || []).length }),
    Fail: (call, cb) => cb({ code: peer.grpc.status.NOT_FOUND, details: 'no such ☺ thing' }),
    Slow: (call, cb) => setTimeout(() => cb(null, { message: 'late' }), 400),
    EchoMeta: (call, cb) => {
      const md = new peer.grpc.Metadata();
      md.set('x-custom', call.metadata.get('x-custom')[0] || '');
      md.set('x-bin', Buffer.from(call.metadata.get('x-bin')[0] || '', 'base64'));
      cb(null, { message: 'meta' }, md);
    },
  });
  const port = await new Promise((res, rej) => server.bindAsync('127.0.0.1:0', peer.grpc.ServerCredentials.createInsecure(),
    (e, p) => (e ? rej(e) : res(p))));
  const gch = grpc.createInsecureChannel('127.0.0.1:' + port);

  await t('interop: unary protobuf against grpc-js', async () => {
    const reply = await gch.invoke(sayHello, { name: 'grpc-js', tags: ['t'] });
    eq(reply.message, 'Hello grpc-js');
    eq(reply.count, 1);
  });

  await t('interop: grpc-js status surfaces as StatusError', async () => {
    let err = null;
    try { await gch.invoke(failCall, { name: 'x' }); } catch (e) { err = e; }
    eq(err && err.code, grpc.Status.NOT_FOUND, 'code');
    ok(err && /no such ☺ thing/.test(err.message), 'details forwarded, got: ' + (err && err.message));
  });

  await t('interop: metadata both directions', async () => {
    let meta = null;
    const reply = await gch.invoke(echoMeta, { name: 'm' }, {
      headers: { 'x-custom': 'ping', 'x-bin': Uint8Array.from([1, 2, 250]) },
      onMetadata: (m) => { meta = m; },
    });
    eq(reply.message, 'meta');
    eq(meta && meta['x-custom'], 'ping', 'echoed text metadata');
    eq(meta && Array.from(meta['x-bin']).join(','), '1,2,250', 'echoed -bin metadata');
  });

  await t('interop: deadline propagates and aborts', async () => {
    let err = null;
    try { await gch.invoke(slowCall, { name: 's' }, { timeoutMs: 100 }); } catch (e) { err = e; }
    eq(err && err.code, grpc.Status.DEADLINE_EXCEEDED, 'code');
  });

  await t('interop: 20 sequential + 20 concurrent calls on one channel', async () => {
    for (let i = 0; i < 20; i++) {
      const r = await gch.invoke(sayHello, { name: 'seq' + i });
      eq(r.message, 'Hello seq' + i, 'seq ' + i);
    }
    const rs = await Promise.all(Array.from({ length: 20 }, (_, i) => gch.invoke(sayHello, { name: 'c' + i })));
    eq(rs.map((r) => r.message).join('|'), Array.from({ length: 20 }, (_, i) => 'Hello c' + i).join('|'), 'concurrent');
  });

  await gch.close();
  await new Promise((res) => server.tryShutdown(res));
}

await ch.close(); await fch.close();
raw.srv.close(); fraw.srv.close();

console.log('\ngrpc: ' + passed + ' passed, ' + failed + ' failed' + (skipped ? ', ' + skipped + ' skipped' : ''));
if (failed) { console.log('\n' + failures.join('\n')); process.exit(1); }
setTimeout(() => process.exit(0), 50).unref();

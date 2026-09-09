/**
 * Phase 3 acceptance — REAL qwrt runtime:
 *   grpc-js TLS client (ALPN h2) → qwrt serve({tls, grpc})
 *   + HTTPS (HTTP/1.1 over TLS, same port) + plaintext h2c on a 2nd port.
 * Usage: node test/grpc_server_tls_e2e.mjs --qwrt-bin ./build/qwrt
 */
import net from 'node:net';
import https from 'node:https';
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { spawn } from 'node:child_process';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const require = createRequire(import.meta.url);
const argv = process.argv.slice(2);
let qwrtPath = null;
for (let i = 0; i < argv.length; i++) if (argv[i] === '--qwrt-bin' && i + 1 < argv.length) qwrtPath = argv[i + 1];
const QWRT = qwrtPath || process.env.QWRT_BIN || path.resolve(__dirname, '..', 'build', 'qwrt');
const CERT = '/tmp/qwrt-tls.crt';
const KEY = '/tmp/qwrt-tls.key';
const PROTO = `syntax = "proto3";
package helloworld;
service Greeter {
  rpc SayHello (HelloRequest) returns (HelloReply) {}
  rpc Fail (HelloRequest) returns (HelloReply) {}
}
message HelloRequest { string name = 1; repeated string tags = 2; }
message HelloReply { string message = 1; int32 count = 2; }`;

const PEER = process.env.QWRT_GRPC_PEER_MODULES || '/tmp/grpcpeer/node_modules';
const grpcjs = require(path.join(PEER, '@grpc', 'grpc-js'));
const loader = require(path.join(PEER, '@grpc', 'proto-loader'));

let failed = 0;
function ok(cond, what) { if (!cond) { failed++; console.log('  FAIL ' + what); } else console.log('  ok   ' + what); }
function freePort() {
  return new Promise((res) => { const s = net.createServer(); s.listen(0, '127.0.0.1', () => { const p = s.address().port; s.close(() => res(p)); }); });
}

function serverScript(port, tls) {
  return tls
    ? `const reg = grpc.loadProto(${JSON.stringify(PROTO)});
const srv = grpc.createServer();
srv.addService(reg, {
  SayHello: (call) => ({ message: 'Hello ' + call.request.name, count: (call.request.tags || []).length }),
  Fail: () => { throw new grpc.StatusError('tls-failed', grpc.Status.PERMISSION_DENIED); },
});
serve({ port: ${port}, tls: { cert: ${JSON.stringify(CERT)}, key: ${JSON.stringify(KEY)} }, grpc: srv },
  (req) => 'https1:' + req.url);
console.log('QWRT-READY ' + ${port});
setInterval(() => {}, 1000);`
    : `const reg = grpc.loadProto(${JSON.stringify(PROTO)});
const srv = grpc.createServer();
srv.addService(reg, { SayHello: (call) => ({ message: 'h2c ' + call.request.name, count: 0 }) });
serve({ port: ${port}, grpc: srv }, (req) => 'h1');
console.log('QWRT-READY ' + ${port});
setInterval(() => {}, 1000);`;
}

async function startServer(tls) {
  const port = await freePort();
  const p = spawn(QWRT, ['-e', serverScript(port, tls)], { stdio: ['ignore', 'pipe', 'pipe'] });
  let out = '';
  p.stdout.on('data', (d) => { out += d; });
  p.stderr.on('data', (d) => { out += d; });
  const readyPort = await new Promise((res, rej) => {
    const to = setTimeout(() => rej(new Error('not ready: ' + out.slice(-600))), 15000);
    p.stdout.on('data', (d) => { const m = /QWRT-READY (\d+)/.exec(out); if (m) { clearTimeout(to); res(parseInt(m[1], 10)); } });
    p.on('exit', (c) => { clearTimeout(to); rej(new Error('qwrt exited rc=' + c + ': ' + out.slice(-400))); });
  });
  await new Promise((res) => setTimeout(res, 150));
  return { p, port: readyPort };
}
async function waitPort(port, tries = 80) {
  for (let i = 0; i < tries; i++) {
    try {
      await new Promise((res, rej) => { const s = net.connect(port, '127.0.0.1'); s.once('connect', () => { s.destroy(); res(); }); s.once('error', rej); });
      return;
    } catch (e) { await new Promise((r) => setTimeout(r, 25)); }
  }
  throw new Error('port ' + port + ' not accepting');
}

const protoTmp = path.join(os.tmpdir(), 'qwrt_tls_e2e.proto');
fs.writeFileSync(protoTmp, PROTO);
const def = loader.loadSync(protoTmp, { keepCase: true, longs: Number, defaults: true });
const pkg = grpcjs.loadPackageDefinition(def);

console.log('real qwrt runtime TLS/h2c gRPC server (' + QWRT + ')');

// ── TLS: h2 (gRPC) + http/1.1 (HTTPS) on one serve() port ──
const tls = await startServer(true);
await waitPort(tls.port);
const greeter = new pkg.helloworld.Greeter('localhost:' + tls.port, grpcjs.credentials.createSsl(fs.readFileSync(CERT)));
try {
  const r = await new Promise((res, rej) => greeter.SayHello({ name: 'tls', tags: ['x'] }, (e, v) => (e ? rej(e) : res(v))));
  ok(r.message === 'Hello tls' && r.count === 1, 'TLS h2 unary via grpc-js (ALPN h2): ' + JSON.stringify(r));
} catch (e) { ok(false, 'TLS h2 unary: ' + (e && e.message)); }
try {
  const err = await new Promise((res) => greeter.Fail({ name: 'x' }, (e) => res(e)));
  ok(err && err.code === grpcjs.status.PERMISSION_DENIED && /tls-failed/.test(err.details || ''),
     'TLS error mapping: code=' + (err && err.code) + ' details=' + JSON.stringify(err && err.details));
} catch (e) { ok(false, 'TLS error mapping threw: ' + e.message); }
const httpsBody = await new Promise((res, rej) => {
  https.get({ host: 'localhost', port: tls.port, path: '/t', rejectUnauthorized: false }, (rsp) => {
    let b = ''; rsp.on('data', (d) => (b += d)); rsp.on('end', () => res(b));
  }).on('error', rej);
});
ok(httpsBody === 'https1:/t', 'HTTPS HTTP/1.1 same port (ALPN http/1.1): ' + JSON.stringify(httpsBody));
greeter.close();
try { tls.p.kill(); } catch (e) {}
await new Promise((res) => setTimeout(res, 100));

// ── plaintext h2c gRPC + plain HTTP/1.1 on a second port ──
const h2c = await startServer(false);
await waitPort(h2c.port);
const greeter2 = new pkg.helloworld.Greeter('127.0.0.1:' + h2c.port, grpcjs.credentials.createInsecure());
try {
  const r = await new Promise((res, rej) => greeter2.SayHello({ name: 'c' }, (e, v) => (e ? rej(e) : res(v))));
  ok(r.message === 'h2c c', 'plaintext h2c gRPC unary (real runtime): ' + JSON.stringify(r));
} catch (e) { ok(false, 'h2c unary: ' + (e && e.message)); }
const h1Body = await new Promise((res, rej) => {
  http.get('http://127.0.0.1:' + h2c.port + '/q', (rsp) => {
    let b = ''; rsp.on('data', (d) => (b += d)); rsp.on('end', () => res(b));
  }).on('error', rej);
});
ok(h1Body === 'h1', 'plain HTTP/1.1 on the h2c gRPC port: ' + JSON.stringify(h1Body));
greeter2.close();
try { h2c.p.kill(); } catch (e) {}
await new Promise((res) => setTimeout(res, 100));

console.log(failed ? 'TLS/h2c e2e: ' + failed + ' failed' : 'TLS/h2c e2e: all checks passed');
process.exit(failed ? 1 : 0);

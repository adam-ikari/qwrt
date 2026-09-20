#!/usr/bin/env node
/**
 * qwrt WebSocket server echo benchmark.
 *
 * Starts a qwrt CLI hosting serve({ port, ws: { '/echo': echo-handler } }),
 * then drives it with N concurrent client connections sending K messages
 * each, measuring round-trip messages/second.
 *
 * Uses the global WebSocket client available on node 22+ / bun / (qwrt's
 * own WebSocket implementation when the bench is driven from node, since the
 * client connects to qwrt's server port+1).
 *
 * Single-connection burst measures the server's per-connection echo
 * throughput; multi-connection burst measures scheduling across connections.
 *
 * Output: one JSON line on stdout, last line.
 *
 * Usage:
 *   node test/bench_ws_server.mjs --qwrt-bin ./build/qwrt \
 *       [--messages 1000] [--connections 1|8] [--duration 5]
 */
const { spawn } = await import('node:child_process');
const net = await import('node:net');

function parseArgs() {
  const a = { qwrtBin: './build_cli/qwrt', messages: 1000, connections: 1 };
  const argv = process.argv.slice(2);
  for (let i = 0; i < argv.length; i++) {
    if (argv[i] === '--qwrt-bin') a.qwrtBin = argv[++i];
    else if (argv[i] === '--messages') a.messages = parseInt(argv[++i], 10);
    else if (argv[i] === '--connections') a.connections = parseInt(argv[++i], 10);
      }
  return a;
}

function freePort() {
  return new Promise((resolve, reject) => {
    const s = net.createServer();
    s.unref();
    s.on('error', reject);
    s.listen(0, '127.0.0.1', () => {
      const p = s.address().port;
      s.close(() => resolve(p));
    });
  });
}

const QWRT_WS_SERVER = `
function main() {
  serve({ port: %PORT%, ws: { '/echo': function (ws) { ws.onmessage = function (e) { ws.send('echo:' + e.data); }; } } }, function () { return 'not-ws'; });
}
main();
`;

async function startQwrtServer(args, port) {
  const js = QWRT_WS_SERVER.replace('%PORT%', String(port));
  const proc = spawn(args.qwrtBin, ['-e', js], { stdio: ['ignore', 'pipe', 'pipe'] });
  // wait for the HTTP (and thus ws) accept
  const httpPort = port;
  const deadline = Date.now() + 30000;
  while (Date.now() < deadline) {
    if (proc.exitCode !== null) {
      const err = await new Promise((r) => {
        let buf = '';
        proc.stderr.on('data', (d) => (buf += d));
        proc.on('close', () => r(buf));
        setTimeout(() => r(buf), 500);
      });
      throw new Error('qwrt exited early: ' + err.slice(-400));
    }
    await new Promise((r) => setTimeout(r, 80));
    try {
      const ok = await new Promise((resolve, reject) => {
        const c = net.createConnection({ host: '127.0.0.1', port: httpPort }, () => { c.end(); resolve(true); });
        c.on('error', reject);
      });
      if (ok) return proc;
    } catch (e) { /* retry */ }
  }
  proc.kill('SIGKILL');
  throw new Error('qwrt ws server never came up on port ' + port);
}

function connectAndBurst(wsPort, messages) {
  return new Promise((resolve, reject) => {
    const url = 'ws://127.0.0.1:' + wsPort + '/echo';
    let sent = 0, recv = 0;
    const t0 = performance.now();
    let ws;
    try {
      ws = new WebSocket(url);
    } catch (e) {
      return reject(new Error('WebSocket ctor: ' + e.message));
    }
    ws.onopen = () => {
      const sendNext = () => {
        if (sent >= messages) return;
        ws.send('m' + sent);
        sent++;
      };
      // burst-send up to a modest window, echo drains one-for-one
      const WINDOW = 64;
      for (let i = 0; i < WINDOW && sent < messages; i++) sendNext();
      ws.onmessage = () => {
        recv++;
        if (sent < messages) sendNext();
        else if (recv >= messages) {
          const secs = (performance.now() - t0) / 1000;
          ws.close();
          resolve(messages / secs);
        }
      };
    };
    ws.onerror = (e) => reject(new Error('ws error: ' + (e.message || 'closed')));
    setTimeout(() => reject(new Error('ws timeout recv=' + recv + '/' + messages)), 60000);
  });
}

async function main() {
  const args = parseArgs();
  const httpPort = await freePort();
  const wsPort = httpPort; // serve() hosts ws on the same port
  const proc = await startQwrtServer(args, httpPort);
  try {
    // single-connection throughput
    let single = null;
    if (args.connections === 1) {
      single = await connectAndBurst(wsPort, args.messages);
    }
    // multi-connection concurrent burst
    const conns = Math.max(1, args.connections);
    const perConn = Math.floor(args.messages / conns);
    const t0 = performance.now();
    const results = [];
    for (let i = 0; i < conns; i++) results.push(connectAndBurst(wsPort, perConn));
    const perConnRates = await Promise.all(results);
    const totalMsgs = perConn * conns;
    const totalSecs = (performance.now() - t0) / 1000;
    const aggregate = totalMsgs / totalSecs;
    const out = {
      ws_bench: true,
      connections: conns,
      messages_per_conn: perConn,
      single_conn_msg_per_s: single ? +single.toFixed(1) : null,
      multi_conn_aggregate_msg_per_s: +aggregate.toFixed(1),
      per_conn_msg_per_s: perConnRates.map((r) => +r.toFixed(1)),
    };
    console.log(JSON.stringify(out));
  } finally {
    proc.kill('SIGTERM');
    try { proc.kill('SIGKILL', { afterWait: 1000 }); } catch (e) {}
  }
}

main().catch((e) => { console.error('BENCH-FAIL: ' + (e && (e.stack || e.message))); process.exit(1); });

#!/usr/bin/env python3
"""amoib TLS/HTTPS server performance benchmark (wrk-based).

Starts a TLS server on localhost, drives it with wrk, prints one JSON line.
Supports two backends for cross-checking:
  amoib  : real amoib CLI hosting serve({port, tls}) — pure-JS HTTP over mbedTLS
  node  : node --tls-min-v1.0 https server (reference point)

Scenarios (same shape as bench_httpserver.py):
  tiny  : 8-byte response keep-alive    — per-request overhead incl. TLS record
  small : 1 KB response                 — typical JSON payload over TLS
  hs    : fresh connection per request  — full TLS handshake cost

Usage:
  python3 test/bench_tls_server.py --backend amoib --amoib-bin ./build/amoib \
      [--duration 5]
  python3 test/bench_tls_server.py --backend node [--duration 5]

Exit code 0 unless infrastructure fails; thresholds are the CI job's business.
"""
import argparse
import json
import os
import socket
import subprocess
import sys
import time

CERT = '/tmp/amoib-bench-tls.crt'
KEY = '/tmp/amoib-bench-tls.key'

AM_TLS_SERVER_JS = """
function main() {
  serve({ port: %PORT%, tls: { cert: %CERT%, key: %KEY% } }, function (req) {
    if (req.url === '/small') {
      return new Response('x'.repeat(1024));
    }
    return 'ok';
  });
}
main();
"""

NODE_TLS_SERVER_JS = """
const https = require('node:https');
const fs = require('node:fs');
const opts = { key: fs.readFileSync(%KEY%), cert: fs.readFileSync(%CERT%) };
https.createServer(opts, (req, res) => {
  if (req.url === '/small') res.end('x'.repeat(1024));
  else res.end('ok');
}).listen(%PORT%, '127.0.0.1');
"""


def free_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(('127.0.0.1', 0))
    port = s.getsockname()[1]
    s.close()
    return port


def ensure_cert():
    if os.path.exists(CERT) and os.path.exists(KEY):
        return
    r = subprocess.run(
        ['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes',
         '-keyout', KEY, '-out', CERT, '-days', '2',
         '-subj', '/CN=bench'],
        capture_output=True, timeout=30)
    if r.returncode != 0:
        raise RuntimeError('openssl cert generation failed: ' +
                           r.stderr.decode()[:200])


def start_server(backend, am_bin):
    port = free_port()
    ensure_cert()
    if backend == 'amoib':
        js = AM_TLS_SERVER_JS.replace('%PORT%', str(port))
        js = js.replace('%CERT%', json.dumps(CERT)).replace('%KEY%', json.dumps(KEY))
        path = '/tmp/amoib-bench-tls-server.js'
        with open(path, 'w') as f:
            f.write(js)
        proc = subprocess.Popen([am_bin, path],
                                stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL)
    elif backend == 'node':
        js = NODE_TLS_SERVER_JS.replace('%PORT%', str(port))
        js = js.replace('%CERT%', json.dumps(CERT)).replace('%KEY%', json.dumps(KEY))
        path = '/tmp/amoib-bench-tls-server-node.js'
        with open(path, 'w') as f:
            f.write(js)
        proc = subprocess.Popen(['node', path],
                                stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL)
    else:
        raise ValueError('unknown backend: ' + backend)

    # wait for TLS accept
    deadline = time.time() + 30
    while time.time() < deadline:
        if proc.poll() is not None:
            raise RuntimeError('server exited early (rc=%s)' % proc.returncode)
        try:
            s = socket.create_connection(('127.0.0.1', port), timeout=1)
            s.close()
            return proc, port
        except OSError:
            time.sleep(0.1)
    proc.kill()
    raise RuntimeError('server never became ready on port %d' % port)


def run_wrk(port, path, duration, new_conn):
    url = 'https://127.0.0.1:%d%s' % (port, path)
    cmd = ['wrk', '-t2', '-c8', '-d%ds' % duration, '--timeout', '5s']
    if new_conn:
        # handshake scenario: close after each request
        cmd += ['-H', 'Connection: close']
    cmd.append(url)
    env = dict(os.environ, LD_PRELOAD='')  # wrk needs no env tweaks; https via -H? no
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=duration + 30)
    if r.returncode != 0:
        raise RuntimeError('wrk failed: ' + r.stderr[:300])
    # wrk parse
    out = r.stdout
    reqs = None
    lat = None
    errs = 0
    for line in out.splitlines():
        if 'Requests/sec' in line:
            reqs = float(line.split(':')[1].strip())
        m = line.split()
        if line.startswith('Latency') and len(m) >= 4 and lat is None:
            lat = m[1]
    # errors: non-2xx + socket errors
    for line in out.splitlines():
        if 'Non-2xx' in line:
            errs += int(line.split()[-1])
        if 'Socket errors' in line:
            errs += 1
    lat_ms = None
    if lat:
        if lat.endswith('ms'):
            lat_ms = float(lat[:-2])
        elif lat.endswith('us'):
            lat_ms = float(lat[:-2]) / 1000
        elif lat.endswith('s'):
            lat_ms = float(lat[:-1]) * 1000
    return {'rps': reqs, 'latency_ms': lat_ms, 'errors': errs}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--backend', choices=['amoib', 'node'], default='amoib')
    ap.add_argument('--amoib-bin', default='./build/amoib')
    ap.add_argument('--duration', type=int, default=5)
    args = ap.parse_args()

    proc, port = start_server(args.backend, args.am_bin)
    try:
        out = {}
        out['tiny'] = run_wrk(port, '/tiny', args.duration, new_conn=False)
        out['small'] = run_wrk(port, '/small', args.duration, new_conn=False)
        out['hs'] = run_wrk(port, '/tiny', args.duration, new_conn=True)
        print(json.dumps({'tls_bench': True, 'backend': args.backend, **out}))
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
    return 0


if __name__ == '__main__':
    sys.exit(main())

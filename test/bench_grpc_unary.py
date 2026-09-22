#!/usr/bin/env python3
"""qzjs gRPC unary performance benchmark (qzjs client -> qzjs server).

Starts one qzjs process hosting serve({port, grpc}) with the Echo unary
method, then a second qzjs process running a client loop invoking Echo N
times over the same persistent channel. Prints one JSON line.

The whole stack is qzjs-native (pure-JS HTTP/2 + gRPC client over the C
transport primitives); node/bun have no equivalent builtin gRPC stack, so
this is qzjs-only — cross-runtime comparison lives in bench_js_api.mjs.

Usage:
  python3 test/bench_grpc_unary.py --qzjs-bin ./build/qzjs [--calls 2000]
"""
import argparse
import json
import os
import socket
import subprocess
import sys
import time

GRPC_SERVER_JS = """
const PROTO = 'syntax = "proto3"; package bench; ' +
  'service GrpcBench { rpc Echo (EchoReq) returns (EchoRes); } ' +
  'message EchoReq { string text = 1; } message EchoRes { string text = 1; }';
var reg = grpc.loadProto(PROTO);
var server = grpc.createServer();
server.addService(reg, { Echo: function (call) { return { text: call.request.text }; } });
var srv = serve({ port: %PORT%, grpc: server }, function () { return 'not-grpc'; });

"""

GRPC_CLIENT_JS = """
const PROTO = 'syntax = "proto3"; package bench; ' +
  'service GrpcBench { rpc Echo (EchoReq) returns (EchoRes); } ' +
  'message EchoReq { string text = 1; } message EchoRes { string text = 1; }';
var reg = grpc.loadProto(PROTO);
var Echo = reg.service('bench.GrpcBench').method('Echo');
var ch = grpc.createInsecureChannel('127.0.0.1:%PORT%');
var N = %N%;
async function main() {
  var t0 = performance.now();
  var ok = 0;
  for (var i = 0; i < N; i++) {
    var r = await ch.invoke(Echo, { text: 'bench' });
    if (r && r.text === 'bench') ok++;
  }
  var ms = performance.now() - t0;
  var out = { grpc_unary: true, calls: N, ok: ok,
              unary_per_s: N / (ms / 1000), latency_us: (ms * 1000) / N };
  console.log(JSON.stringify(out));
}
main().catch(function (e) { console.error('CLIENT-FAIL: ' + e.message); process.exit(1); });
"""


def free_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(('127.0.0.1', 0))
    port = s.getsockname()[1]
    s.close()
    return port


def start_server(qz_bin, port):
    js = GRPC_SERVER_JS.replace('%PORT%', str(port))
    proc = subprocess.Popen([qz_bin, '-e', js],
                            stdout=subprocess.DEVNULL,
                            stderr=subprocess.PIPE)
    deadline = time.time() + 30
    while time.time() < deadline:
        if proc.poll() is not None:
            raise RuntimeError('grpc server exited early (rc=%s)' % proc.returncode)
        try:
            s = socket.create_connection(('127.0.0.1', port), timeout=1)
            s.close()
            return proc
        except OSError:
            time.sleep(0.1)
    proc.kill()
    raise RuntimeError('grpc server never became ready on port %d' % port)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--qzjs-bin', required=True)
    ap.add_argument('--calls', type=int, default=2000)
    args = ap.parse_args()

    port = free_port()
    server = start_server(args.qz_bin, port)
    try:
        js = GRPC_CLIENT_JS.replace('%PORT%', str(port)).replace('%N%', str(args.calls))
        r = subprocess.run([args.qz_bin, '-e', js], capture_output=True,
                           text=True, timeout=180)
        if r.returncode != 0:
            print('FAIL: client rc=%d stderr=%s' % (r.returncode, r.stderr[-300:]),
                  file=sys.stderr)
            return 1
        line = [ln for ln in r.stdout.strip().splitlines() if ln.strip()][-1]
        data = json.loads(line)
        data['latency_us'] = round(data.get('latency_us', 0), 1)
        data['unary_per_s'] = round(data.get('unary_per_s', 0), 1)
        print(json.dumps(data))
    finally:
        server.terminate()
        try:
            server.wait(timeout=5)
        except subprocess.TimeoutExpired:
            server.kill()
    return 0


if __name__ == '__main__':
    sys.exit(main())
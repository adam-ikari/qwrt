#!/usr/bin/env python3
"""qwrt HTTP server performance benchmark (wrk-based).

Starts the real qwrt CLI hosting serve() on localhost, then drives it with
wrk across several scenarios and prints a machine-readable summary.

Scenarios:
  tiny   : 8-byte response ("ok"), keep-alive      — measures per-request overhead
  small  : 1 KB response                            — typical JSON payload
  medium : 16 KB response                           — mid-size payload
  post   : 8-byte body echo via POST               — request-body path

Usage:
  python3 test/bench_httpserver.py --qwrt-bin ./build-ws/qwrt [--duration 5]

Exit code 0 always unless infrastructure fails; thresholds are reported but
not enforced here (the CI job decides what to do with the numbers).
"""
import argparse
import json
import os
import re
import socket
import subprocess
import sys
import time


def free_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


def wait_port(port, timeout=15):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            s = socket.create_connection(("127.0.0.1", port), timeout=0.5)
            s.close()
            return True
        except OSError:
            time.sleep(0.1)
    return False


def run_wrk(port, path, duration, threads=2, connections=64,
            method="GET", body=None):
    cmd = ["wrk", "-t%d" % threads, "-c%d" % connections,
           "-d%ds" % duration, "--latency",
           "http://127.0.0.1:%d%s" % (port, path)]
    if method != "GET":
        script = "wrk.method = %r\n" % method
        if body is not None:
            script += "wrk.body = %r\n" % body
        # write temp lua script
        import tempfile
        f = tempfile.NamedTemporaryFile(mode="w", suffix=".lua", delete=False)
        f.write(script)
        f.close()
        cmd += ["-s", f.name]
        try:
            proc = subprocess.run(cmd, capture_output=True, text=True, timeout=duration + 30)
        finally:
            os.unlink(f.name)
    else:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=duration + 30)
    out = proc.stdout + proc.stderr
    return parse_wrk(out)


def parse_wrk(out):
    rps = latency_avg = None
    m = re.search(r"Requests/sec:\s+([\d.]+)", out)
    if m:
        rps = float(m.group(1))
    m = re.search(r"Latency\s+([\d.]+)(us|ms|s)\s+([\d.]+)(us|ms|s)", out)
    if m:
        val, unit = float(m.group(1)), m.group(2)
        latency_avg = val * {"us": 0.001, "ms": 1, "s": 1000}[unit]
    errors = 0
    m = re.search(r"Non-2xx or 3xx responses:\s+(\d+)", out)
    if m:
        errors += int(m.group(1))
    m = re.search(r"Socket errors: connect (\d+), read (\d+), write (\d+), timeout (\d+)", out)
    if m:
        errors += sum(int(g) for g in m.groups())
    return {"rps": rps, "latency_ms": latency_avg, "errors": errors}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--qwrt-bin", required=True)
    ap.add_argument("--duration", type=int, default=5)
    args = ap.parse_args()

    port = free_port()
    js = (
        "const srv = serve({port: %d}, (req) => {\n"
        "  if (req.url === '/tiny') return 'ok';\n"
        "  if (req.url === '/small') return 'x'.repeat(1024);\n"
        "  if (req.url === '/medium') return 'y'.repeat(16384);\n"
        "  if (req.url === '/post' && req.method === 'POST')\n"
        "    return 'echo:' + (req.body || '').length;\n"
        "  return 'notfound';\n"
        "});" % port
    )
    proc = subprocess.Popen([args.qwrt_bin, "-e", js],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    try:
        if not wait_port(port):
            print("FAIL: qwrt did not listen on %d" % port, file=sys.stderr)
            try:
                proc.kill()
            except Exception:
                pass
            return 1

        scenarios = [
            {"name": "tiny",   "path": "/tiny",   "method": "GET"},
            {"name": "small",  "path": "/small",  "method": "GET"},
            {"name": "medium", "path": "/medium", "method": "GET"},
            {"name": "post",   "path": "/post",   "method": "POST", "body": "abcdefgh"},
        ]
        results = {}
        for sc in scenarios:
            r = run_wrk(port, sc["path"], args.duration,
                        method=sc["method"], body=sc.get("body"))
            results[sc["name"]] = r
            print("%-7s rps=%-10.1f avg_lat=%-8.2fms err=%d"
                  % (sc["name"], r["rps"], r["latency_ms"] or -1, r["errors"]))
        print(json.dumps(results))
        return 0
    finally:
        try:
            proc.kill()
        except Exception:
            pass


if __name__ == "__main__":
    sys.exit(main())

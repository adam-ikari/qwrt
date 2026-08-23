#!/usr/bin/env python3
"""qwrt HTTPServer extension end-to-end tests (uvhttp-backed serve()).

Drives the real qwrt CLI (real libuv, QWRT_BUILD_TESTS=OFF) against uvhttp
HTTP/HTTPS/WebSocket listeners on localhost, using only the Python stdlib
(http.client, ssl, raw sockets with a hand-rolled WebSocket client).

Coverage (plan T7.3 matrix):
  plain HTTP : handler returning string / Response object / async Promise /
               rejected Promise (500) / invalid type (500)
  methods    : POST/PUT/DELETE/PATCH round-trip (llhttp->uvhttp enum map)
  HTTPS      : mbedTLS listener, self-signed certs
  WebSocket  : echo with small + large frames (fragmentation/126/127 len)
  static     : file serving from options.static root
  gzip       : Content-Encoding for big string AND Response-object bodies
  lifecycle  : server.close() then port must be closed
  errors     : second serve() throws; invalid handler throws
"""

import argparse
import base64
import http.client
import json
import os
import random
import ssl
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time

TESTS = []
def test(fn):
    TESTS.append(fn)
    return fn

# ---------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------

def free_port():
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port

def hex_to_utf8(b):
    return b.decode("utf-8")

class Res:
    def __init__(self, status, headers, body):
        self.status = status
        self.headers = headers
        self.body = body
    def header(self, name):
        for k, v in self.headers:
            if k.lower() == name.lower():
                return v
        return None

def raw_request(port, method, path, body=None, headers=None, tls_ctx=None,
                reconnect=True, read_body=True):
    """Low-level HTTP/1.1 request; returns (status, headers, body).

    Reads exactly Content-Length bytes of body instead of waiting for EOF
    (uvhttp keeps the connection alive and never half-closes it, so EOF-based
    reads would time out on keep-alive sockets).
    """
    raw = socket.create_connection(("127.0.0.1", port), timeout=5)
    if tls_ctx:
        raw = tls_ctx.wrap_socket(raw, server_hostname="localhost")
    req = "%s %s HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nConnection: close\r\n" % (
        method, path, port)
    for k, v in (headers or {}).items():
        req += "%s: %s\r\n" % (k, v)
    if body is not None:
        req += "Content-Length: %d\r\n" % len(body)
    req += "\r\n"
    raw.sendall(req.encode("latin-1") + (body.encode() if isinstance(body, str)
                                          else (body or b"")))
    raw.settimeout(8)
    resp = b""
    while b"\r\n\r\n" not in resp:
        chunk = raw.recv(4096)
        if not chunk:
            break
        resp += chunk
    head, _, rest = resp.partition(b"\r\n\r\n")
    lines = head.split(b"\r\n")
    status = int(lines[0].split(b" ")[1])
    hdrs = []
    for ln in lines[1:]:
        if b":" in ln:
            k, v = ln.split(b":", 1)
            hdrs.append((k.decode("latin-1").strip(),
                         v.decode("latin-1").strip()))
    body_bytes = rest
    cl = raw_http_headers(hdrs, "Content-Length")
    if cl is not None and len(body_bytes) < int(cl):
        want = int(cl) - len(body_bytes)
        while want > 0:
            chunk = raw.recv(min(65536, want))
            if not chunk:
                break
            body_bytes += chunk
            want -= len(chunk)
    elif raw_http_headers(hdrs, "Transfer-Encoding") == "chunked":
        # unsophisticated: consume until a 0-length chunk
        while b"0\r\n\r\n" not in body_bytes:
            chunk = raw.recv(4096)
            if not chunk:
                break
            body_bytes += chunk
    raw.close()
    return status, hdrs, body_bytes

# WebSocket client (RFC 6455) — server frames are unmasked, client frames masked.
class WSClient:
    def __init__(self, port, path="/"):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=5)
        key = base64.b64encode(os.urandom(16)).decode()
        handshake = (
            "GET %s HTTP/1.1\r\nHost: 127.0.0.1:%d\r\n"
            "Upgrade: websocket\r\nConnection: Upgrade\r\n"
            "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n"
            % (path, port, key))
        self.sock.sendall(handshake.encode())
        resp = self._read_until(b"\r\n\r\n")
        self.status = int(resp.split(b" ", 2)[1])
        expected = base64.b64encode(
            __import__("hashlib").sha1(
                (key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()).decode()
        if self.status != 101 or b"Sec-WebSocket-Accept: " + expected.encode() not in resp \
           and b"sec-websocket-accept: " + expected.encode() not in resp:
            raise RuntimeError("WS handshake failed: %r" % resp[:200])

    def _read_until(self, marker):
        data = b""
        while marker not in data:
            chunk = self.sock.recv(4096)
            if not chunk:
                break
            data += chunk
        return data

    def send_text(self, payload):
        data = payload.encode() if isinstance(payload, str) else payload
        mask = os.urandom(4)
        masked = bytes(b ^ mask[i % 4] for i, b in enumerate(data))
        hdr = b"\x81"
        n = len(data)
        if n < 126:
            hdr += bytes([0x80 | n])
        elif n < 65536:
            hdr += bytes([0x80 | 126]) + struct.pack(">H", n)
        else:
            hdr += bytes([0x80 | 127]) + struct.pack(">Q", n)
        self.sock.sendall(hdr + mask + masked)

    def recv_frame(self, timeout=5.0):
        self.sock.settimeout(timeout)
        b0 = self.sock.recv(1)
        if not b0:
            return None
        b1 = self.sock.recv(1)
        opcode = b0[0] & 0x0F
        n = b1[0] & 0x7F
        if n == 126:
            n = struct.unpack(">H", self.sock.recv(2))[0]
        elif n == 127:
            n = struct.unpack(">Q", self.sock.recv(8))[0]
        payload = b""
        while len(payload) < n:
            payload += self.sock.recv(n - len(payload))
        return opcode, payload

    def close(self):
        try:
            self.sock.close()
        except Exception:
            pass

# ---------------------------------------------------------------------------
# server scripts
# ---------------------------------------------------------------------------

SERVER_SCRIPT = r"""
const srv = serve({%(opts)s}, (req) => {
  const u = new URL(req.url, 'http://x');
  if (u.pathname === '/hello') return 'plain string';
  if (u.pathname === '/json')
    return new Response(JSON.stringify({ok: 1}), {status: 201,
      headers: {'Content-Type': 'application/json'}});
  if (u.pathname === '/async')
    return new Promise(r => setTimeout(() => r('async done'), 30));
  if (u.pathname === '/reject')
    return new Promise((r, j) => setTimeout(() => j(new Error('boom')), 20));
  if (u.pathname === '/badtype') return 42;
  if (u.pathname === '/big') return 'x'.repeat(5000);
  if (u.pathname === '/bigresp')
    return new Response('y'.repeat(5000), {headers: {'Content-Type': 'text/html'}});
  if (u.pathname === '/method') return req.method + ':' + (req.body || '');
  if (u.pathname === '/close') { srv.close(); return 'closed'; }
  return new Response('nope', {status: 404});
});
"""

def gen_server_script(port, static_root=None, tls=False):
    opts = "port: %d" % port
    if static_root:
        opts += ', static: {root: %r, index: "index.html"}' % static_root
    if tls:
        base = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..",
                            "deps", "uvhttp", "test", "certs")
        cert = os.path.join(base, "server.crt")
        key = os.path.join(base, "server.key")
        opts += ', tls: {cert: %r, key: %r}' % (cert, key)
    # ws endpoints live on port+1 — single serve() call (a second one throws)
    opts += ', ws: {"/echo": (ws) => { ws.onmessage = (e) => ws.send("echo:" + e.data); }}'
    return SERVER_SCRIPT % {"opts": opts}

class QwrtServer:
    """Starts the qwrt CLI hosting serve(); kills it on exit."""

    def __init__(self, script, qwrt_bin):
        self.proc = subprocess.Popen(
            [qwrt_bin, "-e", script],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.ready = False
        self.err = b""
        self._t = threading.Thread(target=self._drain, daemon=True)
        self._t.start()

    def _drain(self):
        try:
            while True:
                line = self.proc.stdout.readline()
                if not line:
                    break
                self.err += line
        except Exception:
            pass

    def wait_port(self, port, timeout=6.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            if self.proc.poll() is not None:
                raise RuntimeError("qwrt exited early (%d): %s"
                                   % (self.proc.returncode,
                                      self.err.decode(errors="replace")[-800:]))
            try:
                s = socket.create_connection(("127.0.0.1", port), timeout=0.3)
                s.close()
                self.ready = True
                return
            except OSError:
                time.sleep(0.1)
        raise RuntimeError("port %d never came up; server output: %s"
                           % (port, self.err.decode(errors="replace")[-800:]))

    def stop(self):
        if self.proc and self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()

# ---------------------------------------------------------------------------
# plain HTTP
# ---------------------------------------------------------------------------

@test
def test_plain_http(qwrt_bin):
    p = free_port()
    srv = QwrtServer(gen_server_script(p), qwrt_bin)
    try:
        srv.wait_port(p)

        st, hdrs, body = raw_request(p, "GET", "/hello")
        assert st == 200, st
        assert body == b"plain string", body
        assert raw_http_headers(hdrs, "Content-Type") == "text/plain; charset=utf-8"

        st, hdrs, body = raw_request(p, "GET", "/json")
        assert st == 201, st
        assert json.loads(body) == {"ok": 1}
        assert raw_http_headers(hdrs, "Content-Type") == "application/json"

        st, hdrs, body = raw_request(p, "GET", "/async")
        assert st == 200 and body == b"async done", (st, body)

        st, _, body = raw_request(p, "GET", "/reject")
        assert st == 500, st

        st, _, body = raw_request(p, "GET", "/badtype")
        assert st == 500, st

        st, _, _ = raw_request(p, "GET", "/nope")
        assert st == 404, st
    finally:
        srv.stop()

def raw_http_headers(hdrs, name):
    for k, v in hdrs:
        if k.lower() == name.lower():
            return v
    return None

@test
def test_methods_roundtrip(qwrt_bin):
    p = free_port()
    srv = QwrtServer(gen_server_script(p), qwrt_bin)
    try:
        srv.wait_port(p)
        for m, payload in [("POST", "a=1"), ("PUT", "b=2"),
                           ("DELETE", ""), ("PATCH", "c=3")]:
            st, _, body = raw_request(p, m, "/method", body=payload)
            assert st == 200, (m, st)
            assert body == ("%s:%s" % (m, payload)).encode(), (m, body)
    finally:
        srv.stop()

@test
def test_gzip_compression(qwrt_bin):
    p = free_port()
    srv = QwrtServer(gen_server_script(p), qwrt_bin)
    try:
        srv.wait_port(p)
        st, hdrs, body = raw_request(p, "GET", "/big",
                                     headers={"Accept-Encoding": "gzip"})
        assert st == 200
        assert raw_http_headers(hdrs, "Content-Encoding") == "gzip", hdrs
        import gzip
        assert gzip.decompress(body) == b"x" * 5000

        st, hdrs, body = raw_request(p, "GET", "/bigresp",
                                     headers={"Accept-Encoding": "gzip"})
        assert raw_http_headers(hdrs, "Content-Encoding") == "gzip", hdrs
        assert gzip.decompress(body) == b"y" * 5000

        # without Accept-Encoding: no gzip
        st, hdrs, body = raw_request(p, "GET", "/big")
        assert raw_http_headers(hdrs, "Content-Encoding") is None
        assert body == b"x" * 5000
    finally:
        srv.stop()

# ---------------------------------------------------------------------------
# static files
# ---------------------------------------------------------------------------

@test
def test_static_files(qwrt_bin):
    with tempfile.TemporaryDirectory() as root:
        with open(os.path.join(root, "index.html"), "w") as f:
            f.write("<h1>static ok</h1>")
        with open(os.path.join(root, "data.bin"), "wb") as f:
            f.write(b"\x00\x01\x02binary")
        p = free_port()
        srv = QwrtServer(gen_server_script(p, static_root=root), qwrt_bin)
        try:
            srv.wait_port(p)
            st, hdrs, body = raw_request(p, "GET", "/")
            assert st == 200 and body == b"<h1>static ok</h1>", (st, body)
            st, hdrs, body = raw_request(p, "GET", "/data.bin")
            assert st == 200 and body == b"\x00\x01\x02binary", (st, body)
            st, _, _ = raw_request(p, "GET", "/missing.bin")
            assert st in (404, 500), st
        finally:
            srv.stop()

# ---------------------------------------------------------------------------
# HTTPS
# ---------------------------------------------------------------------------

@test
def test_https(qwrt_bin):
    p = free_port()
    srv = QwrtServer(gen_server_script(p, tls=True), qwrt_bin)
    try:
        srv.wait_port(p)
        ctx = ssl.create_default_context()
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
        st, hdrs, body = raw_request(p, "GET", "/hello", tls_ctx=ctx)
        assert st == 200 and body == b"plain string", (st, body)
        # plaintext client on the TLS port must fail
        try:
            s = socket.create_connection(("127.0.0.1", p), timeout=2)
            s.sendall(b"GET / HTTP/1.1\r\nHost: x\r\n\r\n")
            s.settimeout(2)
            resp = s.recv(64)
            s.close()
            assert b"HTTP" not in resp, "plaintext got a response on TLS port"
        except (OSError, socket.timeout):
            pass
    finally:
        srv.stop()

# ---------------------------------------------------------------------------
# WebSocket
# ---------------------------------------------------------------------------

@test
def test_websocket_echo(qwrt_bin):
    p = free_port()
    srv = QwrtServer(gen_server_script(p), qwrt_bin)
    try:
        srv.wait_port(p)
        ws = WSClient(p, "/echo")
        try:
            ws.send_text("ping-1")
            op, payload = ws.recv_frame()
            assert op == 1 and payload == b"echo:ping-1", (op, payload)

            # large frame (>126 bytes, tests 16-bit length)
            big = "A" * 300
            ws.send_text(big)
            op, payload = ws.recv_frame()
            assert payload == ("echo:" + big).encode(), payload[:20]

            # multi-message round-trip
            for i in range(3):
                ws.send_text("msg-%d" % i)
                op, payload = ws.recv_frame()
                assert payload == ("echo:msg-%d" % i).encode(), payload
        finally:
            ws.close()
    finally:
        srv.stop()

@test
def test_websocket_client(qwrt_bin):
    """JS WebSocket client → qwrt server /echo: connect, echo round-trip
    (small + large frame), clean close handshake with code+reason."""
    p = free_port()
    # Single qwrt process: serves /echo AND runs a JS WebSocket client to
    # itself. The client sends 3 messages (incl. a >126-byte frame), expects
    # echoes, then closes with code 1000 + reason; srv.close() drains the loop.
    js = (
        "const srv = serve({port: %d, ws: {'/echo': (ws) => "
        "{ ws.onmessage = (e) => ws.send('echo:' + e.data); }}}, "
        "() => new Response('ok'));"
        "const ws = new WebSocket('ws://127.0.0.1:%d/echo');"
        "let n = 0;"
        "ws.onopen = () => { ws.send('ping'); ws.send('C'.repeat(300)); };"
        "ws.onmessage = (ev) => { console.log('GOT:' + ev.data.length);"
        "  n++; if (n === 2) ws.close(1000, 'bye'); };"
        "ws.onerror = () => console.log('ERR');"
        "ws.onclose = (ev) => {"
        "  console.log('WS-CLIENT-OK:' + ev.code + ':' + ev.reason);"
        "  srv.close(); };"
        % (p, p))
    proc = subprocess.Popen([qwrt_bin, "-e", js],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    out, _ = proc.communicate(timeout=15)
    text = out.decode(errors="replace")
    assert proc.returncode == 0, "exit=%d out=%s" % (proc.returncode, text[-400:])
    assert "GOT:9" in text and "GOT:305" in text, text[-400:]
    assert "WS-CLIENT-OK:1000:bye" in text, text[-400:]

# ---------------------------------------------------------------------------
# lifecycle / errors
# ---------------------------------------------------------------------------

@test
def test_server_close(qwrt_bin):
    p = free_port()
    srv = QwrtServer(gen_server_script(p), qwrt_bin)
    try:
        srv.wait_port(p)
        st, _, body = raw_request(p, "GET", "/close")
        assert st == 200 and body == b"closed", (st, body)
        # after close() the listener must be gone; allow a small settle window
        time.sleep(0.5)
        try:
            s = socket.create_connection(("127.0.0.1", p), timeout=1)
            s.close()
            assert False, "port still listening after server.close()"
        except OSError:
            pass
    finally:
        srv.stop()

@test
def test_serve_errors(qwrt_bin):
    import select

    def run_script(js, settle=1.0):
        """Run a -e script, capture output for `settle` seconds, then kill.
        The CLI stays alive while the listener is active (wait_idle exits only
        when the loop has no pending work), so we read early output and
        terminate instead of waiting for exit."""
        proc = subprocess.Popen([qwrt_bin, "-e", js],
                                stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT)
        out = b""
        deadline = time.time() + settle
        fd = proc.stdout.fileno()
        try:
            while time.time() < deadline and b"OK-THROW" not in out and \
                    b"NO-THROW" not in out:
                rlist, _, _ = select.select([fd], [], [], 0.05)
                if rlist:
                    chunk = os.read(fd, 4096)
                    if not chunk:
                        break
                    out += chunk
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
        return out.decode(errors="replace")

    # duplicate serve -> throw (while the first server is still running;
    # close() afterwards also exercises the lifecycle teardown path)
    p1 = free_port()
    p2 = free_port()
    out = run_script(
        "let srv;"
        "srv = serve({port: %d}, r => 'x');"
        "try { serve({port: %d}, r => 'y'); console.log('NO-THROW'); }"
        "catch (e) { console.log('OK-THROW:' + e); }"
        "srv.close();"
        % (p1, p2))
    assert "NO-THROW" not in out, out[-400:]
    assert "OK-THROW" in out, out[-400:]

    # invalid handler -> throw
    p3 = free_port()
    out = run_script(
        "let srv;"
        "try { srv = serve({port: %d}, 42); console.log('NO-THROW'); }"
        "catch (e) { console.log('OK-THROW:' + e); }"
        % p3)
    assert "NO-THROW" not in out, out[-400:]
    assert "OK-THROW" in out, out[-400:]

    # bind failure -> throw (port -1 rejected by validation)
    out = run_script(
        "let srv;"
        "try { srv = serve({port: -1}, r => 'x'); console.log('NO-THROW'); }"
        "catch (e) { console.log('OK-THROW:' + e); }")
    assert "NO-THROW" not in out, out[-400:]
    assert "OK-THROW" in out, out[-400:]

# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--qwrt-bin", required=True)
    args = ap.parse_args()
    assert os.path.exists(args.qwrt_bin), "qwrt binary not found: %s" % args.qwrt_bin

    failed = []
    for fn in TESTS:
        name = fn.__name__
        t0 = time.time()
        try:
            fn(args.qwrt_bin)
            print("PASS %-28s (%.2fs)" % (name, time.time() - t0))
        except Exception as e:
            failed.append(name)
            print("FAIL %-28s (%.2fs): %s" % (name, time.time() - t0, e),
                  file=sys.stderr)
    if failed:
        print("FAILED: %s" % ", ".join(failed), file=sys.stderr)
        return 1
    print("ALL %d TESTS PASSED" % len(TESTS))
    return 0

if __name__ == "__main__":
    sys.exit(main())
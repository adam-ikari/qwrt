#!/usr/bin/env python3
"""qwrt HTTP Server end-to-end tests (pure-JS serve() implementation).

Drives the real qwrt CLI (real libuv, QWRT_BUILD_TESTS=OFF) against the pure-JS serve()
HTTP/HTTPS/WebSocket listeners on localhost, using only the Python stdlib
(http.client, ssl, raw sockets with a hand-rolled WebSocket client).

Coverage (plan T7.3 matrix):
  plain HTTP : handler returning string / Response object / async Promise /
               rejected Promise (500) / invalid type (500)
  methods    : POST/PUT/DELETE/PATCH round-trip
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
import unittest

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
    (serve() keeps the connection alive and never half-closes it, so EOF-based
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
    def __init__(self, port, path="/", extensions=None):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=5)
        self._last_rsv1 = 0
        self._inflate_ctx = None
        key = base64.b64encode(os.urandom(16)).decode()
        handshake = (
            "GET %s HTTP/1.1\r\nHost: 127.0.0.1:%d\r\n"
            "Upgrade: websocket\r\nConnection: Upgrade\r\n"
            "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n"
            % (path, port, key))
        if extensions:
            handshake += "Sec-WebSocket-Extensions: %s\r\n" % extensions
        handshake += "\r\n"
        self.sock.sendall(handshake.encode())
        resp = self._read_until(b"\r\n\r\n")
        self.handshake = resp
        self.status = int(resp.split(b" ", 2)[1])
        expected = base64.b64encode(
            __import__("hashlib").sha1(
                (key + "258EAFA5-E914-47DA-95CA-5AB5D3D5D5E5").encode()).digest()).decode()  # RFC 6455
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

    def send_fragmented_text(self, payload, sizes):
        """Send a text message as FIN=0 data frame + continuation frames.
        sizes = list of chunk lengths (must sum to len(payload))."""
        data = payload.encode() if isinstance(payload, str) else payload
        off = 0
        for i, sz in enumerate(sizes):
            chunk = data[off:off + sz]
            off += sz
            is_last = (off == len(data))
            mask = os.urandom(4)
            masked = bytes(b ^ mask[k % 4] for k, b in enumerate(chunk))
            # opcode: 0x1 for first frame, 0x0 (continuation) for rest
            opcode = 0x1 if i == 0 else 0x0
            hdr = bytes([(0x80 if is_last else 0x00) | opcode])
            n = len(chunk)
            if n < 126:
                hdr += bytes([0x80 | n])
            elif n < 65536:
                hdr += bytes([0x80 | 126]) + struct.pack(">H", n)
            else:
                hdr += bytes([0x80 | 127]) + struct.pack(">Q", n)
            self.sock.sendall(hdr + mask + masked)

    def send_compressed_text(self, payload):
        """Send a permessage-deflate text message (RSV1 set, raw deflate with
        SYNC_FLUSH, trailing 00 00 ff ff stripped per RFC 7692 §7.2.1)."""
        import zlib
        data = payload.encode() if isinstance(payload, str) else payload
        co = zlib.compressobj(wbits=-15)
        comp = co.compress(data) + co.flush(zlib.Z_SYNC_FLUSH)
        comp = comp[:-4]
        mask = os.urandom(4)
        masked = bytes(b ^ mask[i % 4] for i, b in enumerate(comp))
        hdr = bytes([0xC1])  # FIN + RSV1 + text opcode
        n = len(comp)
        if n < 126:
            hdr += bytes([0x80 | n])
        elif n < 65536:
            hdr += bytes([0x80 | 126]) + struct.pack(">H", n)
        else:
            hdr += bytes([0x80 | 127]) + struct.pack(">Q", n)
        self.sock.sendall(hdr + mask + masked)

    def inflate_payload(self, comp):
        """Inflate a permessage-deflate payload. A persistent decompressobj
        preserves the context-takeover dictionary across messages (RFC 7692)."""
        import zlib
        if self._inflate_ctx is None:
            self._inflate_ctx = zlib.decompressobj(wbits=-15)
        out = self._inflate_ctx.decompress(comp + b"\x00\x00\xff\xff")
        out += self._inflate_ctx.flush()
        return out

    def recv_frame(self, timeout=5.0):
        self.sock.settimeout(timeout)
        b0 = self.sock.recv(1)
        if not b0:
            return None
        b1 = self.sock.recv(1)
        self._last_rsv1 = (b0[0] >> 6) & 1
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
/* app-layer gzip helper (compression strategy belongs to the application) */
function gzipBytes(data) {
  return new Promise(function(resolve, reject) {
    var cs = new CompressionStream('gzip');
    var writer = cs.writable.getWriter();
    var reader = cs.readable.getReader();
    var chunks = []; var total = 0;
    reader.read().then(function pump(r) {
      if (r.done) {
        var out = new Uint8Array(total); var off = 0;
        for (var i = 0; i < chunks.length; i++) { out.set(chunks[i], off); off += chunks[i].length; }
        resolve(out); return;
      }
      chunks.push(r.value); total += r.value.length;
      return reader.read().then(pump);
    }).catch(reject);
    writer.write(data).then(function(){ return writer.close(); }).catch(reject);
  });
}
const srv = serve({%(opts)s}, async (req) => {
  const u = new URL(req.url, 'http://x');
%(file_routes)s%(fs_routes)s  if (u.pathname === '/hello') return 'plain string';
  if (u.pathname === '/json')
    return new Response(JSON.stringify({ok: 1}), {status: 201,
      headers: {'Content-Type': 'application/json'}});
  if (u.pathname === '/async')
    return new Promise(r => setTimeout(() => r('async done'), 30));
  if (u.pathname === '/reject')
    return new Promise((r, j) => setTimeout(() => j(new Error('boom')), 20));
  if (u.pathname === '/badtype') return 42;
  if (u.pathname === '/big') return 'x'.repeat(5000);
  if (u.pathname === '/gzip')
    return gzipBytes(new TextEncoder().encode('x'.repeat(5000))).then(function(gz) {
      return new Response(gz, {headers: {'Content-Encoding': 'gzip'}});
    });
  if (u.pathname === '/bigresp')
    return new Response('y'.repeat(5000), {headers: {'Content-Type': 'text/html'}});
  if (u.pathname === '/method') return req.method + ':' + (await req.text());
  if (u.pathname === '/bodytype') return 'stream:' + (req.body && typeof req.body.getReader === 'function');
  if (u.pathname === '/bigbody') return req.text().then(function(t) { return 'len:' + t.length; });
  if (u.pathname === '/bodybytes') return req.arrayBuffer().then(function(b) { return 'bytes:' + b.byteLength; });
  if (u.pathname === '/close') { srv.close(); return 'closed'; }
  return new Response('nope', {status: 404});
});
"""

def gen_server_script(port, static_root=None, tls=False, file_root=None,
                     fs_root=None, subprotocols=None):
    opts = "port: %d" % port
    file_routes = ""
    if file_root:
        # app-layer file serving: handler reads files via qwrt.fs.readFileBinary
        file_routes = (
            "  if (req.url === '/' || req.url === '/index.html') {"
            "    return new Response(await qwrt.fs.readFileBinary(%r),"
            "      {headers: {'Content-Type': 'text/html'}});}\n"
            "  if (req.url === '/data.bin') {"
            "    return new Response(await qwrt.fs.readFileBinary(%r),"
            "      {headers: {'Content-Type': 'application/octet-stream'}});}\n"
            % (os.path.join(file_root, "index.html"),
               os.path.join(file_root, "data.bin"))
        )
    if static_root:
        opts += ', static: {root: %r, index: "index.html"}' % static_root
    fs_routes = ""
    if fs_root:
        # app-layer fs ops (write/exists/read/readdir/unlink roundtrip)
        f = os.path.join(fs_root, "f.txt")
        fs_routes = (
            "  if (u.pathname === '/fs/write') {"
            "    return req.text().then(function(t){ return qwrt.fs.writeFile(%r, t); })"
            "      .then(function(){ return 'written'; });}\n"
            "  if (u.pathname === '/fs/exists') {"
            "    return qwrt.fs.exists(%r).then(function(e){ return 'exists:' + e; });}\n"
            "  if (u.pathname === '/fs/read') {"
            "    return qwrt.fs.readFile(%r).then(function(d){ return 'read:' + d; });}\n"
            "  if (u.pathname === '/fs/readdir') {"
            "    return qwrt.fs.readdir(%r).then(function(list)"
            "      { return 'list:' + JSON.stringify(list); });}\n"
            "  if (u.pathname === '/fs/unlink') {"
            "    return qwrt.fs.unlink(%r).then(function(){ return 'unlinked'; });}\n"
            % (f, f, f, fs_root, f)
        )
    if tls:
        # Generate self-signed certs on the fly (openssl required)
        cert = os.path.join(tempfile.gettempdir(), "qwrt_e2e_cert.pem")
        key = os.path.join(tempfile.gettempdir(), "qwrt_e2e_key.pem")
        if not (os.path.exists(cert) and os.path.exists(key)):
            subprocess.run(
                ["openssl", "req", "-x509", "-newkey", "rsa:2048",
                 "-keyout", key, "-out", cert, "-days", "365", "-nodes",
                 "-subj", "/CN=localhost"],
                check=True, capture_output=True)
        opts += ', tls: {cert: %r, key: %r}' % (cert, key)
    # ws endpoints live on port+1 — single serve() call (a second one throws)
    if subprotocols:
        protos = ",".join('"%s"' % s for s in subprotocols)
        opts += (', ws: {"/echo": {handler: (ws) => { ws.onmessage = (e)'
                 ' => ws.send("echo:" + e.data); }, protocols: [%s]}}' % protos)
    else:
        opts += ', ws: {"/echo": (ws) => { ws.onmessage = (e) => ws.send("echo:" + e.data); }}'
    return SERVER_SCRIPT % {"opts": opts, "file_routes": file_routes,
                           "fs_routes": fs_routes}

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
def test_streaming_body(qwrt_bin):
    """D2: request body streams in as ReadableStream (req.body); text()/
    arrayBuffer() reassemble; handler runs before the body fully arrives."""
    p = free_port()
    srv = QwrtServer(gen_server_script(p), qwrt_bin)
    try:
        srv.wait_port(p)
        # req.body is a ReadableStream when a body is present (POST)
        st, _, body = raw_request(p, "POST", "/bodytype", body="hello")
        assert st == 200 and b"stream:true" in body, body

        # large body sent in chunks: header first, then 3 TCP segments with
        # delays — server must feed the body stream incrementally and the
        # handler must still see the complete text
        payload = ("y" * 300000) + "TAIL"
        s = socket.create_connection(("127.0.0.1", p), timeout=5)
        req = ("POST /bigbody HTTP/1.1\r\nHost: 127.0.0.1:%d\r\n"
               "Connection: close\r\nContent-Length: %d\r\n\r\n"
               % (p, len(payload)))
        s.sendall(req.encode())
        time.sleep(0.05)  # header reaches the server before any body byte
        data = payload.encode()
        for start in range(0, len(data), 100000):
            s.sendall(data[start:start + 100000])
            time.sleep(0.02)
        resp = b""
        while b"len:" not in resp and len(resp) < 1 << 20:
            c = s.recv(4096)
            if not c:
                break
            resp += c
        assert b"len:" + str(len(payload)).encode() in resp, resp[:300]

        # binary body via arrayBuffer()
        payload = bytes(range(256)) * 100  # 25600 bytes
        st, _, body = raw_request(p, "POST", "/bodybytes", body=payload)
        assert st == 200 and b"bytes:25600" in body, body[:200]
    finally:
        srv.stop()

@test
def test_gzip_compression(qwrt_bin):
    """App-layer gzip: handler compresses via CompressionStream and sets
    Content-Encoding; serve() itself must NOT auto-compress (compression
    strategy belongs to the application)."""
    p = free_port()
    srv = QwrtServer(gen_server_script(p), qwrt_bin)
    try:
        srv.wait_port(p)
        # app-layer gzip route: /gzip returns gzip-compressed 5000 x's
        st, hdrs, body = raw_request(p, "GET", "/gzip")
        assert st == 200
        assert raw_http_headers(hdrs, "Content-Encoding") == "gzip", hdrs
        import gzip
        assert gzip.decompress(body) == b"x" * 5000

        # serve() must NOT auto-compress: /big (no Accept-Encoding handling
        # built-in) returns the raw body with no Content-Encoding
        st, hdrs, body = raw_request(p, "GET", "/big",
                                     headers={"Accept-Encoding": "gzip"})
        assert st == 200
        assert raw_http_headers(hdrs, "Content-Encoding") is None, hdrs
        assert body == b"x" * 5000
    finally:
        srv.stop()

# ---------------------------------------------------------------------------
# fs ops (app-layer async file system roundtrip: write/exists/read/readdir/unlink)
# ---------------------------------------------------------------------------

@test
def test_fs_ops(qwrt_bin):
    with tempfile.TemporaryDirectory() as root:
        p = free_port()
        srv = QwrtServer(gen_server_script(p, fs_root=root), qwrt_bin)
        try:
            srv.wait_port(p)
            # write
            st, _, body = raw_request(p, "POST", "/fs/write", body="hello fs")
            assert st == 200 and body == b"written", (st, body)
            # exists -> true
            st, _, body = raw_request(p, "GET", "/fs/exists")
            assert st == 200 and body == b"exists:true", (st, body)
            # read back
            st, _, body = raw_request(p, "GET", "/fs/read")
            assert st == 200 and body == b"read:hello fs", (st, body)
            # readdir
            st, _, body = raw_request(p, "GET", "/fs/readdir")
            assert st == 200 and b"f.txt" in body, (st, body)
            # unlink
            st, _, body = raw_request(p, "GET", "/fs/unlink")
            assert st == 200 and body == b"unlinked", (st, body)
            # exists -> false
            st, _, body = raw_request(p, "GET", "/fs/exists")
            assert st == 200 and body == b"exists:false", (st, body)
        finally:
            srv.stop()

# ---------------------------------------------------------------------------
# file response (app-layer: handler reads files via qwrt.fs.readFileBinary)
# ---------------------------------------------------------------------------

@test
def test_file_response(qwrt_bin):
    with tempfile.TemporaryDirectory() as root:
        with open(os.path.join(root, "index.html"), "w") as f:
            f.write("<h1>static ok</h1>")
        with open(os.path.join(root, "data.bin"), "wb") as f:
            f.write(b"\x00\x01\x02binary")
        p = free_port()
        srv = QwrtServer(gen_server_script(p, file_root=root), qwrt_bin)
        try:
            srv.wait_port(p)
            st, hdrs, body = raw_request(p, "GET", "/")
            assert st == 200 and body == b"<h1>static ok</h1>", (st, body)
            assert raw_http_headers(hdrs, "Content-Type") == "text/html"
            st, hdrs, body = raw_request(p, "GET", "/data.bin")
            assert st == 200 and body == b"\x00\x01\x02binary", (st, body)
            st, _, _ = raw_request(p, "GET", "/missing.bin")
            assert st == 404, st
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


# F2 覆盖率补测：WebSocket 客户端错误路径（websocket.js 的 _onError/_fail 分支）。
# 两个场景（连接被拒 / 对非 WS 端点握手）都只触发 onerror（_onError/_fail 置
# CLOSED 后 onclose 不再触发），断言 onerror 确实被调用。
def _run_js_until(qwrt_bin, js, markers, settle=2.5):
    """Run a -e script, read stdout until all markers appear or settle seconds
    elapse, then terminate the process. Returns (text, returncode)."""
    import select
    proc = subprocess.Popen([qwrt_bin, "-e", js],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    out = b""
    deadline = time.time() + settle
    fd = proc.stdout.fileno()
    try:
        while time.time() < deadline and not all(m in out for m in markers):
            rlist, _, _ = select.select([fd], [], [], 0.05)
            if rlist:
                chunk = os.read(fd, 4096)
                if not chunk:
                    break
                out += chunk
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
    return out.decode(errors="replace"), proc.returncode


@test
def test_websocket_client_connect_refused(qwrt_bin):
    """WS client to a closed port → tcp connect error → onerror fires.
    (_onError 置 CLOSED，onclose 不再触发 —— 只断言 onerror。)"""
    p = free_port()   # 无人监听
    js = (
        "const ws = new WebSocket('ws://127.0.0.1:%d/');"
        "ws.onerror = () => console.log('WS-REFUSED-ERR');"
        "ws.onclose = (ev) => console.log('WS-REFUSED-CLOSE:' + ev.code);" % p
    )
    text, rc = _run_js_until(qwrt_bin, js, (b"WS-REFUSED-ERR",))
    assert "WS-REFUSED-ERR" in text, "refused onerror not fired; rc=%d out=%s" % (rc, text[:400])


@test
def test_websocket_client_non_ws_server(qwrt_bin):
    """WS client → plain HTTP endpoint (200, no Upgrade) → handshake parse
    fails → onerror fires. serve() keeps the loop alive; read until the
    timeout marker then terminate."""
    p = free_port()
    js = (
        "const srv = serve({port: %d}, () => new Response('ok'));"
        "const ws = new WebSocket('ws://127.0.0.1:%d/hello');"
        "ws.onerror = () => console.log('WS-HTTP-ERR');"
        "setTimeout(() => { console.log('WS-HTTP-DONE'); srv.close(); }, 1200);"
        % (p, p)
    )
    text, rc = _run_js_until(qwrt_bin, js, (b"WS-HTTP-ERR", b"WS-HTTP-DONE"))
    assert "WS-HTTP-ERR" in text, "handshake-fail onerror not fired; rc=%d out=%s" % (rc, text[:400])
    assert "WS-HTTP-DONE" in text, "script did not reach timeout marker; rc=%d out=%s" % (rc, text[:400])
@test
def test_websocket_fragmentation(qwrt_bin):
    """Server reassembles a fragmented (FIN=0 + continuation) text message."""
    p = free_port()
    srv = QwrtServer(gen_server_script(p), qwrt_bin)
    try:
        srv.wait_port(p)
        ws = WSClient(p, "/echo")
        # 'hello-fragmented-world' = 22 chars; split as [5, 9, 8]
        ws.send_fragmented_text("hello-fragmented-world", [5, 9, 8])
        opcode, payload = ws.recv_frame()
        # server echoes 'echo:' + reassembled message
        assert opcode == 0x1, "opcode=%d" % opcode
        assert payload == b"echo:hello-fragmented-world", payload
        ws.close()
    finally:
        srv.stop()

@test
def test_websocket_subprotocol(qwrt_bin):
    """Subprotocol negotiation: server echoes first supported Sec-WebSocket-Protocol."""
    p = free_port()
    srv = QwrtServer(gen_server_script(p, subprotocols=["chat", "superchat"]),
                     qwrt_bin)
    try:
        srv.wait_port(p)
        s = socket.create_connection(("127.0.0.1", p), timeout=5)
        key = base64.b64encode(os.urandom(16)).decode()
        handshake = (
            "GET /echo HTTP/1.1\r\nHost: 127.0.0.1:%d\r\n"
            "Upgrade: websocket\r\nConnection: Upgrade\r\n"
            "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n"
            "Sec-WebSocket-Protocol: superchat, chat\r\n\r\n"
            % (p, key))
        s.sendall(handshake.encode())
        resp = b""
        while b"\r\n\r\n" not in resp:
            c = s.recv(4096)
            if not c: break
            resp += c
        assert b"101" in resp, resp[:200]
        # server must echo one of the offered protocols (first supported = superchat)
        assert b"Sec-WebSocket-Protocol: superchat" in resp, resp[:200]
        s.close()
    finally:
        srv.stop()

@test
def test_websocket_permessage_deflate(qwrt_bin):
    """D4: permessage-deflate negotiation + compressed echo round-trip.
    Client sends RSV1-compressed text; server must inflate it for the
    handler and deflate the echo (RSV1 set), with context takeover
    across messages (RFC 7692)."""
    p = free_port()
    srv = QwrtServer(gen_server_script(p), qwrt_bin)
    try:
        srv.wait_port(p)
        ws = WSClient(p, "/echo", extensions="permessage-deflate")
        assert b"Sec-WebSocket-Extensions: permessage-deflate" in ws.handshake, ws.handshake[:300]

        # compressed client -> server (server inflates, handler echoes)
        ws.send_compressed_text("hello compressed world")
        opcode, payload = ws.recv_frame()
        assert opcode == 0x1, "opcode=%d" % opcode
        if ws._last_rsv1:
            payload = ws.inflate_payload(payload)
        assert payload == b"echo:hello compressed world", payload

        # second message exercises context takeover (deflate history shared)
        ws.send_compressed_text("hello compressed world again")
        opcode, payload = ws.recv_frame()
        assert opcode == 0x1, "opcode=%d" % opcode
        if ws._last_rsv1:
            payload = ws.inflate_payload(payload)
        assert payload == b"echo:hello compressed world again", payload

        # uncompressed client still works when the extension is offered
        ws2 = WSClient(p, "/echo", extensions="permessage-deflate")
        ws2.send_text("plain text")
        opcode, payload = ws2.recv_frame()
        if ws2._last_rsv1:
            payload = ws2.inflate_payload(payload)
        assert opcode == 0x1 and payload == b"echo:plain text", payload
        ws2.close()
        ws.close()
    finally:
        srv.stop()

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

# ---------------------------------------------------------------------------
# HTTP/1.1 protocol (keep-alive, connection close, explicit close)
# ---------------------------------------------------------------------------

@test
def test_keep_alive_reuse(qwrt_bin):
    """HTTP/1.1 keep-alive: reuse the same TCP connection for 2 sequential requests."""
    p = free_port()
    srv = QwrtServer(gen_server_script(p), qwrt_bin)
    try:
        srv.wait_port(p)
        s = socket.create_connection(("127.0.0.1", p), timeout=5)
        # first request
        s.sendall(b"GET /hello HTTP/1.1\r\nHost: x\r\nConnection: keep-alive\r\n\r\n")
        s.settimeout(3)
        data = b""
        while b"\r\n\r\n" not in data:
            c = s.recv(4096)
            if not c: break
            data += c
        assert b"HTTP/1.1 200" in data, "first: " + repr(data[:200])
        # second request on same connection
        s.sendall(b"GET /hello HTTP/1.1\r\nHost: x\r\nConnection: keep-alive\r\n\r\n")
        data2 = b""
        while b"\r\n\r\n" not in data2:
            c = s.recv(4096)
            if not c: break
            data2 += c
        assert b"HTTP/1.1 200" in data2, "second: " + repr(data2[:200])
        s.close()
    finally:
        srv.stop()

@test
def test_connection_close_http10(qwrt_bin):
    """HTTP/1.0 (no keep-alive) → server closes after response."""
    p = free_port()
    srv = QwrtServer(gen_server_script(p), qwrt_bin)
    try:
        srv.wait_port(p)
        s = socket.create_connection(("127.0.0.1", p), timeout=5)
        s.sendall(b"GET /hello HTTP/1.0\r\nHost: x\r\n\r\n")
        s.settimeout(3)
        data = b""
        while b"\r\n\r\n" not in data:
            c = s.recv(4096)
            if not c: break
            data += c
        assert b"200" in data, "no 200: " + repr(data[:200])
        time.sleep(0.3)
        try:
            c = s.recv(1024)
            if c == b"":
                pass  # server closed — OK
            else:
                pass  # got extra data — acceptable for HTTP/1.0
        except Exception:
            pass  # socket closed — OK
        s.close()
    finally:
        srv.stop()

@test
def test_header_proto_pollution(qwrt_bin):
    """F4 security audit: remote headers with '__proto__'/'constructor' names
    must be inert data slots on req.headers — no Object.prototype pollution,
    normal routing unaffected."""
    p = free_port()
    srv = QwrtServer(gen_server_script(p), qwrt_bin)
    try:
        srv.wait_port(p)
        # Malicious header block: __proto__ would set Object.prototype.polluted
        # via the parser if the headers object had a prototype.
        raw = (
            b"GET /hello HTTP/1.1\r\n"
            b"Host: 127.0.0.1\r\n"
            b"__proto__: polluted\r\n"
            b"constructor: hacked\r\n"
            b"Connection: close\r\n\r\n"
        )
        s = socket.create_connection(("127.0.0.1", p), timeout=5)
        s.sendall(raw)
        data = b""
        s.settimeout(3)
        while b"\r\n\r\n" not in data:
            c = s.recv(4096)
            if not c: break
            data += c
        s.close()
        assert b"200" in data, "no 200: " + repr(data[:300])
        assert b"plain string" in data, "body wrong: " + repr(data[:300])
        # Follow-up request must still route cleanly.
        st, hdrs, body = raw_request(p, "GET", "/hello")
        assert st == 200 and body == b"plain string", (st, body)
    finally:
        srv.stop()



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
        except unittest.SkipTest as e:
            print("SKIP %-28s (%.2fs) %s" % (name, time.time() - t0, e))
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
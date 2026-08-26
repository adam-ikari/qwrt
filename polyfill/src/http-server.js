/**
 * qwrt polyfill: HTTP server (serve API) — pure JS implementation
 *
 * Uses pal.tcpListen/tcpWrite/tcpClose for raw TCP transport.
 * HTTP request parsing, routing, WebSocket server, and response
 * serialization are implemented entirely in JS.
 *
 * Design: C provides TCP transport (bind/listen/accept/read/write/close);
 * JS provides all protocol semantics. Mirrors the fetch/WS-client pattern.
 */

export function setupHttpServer(pal) {
  if (typeof pal.tcpListen !== 'function') return;

  var WS_GUID = '258EAFA5-E914-47DA-95CA-5AB5D3D5D5E5';  // RFC 6455

  /* Single-server enforcement: only one serve() instance at a time */
  var activeInstance = null;

  /* ── Base64 helpers (RFC 4648) ── */
  var b64chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
  function b64encode(buf) {
    var s = '', i = 0;
    while (i + 2 < buf.length) {
      var b = (buf[i] << 16) | (buf[i + 1] << 8) | buf[i + 2];
      s += b64chars[(b >> 18) & 63] + b64chars[(b >> 12) & 63] +
           b64chars[(b >> 6) & 63] + b64chars[b & 63];
      i += 3;
    }
    if (i < buf.length) {
      var b2 = buf[i] << 16;
      var rem = 1;
      if (i + 1 < buf.length) { b2 |= buf[i + 1] << 8; rem = 2; }
      s += b64chars[(b2 >> 18) & 63] + b64chars[(b2 >> 12) & 63];
      if (rem === 2) s += b64chars[(b2 >> 6) & 63] + '=';
      else s += '==';
    }
    return s;
  }

  /* ── SHA-1 (FIPS 180-4) ── */
  function sha1Bytes(bytes) {
    function rotl(n, b) { return ((n << b) | (n >>> (32 - b))) >>> 0; }
    var H = [0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0];
    var ml = bytes.length * 8;
    var msg = new Uint8Array(((bytes.length + 8) >> 6 << 6) + 64);
    msg.set(bytes, 0);
    msg[bytes.length] = 0x80;
    var mlb = ml;
    for (var i = 0; i < 8; i++) {
      msg[msg.length - 1 - i] = mlb & 0xFF;
      mlb = Math.floor(mlb / 256);
    }
    for (var off = 0; off < msg.length; off += 64) {
      var W = new Array(80);
      for (var t = 0; t < 16; t++)
        W[t] = (msg[off + 4 * t] << 24) | (msg[off + 4 * t + 1] << 16) |
               (msg[off + 4 * t + 2] << 8) | msg[off + 4 * t + 3];
      for (var t = 16; t < 80; t++)
        W[t] = rotl(W[t - 3] ^ W[t - 8] ^ W[t - 14] ^ W[t - 16], 1);
      var a = H[0], b = H[1], c = H[2], d = H[3], e = H[4];
      for (var t = 0; t < 80; t++) {
        var f, k;
        if (t < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
        else if (t < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
        else if (t < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
        else { f = b ^ c ^ d; k = 0xCA62C1D6; }
        var tmp = (rotl(a, 5) + f + e + k + W[t]) >>> 0;
        e = d; d = c; c = rotl(b, 30); b = a; a = tmp;
      }
      H[0] = (H[0] + a) >>> 0; H[1] = (H[1] + b) >>> 0;
      H[2] = (H[2] + c) >>> 0; H[3] = (H[3] + d) >>> 0; H[4] = (H[4] + e) >>> 0;
    }
    var out = new Uint8Array(20);
    for (var i = 0; i < 5; i++) {
      out[4 * i] = (H[i] >>> 24) & 0xFF;
      out[4 * i + 1] = (H[i] >>> 16) & 0xFF;
      out[4 * i + 2] = (H[i] >>> 8) & 0xFF;
      out[4 * i + 3] = H[i] & 0xFF;
    }
    return out;
  }

  /* ── WS accept computation ── */
  function wsAccept(key) {
    var raw = new Uint8Array(key.length + WS_GUID.length);
    for (var i = 0; i < key.length; i++) raw[i] = key.charCodeAt(i);
    for (var i = 0; i < WS_GUID.length; i++) raw[key.length + i] = WS_GUID.charCodeAt(i);
    return b64encode(sha1Bytes(raw));
  }

  /* ── HTTP request parser (header-only; body delivered as a stream) ──
   * headerStr is the decoded header section WITHOUT the trailing \r\n\r\n.
   * The body never round-trips through a string: raw bytes are fed straight
   * into the body ReadableStream, so binary bodies are preserved. */
  function parseRequest(headerStr) {
    var idx = headerStr.indexOf('\r\n');
    if (idx < 0) return null;
    var reqLine = headerStr.substring(0, idx);
    var parts = reqLine.split(' ');
    if (parts.length < 3) return null;
    var method = parts[0], path = parts[1], version = parts[2];
    var headers = {};
    headerStr.substring(idx + 2).split('\r\n').forEach(function(l) {
      var ci = l.indexOf(':');
      if (ci > 0) headers[l.substring(0, ci).toLowerCase()] = l.substring(ci + 1).trim();
    });
    var cl = parseInt(headers['content-length'], 10);
    var contentLength = isNaN(cl) ? 0 : cl;
    var conn = (headers['connection'] || '').toLowerCase();
    var keepAlive = version !== 'HTTP/1.0' && conn !== 'close';
    return {
      method: method, path: path, version: version, headers: headers,
      keepAlive: keepAlive,
      contentLength: contentLength
    };
  }

  function concatBytes(a, b) {
    var out = new Uint8Array(a.length + b.length);
    out.set(a, 0);
    out.set(b, a.length);
    return out;
  }

  /* index of the \r\n\r\n header terminator in raw bytes, or -1 */
  function indexOfHdrEnd(bytes) {
    for (var i = 0; i + 3 < bytes.length; i++)
      if (bytes[i] === 13 && bytes[i + 1] === 10 &&
          bytes[i + 2] === 13 && bytes[i + 3] === 10) return i;
    return -1;
  }

  /* Read a ReadableStream to completion, returning all bytes as one Uint8Array. */
  function streamToBytes(stream) {
    if (!stream) return Promise.resolve(new Uint8Array(0));
    var reader = stream.getReader();
    var chunks = [];
    var total = 0;
    function pump(r) {
      if (r.done) {
        var out = new Uint8Array(total);
        var off = 0;
        for (var i = 0; i < chunks.length; i++) { out.set(chunks[i], off); off += chunks[i].length; }
        return out;
      }
      chunks.push(r.value);
      total += r.value.length;
      return reader.read().then(pump);
    }
    return reader.read().then(pump);
  }

  /* ── WS frame parser (server side: client→server frames are MASKED) ── */
  function parseWSFrame(buf) {
    if (buf.length < 2) return null;
    var first = buf[0], second = buf[1];
    var fin = (first >> 7) & 1;
    var opcode = first & 0x0F;
    var masked = (second >> 7) & 1;
    var len = second & 0x7F;
    var offset = 2;
    if (len === 126) {
      if (buf.length < 4) return null;
      len = (buf[2] << 8) | buf[3];
      offset = 4;
    } else if (len === 127) {
      if (buf.length < 10) return null;
      var hi = 0, lo = 0;
      for (var i = 0; i < 4; i++) hi = (hi * 256) + buf[2 + i];
      for (var i = 4; i < 8; i++) lo = (lo * 256) + buf[2 + i];
      len = hi * 4294967296 + lo;
      offset = 10;
    }
    var mask = null;
    if (masked) {
      if (buf.length < offset + 4) return null;
      mask = buf.slice(offset, offset + 4);
      offset += 4;
    }
    if (buf.length < offset + len) return null;
    var payload = buf.slice(offset, offset + len);
    if (mask) for (var i = 0; i < payload.length; i++) payload[i] ^= mask[i % 4];
    return { fin: fin, opcode: opcode, payload: payload, totalLen: offset + len };
  }

  /* ── Build WS frame (server→client: NOT masked) ── */
  function buildWSFrame(opcode, payload, fin) {
    var len = payload.length;
    var header = [(fin ? 0x80 : 0) | opcode];
    if (len < 126) {
      header.push(len);
    } else if (len < 65536) {
      header.push(126, (len >> 8) & 0xFF, len & 0xFF);
    } else {
      header.push(127);
      for (var i = 7; i >= 0; i--) header.push((len >> (i * 8)) & 0xFF);
    }
    var frame = new Uint8Array(header.length + len);
    for (var i = 0; i < header.length; i++) frame[i] = header[i];
    for (var i = 0; i < len; i++) frame[header.length + i] = payload[i];
    return frame;
  }

  /* ── Build HTTP response bytes ── */
  function buildHTTPResponse(status, statusText, hdrs, bodyBytes) {
    var h = 'HTTP/1.1 ' + status + ' ' + statusText + '\r\n';
    for (var k in hdrs) h += k + ': ' + hdrs[k] + '\r\n';
    h += '\r\n';
    var enc = new TextEncoder();
    var hBytes = enc.encode(h);
    var out = new Uint8Array(hBytes.length + bodyBytes.length);
    out.set(hBytes, 0);
    out.set(bodyBytes, hBytes.length);
    return out;
  }

  /* ── serve() ── */
  globalThis.serve = function serve(options, handler) {
    if (typeof options !== 'object' || options === null)
      throw new TypeError('serve: options object required');
    if (typeof handler !== 'function')
      throw new TypeError('serve: handler must be a function');

    var port = options.port === undefined ? 8080 : options.port;
    var idleTimeout = options.idleTimeout === undefined ? 30000 : options.idleTimeout;
    if (typeof port !== 'number' || port < 0 || port > 65535)
      throw new TypeError('serve: invalid port');
    var hostname = options.hostname || '0.0.0.0';
    var wsRoutes = options.ws || {};
    var activeServer = { closed: false };
    if (activeInstance && !activeInstance.closed)
      throw new Error('serve: a server is already running (call srv.close() first)');
    activeInstance = activeServer;

    /* ── WS connection ── */
    var currentKeepAlive = true;
    var currentResetIdle = function() {};
    function WSConnection(conn) {
      this.conn = conn;
      this.state = 0;  // 0=CONNECTING, 1=OPEN, 2=CLOSING, 3=CLOSED
      this.buf = new Uint8Array(0);
      this.onopen = null;
      this.onmessage = null;
      this.onclose = null;
      this.onerror = null;
      /* fragmentation state */
      this._fragOpcode = 0;    // 0x1 (text) or 0x2 (binary) of a fragmented message
      this._fragParts = [];    // payload chunks of the current fragmented message
    }

    WSConnection.prototype.send = function(data) {
      if (this.state !== 1) return;
      var payload = typeof data === 'string' ?
        new TextEncoder().encode(data) : (data || new Uint8Array(0));
      pal.tcpWrite(this.conn, buildWSFrame(0x1, payload, 1));
    };

    WSConnection.prototype.close = function(code, reason) {
      if (this.state >= 2) return;
      this.state = 2;
      code = code || 1000;
      reason = reason || '';
      var reasonBytes = new TextEncoder().encode(reason);
      var payload = new Uint8Array(2 + reasonBytes.length);
      payload[0] = (code >> 8) & 0xFF;
      payload[1] = code & 0xFF;
      payload.set(reasonBytes, 2);
      pal.tcpWrite(this.conn, buildWSFrame(0x8, payload, 1));
      this.state = 3;
    };

    WSConnection.prototype._processWSData = function(data) {
      /* data is an ArrayBuffer from the C layer — wrap for TypedArray ops */
      var dv = data instanceof Uint8Array ? data : new Uint8Array(data);
      var newBuf = new Uint8Array(this.buf.length + dv.length);
      newBuf.set(this.buf, 0);
      newBuf.set(dv, this.buf.length);
      this.buf = newBuf;

      for (;;) {
        var frame = parseWSFrame(this.buf);
        if (!frame) break;
        this.buf = this.buf.slice(frame.totalLen);

        if (frame.opcode === 0x8) {  // Close
          var closeCode = 1005, closeReason = '';
          if (frame.payload.length >= 2) {
            closeCode = (frame.payload[0] << 8) | frame.payload[1];
            closeReason = new TextDecoder().decode(frame.payload.slice(2));
          }
          pal.tcpWrite(this.conn, buildWSFrame(0x8, frame.payload, 1));
          this.state = 3;
          if (this.onclose) {
            var ev = { code: closeCode, reason: closeReason, wasClean: true };
            try { this.onclose(ev); } catch (e) {}
          }
        } else if (frame.opcode === 0x9) {  // Ping
          pal.tcpWrite(this.conn, buildWSFrame(0xA, frame.payload, 1));
        } else if (frame.opcode === 0x0) {  // Continuation
          this._fragParts.push(frame.payload);
          if (frame.fin) {
            var fragOp = this._fragOpcode;
            var combined = combineBytes(this._fragParts);
            this._fragOpcode = 0;
            this._fragParts = [];
            deliverWS(this, fragOp, combined);
          }
        } else if (frame.opcode === 0x1 || frame.opcode === 0x2) {  // Text/Binary
          if (frame.fin) {
            deliverWS(this, frame.opcode, frame.payload);
          } else {
            // start a fragmented message
            this._fragOpcode = frame.opcode;
            this._fragParts = [frame.payload];
          }
        }
      }
    };

    function combineBytes(parts) {
      var total = 0;
      for (var i = 0; i < parts.length; i++) total += parts[i].length;
      var out = new Uint8Array(total);
      var off = 0;
      for (var i = 0; i < parts.length; i++) { out.set(parts[i], off); off += parts[i].length; }
      return out;
    }

    function deliverWS(ws, opcode, payload) {
      var msg = null;
      if (opcode === 0x1) msg = new TextDecoder().decode(payload);
      else msg = payload;
      if (ws.onmessage) {
        var ev2 = { data: msg };
        try { ws.onmessage(ev2); } catch (e) {}
      }
    }

    function handleConnection(conn) {
      var raw = new Uint8Array(0);  // unparsed raw bytes (header + leftover body/requests)
      var ws = null;
      var idleTimer = null;
      var bodyState = null;  // {controller, remaining, received} of the in-flight request body
      conns.push(conn);

      function resetIdle() {
        clearTimeout(idleTimer);
        if (idleTimeout > 0 && !ws) {
          idleTimer = setTimeout(function() {
            pal.tcpClose(conn);
          }, idleTimeout);
        }
      }

      conn.ondata = function(data) {
        if (ws) {
          ws._processWSData(data);
          return;
        }
        data = data instanceof Uint8Array ? data : new Uint8Array(data);
        resetIdle();

        if (bodyState) {
          /* pure body bytes — feed the stream directly (no string round-trip) */
          var take = Math.min(bodyState.remaining - bodyState.received, data.length);
          if (take > 0) {
            bodyState.controller.enqueue(data.subarray(0, take));
            bodyState.received += take;
          }
          if (take < data.length) {
            /* bytes beyond this body belong to the next request */
            raw = concatBytes(raw, data.subarray(take));
          }
          if (bodyState.received < bodyState.remaining) {
            return;  // body still streaming — wait for more data
          }
          bodyState.controller.close();
          bodyState = null;
          /* fall through: body complete — parse any buffered next request */
        } else {
          raw = concatBytes(raw, data);
        }

        while (true) {
          var hdrEnd = indexOfHdrEnd(raw);
          if (hdrEnd < 0) return;  // header incomplete — wait for more data
          var headerStr = new TextDecoder().decode(raw.subarray(0, hdrEnd));
          var req = parseRequest(headerStr);
          if (!req) return;
          raw = raw.subarray(hdrEnd + 4);
          currentKeepAlive = req.keepAlive;

          /* ── WebSocket upgrade (no body) ── */
          var upgrade = (req.headers['upgrade'] || '').toLowerCase();
          if (upgrade === 'websocket') {
            var wsKey = req.headers['sec-websocket-key'];
            if (!wsKey) {
              pal.tcpWrite(conn, buildHTTPResponse(400, 'Bad Request',
                { 'Content-Length': '0', 'Connection': 'close' }, new Uint8Array(0)));
              return;
            }
            var wsHandler = wsRoutes[req.path];
            if (!wsHandler) {
              pal.tcpWrite(conn, buildHTTPResponse(404, 'Not Found',
                { 'Content-Length': '0', 'Connection': 'close' }, new Uint8Array(0)));
              return;
            }

            var accept = wsAccept(wsKey);
            var respHdrs = {
              'Upgrade': 'websocket',
              'Connection': 'Upgrade',
              'Sec-WebSocket-Accept': accept,
              'Content-Type': 'text/plain',
              'Content-Length': '0'
            };
            /* subprotocol negotiation: echo the first supported protocol */
            var reqProtocols = (req.headers['sec-websocket-protocol'] || '')
              .split(',').map(function(s) { return s.trim(); });
            var supportedProtocols = null;
            if (wsHandler && typeof wsHandler === 'object' &&
                Array.isArray(wsHandler.protocols)) {
              supportedProtocols = wsHandler.protocols;
            }
            if (supportedProtocols && reqProtocols.length) {
              for (var i = 0; i < reqProtocols.length; i++) {
                if (supportedProtocols.indexOf(reqProtocols[i]) >= 0) {
                  respHdrs['Sec-WebSocket-Protocol'] = reqProtocols[i];
                  break;
                }
              }
            }
            pal.tcpWrite(conn, buildHTTPResponse(101, 'Switching Protocols', respHdrs, new Uint8Array(0)));

            var wsRouteFn = typeof wsHandler === 'function' ? wsHandler : wsHandler.handler;
            ws = new WSConnection(conn);
            ws.state = 1;
            clearTimeout(idleTimer);
            if (typeof wsRouteFn === 'function') {
              try { wsRouteFn(ws); } catch (e) {}
            }
            if (ws.onopen) { try { ws.onopen({}); } catch (e) {} }
            return;
          }

          /* ── Regular HTTP request ── */
          var pathname = req.path;
          var qm = pathname.indexOf('?');
          var search = '';
          if (qm >= 0) {
            search = pathname.substring(qm);
            pathname = pathname.substring(0, qm);
          }

          /* body: ReadableStream fed from raw bytes (binary-safe) */
          var bodyStream = null;
          if (req.contentLength > 0) {
            var controller;
            bodyStream = new ReadableStream({ start: function(c) { controller = c; } });
            bodyState = { controller: controller, remaining: req.contentLength, received: 0 };
            /* feed body bytes already buffered in raw */
            var btake = Math.min(bodyState.remaining, raw.length);
            if (btake > 0) {
              bodyState.controller.enqueue(raw.subarray(0, btake));
              raw = raw.subarray(btake);
              bodyState.received += btake;
            }
            if (bodyState.received >= bodyState.remaining) {
              bodyState.controller.close();
              bodyState = null;
            }
          }
          var requestObj = {
            method: req.method,
            url: req.path,
            pathname: pathname,
            search: search,
            headers: req.headers,
            body: bodyStream,
            keepAlive: req.keepAlive
          };
          requestObj.text = function() {
            return streamToBytes(bodyStream).then(function(bytes) {
              return new TextDecoder().decode(bytes);
            });
          };
          requestObj.arrayBuffer = function() {
            return streamToBytes(bodyStream).then(function(bytes) {
              return bytes.buffer;
            });
          };

          currentResetIdle = resetIdle;

          try {
            var result = handler(requestObj);
            if (result && typeof result.then === 'function') {
              result.then(function(val) { sendResponse(conn, val); },
                          function() { sendResponse(conn, null, 500, 'Internal Server Error'); });
            } else {
              sendResponse(conn, result);
            }
          } catch (e) {
            sendResponse(conn, null, 500, 'Internal Server Error');
          }

          if (bodyState) return;  // body still streaming — wait for the remaining data
          /* no body / body complete — continue parsing buffered requests (pipelining) */
        }
      };

      conn.onerror = function(msg) {
        if (ws && ws.onerror) { try { ws.onerror(msg); } catch (e) {} }
        if (bodyState) {
          try { bodyState.controller.error(msg); } catch (e) {}
          bodyState = null;
        }
      };

      conn.onclose = function(code) {
        clearTimeout(idleTimer);
        var idx = conns.indexOf(conn);
        if (idx >= 0) conns.splice(idx, 1);
        if (bodyState) {
          try { bodyState.controller.error(new Error('connection closed')); } catch (e) {}
          bodyState = null;
        }
        if (ws && ws.onclose && ws.state < 3) {
          ws.state = 3;
          var ev = { code: code || 1006, reason: '', wasClean: false };
          try { ws.onclose(ev); } catch (e) {}
        }
      };
    }

    function sendResponse(conn, val, status, statusText) {
      var enc = new TextEncoder();

      if (val === null || val === undefined) {
        status = status || 500;
        statusText = statusText || 'Internal Server Error';
        pal.tcpWrite(conn, buildHTTPResponse(status, statusText, {
          'Content-Type': 'text/plain', 'Content-Length': '0',
          'Connection': 'close'
        }, new Uint8Array(0)));
        return;
      }

      if (typeof val === 'string') {
        var b = enc.encode(val);
        pal.tcpWrite(conn, buildHTTPResponse(200, 'OK', {
          'Content-Type': 'text/plain; charset=utf-8',
          'Content-Length': '' + b.length,
          'Connection': currentKeepAlive ? 'keep-alive' : 'close'
        }, b));
        if (!currentKeepAlive) pal.tcpClose(conn);
        return;
      }

      if (typeof val === 'object' && val !== null) {
        var st = val.status || 200;
        var stText = val.statusText || (st === 200 ? 'OK' : '');
        var hdrs = {};
        if (val.headers && typeof val.headers.forEach === 'function') {
          val.headers.forEach(function(v, k) { hdrs[k] = v; });
        } else if (val.headers) {
          for (var k in val.headers) {
            if (typeof val.headers[k] !== 'function' && k[0] !== '_')
              hdrs[k] = String(val.headers[k]);
          }
        }
        /* Body: _body (string/ArrayBuffer/Uint8Array) or text() sync fallback.
         * ArrayBuffer and Uint8Array go straight to buildHTTPResponse as raw bytes
         * (binary-safe). Strings go through TextEncoder for the rest. */
        var b = val._body;
        var b2;
        if (b instanceof ArrayBuffer) {
          b2 = new Uint8Array(b);
        } else if (typeof b === 'string') {
          b2 = enc.encode(b);
        } else if (b instanceof Uint8Array) {
          b2 = b;
        } else if (b && typeof b === 'object' && !(b instanceof Uint8Array)) {
          /* Blob/FormData/URLSearchParams-like: stringify */
          try { b2 = enc.encode(String(b)); } catch (e) {}
        }
        if (!b2 && typeof val.text === 'function') {
          try { var t = val.text(); if (typeof t === 'string') b2 = enc.encode(t); } catch (e) {}
        }
        if (!b2) b2 = enc.encode('');
        hdrs['Content-Length'] = '' + b2.length;
        if (!hdrs['Connection']) hdrs['Connection'] = currentKeepAlive ? 'keep-alive' : 'close';
        pal.tcpWrite(conn, buildHTTPResponse(st, stText, hdrs, b2));
        if (!currentKeepAlive) pal.tcpClose(conn);
        if (currentKeepAlive) currentResetIdle();
      }

      /* Numbers, booleans, etc. are invalid handler results */
      pal.tcpWrite(conn, buildHTTPResponse(500, 'Internal Server Error', {
        'Content-Type': 'text/plain',
        'Content-Length': '0',
        'Connection': 'close'
      }, new Uint8Array(0)));
      pal.tcpClose(conn);
    }

    var listener;
    var tls = options.tls;
    if (tls && tls.cert && tls.key)
      listener = pal.tcpListen(port, hostname, 128, handleConnection, {cert: tls.cert, key: tls.key});
    else
      listener = pal.tcpListen(port, hostname, 128, handleConnection);
    var conns = [];
    activeServer.close = function() {
      activeServer.closed = true;
      if (activeInstance === activeServer) activeInstance = null;
      pal.tcpCloseListener(listener);
    };
    return activeServer;
  };
}
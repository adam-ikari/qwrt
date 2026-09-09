/**
 * qwrt polyfill: gRPC server — unary semantics on top of the h2 server engine.
 *
 * HTTP/2 + gRPC Phase 3. The server half of the gRPC wire model:
 *
 *   - request: parse :path (/pkg.Svc/Method), content-type (must be
 *     application/grpc*), metadata; reassemble the Length-Prefixed-Message
 *     ([flag:1][len:4 BE][payload]) out of h2 DATA frames and decode it
 *   - response: initial headers → DATA (framed reply) → trailers HEADERS with
 *     grpc-status / grpc-message; errors before any response use the
 *     Trailers-Only form (status in the single HEADERS block, END_STREAM)
 *   - handler contract: `(call) => replyObject` or a Promise; `call` carries
 *     {path, method, request, metadata}. Throwing (or rejecting with) a
 *     StatusError maps to its code; any other error maps to INTERNAL.
 *
 * API shape (design §5.3, minimal usable): `grpc.createServer()` →
 * `server.addService(registry[, impls])` (or `addHandler(method, impl)`) →
 * pass the server as `serve(options, handler)` via `options.grpc`. The h2
 * connection is recognized automatically (ALPN 'h2' or plaintext preface).
 *
 * Scope: unary RPCs only — streaming handlers and message compression are
 * later phases (streaming methods in the registry are refused at dispatch).
 */

import {
  FrameSplitter, frameMessage, b64Decode, RESERVED,
  Status, StatusName, StatusError,
} from './grpc.js';

/* Percent-encode grpc-message exactly like grpc-js's server does (encodeURI):
 * spaces and non-ASCII become %XX while reserved characters stay literal.
 * Clients decode with decodeURI/decodeURIComponent, so round-trips are lossless
 * and peers that skip decoding still read ASCII-only messages. */
function percentEncode(s) {
  try { return encodeURI(s); } catch (e) { return s; }
}

function toStatus(e) {
  if (e instanceof StatusError) {
    return new StatusError(e.rawMessage != null ? e.rawMessage : e.message, e.code | 0);
  }
  if (e && typeof e.code === 'number' && e.message !== undefined) {
    return new StatusError(String(e.message), e.code | 0);
  }
  return new StatusError((e && e.message) || 'internal error', Status.INTERNAL);
}

function parseRequestHeaders(pairs) {
  var res = { path: null, contentType: '', metadata: {} };
  for (var i = 0; i < pairs.length; i++) {
    var k = pairs[i][0], v = pairs[i][1];
    if (k === ':path') { res.path = v; continue; }
    if (k.charAt(0) === ':') continue;
    var lk = k.toLowerCase();
    if (lk === 'content-type') { res.contentType = v; continue; }
    if (RESERVED[lk]) continue;
    res.metadata[lk] = /-bin$/.test(lk) ? b64Decode(v) : v;

  }
  return res;
}

export class GrpcServer {
  constructor(opts) {
    opts = opts || {};
    this._methods = Object.create(null); // path → {method, impl}
    this._maxRecv = opts.maxRecvMsgSize || 4 * 1024 * 1024;
  }

  /* Register every service of a loadProto() registry. impls maps method names
   * (e.g. {SayHello: fn}) to unary handlers; absent entries dispatch to
   * UNIMPLEMENTED when called. */
  addService(registry, impls) {
    if (!registry || typeof registry !== 'object' || !registry.services) {
      throw new Error('grpc: addService(registry, impls?) needs a loadProto() result');
    }
    for (var sname in registry.services) {
      var svc = registry.services[sname];
      if (!svc.methods) continue;
      for (var mname in svc.methods) {
        var md = svc.methods[mname];
        if (md.clientStreaming || md.serverStreaming) continue; // unary only
        this.addHandler(md, impls && impls[mname]);
      }
    }
  }

  /* Register one method: `method` is a bound method object (from
   * loadProto(...).service('pkg.Svc').method('M')) or a '/pkg.Svc/M' path;
   * in the latter case `types = {requestType, responseType}` is required.
   * `impl` may be null to register the signature (calls → UNIMPLEMENTED). */
  addHandler(method, impl, types) {
    var md;
    if (method && typeof method === 'object' && method.path) {
      md = method;
    } else if (typeof method === 'string') {
      types = types || impl && (impl.requestType || impl.responseType) ? {
        requestType: impl.requestType, responseType: impl.responseType,
      } : types;
      if (!types || !types.requestType || !types.responseType) {
        throw new Error('grpc: addHandler(path, impl, {requestType, responseType}) required');
      }
      var path = method.charAt(0) === '/' ? method : '/' + method;
      md = { path: path, requestType: types.requestType, responseType: types.responseType };
    } else {
      throw new Error('grpc: addHandler(methodOrPath, impl) needs a bound method or /pkg.Svc/M path');
    }
    this._methods[md.path] = { method: md, impl: impl || null };
  }

  /* Serve one h2 request stream. */
  _handleStream(stream) {
    var self = this;
    var parsed;
    try { parsed = parseRequestHeaders(stream.headers || []); }
    catch (e) { this._respondError(stream, new StatusError('bad request metadata', Status.INTERNAL)); return; }

    var entry = parsed.path ? this._methods[parsed.path] : null;
    if (!entry) {
      this._respondError(stream, new StatusError('method ' + parsed.path + ' is not implemented',
                                                 Status.UNIMPLEMENTED));
      return;
    }
    if (/^application\/grpc/.test(parsed.contentType) === false) {
      this._respondError(stream, new StatusError('content-type must be application/grpc*',
                                                 Status.UNIMPLEMENTED));
      return;
    }

    var reqBytes = null;
    var framingError = null;
    var splitter = new FrameSplitter(self._maxRecv, function (m) { reqBytes = m; });
    stream.onData = function (chunk) {
      try { splitter.push(chunk); }
      catch (e) { framingError = e instanceof StatusError ? e : new StatusError(e.message, Status.INTERNAL); }
    };
    stream.onEnd = function () {
      if (stream.aborted) return;
      if (framingError) { self._respondError(stream, framingError); return; }
      var request;
      try {
        request = entry.method.requestType.decode(reqBytes || new Uint8Array(0));
      } catch (e) {
        self._respondError(stream, new StatusError('failed to decode request: ' + e.message,
                                                   Status.INTERNAL));
        return;
      }
      var call = {
        path: entry.method.path,
        method: entry.method,
        request: request,
        metadata: parsed.metadata,
      };
      var result;
      try {
        if (!entry.impl) {
          self._respondError(stream, new StatusError('method ' + entry.method.path +
                                                     ' has no handler', Status.UNIMPLEMENTED));
          return;
        }
        result = entry.impl(call);
      } catch (e) {
        self._respondError(stream, toStatus(e));
        return;
      }
      if (result && typeof result.then === 'function') {
        result.then(
          function (val) { self._respondOk(stream, entry.method, val); },
          function (e) { self._respondError(stream, toStatus(e)); });
      } else {
        self._respondOk(stream, entry.method, result);
      }
    };
    stream.onError = function () {}; // cancellation handled via stream.aborted
  }

  _respondOk(stream, method, val) {
    if (stream.aborted || stream.localEnded) return;
    var payload;
    try { payload = method.responseType.encode(val); }
    catch (e) {
      this._respondError(stream, new StatusError('failed to encode response: ' + e.message,
                                                 Status.INTERNAL));
      return;
    }
    var body = frameMessage(payload);
    stream.respond([
      [':status', '200'],
      ['content-type', 'application/grpc+proto'],
      ['grpc-encoding', 'identity'],
    ]);
    stream.write(body);
    stream.end([['grpc-status', '0']]);
  }

  _respondError(stream, status) {
    if (stream.aborted || stream.localEnded) return;
    var statusName = StatusName[status.code] || String(status.code);
    var hs = [
      [':status', '200'],
      ['content-type', 'application/grpc+proto'],
      ['grpc-status', String(status.code)],
    ];
    var msg = status.rawMessage != null ? String(status.rawMessage)
      : (status.message != null ? String(status.message) : statusName);
    // Percent-encode like grpc-js's own server (encodeURI) so any client that
    // decodes (decodeURI / decodeURIComponent) round-trips losslessly, and
    // ASCII-only messages stay readable to peers that skip decoding.
    if (msg) hs.push(['grpc-message', percentEncode(msg)]);
    if (!stream.headersSent) {
      // Trailers-Only: status travels in the single HEADERS block (END_STREAM).
      stream.end(hs);
    } else {
      stream.end(hs);
    }
  }
}

export function createServer(opts) { return new GrpcServer(opts); }

/* Install onto the global grpc namespace (bundle wiring). */
export function setupGrpcServer(pal) {
  globalThis.grpc = globalThis.grpc || {};
  globalThis.grpc.Server = GrpcServer;
  globalThis.grpc.createServer = createServer;
}

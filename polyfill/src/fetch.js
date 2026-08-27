/**
 * qwrt Polyfill - fetch API
 *
 * Implements WHATWG Fetch standard: Headers, Request, Response, fetch()
 * Uses pal.httpRequestStream(url, method, headers_json, body, onHeaders, onData, onEnd)
 * for streaming HTTP responses via ReadableStream.
 *
 * PAL httpRequestStream callbacks:
 *   onHeaders(status, headersJson) — called once with response status and headers
 *   onData(arrayBuffer)            — called for each chunk of response body
 *   onEnd(errorStatus)             — called when response completes (0 = success)
 *
 * Depends on: DOMException (from abort.js)
 */

export function setupFetch(pal) {
  'use strict';

  // ================================================================
  // Status text mapping for common HTTP status codes
  // PAL doesn't return status text, so we map it ourselves.
  // ================================================================

  var STATUS_TEXTS = {
    100: 'Continue',
    101: 'Switching Protocols',
    200: 'OK',
    201: 'Created',
    202: 'Accepted',
    203: 'Non-Authoritative Information',
    204: 'No Content',
    205: 'Reset Content',
    206: 'Partial Content',
    300: 'Multiple Choices',
    301: 'Moved Permanently',
    302: 'Found',
    303: 'See Other',
    304: 'Not Modified',
    307: 'Temporary Redirect',
    308: 'Permanent Redirect',
    400: 'Bad Request',
    401: 'Unauthorized',
    402: 'Payment Required',
    403: 'Forbidden',
    404: 'Not Found',
    405: 'Method Not Allowed',
    406: 'Not Acceptable',
    407: 'Proxy Authentication Required',
    408: 'Request Timeout',
    409: 'Conflict',
    410: 'Gone',
    411: 'Length Required',
    412: 'Precondition Failed',
    413: 'Payload Too Large',
    414: 'URI Too Long',
    415: 'Unsupported Media Type',
    416: 'Range Not Satisfiable',
    417: 'Expectation Failed',
    418: "I'm a Teapot",
    422: 'Unprocessable Entity',
    425: 'Too Early',
    426: 'Upgrade Required',
    428: 'Precondition Required',
    429: 'Too Many Requests',
    431: 'Request Header Fields Too Large',
    451: 'Unavailable For Legal Reasons',
    500: 'Internal Server Error',
    501: 'Not Implemented',
    502: 'Bad Gateway',
    503: 'Service Unavailable',
    504: 'Gateway Timeout',
    505: 'HTTP Version Not Supported',
    506: 'Variant Also Negotiates',
    507: 'Insufficient Storage',
    508: 'Loop Detected',
    510: 'Not Extended',
    511: 'Network Authentication Required'
  };

  // ================================================================
  // Helpers
  // ================================================================

  function normalizeName(name) {
    if (typeof name !== 'string') {
      name = String(name);
    }
    var normalized = name.toLowerCase();
    if (/[^a-z0-9\-!#$%&'*+.^_`|~]/.test(normalized)) {
      throw new TypeError('Invalid header name: ' + name);
    }
    return normalized;
  }

  function normalizeValue(value) {
    if (value === undefined || value === null) {
      throw new TypeError('Invalid header value');
    }
    var str = String(value);
    if (/[^\t\x20-\x7e\x80-\xff]/.test(str)) {
      throw new TypeError('Invalid header value: ' + str);
    }
    return str;
  }

  // String-to-Uint8Array using TextEncoder for proper UTF-8 support
  function stringToUint8Array(str) {
    return new TextEncoder().encode(str);
  }

  // Serialize a Request body to bytes: returns a Uint8Array (or null) when
  // the body is available synchronously, or a Promise<Uint8Array> when it is
  // a stream that must be read asynchronously.
  function serializeBody(body) {
    if (body == null) {
      return null;
    }
    if (typeof body === 'string') {
      return new TextEncoder().encode(body);
    }
    if (body instanceof Uint8Array) {
      return body;
    }
    if (body instanceof ArrayBuffer) {
      return new Uint8Array(body);
    }
    if (typeof body.getReader === 'function') {
      var reader = body.getReader();
      var chunks = [];
      var total = 0;
      function pump() {
        return reader.read().then(function(result) {
          if (result.done) {
            reader.releaseLock();
            var out = new Uint8Array(total);
            var off = 0;
            for (var i = 0; i < chunks.length; i++) {
              out.set(chunks[i], off);
              off += chunks[i].length;
            }
            return out;
          }
          var chunk = result.value;
          if (!(chunk instanceof Uint8Array)) {
            chunk = stringToUint8Array(String(chunk));
          }
          chunks.push(chunk);
          total += chunk.length;
          return pump();
        });
      }
      return pump();
    }
    return new TextEncoder().encode(String(body));
  }

  // ================================================================
  // Headers class
  // WHATWG Headers interface with case-insensitive header names.
  // ================================================================

  function Headers(init) {
    this._map = new Map();

    if (init === null) {
      throw new TypeError('Headers init must not be null');
    }
    if (init !== undefined) {
      if (init instanceof Headers) {
        init._map.forEach(function(value, key) {
          this._map.set(key, value);
        }.bind(this));
      } else if (typeof init === 'object') {
        if (Symbol && Symbol.iterator && init[Symbol.iterator]) {
          // Iterable of [name, value] pairs
          var items = Array.from(init);
          for (var i = 0; i < items.length; i++) {
            var pair = items[i];
            if (!Array.isArray(pair) || pair.length !== 2) {
              throw new TypeError('Headers init: each header must be a [name, value] pair');
            }
            this.append(pair[0], pair[1]);
          }
        } else {
          // Plain object
          var keys = Object.keys(init);
          for (var j = 0; j < keys.length; j++) {
            this.append(keys[j], init[keys[j]]);
          }
        }
      } else {
        throw new TypeError('Headers init must be Headers, object, or iterable');
      }
    }
  }

  Headers.prototype.get = function(name) {
    return this._map.get(normalizeName(name)) || null;
  };

  Headers.prototype.set = function(name, value) {
    this._map.set(normalizeName(name), normalizeValue(value));
  };

  Headers.prototype.has = function(name) {
    return this._map.has(normalizeName(name));
  };

  Headers.prototype.delete = function(name) {
    this._map.delete(normalizeName(name));
  };

  Headers.prototype.append = function(name, value) {
    var key = normalizeName(name);
    var existing = this._map.get(key);
    if (existing) {
      this._map.set(key, existing + ', ' + normalizeValue(value));
    } else {
      this._map.set(key, normalizeValue(value));
    }
  };

  Headers.prototype.forEach = function(callback, thisArg) {
    this._map.forEach(function(value, key) {
      callback.call(thisArg, value, key, this);
    }.bind(this));
  };

  Headers.prototype.entries = function() {
    return this._map.entries();
  };

  Headers.prototype.keys = function() {
    return this._map.keys();
  };

  Headers.prototype.values = function() {
    return this._map.values();
  };

  if (Symbol) {
    Headers.prototype[Symbol.iterator] = function() {
      return this._map.entries();
    };
  }

  // ================================================================
  // Body mixin helper
  // Shared body consumption logic for Request and Response.
  // ================================================================

  function consumeBody(body) {
    if (typeof body === 'string') {
      return body;
    }
    if (body === null || body === undefined) {
      return '';
    }
    return String(body);
  }

  // ================================================================
  // Request class
  // ================================================================

  function Request(input, init) {
    init = init || {};

    // input can be a string URL or another Request
    if (input instanceof Request) {
      this._method = init.method || input.method;
      this._url = input.url;
      this._headers = new Headers(init.headers || input.headers);
      this._body = init.body !== undefined ? init.body : input._body;
      this._signal = init.signal || input.signal;
      this._redirect = init.redirect || input.redirect || 'follow';
      this._keepalive = init.keepalive !== undefined ? !!init.keepalive : input.keepalive;
      this._cache = init.cache || input.cache || 'default';
      this._mode = init.mode || input.mode || 'cors';
      this._credentials = init.credentials || input.credentials || 'same-origin';
    } else {
      this._method = init.method || 'GET';
      this._url = String(input);
      this._headers = new Headers(init.headers);
      this._body = init.body !== undefined ? init.body : null;
      this._signal = init.signal || null;
      this._redirect = init.redirect || 'follow';
      this._keepalive = !!init.keepalive;
      this._cache = init.cache || 'default';
      this._mode = init.mode || 'cors';
      this._credentials = init.credentials || 'same-origin';
    }

    this._bodyUsed = false;

    // Validate method
    if (!/^[A-Z]+$/.test(this._method)) {
      throw new TypeError('Invalid HTTP method: ' + this._method);
    }
  }

  Object.defineProperty(Request.prototype, 'method', {
    get: function() { return this._method; }
  });

  Object.defineProperty(Request.prototype, 'url', {
    get: function() { return this._url; }
  });

  Object.defineProperty(Request.prototype, 'headers', {
    get: function() { return this._headers; }
  });

  Object.defineProperty(Request.prototype, 'body', {
    get: function() { return this._body; }
  });

  Object.defineProperty(Request.prototype, 'bodyUsed', {
    get: function() { return this._bodyUsed; }
  });

  Object.defineProperty(Request.prototype, 'signal', {
    get: function() { return this._signal; }
  });

  Object.defineProperty(Request.prototype, 'redirect', {
    get: function() { return this._redirect; }
  });

  Object.defineProperty(Request.prototype, 'keepalive', {
    get: function() { return this._keepalive; }
  });

  Object.defineProperty(Request.prototype, 'cache', {
    get: function() { return this._cache; }
  });

  Object.defineProperty(Request.prototype, 'mode', {
    get: function() { return this._mode; }
  });

  Object.defineProperty(Request.prototype, 'credentials', {
    get: function() { return this._credentials; }
  });

  Request.prototype.clone = function() {
    if (this._bodyUsed) {
      throw new TypeError('Cannot clone a Request whose body has been used');
    }
    return new Request(this);
  };

  Request.prototype.text = function() {
    if (this._bodyUsed) {
      throw new TypeError('Body has already been used');
    }
    this._bodyUsed = true;
    return Promise.resolve(consumeBody(this._body));
  };

  Request.prototype.json = function() {
    return this.text().then(function(text) {
      return JSON.parse(text);
    });
  };

  Request.prototype.arrayBuffer = function() {
    return this.text().then(function(text) {
      return stringToUint8Array(text);
    });
  };

  Request.prototype.blob = function() {
    // Blob not available in this environment; return body as string
    return this.text();
  };

  // ================================================================
  // ReadableStream (streaming implementation)
  // Supports proper streaming via controller.enqueue/close/error.
  // The start(controller) callback can enqueue chunks, and the PAL
  // streaming callbacks deliver data incrementally.
  // ================================================================

  function ReadableStream(underlyingSource) {
    this._reader = null;
    this._locked = false;
    this._controller = {
      _stream: this,
      _closed: false,
      _pendingReads: [],
      _enqueuedChunks: [],
      enqueue: function(chunk) {
        if (this._closed) return;
        if (this._pendingReads.length > 0) {
          var pending = this._pendingReads.shift();
          pending._resolve({done: false, value: chunk});
        } else {
          this._enqueuedChunks.push(chunk);
        }
      },
      close: function() {
        if (this._closed) return;
        this._closed = true;
        while (this._pendingReads.length > 0) {
          var pending = this._pendingReads.shift();
          pending._resolve({done: true, value: undefined});
        }
        if (this._stream._reader) {
          this._stream._reader._closed = true;
        }
      },
      error: function(e) {
        if (this._closed) return;
        this._closed = true;
        while (this._pendingReads.length > 0) {
          var pending = this._pendingReads.shift();
          pending._reject(e);
        }
        if (this._stream._reader) {
          this._stream._reader._closed = true;
          this._stream._reader._error = e;
        }
      }
    };

    if (underlyingSource && typeof underlyingSource.start === 'function') {
      underlyingSource.start(this._controller);
    }
  }

  function ReadableStreamDefaultReader(stream) {
    if (stream._locked) throw new TypeError('ReadableStream already locked');
    stream._locked = true;
    this._stream = stream;
    this._closed = false;
    this._error = null;
    stream._reader = this;
  }

  ReadableStream.prototype.getReader = function() {
    if (this._locked) throw new TypeError('ReadableStream already locked');
    return new ReadableStreamDefaultReader(this);
  };

  ReadableStreamDefaultReader.prototype.read = function() {
    var self = this;
    if (self._error) {
      return Promise.reject(self._error);
    }
    if (self._closed) {
      return Promise.resolve({done: true, value: undefined});
    }
    var ctrl = self._stream._controller;
    if (ctrl._enqueuedChunks.length > 0) {
      var chunk = ctrl._enqueuedChunks.shift();
      return Promise.resolve({done: false, value: chunk});
    }
    if (ctrl._closed) {
      self._closed = true;
      return Promise.resolve({done: true, value: undefined});
    }
    // No data yet — create a pending read
    return new Promise(function(resolve, reject) {
      ctrl._pendingReads.push({_resolve: resolve, _reject: reject});
    });
  };

  ReadableStreamDefaultReader.prototype.releaseLock = function() {
    this._stream._locked = false;
    this._stream._reader = null;
    this._closed = true;
  };

  // ================================================================
  // Response class
  // ================================================================

  function Response(body, init) {
    init = init || {};

    this._status = init.status !== undefined ? Number(init.status) : 200;
    this._statusText = init.statusText || STATUS_TEXTS[this._status] || '';
    this._headers = new Headers(init.headers);
    this._bodyUsed = false;
    this._type = 'default';
    this._url = init.url || '';
    this._redirected = init.redirected || false;

    // body can be a ReadableStream (duck-type check) or a string/null (non-streaming)
    if (body && typeof body.getReader === 'function') {
      this._bodyStream = body;
      this._body = null;
    } else {
      this._bodyStream = null;
      this._body = body !== undefined ? body : null;
    }
  }

  Object.defineProperty(Response.prototype, 'status', {
    get: function() { return this._status; }
  });

  Object.defineProperty(Response.prototype, 'statusText', {
    get: function() { return this._statusText; }
  });

  Object.defineProperty(Response.prototype, 'ok', {
    get: function() { return this._status >= 200 && this._status <= 299; }
  });

  Object.defineProperty(Response.prototype, 'headers', {
    get: function() { return this._headers; }
  });

  Object.defineProperty(Response.prototype, 'body', {
    get: function() {
      if (this._bodyStream) return this._bodyStream;
      if (this._body == null) return null;
      // Create a ReadableStream from the body string for non-streaming responses
      var bodyStr = consumeBody(this._body);
      var arr = stringToUint8Array(bodyStr);
      return new ReadableStream({
        start: function(controller) {
          controller.enqueue(arr);
          controller.close();
        }
      });
    }
  });

  Object.defineProperty(Response.prototype, 'bodyUsed', {
    get: function() { return this._bodyUsed; }
  });

  Object.defineProperty(Response.prototype, 'type', {
    get: function() { return this._type; }
  });

  Object.defineProperty(Response.prototype, 'url', {
    get: function() { return this._url; }
  });

  Object.defineProperty(Response.prototype, 'redirected', {
    get: function() { return this._redirected; }
  });

  Response.prototype.clone = function() {
    if (this._bodyUsed) {
      throw new TypeError('Cannot clone a Response whose body has been used');
    }
    if (this._bodyStream) {
      throw new TypeError('Cannot clone a streaming Response');
    }
    var cloned = new Response(this._body, {
      status: this._status,
      statusText: this._statusText,
      headers: this._headers,
      url: this._url,
      redirected: this._redirected
    });
    cloned._type = this._type;
    return cloned;
  };

  Response.prototype.text = function() {
    var self = this;
    if (self._bodyUsed) {
      throw new TypeError('Body has already been used');
    }
    self._bodyUsed = true;

    if (self._bodyStream) {
      // Read all chunks from the stream and concatenate
      return self._readStreamFully().then(function(chunks) {
        var totalLen = 0;
        for (var i = 0; i < chunks.length; i++) {
          totalLen += chunks[i].length;
        }
        var combined = new Uint8Array(totalLen);
        var offset = 0;
        for (var i = 0; i < chunks.length; i++) {
          combined.set(chunks[i], offset);
          offset += chunks[i].length;
        }
        // Decode as UTF-8
        return new TextDecoder('utf-8').decode(combined);
      });
    }

    return Promise.resolve(consumeBody(self._body));
  };

  Response.prototype._readStreamFully = function() {
    var reader = this._bodyStream.getReader();
    var chunks = [];
    function pump() {
      return reader.read().then(function(result) {
        if (result.done) return chunks;
        var chunk = result.value;
        // Convert ArrayBuffer to Uint8Array if needed
        if (chunk instanceof ArrayBuffer) {
          chunk = new Uint8Array(chunk);
        }
        chunks.push(chunk);
        return pump();
      });
    }
    return pump();
  };

  Response.prototype.json = function() {
    return this.text().then(function(text) {
      return JSON.parse(text);
    });
  };

  Response.prototype.arrayBuffer = function() {
    var self = this;
    if (self._bodyUsed) {
      throw new TypeError('Body has already been used');
    }
    self._bodyUsed = true;

    if (self._bodyStream) {
      return self._readStreamFully().then(function(chunks) {
        var totalLen = 0;
        for (var i = 0; i < chunks.length; i++) {
          totalLen += chunks[i].length;
        }
        var combined = new Uint8Array(totalLen);
        var offset = 0;
        for (var i = 0; i < chunks.length; i++) {
          combined.set(chunks[i], offset);
          offset += chunks[i].length;
        }
        return combined.buffer;
      });
    }

    return Promise.resolve(stringToUint8Array(consumeBody(self._body)));
  };

  Response.prototype.blob = function() {
    // Blob not available in this environment; return body as string
    return this.text();
  };

  // Static methods

  Response.error = function() {
    var response = new Response(null, {
      status: 0,
      statusText: ''
    });
    response._type = 'error';
    return response;
  };

  Response.redirect = function(url, status) {
    if (status === undefined) status = 302;
    if (status < 300 || status > 399) {
      throw new RangeError('Invalid redirect status: ' + status);
    }
    var response = new Response(null, {
      status: status,
      headers: { location: url }
    });
    response._type = 'opaqueredirect';
    return response;
  };

  Response.json = function(data, init) {
    init = init || {};
    var body = JSON.stringify(data);
    var headers = new Headers(init.headers);
    if (!headers.has('content-type')) {
      headers.set('content-type', 'application/json');
    }
    return new Response(body, {
      status: init.status !== undefined ? init.status : 200,
      statusText: init.statusText || '',
      headers: headers
    });
  };

  // ================================================================
  // fetch function
  // ================================================================

  function fetch(input, init) {
    return new Promise(function(resolve, reject) {
      var request;

      try {
        request = new Request(input, init);
      } catch (e) {
        reject(e);
        return;
      }

      // Check if already aborted
      if (request.signal && request.signal.aborted) {
        reject(new DOMException('The operation was aborted.', 'AbortError'));
        return;
      }

      // One HTTP transfer; redirects re-enter via doRequest. redirectCount
      // guards against infinite redirect loops (WHATWG cap is 20).
      doRequest(request, resolve, reject, 0);
    });
  }

  // One HTTP transfer (plus redirect following). Each hop owns its own abort
  // listener, readable stream, and cleanup; on 3xx, follow/error/manual branch
  // before the body stream is exposed.
  function doRequest(request, resolve, reject, redirectCount) {
    // Serialize headers to JSON
    var headersObj = {};
    request.headers.forEach(function(value, name) {
      headersObj[name] = value;
    });
    var headersJson = JSON.stringify(headersObj);

    // Serialize request body to bytes (Uint8Array) or null. Stream bodies
    // return a Promise; the PAL call below is deferred until it resolves.
    var requestBodyBytes = serializeBody(request.body);

    var aborted = false;
    var onAbort;
    var streamController = null;
    var abandoned = false;   /* this hop's stream was abandoned due to a redirect */

    var resolved = false;    /* fetch promise 已 settle(防 onEnd 重复 reject) */
    if (request.signal) {
      onAbort = function() {
        aborted = true;
        if (streamController) {
          streamController.error(new DOMException('The operation was aborted.', 'AbortError'));
        }
        reject(new DOMException('The operation was aborted.', 'AbortError'));
      };
      request.signal.addEventListener('abort', onAbort);
    }

    function cleanupAbort() {
      if (request.signal && onAbort) {
        request.signal.removeEventListener('abort', onAbort);
      }
    }

    // Run cb once the request body is ready. Serialization is synchronous
    // except for stream bodies (async read); a failed read rejects the fetch.
    function whenBodyReady(cb) {
      if (requestBodyBytes && typeof requestBodyBytes.then === 'function') {
        requestBodyBytes.then(cb, function(err) {
          if (aborted) return;
          cleanupAbort();
          reject(new TypeError('fetch failed: ' + (err || 'unknown error')));
        });
      } else {
        cb(requestBodyBytes);
      }
    }

    // Fallback to non-streaming httpRequest if streaming not available.
    // pal.httpRequest returns a Promise<string> (resolves with the body on
    // success, rejects with an error string on failure); it does NOT provide
    // the HTTP status or headers, so the Response is built with status 200
    // and empty headers (the non-streaming PAL contract's limitation).
    if (typeof pal.httpRequestStream !== 'function') {
      // The non-streaming PAL API accepts only a string body, so decode the
      // serialized bytes (lossless round-trip for text bodies).
      whenBodyReady(function(bytes) {
        if (aborted) { return; }
        cleanupAbort();
        var bodyStr = bytes ? new TextDecoder().decode(bytes) : null;
        var p = pal.httpRequest(request.url, request.method, headersJson, bodyStr);
        Promise.resolve(p).then(function(data) {
          if (aborted) return;
          var resBytes = stringToUint8Array(data || '');
          var res = new Response(resBytes, {
            status: 200,
            headers: new Headers()
          });
          res._url = request.url;
          resolve(res);
        }, function(err) {
          if (aborted) return;
          reject(new TypeError('fetch failed: ' + (err || 'unknown error')));
        });
      });
      return;
    }

    // Create a ReadableStream that will receive chunks from PAL callbacks
    var readableStream = new ReadableStream({
      start: function(controller) {
        streamController = controller;
      }
    });

    // PAL streaming callbacks
    function onHeaders(status, headersJsonStr) {
      if (aborted) return;

      var parsedHeaders = {};
      if (headersJsonStr) {
        try {
          parsedHeaders = JSON.parse(headersJsonStr);
        } catch (e) {
          // If headers parse fails, use empty headers
        }
      }

      // === redirect handling (3xx) ===
      if (status >= 300 && status <= 399) {
        if (request.redirect === 'error') {
          cleanupAbort();
          reject(new TypeError('fetch failed: redirect mode is "error"'));
          return;
        }
        if (request.redirect === 'manual') {
          cleanupAbort();
          var manualRes = new Response(null, { status: 0, statusText: '' });
          manualRes._type = 'opaqueredirect';
          resolve(manualRes);
          return;
        }
        /* redirect === 'follow' (default) */
        if (redirectCount >= 20) {
          cleanupAbort();
          reject(new TypeError('fetch failed: too many redirects'));
          return;
        }
        var location = parsedHeaders['location'] || parsedHeaders['Location'] || null;
        if (location) {
          var nextUrl;
          try {
            nextUrl = new URL(location, request.url).href;
          } catch (e) {
            cleanupAbort();
            reject(new TypeError('fetch failed: invalid redirect location'));
            return;
          }
          var nextInit = {
            method: request.method,
            headers: request.headers,
            signal: request.signal,
            redirect: request.redirect,
            keepalive: request.keepalive,
            cache: request.cache,
            mode: request.mode,
            credentials: request.credentials
          };
          /* 301/302 (POST→GET) and 303 drop the body; 307/308 keep method+body */
          if (status === 303 ||
              (status === 301 && request.method === 'POST') ||
              (status === 302 && request.method === 'POST')) {
            nextInit.method = 'GET';
          } else {
            nextInit.body = request.body;
          }
          var nextReq = new Request(nextUrl, nextInit);
          cleanupAbort();
          abandoned = true;
          doRequest(nextReq, resolve, reject, redirectCount + 1);
          return;
        }
        /* No Location header: fall through and return the response as-is */
      }

      var headers = new Headers(parsedHeaders);
      var resp = new Response(readableStream, {
        status: status,
        statusText: STATUS_TEXTS[status] || '',
        headers: headers,
        url: request.url
      });

      resolved = true;       /* 之后 onEnd 的 EOF 只 close 流,不再 reject */
      resolve(resp);
    }

    function onData(chunk) {
      if (aborted || abandoned) return;
      if (streamController) {
        // chunk is an ArrayBuffer from the bridge; convert to Uint8Array
        var arr;
        if (chunk instanceof ArrayBuffer) {
          arr = new Uint8Array(chunk);
        } else if (chunk instanceof Uint8Array) {
          arr = chunk;
        } else {
          // String fallback
          arr = stringToUint8Array(String(chunk));
        }
        streamController.enqueue(arr);
      }
    }

    function onEnd(errorStatus) {
      // Clean up abort listener
      cleanupAbort();

      if (aborted || abandoned) return;

      /* 网络错误(连接/握手/TLS 失败)且 fetch promise 尚未 settle:必须
       * reject,否则 fetch 永远 pending。 */
      if (errorStatus !== 0 && !resolved) {
        reject(new TypeError('fetch failed: network error ' + errorStatus));
        return;
      }

      if (streamController) {
        if (errorStatus !== 0) {
          streamController.error(new TypeError('fetch failed with status: ' + errorStatus));
        } else {
          streamController.close();
        }
      }
    }

    // Call PAL streaming HTTP once the request body is ready.
    whenBodyReady(function(bytes) {
      if (aborted) return;
      pal.httpRequestStream(request.url, request.method, headersJson, bytes, onHeaders, onData, onEnd);
    });
  }

  // ================================================================
  // Register on globalThis
  // ================================================================

  globalThis.Headers = Headers;
  globalThis.Request = Request;
  globalThis.Response = Response;
  globalThis.fetch = fetch;
}

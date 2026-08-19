/**
 * qwrt polyfill: Streams API
 *
 * Full ReadableStream (with controller), WritableStream, TransformStream,
 * and CompressionStream / DecompressionStream.
 *
 * TC55/ECMA-429 requires these stream APIs.
 * CompressionStream/DecompressionStream delegate to
 * pal.nativeCompress / pal.nativeDecompress (C extension using miniz).
 *
 * Depends on: EventTarget (for AbortSignal integration).
 */

export function setupStreams(pal) {

  // ================================================================
  // ReadableStream
  // ================================================================

  function ReadableStreamUnderlyingSourceDefaultCancel() {}
  function ReadableStreamUnderlyingSourceDefaultPull() { return Promise.resolve(); }
  function ReadableStreamUnderlyingSourceDefaultStart() {}

  class ReadableStreamDefaultController {
    constructor(stream) {
      this._stream = stream;
      this._closeRequested = false;
    }

    get desiredSize() {
      return this._stream._state === 'readable'
        ? this._stream._hwm - this._stream._queue.length
        : null;
    }

    close() {
      if (this._closeRequested) return;
      this._closeRequested = true;
      if (this._stream._state !== 'readable') return;
      this._stream._state = 'closed';
      this._stream._notifyReaders();
    }

    enqueue(chunk) {
      if (this._stream._state !== 'readable') return;
      this._stream._queue.push(chunk);
      this._stream._notifyReaders();
    }

    error(e) {
      if (this._stream._state !== 'readable') return;
      this._stream._state = 'errored';
      this._stream._storedError = e;
      this._stream._notifyReaders();
    }
  }

  class ReadableStreamDefaultReader {
    constructor(stream) {
      if (stream._state === 'errored') {
        throw stream._storedError;
      }
      if (stream._reader) {
        throw new TypeError('ReadableStream already has a reader');
      }
      stream._reader = this;
      this._stream = stream;
      this._isClosed = false;
      this._readResolve = null;
      this._readReject = null;

      // If stream already closed/errored, resolve/reject immediately
      if (stream._state === 'closed') {
        this._closed = Promise.resolve();
      } else if (stream._state === 'errored') {
        this._closed = Promise.reject(stream._storedError);
      } else {
        this._closed = new Promise(function(resolve, reject) {
          this._closedResolve = resolve;
          this._closedReject = reject;
        }.bind(this));
      }
    }

    get closed() { return this._closed; }

    read() {
      if (this._isClosed) {
        return Promise.resolve({ done: true, value: undefined });
      }
      var stream = this._stream;
      if (stream._state === 'errored') {
        return Promise.reject(stream._storedError);
      }
      if (stream._queue.length > 0) {
        var chunk = stream._queue.shift();
        return Promise.resolve({ done: false, value: chunk });
      }
      if (stream._state === 'closed') {
        this._isClosed = true;
        return Promise.resolve({ done: true, value: undefined });
      }

      return new Promise(function(resolve, reject) {
        stream._pendingReads.push({ resolve: resolve, reject: reject });
        stream._maybePull();
      });
    }

    releaseLock() {
      if (this._stream._reader !== this) return;
      this._stream._reader = null;
      this._isClosed = true;
    }
  }

  // ================================================================
  // BYOB (Bring Your Own Buffer) — ECMA-429 Streams
  // ReadableByteStreamController / ReadableStreamBYOBReader /
  // ReadableStreamBYOBRequest
  // ================================================================

  // A pending BYOB read request handed to the source via controller.byobRequest.
  class ReadableStreamBYOBRequest {
    constructor(entry) {
      this._entry = entry;   // {resolve, reject, view, stream}
    }

    get view() {
      return this._entry ? this._entry.view : null;
    }

    respond(bytesWritten) {
      var entry = this._entry;
      if (!entry) throw new TypeError('Invalid ReadableStreamBYOBRequest');
      this._entry = null;
      var view = entry.view;
      var value = new view.constructor(view.buffer, view.byteOffset, bytesWritten);
      _removePending(entry);
      entry.resolve({ done: false, value: value });
    }

    respondWithNewView(newView) {
      var entry = this._entry;
      if (!entry) throw new TypeError('Invalid ReadableStreamBYOBRequest');
      this._entry = null;
      _removePending(entry);
      entry.resolve({ done: false, value: newView });
    }
  }

  function _removePending(entry) {
    var s = entry.stream;
    var idx = s._pendingReads.indexOf(entry);
    if (idx >= 0) s._pendingReads.splice(idx, 1);
  }

  class ReadableStreamBYOBReader {
    constructor(stream) {
      if (stream._state === 'errored') {
        throw stream._storedError;
      }
      if (stream._reader) {
        throw new TypeError('ReadableStream already has a reader');
      }
      stream._reader = this;
      this._stream = stream;
      this._isClosed = false;

      if (stream._state === 'closed') {
        this._closed = Promise.resolve();
      } else if (stream._state === 'errored') {
        this._closed = Promise.reject(stream._storedError);
      } else {
        this._closed = new Promise(function(resolve, reject) {
          this._closedResolve = resolve;
          this._closedReject = reject;
        }.bind(this));
      }
    }

    get closed() { return this._closed; }

    read(view) {
      if (!ArrayBuffer.isView(view) || view.byteLength === 0) {
        return Promise.reject(new TypeError('BYOB read requires a non-empty ArrayBufferView'));
      }
      if (this._isClosed) {
        return Promise.resolve({ done: true, value: view });
      }
      var stream = this._stream;
      if (stream._state === 'errored') {
        return Promise.reject(stream._storedError);
      }
      // Try to satisfy immediately from the queue / closed state
      var immediate;
      if (stream._queue.length > 0) {
        immediate = stream._fillFromQueue(view);
      } else if (stream._state === 'closed') {
        this._isClosed = true;
        immediate = { done: true, value: view };
      }
      if (immediate) return Promise.resolve(immediate);

      // Otherwise register a pending BYOB request (source fills via controller.byobRequest)
      return new Promise(function(resolve, reject) {
        stream._pendingReads.push({ resolve: resolve, reject: reject, view: view, stream: stream });
        stream._maybePull();
      });
    }

    releaseLock() {
      if (this._stream._reader !== this) return;
      this._stream._reader = null;
      this._isClosed = true;
    }
  }

  class ReadableByteStreamController {
    constructor(stream) {
      this._stream = stream;
      this._closeRequested = false;
    }

    get byobRequest() {
      var s = this._stream;
      for (var i = 0; i < s._pendingReads.length; i++) {
        if (s._pendingReads[i].view) {
          return new ReadableStreamBYOBRequest(s._pendingReads[i]);
        }
      }
      return null;
    }

    get desiredSize() {
      return this._stream._state === 'readable'
        ? this._stream._hwm - this._stream._queueBytes()
        : null;
    }

    close() {
      if (this._closeRequested) return;
      this._closeRequested = true;
      if (this._stream._state !== 'readable') return;
      this._stream._state = 'closed';
      this._stream._notifyReaders();
    }

    enqueue(chunk) {
      if (this._stream._state !== 'readable') return;
      if (chunk instanceof ArrayBuffer) {
        chunk = new Uint8Array(chunk);
      } else if (ArrayBuffer.isView(chunk)) {
        chunk = new Uint8Array(chunk.buffer, chunk.byteOffset, chunk.byteLength);
      } else {
        throw new TypeError('ReadableByteStreamController.enqueue requires an ArrayBufferView or ArrayBuffer');
      }
      this._stream._queue.push(chunk);
      this._stream._notifyReaders();
    }

    error(e) {
      if (this._stream._state !== 'readable') return;
      this._stream._state = 'errored';
      this._stream._storedError = e;
      this._stream._notifyReaders();
    }
  }

  class ReadableStream {
    constructor(underlyingSource, strategy) {
      underlyingSource = underlyingSource || {};
      this._state = 'readable';
      this._reader = null;
      this._queue = [];
      this._type = underlyingSource.type === 'bytes' ? 'bytes' : undefined;
      this._hwm = (strategy && strategy.highWaterMark) || 1;
      this._storedError = null;
      this._pendingReads = [];
      this._pulling = false;

      this._controller = this._type === 'bytes'
        ? new ReadableByteStreamController(this)
        : new ReadableStreamDefaultController(this);

      var source = underlyingSource;
      this._cancel = source.cancel || ReadableStreamUnderlyingSourceDefaultCancel;
      this._pull = source.pull || ReadableStreamUnderlyingSourceDefaultPull;

      // Call start
      if (source.start) {
        source.start(this._controller);
      }
    }

    _notifyReaders() {
      // Resolve pending reads (default + BYOB)
      while (this._pendingReads.length > 0) {
        var entry = this._pendingReads[0];
        if (entry.view) {
          // BYOB read: fill the caller-supplied view from queued bytes
          if (this._queue.length > 0) {
            this._pendingReads.shift();
            entry.resolve(this._fillFromQueue(entry.view));
          } else if (this._state === 'closed') {
            this._pendingReads.shift();
            entry.resolve({ done: true, value: entry.view });
          } else if (this._state === 'errored') {
            this._pendingReads.shift();
            entry.reject(this._storedError);
          } else {
            break;
          }
        } else if (this._queue.length > 0) {
          this._pendingReads.shift();
          var chunk = this._queue.shift();
          entry.resolve({ done: false, value: chunk });
        } else if (this._state === 'closed') {
          this._pendingReads.shift();
          entry.resolve({ done: true, value: undefined });
        } else if (this._state === 'errored') {
          this._pendingReads.shift();
          entry.reject(this._storedError);
        } else {
          break;
        }
      }

      // Notify reader closed/errored
      if (this._reader) {
        if (this._state === 'closed' && this._reader._closedResolve) {
          this._reader._closedResolve();
          this._reader._closedResolve = null;
          this._reader._closedReject = null;
        } else if (this._state === 'errored' && this._reader._closedReject) {
          this._reader._closedReject(this._storedError);
          this._reader._closedResolve = null;
          this._reader._closedReject = null;
        }
      }

      // Pull more data if needed
      this._maybePull();
    }

    _queueBytes() {
      var total = 0;
      for (var i = 0; i < this._queue.length; i++) {
        total += this._queue[i].length;
      }
      return total;
    }

    _fillFromQueue(view) {
      var chunk = this._queue[0]; // Uint8Array
      var n = Math.min(chunk.length, view.byteLength);
      var dst = new Uint8Array(view.buffer, view.byteOffset, view.byteLength);
      dst.set(chunk.subarray(0, n));
      if (n === chunk.length) {
        this._queue.shift();
      } else {
        this._queue[0] = chunk.subarray(n);
      }
      return { done: false, value: new view.constructor(view.buffer, view.byteOffset, n) };
    }

    _maybePull() {
      if (this._state !== 'readable' || !this._pull || this._pulling) return;
      var hasByobPending = false;
      for (var i = 0; i < this._pendingReads.length; i++) {
        if (this._pendingReads[i].view) { hasByobPending = true; break; }
      }
      var needsMore = this._type === 'bytes'
        ? (hasByobPending || this._queueBytes() < this._hwm)
        : (this._queue.length < this._hwm);
      if (!needsMore) return;
      this._pulling = true;
      try {
        var result = this._pull(this._controller);
        if (result && typeof result.catch === 'function') {
          result.catch(function(e) {
            this._controller.error(e);
          }.bind(this));
        }
      } catch (e) {
        this._controller.error(e);
      } finally {
        this._pulling = false;
      }
    }

    get locked() { return this._reader !== null; }

    getReader(options) {
      if (this._reader) {
        throw new TypeError('ReadableStream already locked');
      }
      var mode = options && options.mode;
      if (mode === 'byob') {
        if (this._type !== 'bytes') {
          throw new TypeError('ReadableStream is not a byte stream');
        }
        return new ReadableStreamBYOBReader(this);
      }
      return new ReadableStreamDefaultReader(this);
    }

    cancel(reason) {
      if (this._state !== 'readable') return Promise.resolve();
      this._state = 'closed';
      this._queue = [];
      try { this._cancel(reason); } catch (e) {}
      this._notifyReaders();
      return Promise.resolve();
    }

    tee() {
      var source = this;
      var reader = source.getReader();
      var branch1Controller, branch2Controller;
      var branch1Closed = false, branch2Closed = false;
      var reading = false;

      function pullAndDispatch() {
        if (reading) return;
        reading = true;
        reader.read().then(function(result) {
          reading = false;
          if (result.done) {
            if (!branch1Closed && branch1Controller) branch1Controller.close();
            if (!branch2Closed && branch2Controller) branch2Controller.close();
            return;
          }
          if (!branch1Closed && branch1Controller) branch1Controller.enqueue(result.value);
          if (!branch2Closed && branch2Controller) branch2Controller.enqueue(result.value);
          // If both branches still need data and controllers want more, keep pulling
          if (!branch1Closed && !branch2Closed) {
            pullAndDispatch();
          }
        }).catch(function(e) {
          reading = false;
          if (branch1Controller) branch1Controller.error(e);
          if (branch2Controller) branch2Controller.error(e);
        });
      }

      function createBranch() {
        return new ReadableStream({
          start: function(controller) {
            // controller will be set after construction
          },
          pull: function(controller) {
            pullAndDispatch();
          },
          cancel: function() {
            // Release lock when both branches are cancelled
          }
        });
      }

      var branch1 = createBranch();
      var branch2 = createBranch();

      // Grab controllers from the branches (they're the first reader's stream)
      branch1Controller = branch1._controller;
      branch2Controller = branch2._controller;

      return [branch1, branch2];
    }

    pipeTo(dest) {
      var reader = this.getReader();
      var writer = dest.getWriter();

      function pump() {
        return reader.read().then(function(result) {
          if (result.done) {
            reader.releaseLock();
            return writer.close();
          }
          return writer.write(result.value).then(pump);
        });
      }

      return pump().catch(function(e) {
        try { reader.releaseLock(); } catch (x) {}
        try { writer.abort(e); } catch (x) {}
        throw e;
      });
    }

    pipeThrough(transform) {
      this.pipeTo(transform.writable);
      return transform.readable;
    }
  }

  // ReadableStream async iterator: for await (const chunk of stream)
  try {
    if (Symbol.asyncIterator) {
      ReadableStream.prototype[Symbol.asyncIterator] = function() {
        var reader = this.getReader();
        return {
          next: function() { return reader.read(); },
          return: function(value) { reader.releaseLock(); return Promise.resolve({ done: true, value: value }); }
        };
      };
    }
  } catch(e) {}

  // ================================================================
  // WritableStream
  // ================================================================

  function WritableStreamDefaultController(stream) {
    this._stream = stream;
  }

  WritableStreamDefaultController.prototype.error = function(e) {
    this._stream._error(e);
  };

  function WritableStreamDefaultWriter(stream) {
    this._stream = stream;
    this._released = false;
    var self = this;
    this._closedPromise = new Promise(function(resolve, reject) {
      self._closedResolve = resolve;
      self._closedReject = reject;
    });
  }

  Object.defineProperty(WritableStreamDefaultWriter.prototype, 'closed', {
    get: function() { return this._closedPromise; }
  });

  Object.defineProperty(WritableStreamDefaultWriter.prototype, 'ready', {
    get: function() { return Promise.resolve(); }
  });

  Object.defineProperty(WritableStreamDefaultWriter.prototype, 'desiredSize', {
    get: function() { return null; }
  });

  WritableStreamDefaultWriter.prototype.write = function(chunk) {
    return this._stream._writeChunk(chunk);
  };

  WritableStreamDefaultWriter.prototype.close = function() {
    return this._stream._closeStream();
  };

  WritableStreamDefaultWriter.prototype.abort = function(reason) {
    return this._stream._abortStream(reason);
  };

  WritableStreamDefaultWriter.prototype.releaseLock = function() {
    if (this._stream._writer !== this) return;
    this._stream._writer = null;
    this._released = true;
  };

  class WritableStream {
    constructor(underlyingSink, strategy) {
      underlyingSink = underlyingSink || {};
      this._state = 'writable';
      this._storedError = null;
      this._writer = null;
      this._writePromise = null;
      this._closePromise = null;
      this._readyPromise = Promise.resolve();

      this._controller = new WritableStreamDefaultController(this);

      this._start = underlyingSink.start;
      this._write = underlyingSink.write || function() { return Promise.resolve(); };
      this._close = underlyingSink.close || function() { return Promise.resolve(); };
      this._abort = underlyingSink.abort || function() { return Promise.resolve(); };

      if (this._start) {
        var result = this._start(this._controller);
        if (result && typeof result.then === 'function') {
          this._readyPromise = result;
        }
      }

      this._closedPromise = new Promise(function(resolve, reject) {
        this._closedResolve = resolve;
        this._closedReject = reject;
      }.bind(this));
    }

    get locked() { return this._writer !== null; }

    getWriter() {
      if (this._writer) {
        throw new TypeError('WritableStream already has a writer');
      }
      var writer = new WritableStreamDefaultWriter(this);
      this._writer = writer;
      return writer;
    }

    _writeChunk(chunk) {
      if (this._state === 'errored') return Promise.reject(this._storedError);
      if (this._state === 'closed') return Promise.reject(new TypeError('Stream is closed'));

      var self = this;
      try {
        var result = self._write(chunk, self._controller);
        if (!result || typeof result.then !== 'function') {
          result = Promise.resolve(result);
        }
        return result;
      } catch (e) {
        return Promise.reject(e);
      }
    }

    _closeStream() {
      if (this._state !== 'writable') {
        return Promise.reject(new TypeError('Stream is not writable'));
      }
      this._state = 'closed';
      var self = this;
      try {
        var result = self._close();
        if (!result || typeof result.then !== 'function') {
          result = Promise.resolve(result);
        }
        return result.then(function() {
          self._closedResolve();
        });
      } catch (e) {
        self._closedReject(e);
        return Promise.reject(e);
      }
    }

    _abortStream(reason) {
      this._state = 'errored';
      this._storedError = reason;
      var self = this;
      try {
        var result = self._abort(reason);
        if (!result || typeof result.then !== 'function') {
          result = Promise.resolve(result);
        }
        return result.then(function() {
          self._closedReject(reason);
        });
      } catch (e) {
        self._closedReject(e);
        return Promise.reject(e);
      }
    }

    _error(e) {
      if (this._state !== 'writable') return;
      this._state = 'errored';
      this._storedError = e;
      this._closedReject(e);
    }
  }

  // ================================================================
  // TransformStream
  // ================================================================

  class TransformStreamDefaultController {
    constructor() {
      this._readableController = null;
    }
    get desiredSize() {
      return this._readableController ? this._readableController.desiredSize : 0;
    }
    enqueue(chunk) {
      if (this._readableController) this._readableController.enqueue(chunk);
    }
    error(reason) {
      if (this._readableController) this._readableController.error(reason);
    }
    terminate() {
      if (this._readableController) this._readableController.close();
    }
  }

  class TransformStream {
    constructor(transformer) {
      transformer = transformer || {};

      var self = this;
      var readableController;
      var tsController = new TransformStreamDefaultController();

      self._readable = new ReadableStream({
        start: function(c) {
          readableController = c;
          tsController._readableController = c;
        },
        pull: function() {},
        cancel: function() {}
      });

      self._writable = new WritableStream({
        write: function(chunk) {
          if (transformer.transform) {
            return transformer.transform(chunk, tsController);
          }
          // Default: identity transform
          tsController.enqueue(chunk);
          return Promise.resolve();
        },
        close: function() {
          if (transformer.flush) {
            return transformer.flush(tsController);
          }
          tsController.terminate();
          return Promise.resolve();
        },
        abort: function(reason) {
          tsController.error(reason);
          return Promise.resolve();
        }
      });
    }

    get readable() { return this._readable; }
    get writable() { return this._writable; }
  }

  // ================================================================
  // CompressionStream / DecompressionStream
  //
  // Native compression/decompression via pal.nativeCompress /
  // pal.nativeDecompress (registered by the compress extension).
  // If the extension is not loaded, these classes throw.
  // ================================================================

  class CompressionStream {
    constructor(format) {
      format = format || 'gzip';
      if (format !== 'gzip' && format !== 'deflate' && format !== 'deflate-raw') {
        throw new Error('CompressionStream: unsupported format: ' + format);
      }
      this._format = format;

      var self = this;
      self._readable = new ReadableStream({
        start: function() {},
        pull: function() {}
      });

      var chunks = [];

      self._writable = new WritableStream({
        write: function(chunk) {
          chunks.push(chunk);
          return Promise.resolve();
        },
        close: function() {
          // Concatenate all chunks
          var totalLen = 0;
          for (var i = 0; i < chunks.length; i++) {
            totalLen += chunks[i].length || chunks[i].byteLength || 0;
          }
          var combined = new Uint8Array(totalLen);
          var offset = 0;
          for (var i = 0; i < chunks.length; i++) {
            var c = chunks[i] instanceof Uint8Array ? chunks[i] : new Uint8Array(chunks[i]);
            combined.set(c, offset);
            offset += c.length;
          }

          if (typeof pal.nativeCompress !== 'function') {
            self._readable._controller.error(new TypeError('Native compression extension not available'));
            return Promise.resolve();
          }

          try {
            var compressed = pal.nativeCompress(combined, self._format);
            self._readable._controller.enqueue(compressed);
            self._readable._controller.close();
          } catch (e) {
            self._readable._controller.error(e);
          }
          return Promise.resolve();
        }
      });
    }

    get readable() { return this._readable; }
    get writable() { return this._writable; }
  }

  class DecompressionStream {
    constructor(format) {
      format = format || 'gzip';
      if (format !== 'gzip' && format !== 'deflate' && format !== 'deflate-raw') {
        throw new Error('DecompressionStream: unsupported format: ' + format);
      }
      this._format = format;

      var self = this;
      var chunks = [];

      self._readable = new ReadableStream({
        start: function() {},
        pull: function() {}
      });

      self._writable = new WritableStream({
        write: function(chunk) {
          chunks.push(chunk);
          return Promise.resolve();
        },
        close: function() {
          var totalLen = 0;
          for (var i = 0; i < chunks.length; i++) {
            totalLen += chunks[i].length || chunks[i].byteLength || 0;
          }
          var combined = new Uint8Array(totalLen);
          var offset = 0;
          for (var i = 0; i < chunks.length; i++) {
            var c = chunks[i] instanceof Uint8Array ? chunks[i] : new Uint8Array(chunks[i]);
            combined.set(c, offset);
            offset += c.length;
          }

          if (typeof pal.nativeDecompress !== 'function') {
            self._readable._controller.error(new TypeError('Native compression extension not available'));
            return Promise.resolve();
          }

          try {
            var decompressed = pal.nativeDecompress(combined, self._format);
            self._readable._controller.enqueue(decompressed);
            self._readable._controller.close();
          } catch (e) {
            self._readable._controller.error(e);
          }
          return Promise.resolve();
        }
      });
    }

    get readable() { return this._readable; }
    get writable() { return this._writable; }
  }

  // ================================================================
  // Queuing strategies
  // ================================================================

  class ByteLengthQueuingStrategy {
    constructor(options) {
      this._highWaterMark = options?.highWaterMark ?? 1;
    }
    get highWaterMark() { return this._highWaterMark; }
    size(chunk) {
      return chunk?.byteLength ?? 0;
    }
  }

  class CountQueuingStrategy {
    constructor(options) {
      this._highWaterMark = options?.highWaterMark ?? 1;
    }
    get highWaterMark() { return this._highWaterMark; }
    size() { return 1; }
  }

  // ================================================================
  // TextEncoderStream / TextDecoderStream
  // ================================================================

  class TextEncoderStream {
    constructor() {
      this.encoding = 'utf-8';

      var self = this;
      self._readable = new ReadableStream({
        start: function() {},
        pull: function() {}
      });

      self._writable = new WritableStream({
        write: function(chunk) {
          if (typeof chunk === 'string') {
            var encoded = new TextEncoder().encode(chunk);
            self._readable._controller.enqueue(encoded);
          } else {
            self._readable._controller.enqueue(chunk);
          }
          return Promise.resolve();
        },
        close: function() {
          self._readable._controller.close();
          return Promise.resolve();
        }
      });
    }

    get readable() { return this._readable; }
    get writable() { return this._writable; }
  }

  class TextDecoderStream {
    constructor(label, options) {
      label = label || 'utf-8';
      options = options || {};
      this.encoding = label.toLowerCase();
      this.fatal = options.fatal || false;
      this.ignoreBOM = options.ignoreBOM || false;

      var decoder = new TextDecoder(label, { fatal: this.fatal, ignoreBOM: this.ignoreBOM });
      var self = this;

      self._readable = new ReadableStream({
        start: function() {},
        pull: function() {}
      });

      self._writable = new WritableStream({
        write: function(chunk) {
          var decoded = decoder.decode(chunk, { stream: true });
          if (decoded) {
            self._readable._controller.enqueue(decoded);
          }
          return Promise.resolve();
        },
        close: function() {
          var decoded = decoder.decode();
          if (decoded) {
            self._readable._controller.enqueue(decoded);
          }
          self._readable._controller.close();
          return Promise.resolve();
        }
      });
    }

    get readable() { return this._readable; }
    get writable() { return this._writable; }
  }

  // ================================================================
  // Register on globalThis
  // ================================================================

  globalThis.ReadableStream = ReadableStream;
  globalThis.ReadableStreamDefaultController = ReadableStreamDefaultController;
  globalThis.ReadableStreamDefaultReader = ReadableStreamDefaultReader;
  globalThis.ReadableByteStreamController = ReadableByteStreamController;
  globalThis.ReadableStreamBYOBReader = ReadableStreamBYOBReader;
  globalThis.ReadableStreamBYOBRequest = ReadableStreamBYOBRequest;
  globalThis.WritableStream = WritableStream;
  globalThis.WritableStreamDefaultController = WritableStreamDefaultController;
  globalThis.WritableStreamDefaultWriter = WritableStreamDefaultWriter;
  globalThis.TransformStream = TransformStream;
  globalThis.TransformStreamDefaultController = TransformStreamDefaultController;
  globalThis.ByteLengthQueuingStrategy = ByteLengthQueuingStrategy;
  globalThis.CountQueuingStrategy = CountQueuingStrategy;
  globalThis.CompressionStream = CompressionStream;
  globalThis.DecompressionStream = DecompressionStream;
  globalThis.TextEncoderStream = TextEncoderStream;
  globalThis.TextDecoderStream = TextDecoderStream;
}

/**
 * qwrt polyfill: EventSource
 *
 * Simplified WHATWG EventSource API for server-sent events.
 * Uses pal.httpRequestStream for HTTP streaming.
 *
 * Supports: data: / event: / id: / retry: fields.
 * Auto-reconnect with configurable delay.
 * No CORS / credentials / withCredentials handling.
 */

export function setupEventSource(pal) {
  if (typeof pal.httpRequestStream !== 'function') return;

  class EventSource {
    constructor(url, eventSourceInitDict) {
      this._url = url;
      this._reconnectDelay = 3000;
      this._lastEventId = '';
      this._readyState = 0; // CONNECTING
      this._closed = false;
      this._buffer = '';

      this.onopen = null;
      this.onmessage = null;
      this.onerror = null;

      this._connect();
    }

    get CONNECTING() { return 0; }
    get OPEN() { return 1; }
    get CLOSED() { return 2; }

    get url() { return this._url; }
    get readyState() { return this._readyState; }
    get withCredentials() { return false; }

    _connect() {
      if (this._closed) return;
      this._readyState = 0;
      this._buffer = '';

      var self = this;
      var headersJson = JSON.stringify({
        'Accept': 'text/event-stream',
        'Cache-Control': 'no-cache'
      });

      function onHeaders(status) {
        if (status === 200) {
          self._readyState = 1;
          if (typeof self.onopen === 'function') {
            try { self.onopen(new Event('open')); } catch(e) {}
          }
        }
      }

      function onData(chunk) {
        if (self._readyState !== 1) return;
        // Convert ArrayBuffer to string
        var uint8 = new Uint8Array(chunk);
        var text = '';
        for (var i = 0; i < uint8.length; i++) {
          text += String.fromCharCode(uint8[i]);
        }
        self._buffer += text;
        self._processBuffer();
      }

      function onEnd(errorStatus) {
        if (self._closed) return;
        self._readyState = 2;
        // Emit error event
        var ev = new Event('error');
        if (typeof self.onerror === 'function') {
          try { self.onerror(ev); } catch(e) {}
        }
        // Auto-reconnect
        if (!self._closed) {
          setTimeout(function() { self._connect(); }, self._reconnectDelay);
        }
      }

      pal.httpRequestStream(this._url, 'GET', headersJson, '', onHeaders, onData, onEnd);
    }

    _processBuffer() {
      var lines = this._buffer.split('\n');
      // Keep the last incomplete line in the buffer
      this._buffer = lines.pop() || '';

      var eventType = null;
      var data = [];
      var id = null;

      for (var i = 0; i < lines.length; i++) {
        var line = lines[i];
        if (line === '') {
          // Empty line dispatches the event
          if (data.length > 0) {
            var msgEvent = new MessageEvent(eventType || 'message', {
              data: data.join('\n'),
              lastEventId: id || this._lastEventId
            });
            if (typeof this.onmessage === 'function') {
              try { this.onmessage(msgEvent); } catch(e) {}
            }
            this._lastEventId = id || this._lastEventId;
          }
          eventType = null;
          data = [];
          id = null;
        } else if (line.startsWith('data:')) {
          data.push(line.slice(5).trim());
        } else if (line.startsWith('event:')) {
          eventType = line.slice(6).trim();
        } else if (line.startsWith('id:')) {
          id = line.slice(3).trim();
        } else if (line.startsWith('retry:')) {
          var ms = parseInt(line.slice(6).trim(), 10);
          if (!isNaN(ms) && ms > 0) this._reconnectDelay = ms;
        }
        // Lines starting with ':' are comments (SSE heartbeat), ignored
      }
    }

    close() {
      this._closed = true;
      this._readyState = 2;
      this._buffer = '';
    }
  }

  globalThis.EventSource = EventSource;
}
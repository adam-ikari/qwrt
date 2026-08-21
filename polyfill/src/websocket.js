/**
 * qwrt polyfill: WebSocket (client)
 *
 * WHATWG WebSocket API for client-side connections.
 * Uses pal.wsConnect for the C-level TCP + WebSocket handshake.
 */

export function setupWebSocket(pal) {
  if (typeof pal.wsConnect !== 'function') return;

  var CONNECTING = 0;
  var OPEN = 1;
  var CLOSING = 2;
  var CLOSED = 3;

  class WebSocket {
    constructor(url, protocols) {
      this._url = url;
      this._readyState = CONNECTING;
      this._onopen = null;
      this._onmessage = null;
      this._onerror = null;
      this._onclose = null;
      this._conn = null;
      this._protocol = '';

      var self = this;
      this._conn = pal.wsConnect(url, {
        onopen: function() {
          self._readyState = OPEN;
          if (typeof self._onopen === 'function') {
            try { self._onopen(new Event('open')); } catch(e) {}
          }
        },
        onmessage: function(data) {
          if (typeof self._onmessage === 'function') {
            var ev = new MessageEvent('message', { data: data });
            try { self._onmessage(ev); } catch(e) {}
          }
        },
        onerror: function(err) {
          self._readyState = CLOSED;
          if (typeof self._onerror === 'function') {
            try { self._onerror(new Event('error')); } catch(e) {}
          }
        },
        onclose: function(code, reason) {
          self._readyState = CLOSED;
          if (typeof self._onclose === 'function') {
            var ev = new CloseEvent('close', { code: code, reason: reason, wasClean: true });
            try { self._onclose(ev); } catch(e) {}
          }
        }
      });
    }

    get url() { return this._url; }
    get readyState() { return this._readyState; }
    get protocol() { return this._protocol; }
    get CONNECTING() { return CONNECTING; }
    get OPEN() { return OPEN; }
    get CLOSING() { return CLOSING; }
    get CLOSED() { return CLOSED; }

    get onopen() { return this._onopen; }
    set onopen(fn) { this._onopen = fn; }
    get onmessage() { return this._onmessage; }
    set onmessage(fn) { this._onmessage = fn; }
    get onerror() { return this._onerror; }
    set onerror(fn) { this._onerror = fn; }
    get onclose() { return this._onclose; }
    set onclose(fn) { this._onclose = fn; }

    send(data) {
      if (this._readyState !== OPEN) return;
      // TODO: implement actual send via pal
    }

    close(code, reason) {
      if (this._readyState === CLOSING || this._readyState === CLOSED) return;
      this._readyState = CLOSING;
      // TODO: implement actual close via pal
    }
  }

  globalThis.WebSocket = WebSocket;
  globalThis.CloseEvent = globalThis.CloseEvent || class CloseEvent extends Event {
    constructor(type, init) {
      super(type, init);
      this.code = (init && init.code) || 1000;
      this.reason = (init && init.reason) || '';
      this.wasClean = (init && init.wasClean) || false;
    }
  };
}
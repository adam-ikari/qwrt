/**
 * qwrt polyfill: BroadcastChannel
 *
 * WHATWG BroadcastChannel API. Allows same-name channel instances to
 * communicate via postMessage. Built on EventTarget + MessageEvent.
 *
 * Pure JS — no PAL primitives needed.
 * Requires: EventTarget (event-target.js), MessageEvent (message-channel.js).
 */

export function setupBroadcastChannel() {
  // Global channel registry: name -> Set of live BroadcastChannel instances.
  var channels = new Map();

  class BroadcastChannel extends EventTarget {
    constructor(name) {
      super();
      if (typeof name !== 'string') name = String(name);
      this._name = name;
      this._closed = false;
      this._onmessage = null;
      if (!channels.has(name)) channels.set(name, new Set());
      channels.get(name).add(this);
    }

    get name() { return this._name; }

    get onmessage() { return this._onmessage; }
    set onmessage(fn) {
      if (this._onmessage) this.removeEventListener('message', this._onmessage);
      this._onmessage = fn;
      if (typeof fn === 'function') this.addEventListener('message', fn);
    }

    postMessage(message) {
      if (this._closed) return;
      var peers = channels.get(this._name);
      if (!peers) return;
      // Structured-clone the message so receivers get an independent copy
      // (spec requirement). Fall back to the reference if cloning throws.
      var data;
      try {
        data = (typeof structuredClone === 'function')
          ? structuredClone(message) : message;
      } catch (e) {
        data = message;
      }
      peers.forEach(function(peer) {
        if (peer !== this && !peer._closed) {
          peer.dispatchEvent(new MessageEvent('message', { data: data }));
        }
      }, this);
    }

    close() {
      if (this._closed) return;
      this._closed = true;
      var peers = channels.get(this._name);
      if (peers) {
        peers.delete(this);
        if (peers.size === 0) channels.delete(this._name);
      }
      if (this._onmessage) {
        this.removeEventListener('message', this._onmessage);
        this._onmessage = null;
      }
    }
  }

  globalThis.BroadcastChannel = BroadcastChannel;
}

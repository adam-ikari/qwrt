/**
 * qwrt polyfill: MessageChannel, MessagePort, MessageEvent
 *
 * TC55/ECMA-429 requires MessageChannel for structured communication
 * between execution contexts. MessagePort extends EventTarget.
 *
 * Pure JS - no PAL primitives needed.
 *
 * Depends on: EventTarget (must be loaded after event-target.js).
 */

export function setupMessageChannel() {
  if (typeof globalThis.EventTarget !== 'function') {
    throw new Error('MessagePort requires EventTarget to be loaded first');
  }

  /**
   * MessageEvent
   *
   * Event carrying cross-context message data.
   */
  class MessageEvent extends Event {
    constructor(type, options) {
      super(type, options);
      this._data = options?.data ?? null;
      this._origin = options?.origin ?? '';
      this._lastEventId = options?.lastEventId ?? '';
      this._source = options?.source ?? null;
      this._ports = options?.ports ?? [];
    }

    get data() { return this._data; }
    get origin() { return this._origin; }
    get lastEventId() { return this._lastEventId; }
    get source() { return this._source; }
    get ports() { return this._ports; }
  }

  /**
   * MessagePort
   *
   * One end of a MessageChannel. Extends EventTarget.
   * postMessage sends data to the entangled port.
   */
  class MessagePort extends EventTarget {
    constructor() {
      super();
      this._entangledPort = null;
      this._started = false;
      this._messageQueue = [];
      this._onmessage = null;
      this._onmessageerror = null;
    }

    get onmessage() { return this._onmessage; }
    set onmessage(fn) {
      if (this._onmessage) {
        this.removeEventListener('message', this._onmessage);
      }
      this._onmessage = fn;
      if (fn) {
        this.addEventListener('message', fn);
      }
      this._start();
    }

    get onmessageerror() { return this._onmessageerror; }
    set onmessageerror(fn) {
      if (this._onmessageerror) {
        this.removeEventListener('messageerror', this._onmessageerror);
      }
      this._onmessageerror = fn;
      if (fn) {
        this.addEventListener('messageerror', fn);
      }
    }

    postMessage(message, transfer) {
      if (!this._entangledPort) return;

      // Structured clone the message data
      var data;
      try {
        data = typeof globalThis.structuredClone === 'function'
          ? globalThis.structuredClone(message, transfer ? { transfer: transfer } : undefined)
          : JSON.parse(JSON.stringify(message));
      } catch (e) {
        // If structured clone fails, send a messageerror
        var errorEvent = new MessageEvent('messageerror', { data: e });
        this._entangledPort.dispatchEvent(errorEvent);
        return;
      }

      var event = new MessageEvent('message', { data: data, ports: [] });

      if (this._entangledPort._started) {
        this._entangledPort.dispatchEvent(event);
      } else {
        this._entangledPort._messageQueue.push(event);
      }
    }

    start() {
      this._start();
    }

    _start() {
      if (this._started) return;
      this._started = true;

      // Flush queued messages
      for (var i = 0; i < this._messageQueue.length; i++) {
        this.dispatchEvent(this._messageQueue[i]);
      }
      this._messageQueue = [];
    }

    close() {
      this._entangledPort = null;
      this._started = false;
      this._messageQueue = [];
    }
  }

  /**
   * MessageChannel
   *
   * Creates two entangled MessagePorts.
   */
  class MessageChannel {
    constructor() {
      this._port1 = new MessagePort();
      this._port2 = new MessagePort();
      this._port1._entangledPort = this._port2;
      this._port2._entangledPort = this._port1;
    }

    get port1() { return this._port1; }
    get port2() { return this._port2; }
  }

  globalThis.MessageChannel = MessageChannel;
  globalThis.MessagePort = MessagePort;
  globalThis.MessageEvent = MessageEvent;
}

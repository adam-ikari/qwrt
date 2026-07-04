/**
 * qwrt polyfill: Event and EventTarget
 *
 * Standard DOM Event model implementation.
 * Pure JS - no PAL primitives needed.
 */

export function setupEventTarget() {
  /**
   * Event class
   *
   * Represents an event that can be dispatched to an EventTarget.
   */
  class Event {
    constructor(type, options) {
      if (typeof type !== 'string') {
        throw new TypeError('Event type must be a string');
      }

      this._type = type;
      this._bubbles = options?.bubbles ?? false;
      this._cancelable = options?.cancelable ?? false;
      this._composed = options?.composed ?? false;
      this._defaultPrevented = false;
      this._propagationStopped = false;
      this._immediatePropagationStopped = false;
      this._target = null;
      this._currentTarget = null;
      this._eventPhase = Event.NONE;
      this._timeStamp = Date.now();
    }

    get type() { return this._type; }
    get bubbles() { return this._bubbles; }
    get cancelable() { return this._cancelable; }
    get composed() { return this._composed; }
    get defaultPrevented() { return this._defaultPrevented; }
    get target() { return this._target; }
    get currentTarget() { return this._currentTarget; }
    get eventPhase() { return this._eventPhase; }
    get timeStamp() { return this._timeStamp; }

    get NONE() { return Event.NONE; }
    get CAPTURING_PHASE() { return Event.CAPTURING_PHASE; }
    get AT_TARGET() { return Event.AT_TARGET; }
    get BUBBLING_PHASE() { return Event.BUBBLING_PHASE; }

    preventDefault() {
      if (this._cancelable) {
        this._defaultPrevented = true;
      }
    }

    stopPropagation() {
      this._propagationStopped = true;
    }

    stopImmediatePropagation() {
      this._propagationStopped = true;
      this._immediatePropagationStopped = true;
    }

    // Internal methods for EventTarget
    _initEvent(type, bubbles, cancelable) {
      this._type = type;
      this._bubbles = bubbles;
      this._cancelable = cancelable;
      this._defaultPrevented = false;
      this._propagationStopped = false;
      this._immediatePropagationStopped = false;
    }

    _setTarget(target) {
      this._target = target;
    }

    _setCurrentTarget(target) {
      this._currentTarget = target;
    }

    _setEventPhase(phase) {
      this._eventPhase = phase;
    }

    composedPath() {
      // Simplified - just return [target] since we don't have a DOM tree
      const path = [];
      let target = this._target;
      while (target) {
        path.push(target);
        target = target._getParent?.();
      }
      return path;
    }
  }

  // Event phase constants
  Event.NONE = 0;
  Event.CAPTURING_PHASE = 1;
  Event.AT_TARGET = 2;
  Event.BUBBLING_PHASE = 3;

  /**
   * CustomEvent class
   *
   * Event with custom data payload.
   */
  class CustomEvent extends Event {
    constructor(type, options) {
      super(type, options);
      this._detail = options?.detail ?? null;
    }

    get detail() { return this._detail; }
  }

  /**
   * EventTarget class
   *
   * Base class for objects that can receive events.
   */
  class EventTarget {
    constructor() {
      this._listeners = new Map();
      this._onceListeners = new Set();
    }

    addEventListener(type, callback, options) {
      if (typeof type !== 'string') {
        throw new TypeError('Event type must be a string');
      }

      if (callback === null || callback === undefined) {
        return;
      }

      if (typeof callback !== 'function' && typeof callback !== 'object') {
        throw new TypeError('Callback must be a function or object');
      }

      // Normalize options
      let capture = false;
      let once = false;
      let passive = false;

      if (typeof options === 'boolean') {
        capture = options;
      } else if (typeof options === 'object' && options !== null) {
        capture = options.capture ?? false;
        once = options.once ?? false;
        passive = options.passive ?? false;
      }

      // Create listener key (type + capture)
      const key = type + (capture ? ':capture' : '');

      if (!this._listeners.has(key)) {
        this._listeners.set(key, []);
      }

      const listenerList = this._listeners.get(key);

      // Check if already added
      for (const entry of listenerList) {
        if (entry.callback === callback) {
          return; // Already registered
        }
      }

      // Add listener
      listenerList.push({
        callback: callback,
        once: once,
        passive: passive
      });
    }

    removeEventListener(type, callback, options) {
      if (typeof type !== 'string') {
        throw new TypeError('Event type must be a string');
      }

      if (callback === null || callback === undefined) {
        return;
      }

      let capture = false;
      if (typeof options === 'boolean') {
        capture = options;
      } else if (typeof options === 'object' && options !== null) {
        capture = options.capture ?? false;
      }

      const key = type + (capture ? ':capture' : '');
      const listenerList = this._listeners.get(key);

      if (!listenerList) return;

      // Find and remove
      for (let i = 0; i < listenerList.length; i++) {
        if (listenerList[i].callback === callback) {
          listenerList.splice(i, 1);
          return;
        }
      }
    }

    dispatchEvent(event) {
      if (!event || typeof event.type !== 'string') {
        throw new TypeError('Argument must be an Event object');
      }

      // Set event target
      event._setTarget(this);
      event._setCurrentTarget(this);
      event._setEventPhase(Event.AT_TARGET);

      // At the target phase, capture and non-capture listeners both fire.
      // The spec requires registration order across capture/bubble, but
      // since we store them in separate lists we can't reconstruct that.
      // Fire capture listeners first, then bubble — closer to spec than
      // the old code which fired bubble then capture.
      const captureKey = event.type + ':capture';
      const bubbleKey = event.type;
      const toRemove = [];

      for (const key of [captureKey, bubbleKey]) {
        const listenerList = this._listeners.get(key);
        if (!listenerList) continue;

        const listeners = listenerList.slice();
        for (const entry of listeners) {
          if (event._immediatePropagationStopped) break;

          try {
            const callback = typeof entry.callback === 'function'
              ? entry.callback
              : entry.callback.handleEvent;

            if (typeof callback === 'function') {
              callback.call(this, event);
            }
          } catch (err) {
            if (globalThis.console) {
              console.error('Error in event listener:', err);
            }
          }

          if (entry.once) {
            toRemove.push({ entry, list: listenerList });
          }
        }
      }

      // Remove once listeners
      for (const { entry, list } of toRemove) {
        const idx = list.indexOf(entry);
        if (idx >= 0) {
          list.splice(idx, 1);
        }
      }

      return !event._defaultPrevented;
    }
  }

  // Register on globalThis
  globalThis.Event = Event;
  globalThis.CustomEvent = CustomEvent;
  globalThis.EventTarget = EventTarget;
}

(function(pal) {
  // src/pal.js
// src/console.js
  function setupConsole(pal2) {
    const timers = /* @__PURE__ */ new Map();
    const LEVELS = {
      debug: 0,
      log: 1,
      info: 1,
      warn: 2,
      error: 3
    };
    function formatArgs(args) {
      return args.map((arg) => {
        if (arg === null) return "null";
        if (arg === void 0) return "undefined";
        if (typeof arg === "string") return arg;
        if (typeof arg === "number" || typeof arg === "boolean") return String(arg);
        try {
          return JSON.stringify(arg);
        } catch (e) {
          return String(arg);
        }
      }).join(" ");
    }
    const console2 = {
      log: function(...args) {
        pal2.log(LEVELS.log, formatArgs(args));
      },
      info: function(...args) {
        pal2.log(LEVELS.info, formatArgs(args));
      },
      warn: function(...args) {
        pal2.log(LEVELS.warn, formatArgs(args));
      },
      error: function(...args) {
        pal2.log(LEVELS.error, formatArgs(args));
      },
      debug: function(...args) {
        pal2.log(LEVELS.debug, formatArgs(args));
      },
      trace: function(...args) {
        pal2.log(LEVELS.debug, "Trace: " + formatArgs(args));
      },
      dir: function(obj, options) {
        try {
          pal2.log(LEVELS.log, JSON.stringify(obj, null, options?.depth ?? 2));
        } catch (e) {
          pal2.log(LEVELS.log, String(obj));
        }
      },
      time: function(label) {
        label = label || "default";
        timers.set(label, pal2.timeNow());
      },
      timeEnd: function(label) {
        label = label || "default";
        const start = timers.get(label);
        if (start === void 0) {
          pal2.log(LEVELS.warn, `Timer '${label}' does not exist`);
          return;
        }
        timers.delete(label);
        const elapsed = pal2.timeNow() - start;
        pal2.log(LEVELS.info, `${label}: ${elapsed.toFixed(3)}ms`);
      },
      assert: function(condition, ...args) {
        if (!condition) {
          pal2.log(LEVELS.error, "Assertion failed: " + formatArgs(args));
        }
      },
      clear: function() {
      },
      count: function(label) {
        label = label || "default";
        const counts = this._counts || (this._counts = /* @__PURE__ */ new Map());
        const count = (counts.get(label) || 0) + 1;
        counts.set(label, count);
        pal2.log(LEVELS.info, `${label}: ${count}`);
      },
      countReset: function(label) {
        label = label || "default";
        const counts = this._counts;
        if (counts) {
          counts.delete(label);
        }
      },
      group: function(label) {
        pal2.log(LEVELS.info, label || "Group");
      },
      groupEnd: function() {
      },
      table: function(data) {
        try {
          pal2.log(LEVELS.info, JSON.stringify(data, null, 2));
        } catch (e) {
          pal2.log(LEVELS.info, String(data));
        }
      }
    };
    globalThis.console = console2;
  }

  // src/performance.js
  function setupPerformance(pal2) {
    const marks = /* @__PURE__ */ new Map();
    const measures = [];
    const observers = [];
    function notifyEntry(entry) {
      observers.forEach(function(obs) {
        if (obs._connected && obs._entryTypes.indexOf(entry.entryType) >= 0) {
          obs._buffer.push(entry);
          if (!obs._scheduled) {
            obs._scheduled = true;
            obs._scheduled = false;
            if (obs._connected && obs._buffer.length > 0) {
              var entries = obs.takeRecords();
              try {
                obs.callback(new PerformanceObserverEntryList(entries), obs);
              } catch (e) {
              }
            }
          }
        }
      });
    }
    const hasHrtime = typeof pal2.hrtime === "function";
    let _hrtimeOrigin = 0;
    if (hasHrtime) {
      _hrtimeOrigin = pal2.hrtime();
    }
    function nowMs() {
      if (hasHrtime) {
        return (pal2.hrtime() - _hrtimeOrigin) / 1e6;
      }
      return pal2.timeNow();
    }
    class Performance {
      constructor() {
      }
      /**
       * Returns a high-resolution timestamp in milliseconds.
       */
      now() {
        return nowMs();
      }
      /**
       * Get the time origin (approximation - time when runtime started)
       * Since we don't track this precisely, we use 0 as baseline.
       */
      get timeOrigin() {
        return 0;
      }
      /**
       * Create a named performance mark.
       */
      mark(name, options) {
        if (typeof name !== "string" || name === "") {
          throw new TypeError("Mark name must be a non-empty string");
        }
        marks.set(name, {
          name,
          entryType: "mark",
          startTime: nowMs(),
          duration: 0
        });
        notifyEntry(marks.get(name));
      }
      /**
       * Create a named performance measure between two marks.
       */
      measure(name, startMark, endMark) {
        if (typeof name !== "string" || name === "") {
          throw new TypeError("Measure name must be a non-empty string");
        }
        let startTime, endTime;
        if (typeof startMark === "object" && startMark !== null) {
          const options = startMark;
          startTime = options.start !== void 0 ? marks.get(options.start)?.startTime ?? options.start : 0;
          endTime = options.end !== void 0 ? marks.get(options.end)?.startTime ?? options.end : nowMs();
        } else {
          if (startMark) {
            const startEntry = marks.get(startMark);
            if (!startEntry) {
              throw new Error(`Mark '${startMark}' not found`);
            }
            startTime = startEntry.startTime;
          } else {
            startTime = 0;
          }
          if (endMark) {
            const endEntry = marks.get(endMark);
            if (!endEntry) {
              throw new Error(`Mark '${endMark}' not found`);
            }
            endTime = endEntry.startTime;
          } else {
            endTime = nowMs();
          }
        }
        measures.push({
          name,
          entryType: "measure",
          startTime,
          duration: endTime - startTime
        });
        notifyEntry(measures[measures.length - 1]);
      }
      /**
       * Remove a mark by name.
       */
      clearMarks(name) {
        if (name) {
          marks.delete(name);
        } else {
          marks.clear();
        }
      }
      /**
       * Remove measures by name.
       */
      clearMeasures(name) {
        if (name) {
          for (let i = measures.length - 1; i >= 0; i--) {
            if (measures[i].name === name) {
              measures.splice(i, 1);
            }
          }
        } else {
          measures.length = 0;
        }
      }
      /**
       * Get all performance entries.
       */
      getEntries() {
        const result = [];
        marks.forEach((entry) => result.push({ ...entry }));
        measures.forEach((entry) => result.push({ ...entry }));
        return result.sort((a, b) => a.startTime - b.startTime);
      }
      /**
       * Get entries by name.
       */
      getEntriesByName(name, type) {
        return this.getEntries().filter(
          (entry) => entry.name === name && (!type || entry.entryType === type)
        );
      }
      /**
       * Get entries by type.
       */
      getEntriesByType(type) {
        return this.getEntries().filter((entry) => entry.entryType === type);
      }
    }
    globalThis.performance = new Performance();
    globalThis.Performance = Performance;
    class PerformanceObserverEntryList {
      constructor(entries) {
        this._entries = entries;
      }
      getEntries() {
        return this._entries;
      }
      getEntriesByType(type) {
        return this._entries.filter(function(e) {
          return e.entryType === type;
        });
      }
      getEntriesByName(name, type) {
        return this._entries.filter(function(e) {
          return e.name === name && (!type || e.entryType === type);
        });
      }
    }
    class PerformanceObserver {
      constructor(callback) {
        this.callback = callback;
        this._buffer = [];
        this._entryTypes = [];
        this._connected = false;
        this._scheduled = false;
      }
      observe(options) {
        if (!options || !options.entryTypes) {
          throw new TypeError("PerformanceObserver.observe: entryTypes required");
        }
        this._connected = true;
        this._entryTypes = options.entryTypes;
        observers.push(this);
      }
      disconnect() {
        this._connected = false;
        this._buffer = [];
        var idx = observers.indexOf(this);
        if (idx >= 0) observers.splice(idx, 1);
      }
      takeRecords() {
        var r = this._buffer;
        this._buffer = [];
        return r;
      }
    }
    globalThis.PerformanceObserver = PerformanceObserver;
    globalThis.PerformanceObserverEntryList = PerformanceObserverEntryList;
  }

  // src/timers.js
  function setupTimers(pal2) {
    const timerEntries = /* @__PURE__ */ new Map();
    let nextIntervalHandle = -1;
    globalThis.setTimeout = function(callback, delay, ...args) {
      if (typeof callback !== "function") {
        throw new TypeError("setTimeout callback must be a function");
      }
      delay = Math.max(0, Number(delay) || 0);
      const result = pal2.timerStart(delay, 0);
      const handle = result.handle;
      const entry = {
        callback,
        args,
        stopped: false,
        isInterval: false,
        delay,
        currentPalHandle: handle
        // always set so clearTimeout can stop it (handle may be 0)
      };
      timerEntries.set(handle, entry);
      result.promise.then(function() {
        const e = timerEntries.get(handle);
        if (e && !e.stopped) {
          timerEntries.delete(handle);
          try {
            e.callback.apply(null, e.args);
          } catch (err) {
            if (globalThis.console) {
              console.error("Uncaught error in setTimeout callback:", err);
            }
          }
        }
      });
      return handle;
    };
    globalThis.setInterval = function(callback, delay, ...args) {
      if (typeof callback !== "function") {
        throw new TypeError("setInterval callback must be a function");
      }
      delay = Math.max(0, Number(delay) || 0);
      const handle = nextIntervalHandle--;
      let currentPalHandle = null;
      const entry = {
        callback,
        args,
        stopped: false,
        isInterval: true,
        delay,
        currentPalHandle: null
      };
      timerEntries.set(handle, entry);
      function scheduleNext() {
        if (entry.stopped) return;
        const result = pal2.timerStart(delay, 0);
        entry.currentPalHandle = result.handle;
        result.promise.then(function() {
          if (entry.stopped) return;
          try {
            entry.callback.apply(null, entry.args);
          } catch (err) {
            if (globalThis.console) {
              console.error("Uncaught error in setInterval callback:", err);
            }
          }
          if (!entry.stopped) {
            scheduleNext();
          }
        });
      }
      scheduleNext();
      return handle;
    };
    globalThis.clearTimeout = function(handle) {
      if (handle === void 0 || handle === null) return;
      const entry = timerEntries.get(handle);
      if (entry) {
        entry.stopped = true;
        timerEntries.delete(handle);
        if (entry.currentPalHandle !== null && entry.currentPalHandle !== void 0) {
          pal2.timerStop(entry.currentPalHandle);
        } else if (!entry.isInterval && handle > 0) {
          pal2.timerStop(handle);
        }
      }
    };
    globalThis.clearInterval = function(handle) {
      globalThis.clearTimeout(handle);
    };
  }

  // src/event-target.js
  function setupEventTarget() {
    class Event2 {
      constructor(type, options) {
        if (typeof type !== "string") {
          throw new TypeError("Event type must be a string");
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
        this._eventPhase = Event2.NONE;
        this._timeStamp = Date.now();
      }
      get type() {
        return this._type;
      }
      get bubbles() {
        return this._bubbles;
      }
      get cancelable() {
        return this._cancelable;
      }
      get composed() {
        return this._composed;
      }
      get defaultPrevented() {
        return this._defaultPrevented;
      }
      get target() {
        return this._target;
      }
      get currentTarget() {
        return this._currentTarget;
      }
      get eventPhase() {
        return this._eventPhase;
      }
      get timeStamp() {
        return this._timeStamp;
      }
      get NONE() {
        return Event2.NONE;
      }
      get CAPTURING_PHASE() {
        return Event2.CAPTURING_PHASE;
      }
      get AT_TARGET() {
        return Event2.AT_TARGET;
      }
      get BUBBLING_PHASE() {
        return Event2.BUBBLING_PHASE;
      }
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
        if (this._eventPhase === 0) return [];
        const path = [];
        let target = this._target;
        while (target) {
          path.push(target);
          target = target._getParent?.();
        }
        return path;
      }
    }
    Event2.NONE = 0;
    Event2.CAPTURING_PHASE = 1;
    Event2.AT_TARGET = 2;
    Event2.BUBBLING_PHASE = 3;
    class CustomEvent extends Event2 {
      constructor(type, options) {
        super(type, options);
        this._detail = options?.detail ?? null;
      }
      get detail() {
        return this._detail;
      }
    }
    class EventTarget2 {
      constructor() {
        this._listeners = /* @__PURE__ */ new Map();
        this._onceListeners = /* @__PURE__ */ new Set();
      }
      addEventListener(type, callback, options) {
        if (typeof type !== "string") {
          throw new TypeError("Event type must be a string");
        }
        if (callback === null || callback === void 0) {
          return;
        }
        if (typeof callback !== "function" && typeof callback !== "object") {
          throw new TypeError("Callback must be a function or object");
        }
        let capture = false;
        let once = false;
        let passive = false;
        if (typeof options === "boolean") {
          capture = options;
        } else if (typeof options === "object" && options !== null) {
          capture = options.capture ?? false;
          once = options.once ?? false;
          passive = options.passive ?? false;
        }
        const key = type + (capture ? ":capture" : "");
        if (!this._listeners.has(key)) {
          this._listeners.set(key, []);
        }
        const listenerList = this._listeners.get(key);
        for (const entry of listenerList) {
          if (entry.callback === callback) {
            return;
          }
        }
        listenerList.push({
          callback,
          once,
          passive
        });
      }
      removeEventListener(type, callback, options) {
        if (typeof type !== "string") {
          throw new TypeError("Event type must be a string");
        }
        if (callback === null || callback === void 0) {
          return;
        }
        let capture = false;
        if (typeof options === "boolean") {
          capture = options;
        } else if (typeof options === "object" && options !== null) {
          capture = options.capture ?? false;
        }
        const key = type + (capture ? ":capture" : "");
        const listenerList = this._listeners.get(key);
        if (!listenerList) return;
        for (let i = 0; i < listenerList.length; i++) {
          if (listenerList[i].callback === callback) {
            listenerList.splice(i, 1);
            return;
          }
        }
      }
      dispatchEvent(event) {
        if (!event || typeof event.type !== "string") {
          throw new TypeError("Argument must be an Event object");
        }
        event._setTarget(this);
        event._setCurrentTarget(this);
        event._setEventPhase(Event2.AT_TARGET);
        const captureKey = event.type + ":capture";
        const bubbleKey = event.type;
        const toRemove = [];
        for (const key of [captureKey, bubbleKey]) {
          const listenerList = this._listeners.get(key);
          if (!listenerList) continue;
          const listeners = listenerList.slice();
          for (const entry of listeners) {
            if (event._immediatePropagationStopped) break;
            try {
              const callback = typeof entry.callback === "function" ? entry.callback : entry.callback.handleEvent;
              if (typeof callback === "function") {
                callback.call(this, event);
              }
            } catch (err) {
              if (globalThis.console) {
                console.error("Error in event listener:", err);
              }
            }
            if (entry.once) {
              toRemove.push({ entry, list: listenerList });
            }
          }
        }
        for (const { entry, list } of toRemove) {
          const idx = list.indexOf(entry);
          if (idx >= 0) {
            list.splice(idx, 1);
          }
        }
        event._setEventPhase(0);
        event._setCurrentTarget(null);
        return !event._defaultPrevented;
      }
    }
    globalThis.Event = Event2;
    globalThis.CustomEvent = CustomEvent;
    globalThis.EventTarget = EventTarget2;
    var globalTarget = new EventTarget2();
    globalThis.addEventListener = globalTarget.addEventListener.bind(globalTarget);
    globalThis.removeEventListener = globalTarget.removeEventListener.bind(globalTarget);
    globalThis.dispatchEvent = globalTarget.dispatchEvent.bind(globalTarget);
  }

  // src/abort.js
  function setupAbort() {
    if (typeof globalThis.EventTarget !== "function") {
      throw new Error("AbortController requires EventTarget to be loaded first");
    }
    class AbortSignal extends EventTarget {
      constructor() {
        super();
        this._aborted = false;
        this._reason = void 0;
      }
      /**
       * True if the signal has been aborted.
       */
      get aborted() {
        return this._aborted;
      }
      /**
       * The reason for abort (if any).
       */
      get reason() {
        return this._reason;
      }
      /**
       * Throws an AbortError if the signal has been aborted.
       */
      throwIfAborted() {
        if (this._aborted) {
          throw new DOMException(
            this._reason || "The operation was aborted",
            "AbortError"
          );
        }
      }
      /**
       * Internal method to abort the signal.
       */
      /**
       * Override addEventListener to support the AbortSignal spec: if the signal
       * has already been aborted, the new listener is called in a microtask.
       */
      addEventListener(type, callback, options) {
        super.addEventListener(type, callback, options);
        if (type === "abort" && this._aborted) {
          var self = this;
          Promise.resolve().then(function() {
            if (typeof callback === "function") {
              callback.call(self, new Event("abort"));
            } else if (callback && typeof callback.handleEvent === "function") {
              callback.handleEvent.call(self, new Event("abort"));
            }
          });
        }
      }
      _abort(reason) {
        if (this._aborted) return;
        this._aborted = true;
        this._reason = reason;
        const event = new Event("abort");
        this.dispatchEvent(event);
      }
      /**
       * Static method to create an already-aborted signal.
       */
      static abort(reason) {
        const signal = new AbortSignal();
        signal._aborted = true;
        signal._reason = reason;
        return signal;
      }
      /**
       * Static method to create a signal that aborts after a timeout.
       * (Requires setTimeout to be available)
       */
      static timeout(ms) {
        const signal = new AbortSignal();
        const timer = setTimeout(function() {
          signal._abort(new DOMException("The operation timed out", "TimeoutError"));
        }, ms);
        signal.addEventListener("abort", function() {
          clearTimeout(timer);
        });
        return signal;
      }
      /**
       * Static method to create a signal that aborts when any of the given signals abort.
       */
      static any(signals) {
        if (!Array.isArray(signals)) {
          throw new TypeError("signals must be an array");
        }
        const result = new AbortSignal();
        for (const signal of signals) {
          if (!(signal instanceof AbortSignal)) {
            throw new TypeError("All signals must be AbortSignal instances");
          }
          if (signal.aborted) {
            result._abort(signal.reason);
            return result;
          }
          signal.addEventListener("abort", function() {
            result._abort(signal.reason);
          });
        }
        return result;
      }
    }
    class AbortController {
      constructor() {
        this._signal = new AbortSignal();
      }
      /**
       * The associated AbortSignal.
       */
      get signal() {
        return this._signal;
      }
      /**
       * Abort the associated signal.
       *
       * @param reason - Optional reason for abort
       */
      abort(reason) {
        this._signal._abort(reason);
      }
    }
    if (typeof globalThis.DOMException === "undefined") {
      class DOMException2 extends Error {
        constructor(message, name) {
          super(message);
          this.name = name || "Error";
          this.code = DOMException2._codes[this.name] || 0;
        }
      }
      DOMException2._codes = {
        "IndexSizeError": 1,
        "DOMStringSizeError": 2,
        "HierarchyRequestError": 3,
        "WrongDocumentError": 4,
        "InvalidCharacterError": 5,
        "NoDataAllowedError": 6,
        "NoModificationAllowedError": 7,
        "NotFoundError": 8,
        "NotSupportedError": 9,
        "InUseAttributeError": 10,
        "InvalidStateError": 11,
        "SyntaxError": 12,
        "InvalidModificationError": 13,
        "NamespaceError": 14,
        "InvalidAccessError": 15,
        "ValidationError": 16,
        "TypeMismatchError": 17,
        "SecurityError": 18,
        "NetworkError": 19,
        "AbortError": 20,
        "URLMismatchError": 21,
        "QuotaExceededError": 22,
        "TimeoutError": 23,
        "InvalidNodeTypeError": 24,
        "DataCloneError": 25
      };
      globalThis.DOMException = DOMException2;
    }
    globalThis.AbortController = AbortController;
    globalThis.AbortSignal = AbortSignal;
  }

  // src/url.js
  function setupURL() {
    class URLSearchParams {
      constructor(init) {
        this._params = [];
        this._url = null;
        if (init === void 0 || init === null) {
        } else if (typeof init === "string") {
          this._parse(init);
        } else if (init instanceof URLSearchParams) {
          this._params = init._params.slice();
        } else if (typeof init === "object") {
          if (Symbol.iterator in init) {
            for (const pair of init) {
              if (Array.isArray(pair) && pair.length >= 2) {
                this._params.push([String(pair[0]), String(pair[1])]);
              }
            }
          } else {
            for (const key of Object.keys(init)) {
              this._params.push([key, String(init[key])]);
            }
          }
        }
      }
      _parse(str) {
        if (str[0] === "?") {
          str = str.slice(1);
        }
        if (!str) return;
        const pairs = str.split("&");
        for (const pair of pairs) {
          if (!pair) continue;
          const eqIdx = pair.indexOf("=");
          if (eqIdx < 0) {
            this._params.push([this._decode(pair), ""]);
          } else {
            this._params.push([
              this._decode(pair.slice(0, eqIdx)),
              this._decode(pair.slice(eqIdx + 1))
            ]);
          }
        }
      }
      _decode(str) {
        try {
          return decodeURIComponent(str.replace(/\+/g, " "));
        } catch (e) {
          return str;
        }
      }
      _encode(str) {
        return encodeURIComponent(str).replace(/%20/g, "+").replace(/[!'()*]/g, function(c) {
          return "%" + c.charCodeAt(0).toString(16).toUpperCase();
        });
      }
      _sync() {
        if (this._url) {
          this._url._search = this.toString();
        }
      }
      append(name, value) {
        this._params.push([String(name), String(value)]);
        this._sync();
      }
      delete(name) {
        name = String(name);
        this._params = this._params.filter(function(p) {
          return p[0] !== name;
        });
        this._sync();
      }
      get(name) {
        name = String(name);
        for (const p of this._params) {
          if (p[0] === name) return p[1];
        }
        return null;
      }
      getAll(name) {
        name = String(name);
        const result = [];
        for (const p of this._params) {
          if (p[0] === name) result.push(p[1]);
        }
        return result;
      }
      has(name) {
        name = String(name);
        for (const p of this._params) {
          if (p[0] === name) return true;
        }
        return false;
      }
      set(name, value) {
        name = String(name);
        value = String(value);
        let found = false;
        const result = [];
        for (const p of this._params) {
          if (p[0] === name) {
            if (!found) {
              result.push([name, value]);
              found = true;
            }
          } else {
            result.push(p);
          }
        }
        if (!found) {
          result.push([name, value]);
        }
        this._params = result;
        this._sync();
      }
      sort() {
        this._params.sort(function(a, b) {
          return a[0].localeCompare(b[0]);
        });
        this._sync();
      }
      toString() {
        const parts = [];
        for (const p of this._params) {
          parts.push(this._encode(p[0]) + "=" + this._encode(p[1]));
        }
        return parts.join("&");
      }
      forEach(callback, thisArg) {
        for (const p of this._params) {
          callback.call(thisArg, p[1], p[0], this);
        }
      }
      entries() {
        return this._params[Symbol.iterator]();
      }
      keys() {
        const params = this._params;
        let i = 0;
        return {
          next: function() {
            if (i < params.length) {
              return { value: params[i++][0], done: false };
            }
            return { done: true };
          },
          [Symbol.iterator]: function() {
            return this;
          }
        };
      }
      values() {
        const params = this._params;
        let i = 0;
        return {
          next: function() {
            if (i < params.length) {
              return { value: params[i++][1], done: false };
            }
            return { done: true };
          },
          [Symbol.iterator]: function() {
            return this;
          }
        };
      }
      [Symbol.iterator]() {
        return this.entries();
      }
    }
    class URL2 {
      constructor(url, base) {
        let baseUrl = null;
        if (base) {
          baseUrl = base instanceof URL2 ? base : new URL2(base);
        }
        this._parse(url, baseUrl);
      }
      _parse(url, baseUrl) {
        if (typeof url !== "string") {
          throw new TypeError("URL must be a string");
        }
        url = url.trim();
        const URL_REGEX = /^(?:([a-z][a-z0-9+.-]*):)?(?:\/\/(?:([^:@]*)(?::([^@]*))?@)?([^:/?#]*)(?::(\d+))?)?(\/?[^?#]*)?(?:\?([^#]*))?(?:#(.*))?$/i;
        let match = url.match(URL_REGEX);
        if (!match) {
          throw new TypeError("Invalid URL: " + url);
        }
        let [, protocol, username, password, host, port, path, query, hash] = match;
        if (!protocol && !baseUrl) {
          throw new TypeError("Relative URL without base: " + url);
        }
        if (!protocol && baseUrl) {
          protocol = baseUrl._protocol;
          if (!host) {
            host = baseUrl._host;
            port = baseUrl._port;
            username = baseUrl._username;
            password = baseUrl._password;
            if (path && path[0] !== "/") {
              const basePath = baseUrl._pathname || "/";
              const baseDir = basePath.substring(0, basePath.lastIndexOf("/") + 1);
              path = this._resolvePath(baseDir + path);
            } else if (!path) {
              path = baseUrl._pathname;
            }
          }
        }
        this._protocol = (protocol || "").toLowerCase();
        this._username = username || "";
        this._password = password || "";
        this._host = host || "";
        this._port = port || "";
        this._pathname = path || "/";
        this._search = query || "";
        this._hash = hash || "";
        this._searchParams = new URLSearchParams(this._search);
        this._searchParams._url = this;
      }
      _resolvePath(path) {
        const parts = path.split("/");
        const result = [];
        for (const part of parts) {
          if (part === "..") {
            if (result.length > 0 && result[result.length - 1] !== "..") {
              result.pop();
            }
          } else if (part !== "." && part !== "") {
            result.push(part);
          }
        }
        let resolved = result.join("/");
        if (path[0] === "/") resolved = "/" + resolved;
        if (path[path.length - 1] === "/" && resolved[resolved.length - 1] !== "/") {
          resolved += "/";
        }
        return resolved || "/";
      }
      // Getters
      get href() {
        return this.toString();
      }
      get origin() {
        if (!this._host) return "";
        return this._protocol + "//" + this._host + (this._port ? ":" + this._port : "");
      }
      get protocol() {
        return this._protocol + ":";
      }
      get username() {
        return this._username;
      }
      get password() {
        return this._password;
      }
      get host() {
        return this._host + (this._port ? ":" + this._port : "");
      }
      get hostname() {
        return this._host;
      }
      get port() {
        return this._port;
      }
      get pathname() {
        return this._pathname;
      }
      get search() {
        return this._search ? "?" + this._search : "";
      }
      get searchParams() {
        return this._searchParams;
      }
      get hash() {
        return this._hash ? "#" + this._hash : "";
      }
      // Setters
      set protocol(v) {
        v = String(v).toLowerCase();
        if (v.endsWith(":")) v = v.slice(0, -1);
        this._protocol = v;
      }
      set username(v) {
        this._username = String(v);
      }
      set password(v) {
        this._password = String(v);
      }
      set host(v) {
        v = String(v);
        const colonIdx = v.lastIndexOf(":");
        if (colonIdx >= 0) {
          this._host = v.slice(0, colonIdx);
          this._port = v.slice(colonIdx + 1);
        } else {
          this._host = v;
          this._port = "";
        }
      }
      set hostname(v) {
        this._host = String(v);
      }
      set port(v) {
        this._port = String(v);
      }
      set pathname(v) {
        v = String(v);
        this._pathname = v[0] === "/" ? v : "/" + v;
      }
      set search(v) {
        v = String(v);
        this._search = v[0] === "?" ? v.slice(1) : v;
        if (this._searchParams) {
          this._searchParams._params = [];
          this._searchParams._parse(this._search);
        }
      }
      set hash(v) {
        v = String(v);
        this._hash = v[0] === "#" ? v.slice(1) : v;
      }
      set href(v) {
        this._parse(v, null);
      }
      toString() {
        let result = this._protocol + ":";
        if (this._host) {
          result += "//";
          if (this._username) {
            result += this._username;
            if (this._password) {
              result += ":" + this._password;
            }
            result += "@";
          }
          result += this._host;
          if (this._port) {
            result += ":" + this._port;
          }
        }
        result += this._pathname;
        if (this._search) {
          result += "?" + this._search;
        }
        if (this._hash) {
          result += "#" + this._hash;
        }
        return result;
      }
      toJSON() {
        return this.toString();
      }
      // Static method to check if a string is a valid URL
      static canParse(url, base) {
        try {
          new URL2(url, base);
          return true;
        } catch (e) {
          return false;
        }
      }
    }
    globalThis.URL = URL2;
    globalThis.URLSearchParams = URLSearchParams;
  }

  // src/encoding.js
  function setupEncoding(pal2) {
    var useNativeBtoa = typeof pal2.nativeBtoa === "function";
    var useNativeAtob = typeof pal2.nativeAtob === "function";
    const BASE64_CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const BASE64_DECODE = {};
    for (let i = 0; i < BASE64_CHARS.length; i++) {
      BASE64_DECODE[BASE64_CHARS[i]] = i;
    }
    BASE64_DECODE["="] = 0;
    globalThis.btoa = function(binaryString) {
      if (binaryString === null || binaryString === void 0) {
        throw new TypeError("btoa requires a string argument");
      }
      if (useNativeBtoa) {
        return pal2.nativeBtoa(String(binaryString));
      }
      binaryString = String(binaryString);
      for (let i2 = 0; i2 < binaryString.length; i2++) {
        const code = binaryString.charCodeAt(i2);
        if (code > 255) {
          throw new Error(
            "Failed to execute 'btoa': The string to be encoded contains characters outside of the Latin1 range."
          );
        }
      }
      let result = "";
      let i = 0;
      const len = binaryString.length;
      while (i < len) {
        let byteCount = 0;
        const a = binaryString.charCodeAt(i++);
        byteCount++;
        const b = i < len ? (byteCount++, binaryString.charCodeAt(i++)) : 0;
        const c = i < len ? (byteCount++, binaryString.charCodeAt(i++)) : 0;
        const triplet = a << 16 | b << 8 | c;
        result += BASE64_CHARS[triplet >> 18 & 63];
        result += BASE64_CHARS[triplet >> 12 & 63];
        result += byteCount >= 2 ? BASE64_CHARS[triplet >> 6 & 63] : "=";
        result += byteCount >= 3 ? BASE64_CHARS[triplet & 63] : "=";
      }
      return result;
    };
    globalThis.atob = function(base64String) {
      if (base64String === null || base64String === void 0) {
        throw new TypeError("atob requires a string argument");
      }
      if (useNativeAtob) {
        return pal2.nativeAtob(String(base64String));
      }
      base64String = String(base64String);
      base64String = base64String.replace(/\s/g, "");
      if (base64String.length % 4 !== 0) {
        throw new Error(
          "Failed to execute 'atob': The string to be decoded is not correctly encoded."
        );
      }
      const validChars = /^[A-Za-z0-9+/=]*$/;
      if (!validChars.test(base64String)) {
        throw new Error(
          "Failed to execute 'atob': The string to be decoded is not correctly encoded."
        );
      }
      let result = "";
      let i = 0;
      const len = base64String.length;
      while (i < len) {
        const a = BASE64_DECODE[base64String[i++]];
        const b = BASE64_DECODE[base64String[i++]];
        const c = BASE64_DECODE[base64String[i++]];
        const d = BASE64_DECODE[base64String[i++]];
        const triplet = a << 18 | b << 12 | c << 6 | d;
        result += String.fromCharCode(triplet >> 16 & 255);
        if (base64String[i - 2] !== "=") {
          result += String.fromCharCode(triplet >> 8 & 255);
        }
        if (base64String[i - 1] !== "=") {
          result += String.fromCharCode(triplet & 255);
        }
      }
      return result;
    };
  }

  // src/fetch.js
  function setupFetch(pal2) {
    "use strict";
    var STATUS_TEXTS = {
      100: "Continue",
      101: "Switching Protocols",
      200: "OK",
      201: "Created",
      202: "Accepted",
      203: "Non-Authoritative Information",
      204: "No Content",
      205: "Reset Content",
      206: "Partial Content",
      300: "Multiple Choices",
      301: "Moved Permanently",
      302: "Found",
      303: "See Other",
      304: "Not Modified",
      307: "Temporary Redirect",
      308: "Permanent Redirect",
      400: "Bad Request",
      401: "Unauthorized",
      402: "Payment Required",
      403: "Forbidden",
      404: "Not Found",
      405: "Method Not Allowed",
      406: "Not Acceptable",
      407: "Proxy Authentication Required",
      408: "Request Timeout",
      409: "Conflict",
      410: "Gone",
      411: "Length Required",
      412: "Precondition Failed",
      413: "Payload Too Large",
      414: "URI Too Long",
      415: "Unsupported Media Type",
      416: "Range Not Satisfiable",
      417: "Expectation Failed",
      418: "I'm a Teapot",
      422: "Unprocessable Entity",
      425: "Too Early",
      426: "Upgrade Required",
      428: "Precondition Required",
      429: "Too Many Requests",
      431: "Request Header Fields Too Large",
      451: "Unavailable For Legal Reasons",
      500: "Internal Server Error",
      501: "Not Implemented",
      502: "Bad Gateway",
      503: "Service Unavailable",
      504: "Gateway Timeout",
      505: "HTTP Version Not Supported",
      506: "Variant Also Negotiates",
      507: "Insufficient Storage",
      508: "Loop Detected",
      510: "Not Extended",
      511: "Network Authentication Required"
    };
    function normalizeName(name) {
      if (typeof name !== "string") {
        name = String(name);
      }
      var normalized = name.toLowerCase();
      if (/[^a-z0-9\-!#$%&'*+.^_`|~]/.test(normalized)) {
        throw new TypeError("Invalid header name: " + name);
      }
      return normalized;
    }
    function normalizeValue(value) {
      if (value === void 0 || value === null) {
        throw new TypeError("Invalid header value");
      }
      var str = String(value);
      if (/[^\t\x20-\x7e\x80-\xff]/.test(str)) {
        throw new TypeError("Invalid header value: " + str);
      }
      return str;
    }
    function stringToUint8Array(str) {
      return new TextEncoder().encode(str);
    }
    function Headers(init) {
      this._map = /* @__PURE__ */ new Map();
      if (init === null) {
        throw new TypeError("Headers init must not be null");
      }
      if (init !== void 0) {
        if (init instanceof Headers) {
          init._map.forEach(function(value, key) {
            this._map.set(key, value);
          }.bind(this));
        } else if (typeof init === "object") {
          if (Symbol && Symbol.iterator && init[Symbol.iterator]) {
            var items = Array.from(init);
            for (var i = 0; i < items.length; i++) {
              var pair = items[i];
              if (!Array.isArray(pair) || pair.length !== 2) {
                throw new TypeError("Headers init: each header must be a [name, value] pair");
              }
              this.append(pair[0], pair[1]);
            }
          } else {
            var keys = Object.keys(init);
            for (var j = 0; j < keys.length; j++) {
              this.append(keys[j], init[keys[j]]);
            }
          }
        } else {
          throw new TypeError("Headers init must be Headers, object, or iterable");
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
        this._map.set(key, existing + ", " + normalizeValue(value));
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
    function consumeBody(body) {
      if (typeof body === "string") {
        return body;
      }
      if (body === null || body === void 0) {
        return "";
      }
      return String(body);
    }
    function Request2(input, init) {
      init = init || {};
      if (input instanceof Request2) {
        this._method = init.method || input.method;
        this._url = input.url;
        this._headers = new Headers(init.headers || input.headers);
        this._body = init.body !== void 0 ? init.body : input._body;
        this._signal = init.signal || input.signal;
        this._redirect = init.redirect || input.redirect || "follow";
        this._keepalive = init.keepalive !== void 0 ? !!init.keepalive : input.keepalive;
        this._cache = init.cache || input.cache || "default";
        this._mode = init.mode || input.mode || "cors";
        this._credentials = init.credentials || input.credentials || "same-origin";
      } else {
        this._method = init.method || "GET";
        this._url = String(input);
        this._headers = new Headers(init.headers);
        this._body = init.body !== void 0 ? init.body : null;
        this._signal = init.signal || null;
        this._redirect = init.redirect || "follow";
        this._keepalive = !!init.keepalive;
        this._cache = init.cache || "default";
        this._mode = init.mode || "cors";
        this._credentials = init.credentials || "same-origin";
      }
      this._bodyUsed = false;
      if (!/^[A-Z]+$/.test(this._method)) {
        throw new TypeError("Invalid HTTP method: " + this._method);
      }
    }
    Object.defineProperty(Request2.prototype, "method", {
      get: function() {
        return this._method;
      }
    });
    Object.defineProperty(Request2.prototype, "url", {
      get: function() {
        return this._url;
      }
    });
    Object.defineProperty(Request2.prototype, "headers", {
      get: function() {
        return this._headers;
      }
    });
    Object.defineProperty(Request2.prototype, "body", {
      get: function() {
        return this._body;
      }
    });
    Object.defineProperty(Request2.prototype, "bodyUsed", {
      get: function() {
        return this._bodyUsed;
      }
    });
    Object.defineProperty(Request2.prototype, "signal", {
      get: function() {
        return this._signal;
      }
    });
    Object.defineProperty(Request2.prototype, "redirect", {
      get: function() {
        return this._redirect;
      }
    });
    Object.defineProperty(Request2.prototype, "keepalive", {
      get: function() {
        return this._keepalive;
      }
    });
    Object.defineProperty(Request2.prototype, "cache", {
      get: function() {
        return this._cache;
      }
    });
    Object.defineProperty(Request2.prototype, "mode", {
      get: function() {
        return this._mode;
      }
    });
    Object.defineProperty(Request2.prototype, "credentials", {
      get: function() {
        return this._credentials;
      }
    });
    Request2.prototype.clone = function() {
      if (this._bodyUsed) {
        throw new TypeError("Cannot clone a Request whose body has been used");
      }
      return new Request2(this);
    };
    Request2.prototype.text = function() {
      if (this._bodyUsed) {
        throw new TypeError("Body has already been used");
      }
      this._bodyUsed = true;
      return Promise.resolve(consumeBody(this._body));
    };
    Request2.prototype.json = function() {
      return this.text().then(function(text) {
        return JSON.parse(text);
      });
    };
    Request2.prototype.arrayBuffer = function() {
      return this.text().then(function(text) {
        return stringToUint8Array(text);
      });
    };
    Request2.prototype.blob = function() {
      return this.text();
    };
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
            pending._resolve({ done: false, value: chunk });
          } else {
            this._enqueuedChunks.push(chunk);
          }
        },
        close: function() {
          if (this._closed) return;
          this._closed = true;
          while (this._pendingReads.length > 0) {
            var pending = this._pendingReads.shift();
            pending._resolve({ done: true, value: void 0 });
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
      if (underlyingSource && typeof underlyingSource.start === "function") {
        underlyingSource.start(this._controller);
      }
    }
    function ReadableStreamDefaultReader(stream) {
      if (stream._locked) throw new TypeError("ReadableStream already locked");
      stream._locked = true;
      this._stream = stream;
      this._closed = false;
      this._error = null;
      stream._reader = this;
    }
    ReadableStream.prototype.getReader = function() {
      if (this._locked) throw new TypeError("ReadableStream already locked");
      return new ReadableStreamDefaultReader(this);
    };
    ReadableStreamDefaultReader.prototype.read = function() {
      var self = this;
      if (self._error) {
        return Promise.reject(self._error);
      }
      if (self._closed) {
        return Promise.resolve({ done: true, value: void 0 });
      }
      var ctrl = self._stream._controller;
      if (ctrl._enqueuedChunks.length > 0) {
        var chunk = ctrl._enqueuedChunks.shift();
        return Promise.resolve({ done: false, value: chunk });
      }
      if (ctrl._closed) {
        self._closed = true;
        return Promise.resolve({ done: true, value: void 0 });
      }
      return new Promise(function(resolve, reject) {
        ctrl._pendingReads.push({ _resolve: resolve, _reject: reject });
      });
    };
    ReadableStreamDefaultReader.prototype.releaseLock = function() {
      this._stream._locked = false;
      this._stream._reader = null;
      this._closed = true;
    };
    function Response2(body, init) {
      init = init || {};
      this._status = init.status !== void 0 ? Number(init.status) : 200;
      this._statusText = init.statusText || STATUS_TEXTS[this._status] || "";
      this._headers = new Headers(init.headers);
      this._bodyUsed = false;
      this._type = "default";
      this._url = init.url || "";
      this._redirected = init.redirected || false;
      if (body && typeof body.getReader === "function") {
        this._bodyStream = body;
        this._body = null;
      } else {
        this._bodyStream = null;
        this._body = body !== void 0 ? body : null;
      }
    }
    Object.defineProperty(Response2.prototype, "status", {
      get: function() {
        return this._status;
      }
    });
    Object.defineProperty(Response2.prototype, "statusText", {
      get: function() {
        return this._statusText;
      }
    });
    Object.defineProperty(Response2.prototype, "ok", {
      get: function() {
        return this._status >= 200 && this._status <= 299;
      }
    });
    Object.defineProperty(Response2.prototype, "headers", {
      get: function() {
        return this._headers;
      }
    });
    Object.defineProperty(Response2.prototype, "body", {
      get: function() {
        if (this._bodyStream) return this._bodyStream;
        if (this._body == null) return null;
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
    Object.defineProperty(Response2.prototype, "bodyUsed", {
      get: function() {
        return this._bodyUsed;
      }
    });
    Object.defineProperty(Response2.prototype, "type", {
      get: function() {
        return this._type;
      }
    });
    Object.defineProperty(Response2.prototype, "url", {
      get: function() {
        return this._url;
      }
    });
    Object.defineProperty(Response2.prototype, "redirected", {
      get: function() {
        return this._redirected;
      }
    });
    Response2.prototype.clone = function() {
      if (this._bodyUsed) {
        throw new TypeError("Cannot clone a Response whose body has been used");
      }
      if (this._bodyStream) {
        throw new TypeError("Cannot clone a streaming Response");
      }
      var cloned = new Response2(this._body, {
        status: this._status,
        statusText: this._statusText,
        headers: this._headers,
        url: this._url,
        redirected: this._redirected
      });
      cloned._type = this._type;
      return cloned;
    };
    Response2.prototype.text = function() {
      var self = this;
      if (self._bodyUsed) {
        throw new TypeError("Body has already been used");
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
          return new TextDecoder("utf-8").decode(combined);
        });
      }
      return Promise.resolve(consumeBody(self._body));
    };
    Response2.prototype._readStreamFully = function() {
      var reader = this._bodyStream.getReader();
      var chunks = [];
      function pump() {
        return reader.read().then(function(result) {
          if (result.done) return chunks;
          var chunk = result.value;
          if (chunk instanceof ArrayBuffer) {
            chunk = new Uint8Array(chunk);
          }
          chunks.push(chunk);
          return pump();
        });
      }
      return pump();
    };
    Response2.prototype.json = function() {
      return this.text().then(function(text) {
        return JSON.parse(text);
      });
    };
    Response2.prototype.arrayBuffer = function() {
      var self = this;
      if (self._bodyUsed) {
        throw new TypeError("Body has already been used");
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
    Response2.prototype.blob = function() {
      return this.text();
    };
    Response2.error = function() {
      var response = new Response2(null, {
        status: 0,
        statusText: ""
      });
      response._type = "error";
      return response;
    };
    Response2.redirect = function(url, status) {
      if (status === void 0) status = 302;
      if (status < 300 || status > 399) {
        throw new RangeError("Invalid redirect status: " + status);
      }
      var response = new Response2(null, {
        status,
        headers: { location: url }
      });
      response._type = "opaqueredirect";
      return response;
    };
    Response2.json = function(data, init) {
      init = init || {};
      var body = JSON.stringify(data);
      var headers = new Headers(init.headers);
      if (!headers.has("content-type")) {
        headers.set("content-type", "application/json");
      }
      return new Response2(body, {
        status: init.status !== void 0 ? init.status : 200,
        statusText: init.statusText || "",
        headers
      });
    };
    function fetch(input, init) {
      return new Promise(function(resolve, reject) {
        var request;
        try {
          request = new Request2(input, init);
        } catch (e) {
          reject(e);
          return;
        }
        if (request.signal && request.signal.aborted) {
          reject(new DOMException("The operation was aborted.", "AbortError"));
          return;
        }
        doRequest(request, resolve, reject, 0);
      });
    }
    function doRequest(request, resolve, reject, redirectCount) {
      var headersObj = {};
      request.headers.forEach(function(value, name) {
        headersObj[name] = value;
      });
      var headersJson = JSON.stringify(headersObj);
      var bodyStr = null;
      if (request.body != null) {
        bodyStr = typeof request.body === "string" ? request.body : String(request.body);
      }
      var aborted = false;
      var onAbort;
      var streamController = null;
      var abandoned = false;
      if (request.signal) {
        onAbort = function() {
          aborted = true;
          if (streamController) {
            streamController.error(new DOMException("The operation was aborted.", "AbortError"));
          }
          reject(new DOMException("The operation was aborted.", "AbortError"));
        };
        request.signal.addEventListener("abort", onAbort);
      }
      function cleanupAbort() {
        if (request.signal && onAbort) {
          request.signal.removeEventListener("abort", onAbort);
        }
      }
      if (typeof pal2.httpRequestStream !== "function") {
        if (aborted) {
          return;
        }
        cleanupAbort();
        var p = pal2.httpRequest(request.url, request.method, headersJson, bodyStr);
        Promise.resolve(p).then(function(data) {
          if (aborted) return;
          var bodyBytes = stringToUint8Array(data || "");
          var res = new Response2(bodyBytes, {
            status: 200,
            headers: new Headers()
          });
          res._url = request.url;
          resolve(res);
        }, function(err) {
          if (aborted) return;
          reject(new TypeError("fetch failed: " + (err || "unknown error")));
        });
        return;
      }
      var readableStream = new ReadableStream({
        start: function(controller) {
          streamController = controller;
        }
      });
      function onHeaders(status, headersJsonStr) {
        if (aborted) return;
        var parsedHeaders = {};
        if (headersJsonStr) {
          try {
            parsedHeaders = JSON.parse(headersJsonStr);
          } catch (e) {
          }
        }
        if (status >= 300 && status <= 399) {
          if (request.redirect === "error") {
            cleanupAbort();
            reject(new TypeError('fetch failed: redirect mode is "error"'));
            return;
          }
          if (request.redirect === "manual") {
            cleanupAbort();
            var manualRes = new Response2(null, { status: 0, statusText: "" });
            manualRes._type = "opaqueredirect";
            resolve(manualRes);
            return;
          }
          if (redirectCount >= 20) {
            cleanupAbort();
            reject(new TypeError("fetch failed: too many redirects"));
            return;
          }
          var location = parsedHeaders["location"] || parsedHeaders["Location"] || null;
          if (location) {
            var nextUrl;
            try {
              nextUrl = new URL(location, request.url).href;
            } catch (e) {
              cleanupAbort();
              reject(new TypeError("fetch failed: invalid redirect location"));
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
            if (status === 303 || status === 301 && request.method === "POST" || status === 302 && request.method === "POST") {
              nextInit.method = "GET";
            } else {
              nextInit.body = request.body;
            }
            var nextReq = new Request2(nextUrl, nextInit);
            cleanupAbort();
            abandoned = true;
            doRequest(nextReq, resolve, reject, redirectCount + 1);
            return;
          }
        }
        var headers = new Headers(parsedHeaders);
        var resp = new Response2(readableStream, {
          status,
          statusText: STATUS_TEXTS[status] || "",
          headers,
          url: request.url
        });
        resolve(resp);
      }
      function onData(chunk) {
        if (aborted || abandoned) return;
        if (streamController) {
          var arr;
          if (chunk instanceof ArrayBuffer) {
            arr = new Uint8Array(chunk);
          } else if (chunk instanceof Uint8Array) {
            arr = chunk;
          } else {
            arr = stringToUint8Array(String(chunk));
          }
          streamController.enqueue(arr);
        }
      }
      function onEnd(errorStatus) {
        cleanupAbort();
        if (aborted || abandoned) return;
        if (streamController) {
          if (errorStatus !== 0) {
            streamController.error(new TypeError("fetch failed with status: " + errorStatus));
          } else {
            streamController.close();
          }
        }
      }
      pal2.httpRequestStream(request.url, request.method, headersJson, bodyStr, onHeaders, onData, onEnd);
    }
    globalThis.Headers = Headers;
    globalThis.Request = Request2;
    globalThis.Response = Response2;
    globalThis.fetch = fetch;
  }

  // src/fs.js
  function setupFS(pal2) {
    if (!globalThis.qwrt) globalThis.qwrt = {};
    var fs = {
      async readFile(path, options) {
        var data = await pal2.fsRead(path);
        if (options && options.encoding) {
          return data;
        }
        return data;
      },
      /* Binary-safe read: resolves with an ArrayBuffer of the raw bytes. */
      async readFileBinary(path) {
        return await pal2.fsReadBinary(path);
      },
      async writeFile(path, data, options) {
        await pal2.fsWrite(path, typeof data === "string" ? data : String(data));
      },
      async exists(path) {
        var result = await pal2.fsExists(path);
        return result === "true";
      },
      async readdir(path) {
        var result = await pal2.fsList(path);
        return JSON.parse(result);
      },
      async unlink(path) {
        await pal2.fsRemove(path);
      },
      // Sync-style aliases (still async under the hood)
      readFileSync(path, options) {
        throw new Error("Synchronous fs operations not supported in qwrt");
      }
    };
    globalThis.qwrt.fs = fs;
  }

  // src/storage.js
  function setupStorage(pal2) {
    if (!globalThis.qwrt) globalThis.qwrt = {};
    var storage = {
      async get(key) {
        var value = await pal2.storageGet(key);
        return value;
      },
      async set(key, value) {
        await pal2.storageSet(key, String(value));
      },
      async delete(key) {
        await pal2.storageDel(key);
      }
    };
    globalThis.qwrt.storage = storage;
  }

  // src/text-encoding.js
  function setupTextEncoding(pal2) {
    var useNativeEncode = typeof pal2.nativeEncodeUtf8 === "function";
    var useNativeDecode = typeof pal2.nativeDecodeUtf8 === "function";
    function TextEncoder2() {
      this.encoding = "utf-8";
    }
    TextEncoder2.prototype.encode = function encode(input) {
      var str = input === void 0 ? "" : String(input);
      if (useNativeEncode) {
        return pal2.nativeEncodeUtf8(str);
      }
      var bytes = [];
      for (var i = 0; i < str.length; i++) {
        var code = str.charCodeAt(i);
        if (code < 128) {
          bytes.push(code);
        } else if (code < 2048) {
          bytes.push(192 | code >> 6, 128 | code & 63);
        } else if (code >= 55296 && code <= 56319) {
          var hi = code;
          var lo = str.charCodeAt(++i);
          var codePoint = (hi - 55296 << 10) + (lo - 56320) + 65536;
          bytes.push(
            240 | codePoint >> 18,
            128 | codePoint >> 12 & 63,
            128 | codePoint >> 6 & 63,
            128 | codePoint & 63
          );
        } else {
          bytes.push(224 | code >> 12, 128 | code >> 6 & 63, 128 | code & 63);
        }
      }
      return new Uint8Array(bytes);
    };
    TextEncoder2.prototype.encodeInto = function encodeInto(src, dst) {
      src = String(src);
      if (!(dst instanceof Uint8Array)) {
        throw new TypeError("encodeInto: destination must be a Uint8Array");
      }
      var srcLen = src.length;
      var dstLen = dst.length;
      var read = 0;
      var written = 0;
      var i = 0;
      var o = 0;
      while (i < srcLen && o < dstLen) {
        var code = src.charCodeAt(i);
        if (code < 128) {
          if (o + 1 > dstLen) break;
          dst[o] = code;
          o += 1;
          i += 1;
          written += 1;
          read += 1;
        } else if (code >= 55296 && code <= 56319) {
          if (i + 1 < srcLen) {
            var lo = src.charCodeAt(i + 1);
            if (lo >= 56320 && lo <= 57343) {
              if (o + 4 > dstLen) break;
              var cp = (code - 55296 << 10) + (lo - 56320) + 65536;
              dst[o] = 240 | cp >> 18;
              dst[o + 1] = 128 | cp >> 12 & 63;
              dst[o + 2] = 128 | cp >> 6 & 63;
              dst[o + 3] = 128 | cp & 63;
              o += 4;
              i += 2;
              written += 4;
              read += 2;
              continue;
            }
          }
          if (o + 3 > dstLen) break;
          dst[o] = 239;
          dst[o + 1] = 191;
          dst[o + 2] = 189;
          o += 3;
          i += 1;
          written += 3;
          read += 1;
        } else if (code >= 56320 && code <= 57343) {
          if (o + 3 > dstLen) break;
          dst[o] = 239;
          dst[o + 1] = 191;
          dst[o + 2] = 189;
          o += 3;
          i += 1;
          written += 3;
          read += 1;
        } else if (code < 2048) {
          if (o + 2 > dstLen) break;
          dst[o] = 192 | code >> 6;
          dst[o + 1] = 128 | code & 63;
          o += 2;
          i += 1;
          written += 2;
          read += 1;
        } else {
          if (o + 3 > dstLen) break;
          dst[o] = 224 | code >> 12;
          dst[o + 1] = 128 | code >> 6 & 63;
          dst[o + 2] = 128 | code & 63;
          o += 3;
          i += 1;
          written += 3;
          read += 1;
        }
      }
      return { read, written };
    };
    function TextDecoder2(encoding, options) {
      var label = (encoding || "utf-8").toLowerCase();
      this.fatal = options && options.fatal || false;
      this.ignoreBOM = options && options.ignoreBOM || false;
      this._buffer = null;
      this._BOMSeen = false;
      if (label === "unicode-1-1-utf-8" || label === "utf-8" || label === "utf8") {
        this.encoding = "utf-8";
        this._decoder = "utf8";
      } else if (label === "replacement") {
        throw new RangeError('The "replacement" label is not a valid encoding label');
      } else if (0) {
        if (label === "iso-8859-1" || label === "iso_8859-1" || label === "latin1" || label === "l1" || label === "ibm819" || label === "cp819" || label === "csisolatin1" || label === "iso-ir-100" || label === "windows-28591") {
          this.encoding = "windows-1252";
          this._decoder = "latin1";
        } else if (this.fatal) {
          throw new RangeError('Encoding "' + encoding + '" not supported');
        } else {
          this.encoding = "utf-8";
          this._decoder = "utf8";
        }
      } else {
        if (this.fatal) {
          throw new RangeError('Encoding "' + encoding + '" not supported');
        }
        this.encoding = "utf-8";
        this._decoder = "utf8";
      }
    }
    function utf8LeadLen(byte) {
      if (byte < 128) return 0;
      if (byte < 192) return -1;
      if (byte < 224) return 2;
      if (byte < 240) return 3;
      if (byte < 248) return 4;
      return -1;
    }
    TextDecoder2.prototype.decode = function decode(input, options) {
      var streamMode = options && options.stream;
      var bytes = input instanceof Uint8Array ? input : new Uint8Array(input || new Uint8Array(0));
      if (this._decoder === "latin1") {
        var str = "";
        for (var i = 0; i < bytes.length; i++) {
          str += String.fromCharCode(bytes[i] & 255);
        }
        return str;
      }
      if (false) {
        return pal2.nativeDecodeUtf8(bytes);
      }
      var allBytes;
      if (this._buffer && this._buffer.length > 0) {
        allBytes = new Uint8Array(this._buffer.length + bytes.length);
        allBytes.set(new Uint8Array(this._buffer), 0);
        allBytes.set(bytes, this._buffer.length);
        this._buffer = null;
      } else {
        allBytes = bytes;
      }
      if (this._decoder === "utf8" && !this._BOMSeen && !this.ignoreBOM && allBytes.length >= 3 && allBytes[0] === 239 && allBytes[1] === 187 && allBytes[2] === 191) {
        allBytes = allBytes.subarray(3);
      }
      if (!this._BOMSeen && allBytes.length > 0) {
        this._BOMSeen = true;
      }
      var str = "";
      var i = 0;
      var lastComplete = 0;
      while (i < allBytes.length) {
        var lead = allBytes[i];
        var want = utf8LeadLen(lead);
        if (want < 0) {
          if (this.fatal) throw new TypeError("TextDecoder: invalid UTF-8 sequence");
          str += "\uFFFD";
          i++;
          continue;
        }
        if (want === 0) {
          str += String.fromCharCode(lead);
          i++;
          continue;
        }
        var ok = true;
        if (want >= 2 && i + 1 < allBytes.length) {
          var b1 = allBytes[i + 1];
          if (lead === 224 && b1 < 160 || lead === 237 && b1 > 159 || lead === 240 && b1 < 144 || lead === 244 && b1 > 143) {
            ok = false;
          }
        }
        if (!ok) {
          if (this.fatal) throw new TypeError("TextDecoder: invalid UTF-8 sequence");
          str += "\uFFFD";
          i++;
          continue;
        }
        if (i + want > allBytes.length) {
          if (streamMode) {
            var bufWant = Math.min(want, allBytes.length - i);
            this._buffer = [];
            for (var k = i; k < i + bufWant; k++) this._buffer.push(allBytes[k]);
            i += bufWant;
            if (i >= allBytes.length) break;
            continue;
          }
          if (this.fatal) throw new TypeError("TextDecoder: incomplete UTF-8 sequence");
          str += "\uFFFD";
          i++;
          var availCont = 0;
          while (i + availCont < allBytes.length && (allBytes[i + availCont] & 192) === 128) {
            availCont++;
          }
          if (availCont > 0 && availCont <= want - 1) {
            i += availCont;
          }
          continue;
        }
        var ok = true;
        for (var j = 1; j < want && i + j < allBytes.length; j++) {
          if ((allBytes[i + j] & 192) !== 128) {
            ok = false;
            break;
          }
        }
        if (!ok) {
          if (this.fatal) throw new TypeError("TextDecoder: invalid UTF-8 sequence");
          str += "\uFFFD";
          i++;
          continue;
        }
        if (want === 2) {
          str += String.fromCharCode((lead & 31) << 6 | allBytes[i + 1] & 63);
        } else if (want === 3) {
          str += String.fromCharCode(
            (lead & 15) << 12 | (allBytes[i + 1] & 63) << 6 | allBytes[i + 2] & 63
          );
        } else {
          var codepoint = (lead & 7) << 18 | (allBytes[i + 1] & 63) << 12 | (allBytes[i + 2] & 63) << 6 | allBytes[i + 3] & 63;
          str += String.fromCodePoint(codepoint);
        }
        i += want;
      }
      return str;
    };
    globalThis.TextEncoder = TextEncoder2;
    globalThis.TextDecoder = TextDecoder2;
  }

  // src/crypto.js
  function setupCrypto(pal2) {
    class Crypto {
      constructor() {
        this.subtle = void 0;
      }
      getRandomValues(typedArray) {
        if (!(typedArray instanceof Uint8Array) && !(typedArray instanceof Uint16Array) && !(typedArray instanceof Uint32Array) && !(typedArray instanceof Int8Array) && !(typedArray instanceof Int16Array) && !(typedArray instanceof Int32Array)) {
          throw new TypeError("Argument must be a TypedArray");
        }
        var totalBytes = typedArray.length * typedArray.BYTES_PER_ELEMENT;
        if (totalBytes > 65536) {
          throw new DOMException("getRandomValues: requested length exceeds 65536 bytes", "QuotaExceededError");
        }
        var ab = pal2.randomBytes(totalBytes);
        var src = new Uint8Array(ab);
        var dst = new Uint8Array(typedArray.buffer, typedArray.byteOffset, totalBytes);
        dst.set(src);
        return typedArray;
      }
      randomUUID() {
        var bytes = new Uint8Array(16);
        this.getRandomValues(bytes);
        bytes[6] = bytes[6] & 15 | 64;
        bytes[8] = bytes[8] & 63 | 128;
        var hex = Array.from(bytes, function(b) {
          return b.toString(16).padStart(2, "0");
        }).join("");
        return hex.slice(0, 8) + "-" + hex.slice(8, 12) + "-" + hex.slice(12, 16) + "-" + hex.slice(16, 20) + "-" + hex.slice(20);
      }
    }
    globalThis.crypto = new Crypto();
    globalThis.Crypto = Crypto;
  }

  // src/error-events.js
  function setupErrorEvents() {
    if (typeof globalThis.Event !== "function") {
      throw new Error("ErrorEvent requires Event to be loaded first");
    }
    class ErrorEvent extends Event {
      constructor(type, options) {
        super(type, options);
        this._message = options?.message ?? "";
        this._filename = options?.filename ?? "";
        this._lineno = options?.lineno ?? 0;
        this._colno = options?.colno ?? 0;
        this._error = options?.error ?? null;
      }
      get message() {
        return this._message;
      }
      get filename() {
        return this._filename;
      }
      get lineno() {
        return this._lineno;
      }
      get colno() {
        return this._colno;
      }
      get error() {
        return this._error;
      }
    }
    class PromiseRejectionEvent extends Event {
      constructor(type, options) {
        super(type, { cancelable: type === "unhandledrejection" });
        this._promise = options?.promise ?? null;
        this._reason = options?.reason ?? void 0;
      }
      get promise() {
        return this._promise;
      }
      get reason() {
        return this._reason;
      }
    }
    globalThis.ErrorEvent = ErrorEvent;
    globalThis.PromiseRejectionEvent = PromiseRejectionEvent;
  }

  // src/message-channel.js
  function setupMessageChannel(pal2) {
    if (typeof globalThis.EventTarget !== "function") {
      throw new Error("MessagePort requires EventTarget to be loaded first");
    }
    var portRegistry = /* @__PURE__ */ new Map();
    var inWorker = typeof pal2.workerClose === "function";
    function registerPort(p) {
      if (p._id) portRegistry.set(p._id, p);
    }
    function lookupPort(id) {
      return portRegistry.get(id);
    }
    class MessageEvent2 extends Event {
      constructor(type, options) {
        super(type, options);
        this._data = options?.data ?? null;
        this._origin = options?.origin ?? "";
        this._lastEventId = options?.lastEventId ?? "";
        this._source = options?.source ?? null;
        this._ports = options?.ports ?? [];
      }
      get data() {
        return this._data;
      }
      get origin() {
        return this._origin;
      }
      get lastEventId() {
        return this._lastEventId;
      }
      get source() {
        return this._source;
      }
      get ports() {
        return this._ports;
      }
    }
    class MessagePort2 extends EventTarget {
      constructor(id, peerId) {
        super();
        this._id = id;
        this._peerId = peerId;
        this._entangledPort = null;
        this._peerThread = "local";
        this._detached = false;
        this._started = false;
        this._messageQueue = [];
        this._onmessage = null;
        this._onmessageerror = null;
      }
      get onmessage() {
        return this._onmessage;
      }
      set onmessage(fn) {
        if (this._onmessage) {
          this.removeEventListener("message", this._onmessage);
        }
        this._onmessage = fn;
        if (fn) {
          this.addEventListener("message", fn);
        }
        this._start();
      }
      get onmessageerror() {
        return this._onmessageerror;
      }
      set onmessageerror(fn) {
        if (this._onmessageerror) {
          this.removeEventListener("messageerror", this._onmessageerror);
        }
        this._onmessageerror = fn;
        if (fn) {
          this.addEventListener("messageerror", fn);
        }
      }
      /* 跨线程发送：消息字节 → 包装 {__qwrt_port_msg} → 经现有通道投递到对端线程 */
      _sendRemote(payloadBytes) {
        var wrapped = __qwrt_serialize__(
          { __qwrt_port_msg: { target: this._peerId, payload: payloadBytes } }
        );
        if (inWorker) {
          pal2.postMessage(wrapped);
        } else if (this._peerThread > 0) {
          pal2.workerPost(this._peerThread, wrapped);
        } else {
          throw new Error("MessagePort: cannot route to parent from parent");
        }
      }
      postMessage(message, transfer) {
        if (this._detached) {
          throw new Error("MessagePort: port is detached");
        }
        if (this._peerThread === "local") {
          if (!this._entangledPort) return;
          var data;
          try {
            data = typeof globalThis.structuredClone === "function" ? globalThis.structuredClone(message, transfer ? { transfer } : void 0) : JSON.parse(JSON.stringify(message));
          } catch (e) {
            var errorEvent = new MessageEvent2("messageerror", { data: e });
            this._entangledPort.dispatchEvent(errorEvent);
            return;
          }
          var event = new MessageEvent2("message", { data, ports: [] });
          if (this._entangledPort._started) {
            this._entangledPort.dispatchEvent(event);
          } else {
            this._entangledPort._messageQueue.push(event);
          }
        } else {
          var bytes = __qwrt_serialize__(message, transfer);
          this._sendRemote(bytes);
        }
      }
      /* 入站：接收跨线程 port 消息（payload 是序列化字节） */
      _deliverRemote(payloadBytes) {
        var v;
        try {
          v = __qwrt_deserialize__(payloadBytes);
        } catch (err) {
          var errEvent = new MessageEvent2("messageerror", { data: err });
          this.dispatchEvent(errEvent);
          return;
        }
        var event = new MessageEvent2("message", { data: v, ports: [] });
        if (this._started) {
          this.dispatchEvent(event);
        } else {
          this._messageQueue.push(event);
        }
      }
      start() {
        this._start();
      }
      _start() {
        if (this._started) return;
        this._started = true;
        for (var i = 0; i < this._messageQueue.length; i++) {
          this.dispatchEvent(this._messageQueue[i]);
        }
        this._messageQueue = [];
      }
      close() {
        this._detached = true;
        this._entangledPort = null;
        this._started = false;
        this._messageQueue = [];
      }
    }
    class MessageChannel {
      constructor() {
        var ids = pal2.portCreate();
        this._port1 = new MessagePort2(ids.id1, ids.id2);
        this._port2 = new MessagePort2(ids.id2, ids.id1);
        this._port1._entangledPort = this._port2;
        this._port2._entangledPort = this._port1;
        registerPort(this._port1);
        registerPort(this._port2);
      }
      get port1() {
        return this._port1;
      }
      get port2() {
        return this._port2;
      }
    }
    globalThis.MessageChannel = MessageChannel;
    globalThis.MessagePort = MessagePort2;
    globalThis.MessageEvent = MessageEvent2;
    globalThis.__qwrt_lookup_port__ = lookupPort;
    globalThis.__qwrt_deliver_port_msg__ = function(msg) {
      if (!msg || typeof msg !== "object" || !msg.__qwrt_port_msg) return false;
      var pm = msg.__qwrt_port_msg;
      var target = pm && typeof pm === "object" ? pm.target : null;
      var payload = pm && typeof pm === "object" ? pm.payload : null;
      var port = target !== null && target !== void 0 ? lookupPort(target) : null;
      if (!port) return false;
      port._deliverRemote(payload);
      return true;
    };
    globalThis.__qwrt_port_from_ref__ = function(info) {
      if (!info || info.id === void 0 || info.id === null) {
        throw new DOMException("invalid MessagePort reference", "DataCloneError");
      }
      var p = new MessagePort2(info.id, info.peerId);
      p._detached = false;
      var peer = lookupPort(info.peerId);
      if (peer && peer !== p) {
        p._peerThread = "local";
        p._entangledPort = peer;
        peer._peerThread = "local";
        peer._entangledPort = p;
      } else {
        p._peerThread = info.peerThread || "parent";
      }
      registerPort(p);
      return p;
    };
  }

  // src/broadcast-channel.js
  function setupBroadcastChannel() {
    var channels = /* @__PURE__ */ new Map();
    class BroadcastChannel extends EventTarget {
      constructor(name) {
        super();
        if (typeof name !== "string") name = String(name);
        this._name = name;
        this._closed = false;
        this._onmessage = null;
        if (!channels.has(name)) channels.set(name, /* @__PURE__ */ new Set());
        channels.get(name).add(this);
      }
      get name() {
        return this._name;
      }
      get onmessage() {
        return this._onmessage;
      }
      set onmessage(fn) {
        if (this._onmessage) this.removeEventListener("message", this._onmessage);
        this._onmessage = fn;
        if (typeof fn === "function") this.addEventListener("message", fn);
      }
      postMessage(message) {
        if (this._closed) return;
        var peers = channels.get(this._name);
        if (!peers) return;
        var data;
        try {
          data = typeof structuredClone === "function" ? structuredClone(message) : message;
        } catch (e) {
          data = message;
        }
        peers.forEach(function(peer) {
          if (peer !== this && !peer._closed) {
            peer.dispatchEvent(new MessageEvent("message", { data }));
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
          this.removeEventListener("message", this._onmessage);
          this._onmessage = null;
        }
      }
    }
    globalThis.BroadcastChannel = BroadcastChannel;
  }

  // src/cache-storage.js
  function setupCacheStorage() {
    function urlKey(request) {
      if (typeof request === "string") return request;
      if (request instanceof Request) return request.url;
      return String(request);
    }
    class Cache {
      constructor(name) {
        this._name = name;
        this._map = /* @__PURE__ */ new Map();
      }
      put(request, response) {
        if (!(response instanceof Response)) {
          return Promise.reject(new TypeError("Cache.put: response must be a Response"));
        }
        var key = urlKey(request);
        this._map.set(key, response);
        return Promise.resolve();
      }
      match(request) {
        var key = urlKey(request);
        var r = this._map.get(key);
        return Promise.resolve(r ? r.clone() : void 0);
      }
      matchAll(request) {
        var key = request ? urlKey(request) : null;
        var results = [];
        this._map.forEach(function(v, k) {
          if (!key || k === key) results.push(v.clone());
        });
        return Promise.resolve(results);
      }
      delete(request) {
        var key = urlKey(request);
        return Promise.resolve(this._map.delete(key));
      }
      keys(request) {
        var key = request ? urlKey(request) : null;
        var results = [];
        this._map.forEach(function(v, k) {
          if (!key || k === key) results.push(k);
        });
        return Promise.resolve(results);
      }
    }
    class CacheStorage {
      constructor() {
        this._caches = /* @__PURE__ */ new Map();
      }
      open(name) {
        if (!this._caches.has(name)) {
          this._caches.set(name, new Cache(name));
        }
        return Promise.resolve(this._caches.get(name));
      }
      has(name) {
        return Promise.resolve(this._caches.has(name));
      }
      delete(name) {
        return Promise.resolve(this._caches.delete(name));
      }
      keys() {
        return Promise.resolve(Array.from(this._caches.keys()));
      }
    }
    globalThis.Cache = Cache;
    globalThis.CacheStorage = CacheStorage;
    globalThis.caches = new CacheStorage();
  }

  // src/event-source.js
  function setupEventSource(pal2) {
    if (typeof pal2.httpRequestStream !== "function") return;
    class EventSource {
      constructor(url, eventSourceInitDict) {
        this._url = url;
        this._reconnectDelay = 3e3;
        this._lastEventId = "";
        this._readyState = 0;
        this._closed = false;
        this._buffer = "";
        this.onopen = null;
        this.onmessage = null;
        this.onerror = null;
        this._connect();
      }
      get CONNECTING() {
        return 0;
      }
      get OPEN() {
        return 1;
      }
      get CLOSED() {
        return 2;
      }
      get url() {
        return this._url;
      }
      get readyState() {
        return this._readyState;
      }
      get withCredentials() {
        return false;
      }
      _connect() {
        if (this._closed) return;
        this._readyState = 0;
        this._buffer = "";
        var self = this;
        var headersJson = JSON.stringify({
          "Accept": "text/event-stream",
          "Cache-Control": "no-cache"
        });
        function onHeaders(status) {
          if (status === 200) {
            self._readyState = 1;
            if (typeof self.onopen === "function") {
              try {
                self.onopen(new Event("open"));
              } catch (e) {
              }
            }
          }
        }
        function onData(chunk) {
          if (self._readyState !== 1) return;
          var uint8 = new Uint8Array(chunk);
          var text = "";
          for (var i = 0; i < uint8.length; i++) {
            text += String.fromCharCode(uint8[i]);
          }
          self._buffer += text;
          self._processBuffer();
        }
        function onEnd(errorStatus) {
          if (self._closed) return;
          self._readyState = 2;
          var ev = new Event("error");
          if (typeof self.onerror === "function") {
            try {
              self.onerror(ev);
            } catch (e) {
            }
          }
          if (!self._closed) {
            setTimeout(function() {
              self._connect();
            }, self._reconnectDelay);
          }
        }
        pal2.httpRequestStream(this._url, "GET", headersJson, "", onHeaders, onData, onEnd);
      }
      _processBuffer() {
        var lines = this._buffer.split("\n");
        this._buffer = lines.pop() || "";
        var eventType = null;
        var data = [];
        var id = null;
        for (var i = 0; i < lines.length; i++) {
          var line = lines[i];
          if (line === "") {
            if (data.length > 0) {
              var msgEvent = new MessageEvent(eventType || "message", {
                data: data.join("\n"),
                lastEventId: id || this._lastEventId
              });
              if (typeof this.onmessage === "function") {
                try {
                  this.onmessage(msgEvent);
                } catch (e) {
                }
              }
              this._lastEventId = id || this._lastEventId;
            }
            eventType = null;
            data = [];
            id = null;
          } else if (line.startsWith("data:")) {
            data.push(line.slice(5).trim());
          } else if (line.startsWith("event:")) {
            eventType = line.slice(6).trim();
          } else if (line.startsWith("id:")) {
            id = line.slice(3).trim();
          } else if (line.startsWith("retry:")) {
            var ms = parseInt(line.slice(6).trim(), 10);
            if (!isNaN(ms) && ms > 0) this._reconnectDelay = ms;
          }
        }
      }
      close() {
        this._closed = true;
        this._readyState = 2;
        this._buffer = "";
      }
    }
    globalThis.EventSource = EventSource;
  }

  // src/websocket.js
  function setupWebSocket(pal2) {
    if (typeof pal2.tcpConnect !== "function") return;
    var CONNECTING = 0;
    var OPEN = 1;
    var CLOSING = 2;
    var CLOSED = 3;
    var OPCODE_CONT = 0;
    var OPCODE_TEXT = 1;
    var OPCODE_BINARY = 2;
    var OPCODE_CLOSE = 8;
    var OPCODE_PING = 9;
    var OPCODE_PONG = 10;
    function makeKey() {
      var key = new Uint8Array(16);
      crypto.getRandomValues(key);
      return btoa(String.fromCharCode.apply(null, key));
    }
    function computeAccept(key) {
      var MAGIC = "258EAFA5-E914-47DA-95CA-5AB5D3D5D5E5";
      var s = key + MAGIC;
      return crypto.subtle.digest("SHA-1", new TextEncoder().encode(s)).then(function(hash) {
        return btoa(String.fromCharCode.apply(null, new Uint8Array(hash)));
      });
    }
    function buildFrame(opcode, payload, mask) {
      var len = payload.length;
      var headerSize = 2;
      if (len >= 126) headerSize += len < 65536 ? 2 : 8;
      var maskLen = mask ? 4 : 0;
      var frame = new Uint8Array(headerSize + maskLen + len);
      var pos = 0;
      frame[pos++] = 128 | opcode;
      if (len < 126) {
        frame[pos++] = (mask ? 128 : 0) | len;
      } else if (len < 65536) {
        frame[pos++] = (mask ? 128 : 0) | 126;
        frame[pos++] = len >> 8 & 255;
        frame[pos++] = len & 255;
      } else {
        frame[pos++] = (mask ? 128 : 0) | 127;
        for (var i = 7; i >= 0; i--) frame[pos++] = len >> i * 8 & 255;
      }
      if (mask) {
        var maskKey = new Uint8Array(4);
        crypto.getRandomValues(maskKey);
        frame.set(maskKey, pos);
        pos += 4;
        for (var i = 0; i < len; i++)
          frame[pos + i] = payload[i] ^ maskKey[i % 4];
      } else {
        frame.set(payload, pos);
      }
      return frame.buffer;
    }
    class WebSocket {
      constructor(url) {
        this._url = url;
        this._readyState = CONNECTING;
        this._onopen = null;
        this._onmessage = null;
        this._onerror = null;
        this._onclose = null;
        this._protocol = "";
        this._tcp = null;
        this._buf = null;
        this._bufView = null;
        this._handshakeDone = false;
        this._closeSent = false;
        this._closeCode = 1e3;
        this._closeReason = "";
        var isSecure = url.indexOf("wss://") === 0;
        if (isSecure) throw new Error("wss:// not supported yet");
        if (url.indexOf("ws://") !== 0) throw new Error("invalid WebSocket URL: " + url);
        var rest = url.slice(5);
        var slashIdx = rest.indexOf("/");
        var hostPort = slashIdx >= 0 ? rest.slice(0, slashIdx) : rest;
        var path = slashIdx >= 0 ? rest.slice(slashIdx) : "/";
        var colonIdx = hostPort.indexOf(":");
        var host = colonIdx >= 0 ? hostPort.slice(0, colonIdx) : hostPort;
        var port = colonIdx >= 0 ? parseInt(hostPort.slice(colonIdx + 1), 10) : 80;
        if (!port || port < 1) port = 80;
        var self = this;
        this._key = makeKey();
        this._host = host;
        this._port = port;
        this._path = path;
        this._tcp = pal2.tcpConnect(host, port, {
          onconnect: function() {
            self._onConnect();
          },
          ondata: function(data) {
            self._onData(data);
          },
          onerror: function(msg) {
            self._onError(msg);
          },
          onclose: function() {
            self._onTcpClose();
          }
        });
      }
      // ── Data accumulation ──
      _appendData(data) {
        var incoming = new Uint8Array(data);
        if (!this._buf) {
          this._buf = data;
          this._bufView = incoming;
          this._bufPos = 0;
        } else {
          var merged = new Uint8Array(this._buf.byteLength + incoming.length);
          merged.set(this._bufView, 0);
          merged.set(incoming, this._bufView.length);
          this._buf = merged.buffer;
          this._bufView = merged;
        }
      }
      _consume(n) {
        if (n >= this._bufView.length) {
          this._buf = null;
          this._bufView = null;
          return;
        }
        this._bufView = this._bufView.subarray(n);
        this._buf = this._bufView.buffer;
      }
      // ── Incoming data handler ──
      _onConnect() {
        var req = "GET " + this._path + " HTTP/1.1\r\nHost: " + this._host + ":" + this._port + "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + this._key + "\r\nSec-WebSocket-Version: 13\r\n\r\n";
        pal2.tcpWrite(this._tcp, req);
      }
      _onData(data) {
        try {
          this._appendData(data);
          if (this._handshakeDone) {
            this._parseFrames();
          } else {
            this._parseHandshake();
          }
        } catch (e) {
          this._fail(e.message);
        }
      }
      // ── Handshake ──
      _parseHandshake() {
        var view = this._bufView;
        var headerEnd = -1;
        for (var i = 0; i + 3 < view.length; i++) {
          if (view[i] === 13 && view[i + 1] === 10 && view[i + 2] === 13 && view[i + 3] === 10) {
            headerEnd = i + 4;
            break;
          }
        }
        if (headerEnd < 0) return;
        var headerStr = new TextDecoder().decode(view.subarray(0, headerEnd));
        var lines = headerStr.split("\r\n");
        if (lines[0].indexOf("101") < 0) {
          this._fail("unexpected HTTP status: " + lines[0]);
          return;
        }
        var accept = null;
        for (var j = 1; j < lines.length; j++) {
          var l = lines[j].toLowerCase();
          if (l.indexOf("sec-websocket-accept:") === 0) {
            accept = lines[j].split(":")[1].trim();
            break;
          }
        }
        var self = this;
        computeAccept(this._key).then(function(expected) {
          if (accept !== expected) {
            self._fail("invalid Sec-WebSocket-Accept");
            return;
          }
          self._consume(headerEnd);
          self._handshakeDone = true;
          self._readyState = OPEN;
          if (typeof self._onopen === "function") {
            try {
              self._onopen(new Event("open"));
            } catch (e) {
            }
          }
          if (self._bufView) self._parseFrames();
        }).catch(function(e) {
          self._fail("SHA-1 failed: " + (e && e.message ? e.message : String(e)));
        });
      }
      // ── Frame parsing ──
      _parseFrames() {
        while (this._bufView && this._bufView.length >= 2) {
          var view = this._bufView;
          var b0 = view[0];
          var b1 = view[1];
          var fin = (b0 & 128) !== 0;
          var opcode = b0 & 15;
          var masked = (b1 & 128) !== 0;
          var len = b1 & 127;
          var offset = 2;
          if (len === 126) {
            if (view.length < 4) break;
            len = view[2] << 8 | view[3];
            offset = 4;
          } else if (len === 127) {
            if (view.length < 10) break;
            len = 0;
            for (var i = 0; i < 8; i++) len = len << 8 | view[2 + i];
            offset = 10;
          }
          var maskKey = null;
          if (masked) {
            if (view.length < offset + 4) break;
            maskKey = view.subarray(offset, offset + 4);
            offset += 4;
          }
          if (view.length < offset + len) break;
          var payload = view.subarray(offset, offset + len);
          if (masked && maskKey) {
            for (var i = 0; i < len; i++) payload[i] ^= maskKey[i % 4];
          }
          this._consume(offset + len);
          this._handleFrame(fin, opcode, payload);
        }
      }
      _handleFrame(fin, opcode, payload) {
        if (opcode === OPCODE_TEXT || opcode === OPCODE_BINARY) {
          var text = new TextDecoder().decode(payload);
          if (typeof this._onmessage === "function") {
            try {
              this._onmessage(new MessageEvent("message", { data: text }));
            } catch (e) {
            }
          }
        } else if (opcode === OPCODE_CLOSE) {
          var code = 1e3;
          var reason = "";
          if (payload.length >= 2) {
            code = payload[0] << 8 | payload[1];
            if (payload.length > 2)
              reason = new TextDecoder().decode(payload.subarray(2));
          }
          if (!this._closeSent) {
            var closePayload = new Uint8Array(2 + reason.length);
            closePayload[0] = code >> 8 & 255;
            closePayload[1] = code & 255;
            for (var i = 0; i < reason.length; i++)
              closePayload[2 + i] = reason.charCodeAt(i);
            var frame = buildFrame(OPCODE_CLOSE, closePayload, true);
            pal2.tcpWrite(this._tcp, frame);
          }
          this._readyState = CLOSED;
          if (typeof this._onclose === "function") {
            try {
              this._onclose(new CloseEvent("close", { code, reason, wasClean: true }));
            } catch (e) {
            }
          }
          pal2.tcpClose(this._tcp);
        } else if (opcode === OPCODE_PING) {
          var pong = buildFrame(OPCODE_PONG, payload, true);
          pal2.tcpWrite(this._tcp, pong);
        } else if (opcode === OPCODE_PONG) {
        } else if (opcode === OPCODE_CONT) {
        }
      }
      // ── Error handling ──
      _onError(msg) {
        if (this._readyState === CLOSED) return;
        this._readyState = CLOSED;
        if (typeof this._onerror === "function") {
          try {
            this._onerror(new Event("error"));
          } catch (e) {
          }
        }
      }
      _fail(msg) {
        this._readyState = CLOSED;
        if (typeof this._onerror === "function") {
          try {
            this._onerror(new Event("error"));
          } catch (e) {
          }
        }
      }
      _onTcpClose() {
        if (this._readyState === CLOSED) return;
        this._readyState = CLOSED;
        if (typeof this._onclose === "function") {
          try {
            this._onclose(new CloseEvent("close", { code: 1006, reason: "connection closed", wasClean: false }));
          } catch (e) {
          }
        }
      }
      // ── Public API ──
      get url() {
        return this._url;
      }
      get readyState() {
        return this._readyState;
      }
      get protocol() {
        return this._protocol;
      }
      get CONNECTING() {
        return CONNECTING;
      }
      get OPEN() {
        return OPEN;
      }
      get CLOSING() {
        return CLOSING;
      }
      get CLOSED() {
        return CLOSED;
      }
      get onopen() {
        return this._onopen;
      }
      set onopen(fn) {
        this._onopen = fn;
      }
      get onmessage() {
        return this._onmessage;
      }
      set onmessage(fn) {
        this._onmessage = fn;
      }
      get onerror() {
        return this._onerror;
      }
      set onerror(fn) {
        this._onerror = fn;
      }
      get onclose() {
        return this._onclose;
      }
      set onclose(fn) {
        this._onclose = fn;
      }
      send(data) {
        if (this._readyState !== OPEN) return;
        var encoded = new TextEncoder().encode(String(data));
        var frame = buildFrame(OPCODE_TEXT, encoded, true);
        pal2.tcpWrite(this._tcp, frame);
      }
      close(code, reason) {
        if (this._readyState === CLOSING || this._readyState === CLOSED) return;
        this._readyState = CLOSING;
        this._closeSent = true;
        this._closeCode = code || 1e3;
        this._closeReason = reason || "";
        var reasonBytes = new TextEncoder().encode(this._closeReason);
        var payload = new Uint8Array(2 + reasonBytes.length);
        payload[0] = this._closeCode >> 8 & 255;
        payload[1] = this._closeCode & 255;
        if (reasonBytes.length > 0) payload.set(reasonBytes, 2);
        var frame = buildFrame(OPCODE_CLOSE, payload, true);
        pal2.tcpWrite(this._tcp, frame);
      }
    }
    globalThis.WebSocket = WebSocket;
    globalThis.CloseEvent = globalThis.CloseEvent || class CloseEvent extends Event {
      constructor(type, init) {
        super(type, init);
        this.code = init && init.code || 1e3;
        this.reason = init && init.reason || "";
        this.wasClean = init && init.wasClean || false;
      }
    };
  }

  // src/http-server.js
  function setupHttpServer(pal2) {
    if (typeof pal2.tcpListen !== "function") return;
    var WS_GUID = "258EAFA5-E914-47DA-95CA-5AB5D3D5D5E5";
    var activeInstance = null;
    var b64chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    function b64encode(buf) {
      var s = "", i = 0;
      while (i + 2 < buf.length) {
        var b = buf[i] << 16 | buf[i + 1] << 8 | buf[i + 2];
        s += b64chars[b >> 18 & 63] + b64chars[b >> 12 & 63] + b64chars[b >> 6 & 63] + b64chars[b & 63];
        i += 3;
      }
      if (i < buf.length) {
        var b2 = buf[i] << 16;
        var rem = 1;
        if (i + 1 < buf.length) {
          b2 |= buf[i + 1] << 8;
          rem = 2;
        }
        s += b64chars[b2 >> 18 & 63] + b64chars[b2 >> 12 & 63];
        if (rem === 2) s += b64chars[b2 >> 6 & 63] + "=";
        else s += "==";
      }
      return s;
    }
    function sha1Bytes(bytes) {
      function rotl(n, b2) {
        return (n << b2 | n >>> 32 - b2) >>> 0;
      }
      var H = [1732584193, 4023233417, 2562383102, 271733878, 3285377520];
      var ml = bytes.length * 8;
      var msg = new Uint8Array((bytes.length + 8 >> 6 << 6) + 64);
      msg.set(bytes, 0);
      msg[bytes.length] = 128;
      var mlb = ml;
      for (var i = 0; i < 8; i++) {
        msg[msg.length - 1 - i] = mlb & 255;
        mlb = Math.floor(mlb / 256);
      }
      for (var off = 0; off < msg.length; off += 64) {
        var W = new Array(80);
        for (var t = 0; t < 16; t++)
          W[t] = msg[off + 4 * t] << 24 | msg[off + 4 * t + 1] << 16 | msg[off + 4 * t + 2] << 8 | msg[off + 4 * t + 3];
        for (var t = 16; t < 80; t++)
          W[t] = rotl(W[t - 3] ^ W[t - 8] ^ W[t - 14] ^ W[t - 16], 1);
        var a = H[0], b = H[1], c = H[2], d = H[3], e = H[4];
        for (var t = 0; t < 80; t++) {
          var f, k;
          if (t < 20) {
            f = b & c | ~b & d;
            k = 1518500249;
          } else if (t < 40) {
            f = b ^ c ^ d;
            k = 1859775393;
          } else if (t < 60) {
            f = b & c | b & d | c & d;
            k = 2400959708;
          } else {
            f = b ^ c ^ d;
            k = 3395469782;
          }
          var tmp = rotl(a, 5) + f + e + k + W[t] >>> 0;
          e = d;
          d = c;
          c = rotl(b, 30);
          b = a;
          a = tmp;
        }
        H[0] = H[0] + a >>> 0;
        H[1] = H[1] + b >>> 0;
        H[2] = H[2] + c >>> 0;
        H[3] = H[3] + d >>> 0;
        H[4] = H[4] + e >>> 0;
      }
      var out = new Uint8Array(20);
      for (var i = 0; i < 5; i++) {
        out[4 * i] = H[i] >>> 24 & 255;
        out[4 * i + 1] = H[i] >>> 16 & 255;
        out[4 * i + 2] = H[i] >>> 8 & 255;
        out[4 * i + 3] = H[i] & 255;
      }
      return out;
    }
    function wsAccept(key) {
      var raw = new Uint8Array(key.length + WS_GUID.length);
      for (var i = 0; i < key.length; i++) raw[i] = key.charCodeAt(i);
      for (var i = 0; i < WS_GUID.length; i++) raw[key.length + i] = WS_GUID.charCodeAt(i);
      return b64encode(sha1Bytes(raw));
    }
    function parseRequest(raw) {
      var idx = raw.indexOf("\r\n");
      if (idx < 0) return null;
      var reqLine = raw.substring(0, idx);
      var parts = reqLine.split(" ");
      if (parts.length < 3) return null;
      var method = parts[0], path = parts[1], version = parts[2];
      var hdrEnd = raw.indexOf("\r\n\r\n");
      if (hdrEnd < 0) return null;
      var hdrSection = raw.substring(idx + 2, hdrEnd);
      var headers = {};
      hdrSection.split("\r\n").forEach(function(l) {
        var ci = l.indexOf(":");
        if (ci > 0) headers[l.substring(0, ci).toLowerCase()] = l.substring(ci + 1).trim();
      });
      var bodyStart = hdrEnd + 4;
      var body = raw.substring(bodyStart);
      var cl = parseInt(headers["content-length"], 10);
      var consumed = 0;
      if (!isNaN(cl)) {
        if (body.length < cl) return null;
        body = body.substring(0, cl);
        consumed = bodyStart + cl;
      } else {
        consumed = bodyStart + body.length;
      }
      var conn = (headers["connection"] || "").toLowerCase();
      var keepAlive = version !== "HTTP/1.0" && conn !== "close";
      return { method, path, version, headers, body, keepAlive, consumed };
    }
    function parseWSFrame(buf) {
      if (buf.length < 2) return null;
      var first = buf[0], second = buf[1];
      var fin = first >> 7 & 1;
      var opcode = first & 15;
      var masked = second >> 7 & 1;
      var len = second & 127;
      var offset = 2;
      if (len === 126) {
        if (buf.length < 4) return null;
        len = buf[2] << 8 | buf[3];
        offset = 4;
      } else if (len === 127) {
        if (buf.length < 10) return null;
        var hi = 0, lo = 0;
        for (var i = 0; i < 4; i++) hi = hi * 256 + buf[2 + i];
        for (var i = 4; i < 8; i++) lo = lo * 256 + buf[2 + i];
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
      return { fin, opcode, payload, totalLen: offset + len };
    }
    function buildWSFrame(opcode, payload, fin) {
      var len = payload.length;
      var header = [(fin ? 128 : 0) | opcode];
      if (len < 126) {
        header.push(len);
      } else if (len < 65536) {
        header.push(126, len >> 8 & 255, len & 255);
      } else {
        header.push(127);
        for (var i = 7; i >= 0; i--) header.push(len >> i * 8 & 255);
      }
      var frame = new Uint8Array(header.length + len);
      for (var i = 0; i < header.length; i++) frame[i] = header[i];
      for (var i = 0; i < len; i++) frame[header.length + i] = payload[i];
      return frame;
    }
    function buildHTTPResponse(status, statusText, hdrs, bodyBytes) {
      var h = "HTTP/1.1 " + status + " " + statusText + "\r\n";
      for (var k in hdrs) h += k + ": " + hdrs[k] + "\r\n";
      h += "\r\n";
      var enc = new TextEncoder();
      var hBytes = enc.encode(h);
      var out = new Uint8Array(hBytes.length + bodyBytes.length);
      out.set(hBytes, 0);
      out.set(bodyBytes, hBytes.length);
      return out;
    }
    globalThis.serve = function serve(options, handler) {
      if (typeof options !== "object" || options === null)
        throw new TypeError("serve: options object required");
      if (typeof handler !== "function")
        throw new TypeError("serve: handler must be a function");
      var port = options.port === void 0 ? 8080 : options.port;
      var idleTimeout = options.idleTimeout === void 0 ? 3e4 : options.idleTimeout;
      if (typeof port !== "number" || port < 0 || port > 65535)
        throw new TypeError("serve: invalid port");
      var hostname = options.hostname || "0.0.0.0";
      var wsRoutes = options.ws || {};
      var activeServer = { closed: false };
      if (activeInstance && !activeInstance.closed)
        throw new Error("serve: a server is already running (call srv.close() first)");
      activeInstance = activeServer;
      var currentKeepAlive = true;
      var currentResetIdle = function() {
      };
      function WSConnection(conn) {
        this.conn = conn;
        this.state = 0;
        this.buf = new Uint8Array(0);
        this.onopen = null;
        this.onmessage = null;
        this.onclose = null;
        this.onerror = null;
        this._fragOpcode = 0;
        this._fragParts = [];
      }
      WSConnection.prototype.send = function(data) {
        if (this.state !== 1) return;
        var payload = typeof data === "string" ? new TextEncoder().encode(data) : data || new Uint8Array(0);
        pal2.tcpWrite(this.conn, buildWSFrame(1, payload, 1));
      };
      WSConnection.prototype.close = function(code, reason) {
        if (this.state >= 2) return;
        this.state = 2;
        code = code || 1e3;
        reason = reason || "";
        var reasonBytes = new TextEncoder().encode(reason);
        var payload = new Uint8Array(2 + reasonBytes.length);
        payload[0] = code >> 8 & 255;
        payload[1] = code & 255;
        payload.set(reasonBytes, 2);
        pal2.tcpWrite(this.conn, buildWSFrame(8, payload, 1));
        this.state = 3;
      };
      WSConnection.prototype._processWSData = function(data) {
        var dv = data instanceof Uint8Array ? data : new Uint8Array(data);
        var newBuf = new Uint8Array(this.buf.length + dv.length);
        newBuf.set(this.buf, 0);
        newBuf.set(dv, this.buf.length);
        this.buf = newBuf;
        for (; ; ) {
          var frame = parseWSFrame(this.buf);
          if (!frame) break;
          this.buf = this.buf.slice(frame.totalLen);
          if (frame.opcode === 8) {
            var closeCode = 1005, closeReason = "";
            if (frame.payload.length >= 2) {
              closeCode = frame.payload[0] << 8 | frame.payload[1];
              closeReason = new TextDecoder().decode(frame.payload.slice(2));
            }
            pal2.tcpWrite(this.conn, buildWSFrame(8, frame.payload, 1));
            this.state = 3;
            if (this.onclose) {
              var ev = { code: closeCode, reason: closeReason, wasClean: true };
              try {
                this.onclose(ev);
              } catch (e) {
              }
            }
          } else if (frame.opcode === 9) {
            pal2.tcpWrite(this.conn, buildWSFrame(10, frame.payload, 1));
          } else if (frame.opcode === 0) {
            this._fragParts.push(frame.payload);
            if (frame.fin) {
              var fragOp = this._fragOpcode;
              var combined = combineBytes(this._fragParts);
              this._fragOpcode = 0;
              this._fragParts = [];
              deliverWS(this, fragOp, combined);
            }
          } else if (frame.opcode === 1 || frame.opcode === 2) {
            if (frame.fin) {
              deliverWS(this, frame.opcode, frame.payload);
            } else {
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
        for (var i = 0; i < parts.length; i++) {
          out.set(parts[i], off);
          off += parts[i].length;
        }
        return out;
      }
      function deliverWS(ws, opcode, payload) {
        var msg = null;
        if (opcode === 1) msg = new TextDecoder().decode(payload);
        else msg = payload;
        if (ws.onmessage) {
          var ev2 = { data: msg };
          try {
            ws.onmessage(ev2);
          } catch (e) {
          }
        }
      }
      function handleConnection(conn) {
        var buf = "";
        var ws = null;
        var idleTimer = null;
        conns.push(conn);
        function resetIdle() {
          if (idleTimer) clearTimeout(idleTimer);
          if (idleTimeout > 0 && !ws) {
            idleTimer = setTimeout(function() {
              pal2.tcpClose(conn);
            }, idleTimeout);
          }
        }
        conn.ondata = function(data) {
          if (ws) {
            ws._processWSData(data);
            return;
          }
          resetIdle();
          buf += new TextDecoder().decode(data);
          var req = parseRequest(buf);
          if (!req) return;
          var raw = buf;
          buf = raw.substring(req.consumed);
          currentKeepAlive = req.keepAlive;
          var upgrade = (req.headers["upgrade"] || "").toLowerCase();
          if (upgrade === "websocket") {
            var wsKey = req.headers["sec-websocket-key"];
            if (!wsKey) {
              pal2.tcpWrite(conn, buildHTTPResponse(
                400,
                "Bad Request",
                { "Content-Length": "0", "Connection": "close" },
                new Uint8Array(0)
              ));
              return;
            }
            var wsHandler = wsRoutes[req.path];
            if (!wsHandler) {
              pal2.tcpWrite(conn, buildHTTPResponse(
                404,
                "Not Found",
                { "Content-Length": "0", "Connection": "close" },
                new Uint8Array(0)
              ));
              return;
            }
            var accept = wsAccept(wsKey);
            var respHdrs = {
              "Upgrade": "websocket",
              "Connection": "Upgrade",
              "Sec-WebSocket-Accept": accept,
              "Content-Type": "text/plain",
              "Content-Length": "0"
            };
            var reqProtocols = (req.headers["sec-websocket-protocol"] || "").split(",").map(function(s) {
              return s.trim();
            });
            var supportedProtocols = null;
            if (wsHandler && typeof wsHandler === "object" && Array.isArray(wsHandler.protocols)) {
              supportedProtocols = wsHandler.protocols;
            }
            if (supportedProtocols && reqProtocols.length) {
              for (var i = 0; i < reqProtocols.length; i++) {
                if (supportedProtocols.indexOf(reqProtocols[i]) >= 0) {
                  respHdrs["Sec-WebSocket-Protocol"] = reqProtocols[i];
                  break;
                }
              }
            }
            pal2.tcpWrite(conn, buildHTTPResponse(101, "Switching Protocols", respHdrs, new Uint8Array(0)));
            var wsRouteFn = typeof wsHandler === "function" ? wsHandler : wsHandler.handler;
            ws = new WSConnection(conn);
            ws.state = 1;
            clearTimeout(idleTimer);
            if (typeof wsRouteFn === "function") {
              try {
                wsRouteFn(ws);
              } catch (e) {
              }
            }
            if (ws.onopen) {
              try {
                ws.onopen({});
              } catch (e) {
              }
            }
            return;
          }
          var pathname = req.path;
          var qm = pathname.indexOf("?");
          var search = "";
          if (qm >= 0) {
            search = pathname.substring(qm);
            pathname = pathname.substring(0, qm);
          }
          var requestObj = {
            method: req.method,
            url: req.path,
            pathname,
            search,
            headers: req.headers,
            body: req.body || "",
            keepAlive: req.keepAlive
          };
          currentResetIdle = resetIdle;
          try {
            var result = handler(requestObj);
            if (result && typeof result.then === "function") {
              result.then(
                function(val) {
                  sendResponse(conn, val);
                },
                function() {
                  sendResponse(conn, null, 500, "Internal Server Error");
                }
              );
            } else {
              sendResponse(conn, result);
            }
          } catch (e) {
            sendResponse(conn, null, 500, "Internal Server Error");
          }
        };
        conn.onerror = function(msg) {
          if (ws && ws.onerror) {
            try {
              ws.onerror(msg);
            } catch (e) {
            }
          }
        };
        conn.onclose = function(code) {
          if (idleTimer) clearTimeout(idleTimer);
          var idx = conns.indexOf(conn);
          if (idx >= 0) conns.splice(idx, 1);
          if (ws && ws.onclose && ws.state < 3) {
            ws.state = 3;
            var ev = { code: code || 1006, reason: "", wasClean: false };
            try {
              ws.onclose(ev);
            } catch (e) {
            }
          }
        };
      }
      function sendResponse(conn, val, status, statusText) {
        var enc = new TextEncoder();
        if (val === null || val === void 0) {
          status = status || 500;
          statusText = statusText || "Internal Server Error";
          pal2.tcpWrite(conn, buildHTTPResponse(status, statusText, {
            "Content-Type": "text/plain",
            "Content-Length": "0",
            "Connection": "close"
          }, new Uint8Array(0)));
          return;
        }
        if (typeof val === "string") {
          var b = enc.encode(val);
          pal2.tcpWrite(conn, buildHTTPResponse(200, "OK", {
            "Content-Type": "text/plain; charset=utf-8",
            "Content-Length": "" + b.length,
            "Connection": currentKeepAlive ? "keep-alive" : "close"
          }, b));
          if (!currentKeepAlive) pal2.tcpClose(conn);
          return;
        }
        if (typeof val === "object" && val !== null) {
          var st = val.status || 200;
          var stText = val.statusText || (st === 200 ? "OK" : "");
          var hdrs = {};
          if (val.headers && typeof val.headers.forEach === "function") {
            val.headers.forEach(function(v, k2) {
              hdrs[k2] = v;
            });
          } else if (val.headers) {
            for (var k in val.headers) {
              if (typeof val.headers[k] !== "function" && k[0] !== "_")
                hdrs[k] = String(val.headers[k]);
            }
          }
          var b = val._body;
          var b2;
          if (b instanceof ArrayBuffer) {
            b2 = new Uint8Array(b);
          } else if (typeof b === "string") {
            b2 = enc.encode(b);
          } else if (b instanceof Uint8Array) {
            b2 = b;
          } else if (b && typeof b === "object" && !(b instanceof Uint8Array)) {
            try {
              b2 = enc.encode(String(b));
            } catch (e) {
            }
          }
          if (!b2 && typeof val.text === "function") {
            try {
              var t = val.text();
              if (typeof t === "string") b2 = enc.encode(t);
            } catch (e) {
            }
          }
          if (!b2) b2 = enc.encode("");
          hdrs["Content-Length"] = "" + b2.length;
          if (!hdrs["Connection"]) hdrs["Connection"] = currentKeepAlive ? "keep-alive" : "close";
          pal2.tcpWrite(conn, buildHTTPResponse(st, stText, hdrs, b2));
          if (!currentKeepAlive) pal2.tcpClose(conn);
          if (currentKeepAlive) currentResetIdle();
        }
        pal2.tcpWrite(conn, buildHTTPResponse(500, "Internal Server Error", {
          "Content-Type": "text/plain",
          "Content-Length": "0",
          "Connection": "close"
        }, new Uint8Array(0)));
        pal2.tcpClose(conn);
      }
      var listener;
      var tls = options.tls;
      if (tls && tls.cert && tls.key)
        listener = pal2.tcpListen(port, hostname, 128, handleConnection, { cert: tls.cert, key: tls.key });
      else
        listener = pal2.tcpListen(port, hostname, 128, handleConnection);
      var conns = [];
      activeServer.close = function() {
        activeServer.closed = true;
        if (activeInstance === activeServer) activeInstance = null;
        pal2.tcpCloseListener(listener);
      };
      return activeServer;
    };
  }

  // src/host-messaging.js
  function setupHostMessaging(pal2) {
    var self = globalThis;
    globalThis.postMessage = function(data) {
      pal2.postMessage(data);
    };
    globalThis.__qwrt_dispatch__ = function(data, source) {
      self.dispatchEvent(new MessageEvent("message", { data }));
    };
    var __onmsg = null;
    Object.defineProperty(self, "onmessage", {
      get: function() {
        return __onmsg;
      },
      set: function(fn) {
        if (__onmsg) self.removeEventListener("message", __onmsg);
        __onmsg = function(e) {
          try {
            fn.call(self, e);
          } catch (err) {
            reportError(err);
          }
        };
        if (fn) self.addEventListener("message", __onmsg);
      },
      configurable: true
    });
  }

  // src/streams.js
  function setupStreams(pal2) {
    function ReadableStreamUnderlyingSourceDefaultCancel() {
    }
    function ReadableStreamUnderlyingSourceDefaultPull() {
      return Promise.resolve();
    }
    function ReadableStreamUnderlyingSourceDefaultStart() {
    }
    class ReadableStreamDefaultController {
      constructor(stream) {
        this._stream = stream;
        this._closeRequested = false;
      }
      get desiredSize() {
        return this._stream._state === "readable" ? this._stream._hwm - this._stream._queue.length : null;
      }
      close() {
        if (this._closeRequested) return;
        this._closeRequested = true;
        if (this._stream._state !== "readable") return;
        this._stream._state = "closed";
        this._stream._notifyReaders();
      }
      enqueue(chunk) {
        if (this._stream._state !== "readable") return;
        this._stream._queue.push(chunk);
        this._stream._notifyReaders();
      }
      error(e) {
        if (this._stream._state !== "readable") return;
        this._stream._state = "errored";
        this._stream._storedError = e;
        this._stream._notifyReaders();
      }
    }
    class ReadableStreamDefaultReader {
      constructor(stream) {
        if (stream._state === "errored") {
          throw stream._storedError;
        }
        if (stream._reader) {
          throw new TypeError("ReadableStream already has a reader");
        }
        stream._reader = this;
        this._stream = stream;
        this._isClosed = false;
        this._readResolve = null;
        this._readReject = null;
        if (stream._state === "closed") {
          this._closed = Promise.resolve();
        } else if (stream._state === "errored") {
          this._closed = Promise.reject(stream._storedError);
        } else {
          this._closed = new Promise(function(resolve, reject) {
            this._closedResolve = resolve;
            this._closedReject = reject;
          }.bind(this));
        }
      }
      get closed() {
        return this._closed;
      }
      read() {
        if (this._isClosed) {
          return Promise.resolve({ done: true, value: void 0 });
        }
        var stream = this._stream;
        if (stream._state === "errored") {
          return Promise.reject(stream._storedError);
        }
        if (stream._queue.length > 0) {
          var chunk = stream._queue.shift();
          return Promise.resolve({ done: false, value: chunk });
        }
        if (stream._state === "closed") {
          this._isClosed = true;
          return Promise.resolve({ done: true, value: void 0 });
        }
        return new Promise(function(resolve, reject) {
          stream._pendingReads.push({ resolve, reject });
          stream._maybePull();
        });
      }
      releaseLock() {
        if (this._stream._reader !== this) return;
        this._stream._reader = null;
        this._isClosed = true;
      }
      cancel(reason) {
        return this._stream.cancel(reason);
      }
    }
    class ReadableStreamBYOBRequest {
      constructor(entry) {
        this._entry = entry;
      }
      get view() {
        return this._entry ? this._entry.view : null;
      }
      respond(bytesWritten) {
        var entry = this._entry;
        if (!entry) throw new TypeError("Invalid ReadableStreamBYOBRequest");
        this._entry = null;
        var view = entry.view;
        var value = new view.constructor(view.buffer, view.byteOffset, bytesWritten);
        _removePending(entry);
        entry.resolve({ done: false, value });
      }
      respondWithNewView(newView) {
        var entry = this._entry;
        if (!entry) throw new TypeError("Invalid ReadableStreamBYOBRequest");
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
        if (stream._state === "errored") {
          throw stream._storedError;
        }
        if (stream._reader) {
          throw new TypeError("ReadableStream already has a reader");
        }
        stream._reader = this;
        this._stream = stream;
        this._isClosed = false;
        if (stream._state === "closed") {
          this._closed = Promise.resolve();
        } else if (stream._state === "errored") {
          this._closed = Promise.reject(stream._storedError);
        } else {
          this._closed = new Promise(function(resolve, reject) {
            this._closedResolve = resolve;
            this._closedReject = reject;
          }.bind(this));
        }
      }
      get closed() {
        return this._closed;
      }
      read(view) {
        if (!ArrayBuffer.isView(view) || view.byteLength === 0) {
          return Promise.reject(new TypeError("BYOB read requires a non-empty ArrayBufferView"));
        }
        if (this._isClosed) {
          return Promise.resolve({ done: true, value: view });
        }
        var stream = this._stream;
        if (stream._state === "errored") {
          return Promise.reject(stream._storedError);
        }
        var immediate;
        if (stream._queue.length > 0) {
          immediate = stream._fillFromQueue(view);
        } else if (stream._state === "closed") {
          this._isClosed = true;
          immediate = { done: true, value: view };
        }
        if (immediate) return Promise.resolve(immediate);
        return new Promise(function(resolve, reject) {
          stream._pendingReads.push({ resolve, reject, view, stream });
          stream._maybePull();
        });
      }
      releaseLock() {
        if (this._stream._reader !== this) return;
        this._stream._reader = null;
        this._isClosed = true;
      }
      cancel(reason) {
        return this._stream.cancel(reason);
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
        return this._stream._state === "readable" ? this._stream._hwm - this._stream._queueBytes() : null;
      }
      close() {
        if (this._closeRequested) return;
        this._closeRequested = true;
        if (this._stream._state !== "readable") return;
        this._stream._state = "closed";
        this._stream._notifyReaders();
      }
      enqueue(chunk) {
        if (this._stream._state !== "readable") return;
        if (chunk instanceof ArrayBuffer) {
          chunk = new Uint8Array(chunk);
        } else if (ArrayBuffer.isView(chunk)) {
          chunk = new Uint8Array(chunk.buffer, chunk.byteOffset, chunk.byteLength);
        } else {
          throw new TypeError("ReadableByteStreamController.enqueue requires an ArrayBufferView or ArrayBuffer");
        }
        this._stream._queue.push(chunk);
        this._stream._notifyReaders();
      }
      error(e) {
        if (this._stream._state !== "readable") return;
        this._stream._state = "errored";
        this._stream._storedError = e;
        this._stream._notifyReaders();
      }
    }
    class ReadableStream {
      constructor(underlyingSource, strategy) {
        underlyingSource = underlyingSource || {};
        this._state = "readable";
        this._reader = null;
        this._queue = [];
        this._type = underlyingSource.type === "bytes" ? "bytes" : void 0;
        this._hwm = strategy && strategy.highWaterMark || 1;
        this._storedError = null;
        this._pendingReads = [];
        this._pulling = false;
        this._controller = this._type === "bytes" ? new ReadableByteStreamController(this) : new ReadableStreamDefaultController(this);
        var source = underlyingSource;
        this._cancel = source.cancel || ReadableStreamUnderlyingSourceDefaultCancel;
        this._pull = source.pull || ReadableStreamUnderlyingSourceDefaultPull;
        if (source.start) {
          source.start(this._controller);
        }
      }
      _notifyReaders() {
        while (this._pendingReads.length > 0) {
          var entry = this._pendingReads[0];
          if (entry.view) {
            if (this._queue.length > 0) {
              this._pendingReads.shift();
              entry.resolve(this._fillFromQueue(entry.view));
            } else if (this._state === "closed") {
              this._pendingReads.shift();
              entry.resolve({ done: true, value: entry.view });
            } else if (this._state === "errored") {
              this._pendingReads.shift();
              entry.reject(this._storedError);
            } else {
              break;
            }
          } else if (this._queue.length > 0) {
            this._pendingReads.shift();
            var chunk = this._queue.shift();
            entry.resolve({ done: false, value: chunk });
          } else if (this._state === "closed") {
            this._pendingReads.shift();
            entry.resolve({ done: true, value: void 0 });
          } else if (this._state === "errored") {
            this._pendingReads.shift();
            entry.reject(this._storedError);
          } else {
            break;
          }
        }
        if (this._reader) {
          if (this._state === "closed" && this._reader._closedResolve) {
            this._reader._closedResolve();
            this._reader._closedResolve = null;
            this._reader._closedReject = null;
          } else if (this._state === "errored" && this._reader._closedReject) {
            this._reader._closedReject(this._storedError);
            this._reader._closedResolve = null;
            this._reader._closedReject = null;
          }
        }
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
        var chunk = this._queue[0];
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
        if (this._state !== "readable" || !this._pull || this._pulling) return;
        var hasByobPending = false;
        for (var i = 0; i < this._pendingReads.length; i++) {
          if (this._pendingReads[i].view) {
            hasByobPending = true;
            break;
          }
        }
        var needsMore = this._type === "bytes" ? hasByobPending || this._queueBytes() < this._hwm : this._queue.length < this._hwm;
        if (!needsMore) return;
        this._pulling = true;
        try {
          var result = this._pull(this._controller);
          if (result && typeof result.catch === "function") {
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
      get locked() {
        return this._reader !== null;
      }
      getReader(options) {
        if (this._reader) {
          throw new TypeError("ReadableStream already locked");
        }
        var mode = options && options.mode;
        if (mode === "byob") {
          if (this._type !== "bytes") {
            throw new TypeError("ReadableStream is not a byte stream");
          }
          return new ReadableStreamBYOBReader(this);
        }
        return new ReadableStreamDefaultReader(this);
      }
      cancel(reason) {
        if (this._state !== "readable") return Promise.resolve();
        this._state = "closed";
        this._queue = [];
        try {
          this._cancel(reason);
        } catch (e) {
        }
        this._notifyReaders();
        return Promise.resolve();
      }
      tee() {
        var source = this;
        var reader = source.getReader();
        var branch1Controller, branch2Controller;
        var branch1Closed = false, branch2Closed = false;
        var reading = false;
        var flags = { b1: false, b2: false };
        var sourceCancelled = false;
        function pullAndDispatch() {
          if (reading) return;
          reading = true;
          reader.read().then(function(result) {
            reading = false;
            if (result.done) {
              if (!flags.b1 && !branch1Closed && branch1Controller) branch1Controller.close();
              if (!flags.b2 && !branch2Closed && branch2Controller) branch2Controller.close();
              return;
            }
            if (!flags.b1 && !branch1Closed && branch1Controller) branch1Controller.enqueue(result.value);
            if (!flags.b2 && !branch2Closed && branch2Controller) branch2Controller.enqueue(result.value);
            if (!flags.b1 && !branch1Closed || !flags.b2 && !branch2Closed) {
              pullAndDispatch();
            }
          }).catch(function(e) {
            reading = false;
            if (!flags.b1 && branch1Controller) branch1Controller.error(e);
            if (!flags.b2 && branch2Controller) branch2Controller.error(e);
          });
        }
        function createBranch(which) {
          return new ReadableStream({
            start: function(controller) {
            },
            pull: function(controller) {
              pullAndDispatch();
            },
            cancel: function(reason) {
              flags[which] = true;
              if (flags.b1 && flags.b2 && !sourceCancelled) {
                sourceCancelled = true;
                try {
                  reader.releaseLock();
                } catch (e) {
                }
                source.cancel(reason);
              }
            }
          });
        }
        var branch1 = createBranch("b1");
        var branch2 = createBranch("b2");
        branch1Controller = branch1._controller;
        branch2Controller = branch2._controller;
        return [branch1, branch2];
      }
      pipeTo(dest, options) {
        options = options || {};
        var preventClose = !!options.preventClose;
        var preventAbort = !!options.preventAbort;
        var preventCancel = !!options.preventCancel;
        var reader, writer;
        try {
          reader = this.getReader();
          writer = dest.getWriter();
        } catch (e) {
          return Promise.reject(e);
        }
        function pump() {
          return reader.read().then(function(result) {
            if (result.done) {
              reader.releaseLock();
              if (preventClose) {
                return Promise.resolve();
              }
              return writer.close();
            }
            return writer.write(result.value).then(pump);
          });
        }
        return pump().catch(function(e) {
          try {
            reader.releaseLock();
          } catch (x) {
          }
          if (!preventAbort) {
            try {
              writer.abort(e);
            } catch (x) {
            }
          }
          if (!preventCancel) {
            try {
              reader.cancel(e);
            } catch (x) {
            }
          }
          throw e;
        });
      }
      pipeThrough(transform) {
        this.pipeTo(transform.writable);
        return transform.readable;
      }
    }
    try {
      if (Symbol.asyncIterator) {
        ReadableStream.prototype[Symbol.asyncIterator] = function() {
          var reader = this.getReader();
          return {
            next: function() {
              return reader.read();
            },
            return: function(value) {
              reader.releaseLock();
              return Promise.resolve({ done: true, value });
            }
          };
        };
      }
    } catch (e) {
    }
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
    Object.defineProperty(WritableStreamDefaultWriter.prototype, "closed", {
      get: function() {
        return this._closedPromise;
      }
    });
    Object.defineProperty(WritableStreamDefaultWriter.prototype, "ready", {
      get: function() {
        return Promise.resolve();
      }
    });
    Object.defineProperty(WritableStreamDefaultWriter.prototype, "desiredSize", {
      get: function() {
        return null;
      }
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
        this._state = "writable";
        this._storedError = null;
        this._writer = null;
        this._writePromise = null;
        this._closePromise = null;
        this._readyPromise = Promise.resolve();
        this._controller = new WritableStreamDefaultController(this);
        this._start = underlyingSink.start;
        this._write = underlyingSink.write || function() {
          return Promise.resolve();
        };
        this._close = underlyingSink.close || function() {
          return Promise.resolve();
        };
        this._abort = underlyingSink.abort || function() {
          return Promise.resolve();
        };
        if (this._start) {
          var result = this._start(this._controller);
          if (result && typeof result.then === "function") {
            this._readyPromise = result;
          }
        }
        this._closedPromise = new Promise(function(resolve, reject) {
          this._closedResolve = resolve;
          this._closedReject = reject;
        }.bind(this));
      }
      get locked() {
        return this._writer !== null;
      }
      getWriter() {
        if (this._writer) {
          throw new TypeError("WritableStream already has a writer");
        }
        var writer = new WritableStreamDefaultWriter(this);
        this._writer = writer;
        return writer;
      }
      _writeChunk(chunk) {
        if (this._state === "errored") return Promise.reject(this._storedError);
        if (this._state === "closed") return Promise.reject(new TypeError("Stream is closed"));
        var self = this;
        try {
          var result = self._write(chunk, self._controller);
          if (!result || typeof result.then !== "function") {
            result = Promise.resolve(result);
          }
          return result;
        } catch (e) {
          return Promise.reject(e);
        }
      }
      _closeStream() {
        if (this._state !== "writable") {
          return Promise.reject(new TypeError("Stream is not writable"));
        }
        this._state = "closed";
        var self = this;
        try {
          var result = self._close();
          if (!result || typeof result.then !== "function") {
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
        this._state = "errored";
        this._storedError = reason;
        var self = this;
        try {
          var result = self._abort(reason);
          if (!result || typeof result.then !== "function") {
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
        if (this._state !== "writable") return;
        this._state = "errored";
        this._storedError = e;
        this._closedReject(e);
      }
    }
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
          pull: function() {
          },
          cancel: function() {
          }
        });
        self._writable = new WritableStream({
          write: function(chunk) {
            if (transformer.transform) {
              return transformer.transform(chunk, tsController);
            }
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
      get readable() {
        return this._readable;
      }
      get writable() {
        return this._writable;
      }
    }
    class CompressionStream {
      constructor(format) {
        format = format || "gzip";
        if (format !== "gzip" && format !== "deflate" && format !== "deflate-raw") {
          throw new Error("CompressionStream: unsupported format: " + format);
        }
        this._format = format;
        var self = this;
        self._readable = new ReadableStream({
          start: function() {
          },
          pull: function() {
          }
        });
        var chunks = [];
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
            if (typeof pal2.nativeCompress !== "function") {
              self._readable._controller.error(new TypeError("Native compression extension not available"));
              return Promise.resolve();
            }
            try {
              var compressed = pal2.nativeCompress(combined, self._format);
              self._readable._controller.enqueue(compressed);
              self._readable._controller.close();
            } catch (e) {
              self._readable._controller.error(e);
            }
            return Promise.resolve();
          }
        });
      }
      get readable() {
        return this._readable;
      }
      get writable() {
        return this._writable;
      }
    }
    class DecompressionStream {
      constructor(format) {
        format = format || "gzip";
        if (format !== "gzip" && format !== "deflate" && format !== "deflate-raw") {
          throw new Error("DecompressionStream: unsupported format: " + format);
        }
        this._format = format;
        var self = this;
        var chunks = [];
        self._readable = new ReadableStream({
          start: function() {
          },
          pull: function() {
          }
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
            if (typeof pal2.nativeDecompress !== "function") {
              self._readable._controller.error(new TypeError("Native compression extension not available"));
              return Promise.resolve();
            }
            try {
              var decompressed = pal2.nativeDecompress(combined, self._format);
              self._readable._controller.enqueue(decompressed);
              self._readable._controller.close();
            } catch (e) {
              self._readable._controller.error(e);
            }
            return Promise.resolve();
          }
        });
      }
      get readable() {
        return this._readable;
      }
      get writable() {
        return this._writable;
      }
    }
    class ByteLengthQueuingStrategy {
      constructor(options) {
        this._highWaterMark = options?.highWaterMark ?? 1;
      }
      get highWaterMark() {
        return this._highWaterMark;
      }
      size(chunk) {
        return chunk?.byteLength ?? 0;
      }
    }
    class CountQueuingStrategy {
      constructor(options) {
        this._highWaterMark = options?.highWaterMark ?? 1;
      }
      get highWaterMark() {
        return this._highWaterMark;
      }
      size() {
        return 1;
      }
    }
    class TextEncoderStream {
      constructor() {
        this.encoding = "utf-8";
        var self = this;
        self._readable = new ReadableStream({
          start: function() {
          },
          pull: function() {
          }
        });
        self._writable = new WritableStream({
          write: function(chunk) {
            if (typeof chunk === "string") {
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
      get readable() {
        return this._readable;
      }
      get writable() {
        return this._writable;
      }
    }
    class TextDecoderStream {
      constructor(label, options) {
        label = label || "utf-8";
        options = options || {};
        this.encoding = label.toLowerCase();
        this.fatal = options.fatal || false;
        this.ignoreBOM = options.ignoreBOM || false;
        var decoder = new TextDecoder(label, { fatal: this.fatal, ignoreBOM: this.ignoreBOM });
        var self = this;
        self._readable = new ReadableStream({
          start: function() {
          },
          pull: function() {
          }
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
      get readable() {
        return this._readable;
      }
      get writable() {
        return this._writable;
      }
    }
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

  // src/blob-file-formdata.js
  function setupBlobFileFormData() {
    class Blob2 {
      constructor(blobParts, options) {
        options = options || {};
        if (blobParts !== void 0) {
          if (blobParts === null || typeof blobParts !== "object") {
            throw new TypeError("Blob: blobParts must be an iterable object");
          }
          if (!(Symbol && Symbol.iterator && typeof blobParts[Symbol.iterator] === "function")) {
            throw new TypeError("Blob: blobParts must be iterable");
          }
        }
        var parts = blobParts === void 0 ? [] : blobParts;
        var buffers = [];
        var totalSize = 0;
        var iter = parts[Symbol.iterator]();
        for (; ; ) {
          var next = iter.next();
          if (next.done) break;
          var part = next.value;
          var bytes;
          if (part instanceof Blob2) {
            bytes = part._getBytes();
          } else if (part instanceof ArrayBuffer) {
            bytes = new Uint8Array(part);
          } else if (ArrayBuffer.isView(part)) {
            bytes = new Uint8Array(part.buffer, part.byteOffset, part.byteLength);
          } else {
            var str = String(part);
            bytes = new TextEncoder().encode(str);
          }
          buffers.push(bytes);
          totalSize += bytes.length;
        }
        this._buffers = buffers;
        this._size = totalSize;
        this._type = normalizeType(options.type || "");
      }
      get size() {
        return this._size;
      }
      get type() {
        return this._type;
      }
      slice(start, end, contentType) {
        start = start === void 0 ? 0 : start;
        start = start | 0;
        if (start < 0) start = Math.max(this._size + start, 0);
        if (start > this._size) start = this._size;
        end = end === void 0 ? this._size : end;
        end = end | 0;
        if (end < 0) end = Math.max(this._size + end, 0);
        if (end > this._size) end = this._size;
        if (end < start) end = start;
        var sliceLen = end - start;
        var effectiveCT = contentType !== void 0 ? String(contentType) : "";
        var result = new Uint8Array(sliceLen);
        var offset = 0;
        var globalStart = start;
        for (var i = 0; i < this._buffers.length && offset < sliceLen; i++) {
          var buf = this._buffers[i];
          if (globalStart >= buf.length) {
            globalStart -= buf.length;
            continue;
          }
          var localStart = globalStart;
          var localEnd = Math.min(buf.length, localStart + sliceLen - offset);
          var copyLen = localEnd - localStart;
          if (copyLen > 0) {
            result.set(buf.subarray(localStart, localEnd), offset);
            offset += copyLen;
          }
          globalStart = 0;
        }
        return new Blob2([result], { type: effectiveCT });
      }
      arrayBuffer() {
        var result = new ArrayBuffer(this._size);
        var view = new Uint8Array(result);
        var offset = 0;
        for (var i = 0; i < this._buffers.length; i++) {
          view.set(this._buffers[i], offset);
          offset += this._buffers[i].length;
        }
        return Promise.resolve(result);
      }
      text() {
        return this.arrayBuffer().then(function(buf) {
          return decodeUint8Array(new Uint8Array(buf));
        });
      }
      json() {
        return this.text().then(function(txt) {
          return JSON.parse(txt);
        });
      }
      _getBytes() {
        if (this._buffers.length === 1) return this._buffers[0];
        var result = new Uint8Array(this._size);
        var offset = 0;
        for (var i = 0; i < this._buffers.length; i++) {
          result.set(this._buffers[i], offset);
          offset += this._buffers[i].length;
        }
        return result;
      }
      static isBlob(obj) {
        return obj instanceof Blob2;
      }
    }
    class File2 extends Blob2 {
      constructor(fileBits, fileName, options) {
        options = options || {};
        super(fileBits, options);
        this._name = String(fileName);
        this._lastModified = options.lastModified || Date.now();
      }
      get name() {
        return this._name;
      }
      get lastModified() {
        return this._lastModified;
      }
    }
    class FormData {
      constructor() {
        this._entries = [];
      }
      append(name, value, filename) {
        this._entries.push({
          name: String(name),
          value: this._normalizeValue(value, filename)
        });
      }
      delete(name) {
        this._entries = this._entries.filter(function(entry) {
          return entry.name !== name;
        });
      }
      get(name) {
        var entry = this._entries.find(function(e) {
          return e.name === name;
        });
        return entry ? entry.value : null;
      }
      getAll(name) {
        return this._entries.filter(function(e) {
          return e.name === name;
        }).map(function(e) {
          return e.value;
        });
      }
      has(name) {
        return this._entries.some(function(e) {
          return e.name === name;
        });
      }
      set(name, value, filename) {
        var found = false;
        var newEntries = [];
        for (var i = 0; i < this._entries.length; i++) {
          if (this._entries[i].name === name) {
            if (!found) {
              newEntries.push({
                name: String(name),
                value: this._normalizeValue(value, filename)
              });
              found = true;
            }
          } else {
            newEntries.push(this._entries[i]);
          }
        }
        if (!found) {
          newEntries.push({
            name: String(name),
            value: this._normalizeValue(value, filename)
          });
        }
        this._entries = newEntries;
      }
      forEach(callback, thisArg) {
        for (var i = 0; i < this._entries.length; i++) {
          callback.call(thisArg, this._entries[i].value, this._entries[i].name, this);
        }
      }
      keys() {
        return this._entries.map(function(e) {
          return e.name;
        })[Symbol.iterator]();
      }
      values() {
        return this._entries.map(function(e) {
          return e.value;
        })[Symbol.iterator]();
      }
      entries() {
        return this._entries.map(function(e) {
          return [e.name, e.value];
        })[Symbol.iterator]();
      }
      _normalizeValue(value, filename) {
        if (value instanceof Blob2) {
          if (!(value instanceof File2) && filename) {
            return new File2([value], filename, { type: value.type });
          }
          return value;
        }
        return String(value);
      }
    }
    function normalizeType(type) {
      var s = String(type).toLowerCase();
      return s.replace(/[\x09\x0A\x0D]/g, "");
    }
    function decodeUint8Array(bytes) {
      return new TextDecoder().decode(bytes);
    }
    globalThis.Blob = Blob2;
    globalThis.File = File2;
    globalThis.FormData = FormData;
  }

  // src/url-pattern.js
  function setupURLPattern() {
    class URLPattern {
      constructor(input, baseURL) {
        var pattern;
        if (typeof input === "string") {
          pattern = { pathname: input || "*" };
        } else {
          pattern = input || {};
        }
        this._protocol = compilePattern(pattern.protocol || "*");
        this._username = compilePattern(pattern.username || "*");
        this._password = compilePattern(pattern.password || "*");
        this._hostname = compilePattern(pattern.hostname || "*");
        this._port = compilePattern(pattern.port || "*");
        this._pathname = compilePattern(pattern.pathname || "*");
        this._search = compilePattern(pattern.search || "*");
        this._hash = compilePattern(pattern.hash || "*");
        this._baseURL = baseURL || pattern.baseURL || null;
        this._protocolRegex = buildRegex(this._protocol);
        this._usernameRegex = buildRegex(this._username);
        this._passwordRegex = buildRegex(this._password);
        this._hostnameRegex = buildRegex(this._hostname);
        this._portRegex = buildRegex(this._port);
        this._pathnameRegex = buildRegex(this._pathname);
        this._searchRegex = buildRegex(this._search);
        this._hashRegex = buildRegex(this._hash);
      }
      get protocol() {
        return this._protocol.pattern;
      }
      get username() {
        return this._username.pattern;
      }
      get password() {
        return this._password.pattern;
      }
      get hostname() {
        return this._hostname.pattern;
      }
      get port() {
        return this._port.pattern;
      }
      get pathname() {
        return this._pathname.pattern;
      }
      get search() {
        return this._search.pattern;
      }
      get hash() {
        return this._hash.pattern;
      }
      test(input, baseURL) {
        return this.exec(input, baseURL) !== null;
      }
      exec(input, baseURL) {
        var url;
        if (typeof input === "string") {
          try {
            url = parseURL(input, baseURL || this._baseURL);
          } catch (e) {
            return null;
          }
        } else {
          url = input;
        }
        var protocolResult = matchPattern(this._protocol, this._protocolRegex, url.protocol || "");
        var usernameResult = matchPattern(this._username, this._usernameRegex, url.username || "");
        var passwordResult = matchPattern(this._password, this._passwordRegex, url.password || "");
        var hostnameResult = matchPattern(this._hostname, this._hostnameRegex, url.hostname || "");
        var portResult = matchPattern(this._port, this._portRegex, url.port || "");
        var pathnameResult = matchPattern(this._pathname, this._pathnameRegex, url.pathname || "");
        var searchResult = matchPattern(this._search, this._searchRegex, url.search || "");
        var hashResult = matchPattern(this._hash, this._hashRegex, url.hash || "");
        if (!protocolResult || !usernameResult || !passwordResult || !hostnameResult || !portResult || !pathnameResult || !searchResult || !hashResult) {
          return null;
        }
        return {
          inputs: [input],
          protocol: protocolResult,
          username: usernameResult,
          password: passwordResult,
          hostname: hostnameResult,
          port: portResult,
          pathname: pathnameResult,
          search: searchResult,
          hash: hashResult
        };
      }
    }
    function compilePattern(patternStr) {
      if (patternStr === "*") {
        return {
          pattern: patternStr,
          regexStr: "(.*)",
          names: ["*"]
        };
      }
      var regexStr = "";
      var names = [];
      var i = 0;
      while (i < patternStr.length) {
        var ch = patternStr[i];
        if (ch === ":") {
          i++;
          var name = "";
          while (i < patternStr.length && /[a-zA-Z0-9_]/.test(patternStr[i])) {
            name += patternStr[i];
            i++;
          }
          var modifier = "";
          if (i < patternStr.length && (patternStr[i] === "?" || patternStr[i] === "+" || patternStr[i] === "*")) {
            modifier = patternStr[i];
            i++;
          }
          names.push(name);
          if (modifier === "?") {
            regexStr += "(?:([^/]*))?";
          } else if (modifier === "+") {
            regexStr += "(.+)";
          } else if (modifier === "*") {
            regexStr += "(.*)";
          } else {
            regexStr += "([^/]*)";
          }
        } else if (ch === "{") {
          i++;
          var groupContent = "";
          var depth = 1;
          while (i < patternStr.length && depth > 0) {
            if (patternStr[i] === "{") depth++;
            if (patternStr[i] === "}") depth--;
            if (depth > 0) groupContent += patternStr[i];
            i++;
          }
          regexStr += "(" + groupContent + ")";
          names.push(groupContent);
        } else if (ch === "*") {
          regexStr += "(.*?)";
          names.push("*");
          i++;
        } else {
          if (/[\\^$.|?+(){}[\]]/.test(ch)) {
            regexStr += "\\" + ch;
          } else {
            regexStr += ch;
          }
          i++;
        }
      }
      return {
        pattern: patternStr,
        regexStr: "^" + regexStr + "$",
        names
      };
    }
    function buildRegex(compiled) {
      try {
        return new RegExp(compiled.regexStr, "i");
      } catch (e) {
        return null;
      }
    }
    function matchPattern(compiled, regex, value) {
      if (!regex) return null;
      var match = value.match(regex);
      if (!match) return null;
      var groups = {};
      var input = match[0];
      for (var i = 0; i < compiled.names.length; i++) {
        var name = compiled.names[i];
        var value = match[i + 1];
        if (name && name !== "*") {
          groups[name] = value !== void 0 ? value : "";
        }
      }
      return {
        input,
        groups
      };
    }
    function parseURL(url, base) {
      var str = String(url);
      var result = {
        protocol: "",
        username: "",
        password: "",
        hostname: "",
        port: "",
        pathname: "/",
        search: "",
        hash: ""
      };
      var match = str.match(/^(?:([a-z][a-z0-9+.-]*):)?(?:\/\/(?:([^:@]*)(?::([^@]*))?@)?([^:/?#]*)(?::(\d+))?)?(\/?[^?#]*)?(?:\?([^#]*))?(?:#(.*))?$/i);
      if (!match) throw new TypeError("Invalid URL: " + str);
      result.protocol = match[1] || "";
      result.username = match[2] || "";
      result.password = match[3] || "";
      result.hostname = match[4] || "";
      result.port = match[5] || "";
      result.pathname = match[6] || "/";
      result.search = match[7] || "";
      result.hash = match[8] || "";
      return result;
    }
    globalThis.URLPattern = URLPattern;
  }

  // src/navigator.js
  function setupNavigatorReportError() {
    var navigator = {
      userAgent: "qwrt/1.0 (WinterTC)",
      language: "en-US",
      platform: "wintercg",
      hardwareConcurrency: 1,
      onLine: true,
      maxTouchPoints: 0
    };
    globalThis.navigator = navigator;
    globalThis.self = globalThis;
    globalThis.reportError = function reportError2(error) {
      if (error === void 0 || error === null) return;
      var event;
      if (typeof globalThis.ErrorEvent === "function") {
        var message = error instanceof Error ? error.message : String(error);
        var filename = error instanceof Error ? error.fileName || "" : "";
        var lineno = error instanceof Error ? error.lineNumber || 0 : 0;
        var colno = error instanceof Error ? error.columnNumber || 0 : 0;
        event = new globalThis.ErrorEvent("error", {
          message,
          filename,
          lineno,
          colno,
          error,
          cancelable: true
        });
      } else {
        event = new Event("error");
        event.message = error instanceof Error ? error.message : String(error);
        event.error = error;
      }
      globalThis.dispatchEvent(event);
    };
    Object.defineProperty(globalThis, "onerror", {
      get: function() {
        var listener = this._onerrorHandler;
        return listener || null;
      },
      set: function(handler) {
        if (this._onerrorHandler) {
          this.removeEventListener("error", this._onerrorHandler);
        }
        this._onerrorHandler = handler;
        if (handler) {
          this.addEventListener("error", handler);
        }
      },
      configurable: true,
      enumerable: true
    });
    Object.defineProperty(globalThis, "onunhandledrejection", {
      get: function() {
        var listener = this._onunhandledrejectionHandler;
        return listener || null;
      },
      set: function(handler) {
        if (this._onunhandledrejectionHandler) {
          this.removeEventListener("unhandledrejection", this._onunhandledrejectionHandler);
        }
        this._onunhandledrejectionHandler = handler;
        if (handler) {
          this.addEventListener("unhandledrejection", handler);
        }
      },
      configurable: true,
      enumerable: true
    });
    Object.defineProperty(globalThis, "onrejectionhandled", {
      get: function() {
        var listener = this._onrejectionhandledHandler;
        return listener || null;
      },
      set: function(handler) {
        if (this._onrejectionhandledHandler) {
          this.removeEventListener("rejectionhandled", this._onrejectionhandledHandler);
        }
        this._onrejectionhandledHandler = handler;
        if (handler) {
          this.addEventListener("rejectionhandled", handler);
        }
      },
      configurable: true,
      enumerable: true
    });
  }

  // src/crypto-subtle.js
  function setupCryptoSubtle(pal2) {
    pal2.__installCryptoSubtle__ = function() {
      installCryptoSubtle(pal2);
    };
  }
  function installCryptoSubtle(pal2) {
    function toUint8Array(data) {
      if (data instanceof Uint8Array) return data;
      if (data instanceof ArrayBuffer) return new Uint8Array(data);
      if (ArrayBuffer.isView(data)) return new Uint8Array(data.buffer, data.byteOffset, data.byteLength);
      throw new TypeError("Expected ArrayBuffer or TypedArray");
    }
    function toArrayBuffer(u8) {
      return u8.buffer.slice(u8.byteOffset, u8.byteOffset + u8.byteLength);
    }
    var B64_CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    function base64UrlEncode(bytes) {
      var str = "";
      for (var i = 0; i < bytes.length; i += 3) {
        var b0 = bytes[i], b1 = i + 1 < bytes.length ? bytes[i + 1] : 0, b2 = i + 2 < bytes.length ? bytes[i + 2] : 0;
        str += B64_CHARS[b0 >> 2];
        str += B64_CHARS[(b0 & 3) << 4 | b1 >> 4];
        str += i + 1 < bytes.length ? B64_CHARS[(b1 & 15) << 2 | b2 >> 6] : "=";
        str += i + 2 < bytes.length ? B64_CHARS[b2 & 63] : "=";
      }
      return str.replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
    }
    function base64UrlDecode(str) {
      str = str.replace(/-/g, "+").replace(/_/g, "/");
      while (str.length % 4) str += "=";
      var chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
      var bytes = [];
      for (var i = 0; i < str.length; i += 4) {
        var c0 = chars.indexOf(str[i]), c1 = chars.indexOf(str[i + 1]);
        var c2 = chars.indexOf(str[i + 2]), c3 = chars.indexOf(str[i + 3]);
        bytes.push(c0 << 2 | c1 >> 4);
        if (c2 !== -1 && str[i + 2] !== "=") bytes.push((c1 & 15) << 4 | c2 >> 2);
        if (c3 !== -1 && str[i + 3] !== "=") bytes.push((c2 & 3) << 6 | c3);
      }
      return new Uint8Array(bytes);
    }
    class CryptoKey {
      constructor(type, algorithm, extractable, usages, data) {
        this._type = type;
        this._algorithm = algorithm;
        this._extractable = extractable;
        this._usages = usages;
        this._data = data;
      }
      get type() {
        return this._type;
      }
      get algorithm() {
        return this._algorithm;
      }
      get extractable() {
        return this._extractable;
      }
      get usages() {
        return this._usages;
      }
    }
    class SubtleCrypto {
      constructor() {
      }
      digest(algorithm, data) {
        return new Promise(function(resolve, reject) {
          var name = typeof algorithm === "string" ? algorithm : algorithm.name;
          if (typeof pal2.nativeDigest !== "function") {
            reject(new DOMException("Crypto extension not available", "NotSupportedError"));
            return;
          }
          try {
            var result = pal2.nativeDigest(name, toUint8Array(data));
            resolve(toArrayBuffer(result));
          } catch (e) {
            reject(e);
          }
        });
      }
      importKey(format, keyData, algorithm, extractable, keyUsages) {
        return new Promise(function(resolve, reject) {
          var algoName = typeof algorithm === "string" ? algorithm : algorithm.name;
          var data;
          if (format === "raw") {
            if (keyData instanceof ArrayBuffer) {
              data = new Uint8Array(keyData);
            } else if (ArrayBuffer.isView(keyData)) {
              data = new Uint8Array(keyData.buffer, keyData.byteOffset, keyData.byteLength);
            } else {
              reject(new TypeError("Invalid keyData"));
              return;
            }
          } else if (format === "jwk") {
            if (!keyData || !keyData.k) {
              reject(new TypeError("Invalid JWK key data"));
              return;
            }
            data = base64UrlDecode(keyData.k);
          } else {
            reject(new DOMException("Unsupported key format: " + format, "NotSupportedError"));
            return;
          }
          resolve(new CryptoKey(
            "secret",
            { name: algoName },
            extractable,
            keyUsages,
            data
          ));
        });
      }
      sign(algorithm, key, data) {
        return new Promise(function(resolve, reject) {
          var algoName = typeof algorithm === "string" ? algorithm : algorithm.name;
          if (algoName === "HMAC") {
            var hashAlgo = algorithm.hash ? typeof algorithm.hash === "string" ? algorithm.hash : algorithm.hash.name : "SHA-256";
            if (typeof pal2.nativeHmac !== "function") {
              reject(new DOMException("Crypto extension not available", "NotSupportedError"));
              return;
            }
            try {
              var result = pal2.nativeHmac(hashAlgo, key._data, toUint8Array(data));
              resolve(toArrayBuffer(result));
            } catch (e) {
              reject(e);
            }
            return;
          }
          reject(new DOMException("Unsupported algorithm: " + algoName, "NotSupportedError"));
        });
      }
      verify(algorithm, key, signature, data) {
        return new Promise(function(resolve, reject) {
          var algoName = typeof algorithm === "string" ? algorithm : algorithm.name;
          if (algoName === "HMAC") {
            this.sign(algorithm, key, data).then(function(computed) {
              var sig = toUint8Array(signature);
              var comp = new Uint8Array(computed);
              if (sig.length !== comp.length) {
                resolve(false);
                return;
              }
              var diff = 0;
              for (var i = 0; i < sig.length; i++) {
                diff |= sig[i] ^ comp[i];
              }
              resolve(diff === 0);
            }, reject);
            return;
          }
          reject(new DOMException("Unsupported algorithm: " + algoName, "NotSupportedError"));
        }.bind(this));
      }
      encrypt(algorithm, key, data) {
        return new Promise(function(resolve, reject) {
          var algoName = typeof algorithm === "string" ? algorithm : algorithm.name;
          var plaintext = toUint8Array(data);
          if (typeof pal2.nativeAesEncrypt !== "function") {
            reject(new DOMException("Crypto extension not available", "NotSupportedError"));
            return;
          }
          try {
            if (algoName === "AES-CBC") {
              var iv = toUint8Array(algorithm.iv);
              var result = pal2.nativeAesEncrypt(plaintext, key._data, iv, "AES-CBC");
              resolve(toArrayBuffer(result));
              return;
            }
            if (algoName === "AES-GCM") {
              var iv = toUint8Array(algorithm.iv);
              var aad = algorithm.additionalData ? toUint8Array(algorithm.additionalData) : void 0;
              var tagLen = algorithm.tagLength !== void 0 ? algorithm.tagLength / 8 : 16;
              var result = pal2.nativeAesEncrypt(plaintext, key._data, iv, "AES-GCM", aad, tagLen);
              resolve(toArrayBuffer(result));
              return;
            }
            if (algoName === "AES-CTR") {
              var counter = toUint8Array(algorithm.counter);
              var result = pal2.nativeAesEncrypt(plaintext, key._data, counter, "AES-CTR");
              resolve(toArrayBuffer(result));
              return;
            }
          } catch (e) {
            reject(e);
            return;
          }
          reject(new DOMException("Unsupported algorithm: " + algoName, "NotSupportedError"));
        });
      }
      decrypt(algorithm, key, data) {
        return new Promise(function(resolve, reject) {
          var algoName = typeof algorithm === "string" ? algorithm : algorithm.name;
          var ciphertext = toUint8Array(data);
          if (typeof pal2.nativeAesDecrypt !== "function") {
            reject(new DOMException("Crypto extension not available", "NotSupportedError"));
            return;
          }
          try {
            if (algoName === "AES-CBC") {
              var iv = toUint8Array(algorithm.iv);
              var result = pal2.nativeAesDecrypt(ciphertext, key._data, iv, "AES-CBC");
              resolve(toArrayBuffer(result));
              return;
            }
            if (algoName === "AES-GCM") {
              var iv = toUint8Array(algorithm.iv);
              var aad = algorithm.additionalData ? toUint8Array(algorithm.additionalData) : void 0;
              var tagLen = algorithm.tagLength !== void 0 ? algorithm.tagLength / 8 : 16;
              var result = pal2.nativeAesDecrypt(ciphertext, key._data, iv, "AES-GCM", aad, tagLen);
              resolve(toArrayBuffer(result));
              return;
            }
            if (algoName === "AES-CTR") {
              var counter = toUint8Array(algorithm.counter);
              var result = pal2.nativeAesDecrypt(ciphertext, key._data, counter, "AES-CTR");
              resolve(toArrayBuffer(result));
              return;
            }
          } catch (e) {
            reject(e);
            return;
          }
          reject(new DOMException("Unsupported algorithm: " + algoName, "NotSupportedError"));
        });
      }
      generateKey(algorithm, extractable, keyUsages) {
        return new Promise(function(resolve, reject) {
          var algoName = typeof algorithm === "string" ? algorithm : algorithm.name;
          if (algoName === "HMAC") {
            var hashAlgo = algorithm.hash ? typeof algorithm.hash === "string" ? algorithm.hash : algorithm.hash.name : "SHA-256";
            var lengthBits = algorithm.length !== void 0 ? algorithm.length : 0;
            var lengthBytes;
            if (lengthBits > 0) {
              lengthBytes = Math.ceil(lengthBits / 8);
            } else {
              lengthBytes = hashAlgo === "SHA-1" ? 20 : hashAlgo === "SHA-512" ? 64 : 32;
            }
            var keyBytes = new Uint8Array(lengthBytes);
            crypto.getRandomValues(keyBytes);
            resolve(new CryptoKey("secret", { name: "HMAC", hash: hashAlgo }, extractable, keyUsages, keyBytes));
            return;
          }
          if (algoName === "AES-CBC" || algoName === "AES-GCM" || algoName === "AES-CTR") {
            var length = algorithm.length || 128;
            if (length !== 128 && length !== 192 && length !== 256) {
              reject(new DOMException("Invalid AES key length", "OperationError"));
              return;
            }
            var keyBytes = new Uint8Array(length / 8);
            crypto.getRandomValues(keyBytes);
            resolve(new CryptoKey("secret", { name: algoName, length }, extractable, keyUsages, keyBytes));
            return;
          }
          reject(new DOMException("Unsupported algorithm: " + algoName, "NotSupportedError"));
        });
      }
      exportKey(format, key) {
        return new Promise(function(resolve, reject) {
          if (!key.extractable) {
            reject(new DOMException("Key is not extractable", "InvalidAccessError"));
            return;
          }
          if (format === "raw") {
            resolve(toArrayBuffer(key._data));
            return;
          }
          if (format === "jwk") {
            var jwk = {
              kty: "oct",
              k: base64UrlEncode(key._data),
              alg: key.algorithm.name === "HMAC" ? "HS" + (key.algorithm.hash ? key.algorithm.hash.replace("SHA-", "") : "256") : key.algorithm.name,
              ext: true,
              key_ops: key.usages
            };
            resolve(jwk);
            return;
          }
          reject(new DOMException("Unsupported export format: " + format, "NotSupportedError"));
        });
      }
      wrapKey(format, key, wrappingKey, wrapAlgorithm) {
        return new Promise(function(resolve, reject) {
          var wrapName = typeof wrapAlgorithm === "string" ? wrapAlgorithm : wrapAlgorithm.name;
          if (wrapName !== "AES-GCM" && wrapName !== "AES-CBC") {
            reject(new DOMException("Unsupported wrap algorithm: " + wrapName, "NotSupportedError"));
            return;
          }
          if (typeof pal2.nativeAesEncrypt !== "function") {
            reject(new DOMException("Crypto extension not available", "NotSupportedError"));
            return;
          }
          var plaintext;
          try {
            if (format === "raw") {
              plaintext = toUint8Array(key._data);
            } else if (format === "jwk") {
              var jwk = {
                kty: "oct",
                k: base64UrlEncode(key._data),
                alg: key.algorithm.name === "HMAC" ? "HS" + (key.algorithm.hash ? key.algorithm.hash.replace("SHA-", "") : "256") : key.algorithm.name,
                ext: key.extractable,
                key_ops: key.usages
              };
              plaintext = new TextEncoder().encode(JSON.stringify(jwk));
            } else {
              reject(new DOMException("Unsupported wrap format: " + format, "NotSupportedError"));
              return;
            }
          } catch (e) {
            reject(e);
            return;
          }
          try {
            if (wrapName === "AES-GCM") {
              var iv = toUint8Array(wrapAlgorithm.iv);
              var aad = wrapAlgorithm.additionalData ? toUint8Array(wrapAlgorithm.additionalData) : void 0;
              var tagLen = wrapAlgorithm.tagLength !== void 0 ? wrapAlgorithm.tagLength / 8 : 16;
              resolve(toArrayBuffer(pal2.nativeAesEncrypt(plaintext, wrappingKey._data, iv, "AES-GCM", aad, tagLen)));
              return;
            }
            if (wrapName === "AES-CBC") {
              var iv = toUint8Array(wrapAlgorithm.iv);
              resolve(toArrayBuffer(pal2.nativeAesEncrypt(plaintext, wrappingKey._data, iv, "AES-CBC")));
              return;
            }
          } catch (e) {
            reject(e);
            return;
          }
        });
      }
      unwrapKey(format, wrappedKey, unwrappingKey, unwrapAlgorithm, unwrappedKeyAlgorithm, extractable, keyUsages) {
        return new Promise(function(resolve, reject) {
          var unwrapName = typeof unwrapAlgorithm === "string" ? unwrapAlgorithm : unwrapAlgorithm.name;
          if (unwrapName !== "AES-GCM" && unwrapName !== "AES-CBC") {
            reject(new DOMException("Unsupported unwrap algorithm: " + unwrapName, "NotSupportedError"));
            return;
          }
          if (typeof pal2.nativeAesDecrypt !== "function") {
            reject(new DOMException("Crypto extension not available", "NotSupportedError"));
            return;
          }
          var plaintext;
          try {
            if (unwrapName === "AES-GCM") {
              var iv = toUint8Array(unwrapAlgorithm.iv);
              var aad = unwrapAlgorithm.additionalData ? toUint8Array(unwrapAlgorithm.additionalData) : void 0;
              var tagLen = unwrapAlgorithm.tagLength !== void 0 ? unwrapAlgorithm.tagLength / 8 : 16;
              plaintext = pal2.nativeAesDecrypt(toUint8Array(wrappedKey), unwrappingKey._data, iv, "AES-GCM", aad, tagLen);
            } else {
              var iv = toUint8Array(unwrapAlgorithm.iv);
              plaintext = pal2.nativeAesDecrypt(toUint8Array(wrappedKey), unwrappingKey._data, iv, "AES-CBC");
            }
          } catch (e) {
            reject(e);
            return;
          }
          try {
            if (format === "raw") {
              resolve(new CryptoKey("secret", unwrappedKeyAlgorithm, extractable, keyUsages, new Uint8Array(plaintext)));
              return;
            }
            if (format === "jwk") {
              var json = JSON.parse(new TextDecoder().decode(plaintext));
              if (!json || !json.k) {
                reject(new DOMException("Invalid JWK", "DataError"));
                return;
              }
              resolve(new CryptoKey("secret", unwrappedKeyAlgorithm, extractable, keyUsages, base64UrlDecode(json.k)));
              return;
            }
          } catch (e) {
            reject(e);
            return;
          }
          reject(new DOMException("Unsupported unwrap format: " + format, "NotSupportedError"));
        });
      }
      deriveBits(algorithm, key, length) {
        return new Promise(function(resolve, reject) {
          var algoName = typeof algorithm === "string" ? algorithm : algorithm.name;
          if (algoName === "PBKDF2") {
            var salt = toUint8Array(algorithm.salt);
            var iterations = algorithm.iterations;
            var hashAlgo = algorithm.hash ? typeof algorithm.hash === "string" ? algorithm.hash : algorithm.hash.name : "SHA-1";
            if (typeof pal2.nativePbkdf2 !== "function") {
              reject(new DOMException("Crypto extension not available", "NotSupportedError"));
              return;
            }
            try {
              var dkLen = Math.ceil(length / 8);
              var result = pal2.nativePbkdf2(key._data, salt, iterations, hashAlgo, dkLen);
              resolve(toArrayBuffer(result));
            } catch (e) {
              reject(e);
            }
            return;
          }
          reject(new DOMException("Unsupported algorithm: " + algoName, "NotSupportedError"));
        });
      }
      deriveKey(algorithm, key, derivedKeyType, extractable, keyUsages) {
        var self = this;
        return new Promise(function(resolve, reject) {
          var bitsLength = typeof derivedKeyType === "string" ? 256 : derivedKeyType.length || 256;
          self.deriveBits(algorithm, key, bitsLength).then(function(bits) {
            var data = new Uint8Array(bits);
            var algoName = typeof derivedKeyType === "string" ? derivedKeyType : derivedKeyType.name;
            resolve(new CryptoKey("secret", { name: algoName }, extractable, keyUsages, data));
          }, reject);
        });
      }
    }
    if (!globalThis.crypto) {
      globalThis.crypto = {};
    }
    globalThis.crypto.subtle = new SubtleCrypto();
    globalThis.CryptoKey = CryptoKey;
    globalThis.SubtleCrypto = SubtleCrypto;
  }

  // src/structured-clone.js
  function setupStructuredClone() {
    globalThis.structuredClone = function structuredClone2(value, options) {
      var transferSet = null;
      if (options && options.transfer !== void 0 && options.transfer !== null) {
        if (!Array.isArray(options.transfer))
          throw new DOMException("transfer must be a sequence", "DataCloneError");
        transferSet = /* @__PURE__ */ new Set();
        for (var i = 0; i < options.transfer.length; i++) {
          var t = options.transfer[i];
          if (transferSet.has(t))
            throw new DOMException("duplicate transferable", "DataCloneError");
          var isPort = typeof globalThis.MessagePort === "function" && t instanceof globalThis.MessagePort;
          if (!(t instanceof ArrayBuffer) && !isPort)
            throw new DOMException("object is not transferable", "DataCloneError");
          transferSet.add(t);
        }
      }
      if (options && typeof options === "object") {
        options = { transfer: options.transfer, _qwrtTransfer: transferSet };
      } else {
        options = { _qwrtTransfer: transferSet };
      }
      var seen = /* @__PURE__ */ new Map();
      var result = clone(value, seen, options);
      if (transferSet) {
        transferSet.forEach(function(t2) {
          if (t2 instanceof ArrayBuffer) {
            if (!t2.detached) t2.transfer();
          } else if (typeof t2._detached === "boolean") {
            t2._detached = true;
          }
        });
      }
      return result;
    };
    function clone(value, seen, options) {
      if (value === null || value === void 0) return value;
      var type = typeof value;
      if (type === "boolean" || type === "number" || type === "string" || type === "bigint") {
        return value;
      }
      if (type === "symbol") {
        throw new DOMException("Symbols cannot be cloned", "DataCloneError");
      }
      if (typeof value === "object" || typeof value === "function") {
        if (seen.has(value)) {
          return seen.get(value);
        }
      }
      if (typeof value === "function") {
        throw new DOMException("Functions cannot be cloned", "DataCloneError");
      }
      if (value instanceof Date) {
        return new Date(value.getTime());
      }
      if (value instanceof RegExp) {
        return new RegExp(value.source, value.flags);
      }
      if (value instanceof Error) {
        var Ctor = value.constructor;
        if (Ctor === Error || Ctor === TypeError || Ctor === RangeError || Ctor === SyntaxError || Ctor === URIError || Ctor === ReferenceError || Ctor === EvalError) {
          var err = new Ctor(value.message);
          err.stack = value.stack;
          return err;
        }
        if (typeof DOMException === "function" && value instanceof DOMException) {
          return new DOMException(value.message, value.name);
        }
        var err = new Error(value.message);
        err.name = value.name;
        err.stack = value.stack;
        return err;
      }
      if (value instanceof Map) {
        var result = /* @__PURE__ */ new Map();
        seen.set(value, result);
        value.forEach(function(v, k) {
          result.set(clone(k, seen, options), clone(v, seen, options));
        });
        return result;
      }
      if (value instanceof Set) {
        var result = /* @__PURE__ */ new Set();
        seen.set(value, result);
        value.forEach(function(v) {
          result.add(clone(v, seen, options));
        });
        return result;
      }
      if (typeof globalThis.MessagePort === "function" && value instanceof globalThis.MessagePort) {
        var ts = options && options._qwrtTransfer;
        if (ts && ts.has(value)) {
          ts.delete(value);
          if (globalThis.__qwrt_port_from_ref__) {
            var peer = value._entangledPort;
            value._detached = true;
            var pr = globalThis.__qwrt_port_from_ref__(
              { id: value._id, peerId: value._peerId, peerThread: "local" }
            );
            if (peer && pr) pr._entangledPort = peer;
            seen.set(value, pr);
            return pr;
          }
          seen.set(value, value);
          return value;
        }
        throw new DOMException("MessagePort cannot be cloned (use transfer)", "DataCloneError");
      }
      if (value instanceof ArrayBuffer) {
        var ts = options && options._qwrtTransfer;
        var result;
        if (ts && ts.has(value)) {
          ts.delete(value);
          result = value.transfer();
        } else {
          result = value.slice(0);
        }
        seen.set(value, result);
        return result;
      }
      if (value instanceof DataView) {
        var buf = clone(value.buffer, seen, options);
        return new DataView(buf, value.byteOffset, value.byteLength);
      }
      if (value instanceof Int8Array) return cloneTypedArray(value, Int8Array, seen);
      if (value instanceof Uint8Array) return cloneTypedArray(value, Uint8Array, seen);
      if (value instanceof Uint8ClampedArray) return cloneTypedArray(value, Uint8ClampedArray, seen);
      if (value instanceof Int16Array) return cloneTypedArray(value, Int16Array, seen);
      if (value instanceof Uint16Array) return cloneTypedArray(value, Uint16Array, seen);
      if (value instanceof Int32Array) return cloneTypedArray(value, Int32Array, seen);
      if (value instanceof Uint32Array) return cloneTypedArray(value, Uint32Array, seen);
      if (value instanceof Float32Array) return cloneTypedArray(value, Float32Array, seen);
      if (value instanceof Float64Array) return cloneTypedArray(value, Float64Array, seen);
      if (typeof BigInt64Array !== "undefined" && value instanceof BigInt64Array)
        return cloneTypedArray(value, BigInt64Array, seen);
      if (typeof BigUint64Array !== "undefined" && value instanceof BigUint64Array)
        return cloneTypedArray(value, BigUint64Array, seen);
      if (typeof Blob !== "undefined" && value instanceof Blob) {
        return new Blob([value], { type: value.type });
      }
      if (typeof File !== "undefined" && value instanceof File) {
        return new File([value], value.name, { type: value.type, lastModified: value.lastModified });
      }
      if (Array.isArray(value)) {
        var result = [];
        seen.set(value, result);
        for (var i = 0; i < value.length; i++) {
          result[i] = clone(value[i], seen, options);
        }
        return result;
      }
      if (value.constructor === Object || !value.constructor) {
        var result = {};
        seen.set(value, result);
        var keys = Object.keys(value);
        for (var i = 0; i < keys.length; i++) {
          result[keys[i]] = clone(value[keys[i]], seen, options);
        }
        return result;
      }
      var result = {};
      seen.set(value, result);
      try {
        var keys = Object.keys(value);
        for (var i = 0; i < keys.length; i++) {
          result[keys[i]] = clone(value[keys[i]], seen, options);
        }
      } catch (e) {
      }
      return result;
    }
    function cloneTypedArray(value, Ctor, seen) {
      var result = new Ctor(value);
      seen.set(value, result);
      return result;
    }
    var TA_CTORS = [
      Int8Array,
      Uint8Array,
      Uint8ClampedArray,
      Int16Array,
      Uint16Array,
      Int32Array,
      Uint32Array,
      Float32Array,
      Float64Array
    ];
    if (typeof BigInt64Array !== "undefined") TA_CTORS.push(BigInt64Array, BigUint64Array);
    function utf8Encode(s) {
      var out = [];
      for (var i = 0; i < s.length; i++) {
        var c = s.charCodeAt(i);
        if (c < 128) {
          out.push(c);
        } else if (c < 2048) {
          out.push(192 | c >> 6, 128 | c & 63);
        } else if (c < 55296 || c >= 57344) {
          out.push(224 | c >> 12, 128 | c >> 6 & 63, 128 | c & 63);
        } else {
          var c2 = s.charCodeAt(++i);
          var cp = 65536 + ((c & 1023) << 10) + (c2 & 1023);
          out.push(
            240 | cp >> 18,
            128 | cp >> 12 & 63,
            128 | cp >> 6 & 63,
            128 | cp & 63
          );
        }
      }
      return out;
    }
    function utf8Decode(u8, start, len) {
      var out = "";
      var i = start, end = start + len;
      while (i < end) {
        var b = u8[i];
        if (b < 128) {
          out += String.fromCharCode(b);
          i += 1;
        } else if (b < 224) {
          out += String.fromCharCode((b & 31) << 6 | u8[i + 1] & 63);
          i += 2;
        } else if (b < 240) {
          out += String.fromCharCode((b & 15) << 12 | (u8[i + 1] & 63) << 6 | u8[i + 2] & 63);
          i += 3;
        } else {
          var cp = (b & 7) << 18 | (u8[i + 1] & 63) << 12 | (u8[i + 2] & 63) << 6 | u8[i + 3] & 63;
          var u = cp - 65536;
          out += String.fromCharCode(55296 + (u >> 10)) + String.fromCharCode(56320 + (u & 1023));
          i += 4;
        }
      }
      return out;
    }
    function ByteWriter() {
      var bytes = [];
      return {
        u8: function(b) {
          bytes.push(b & 255);
        },
        u32: function(v) {
          bytes.push(v & 255, v >>> 8 & 255, v >>> 16 & 255, v >>> 24 & 255);
        },
        f64: function(v) {
          var ab = new ArrayBuffer(8), f = new Float64Array(ab), u = new Uint8Array(ab);
          f[0] = v;
          for (var i = 0; i < 8; i++) bytes.push(u[i]);
        },
        raw: function(u8) {
          for (var i = 0; i < u8.length; i++) bytes.push(u8[i]);
        },
        done: function() {
          return new Uint8Array(bytes).buffer;
        }
      };
    }
    function encodeString(bytes, s) {
      var u = utf8Encode(String(s));
      bytes.u32(u.length);
      for (var i = 0; i < u.length; i++) bytes.u8(u[i]);
    }
    function serializeToBytes(value, transfer) {
      var transferSet = null;
      if (transfer !== void 0 && transfer !== null) {
        if (!Array.isArray(transfer))
          throw new DOMException("transfer must be a sequence", "DataCloneError");
        transferSet = /* @__PURE__ */ new Set();
        for (var i = 0; i < transfer.length; i++) {
          var t = transfer[i];
          if (transferSet.has(t))
            throw new DOMException("duplicate transferable", "DataCloneError");
          var isPort = typeof globalThis.MessagePort === "function" && t instanceof globalThis.MessagePort;
          if (!(t instanceof ArrayBuffer) && !isPort)
            throw new DOMException("object is not transferable", "DataCloneError");
          transferSet.add(t);
        }
      }
      var refs = /* @__PURE__ */ new Map();
      var next = 0;
      var bytes = ByteWriter();
      function w(v) {
        if (v === null) {
          bytes.u8(1);
          return;
        }
        if (v === void 0) {
          bytes.u8(2);
          return;
        }
        var t2 = typeof v;
        if (t2 === "boolean") {
          bytes.u8(v ? 3 : 4);
          return;
        }
        if (t2 === "number") {
          if (Number.isInteger(v) && !Object.is(v, -0) && v >= -2147483648 && v <= 2147483647) {
            bytes.u8(5);
            bytes.u32(v >>> 0);
          } else {
            bytes.u8(6);
            bytes.f64(v);
          }
          return;
        }
        if (t2 === "string") {
          bytes.u8(7);
          encodeString(bytes, v);
          return;
        }
        if (t2 === "bigint") {
          bytes.u8(31);
          encodeString(bytes, v.toString());
          return;
        }
        if (t2 === "symbol") throw new DOMException("Symbols cannot be cloned", "DataCloneError");
        if (t2 === "function") throw new DOMException("Functions cannot be cloned", "DataCloneError");
        if (refs.has(v)) {
          bytes.u8(30);
          bytes.u32(refs.get(v));
          return;
        }
        if (v instanceof Date) {
          refs.set(v, next++);
          bytes.u8(8);
          bytes.f64(v.getTime());
          return;
        }
        if (v instanceof RegExp) {
          refs.set(v, next++);
          bytes.u8(9);
          encodeString(bytes, v.source);
          encodeString(bytes, v.flags);
          return;
        }
        if (v instanceof Error) {
          refs.set(v, next++);
          bytes.u8(10);
          encodeString(bytes, v.name || "Error");
          encodeString(bytes, v.message || "");
          return;
        }
        if (typeof File !== "undefined" && v instanceof File) {
          refs.set(v, next++);
          bytes.u8(27);
          encodeString(bytes, v.name);
          encodeString(bytes, v.type || "");
          bytes.f64(v.lastModified);
          var fbytes = typeof v._getBytes === "function" ? v._getBytes() : null;
          if (!fbytes) throw new DOMException("File cannot be cloned", "DataCloneError");
          bytes.u32(fbytes.length);
          bytes.raw(fbytes);
          return;
        }
        if (typeof Blob !== "undefined" && v instanceof Blob) {
          refs.set(v, next++);
          bytes.u8(26);
          encodeString(bytes, v.type || "");
          var bbytes = typeof v._getBytes === "function" ? v._getBytes() : null;
          if (!bbytes) throw new DOMException("Blob cannot be cloned", "DataCloneError");
          bytes.u32(bbytes.length);
          bytes.raw(bbytes);
          return;
        }
        if (v instanceof Map) {
          refs.set(v, next++);
          bytes.u8(11);
          bytes.u32(v.size);
          v.forEach(function(val, key) {
            w(key);
            w(val);
          });
          return;
        }
        if (v instanceof Set) {
          refs.set(v, next++);
          bytes.u8(12);
          bytes.u32(v.size);
          v.forEach(function(val) {
            w(val);
          });
          return;
        }
        if (typeof globalThis.MessagePort === "function" && v instanceof globalThis.MessagePort) {
          if (!transferSet || !transferSet.has(v))
            throw new DOMException("MessagePort cannot be cloned (use transfer)", "DataCloneError");
          transferSet.delete(v);
          v._detached = true;
          refs.set(v, next++);
          bytes.u8(32);
          bytes.u32(v._id || 0);
          bytes.u32(v._peerId || 0);
          encodeString(bytes, v._peerThread || "local");
          return;
        }
        if (v instanceof ArrayBuffer) {
          refs.set(v, next++);
          bytes.u8(13);
          var au8 = new Uint8Array(v);
          bytes.u32(au8.length);
          bytes.raw(au8);
          if (transferSet && transferSet.has(v)) {
            transferSet.delete(v);
            v.transfer();
          }
          return;
        }
        if (v instanceof DataView) {
          refs.set(v, next++);
          bytes.u8(14);
          w(v.buffer);
          bytes.u32(v.byteOffset);
          bytes.u32(v.byteLength);
          return;
        }
        for (var i2 = 0; i2 < TA_CTORS.length; i2++) {
          if (v instanceof TA_CTORS[i2]) {
            refs.set(v, next++);
            bytes.u8(15 + i2);
            var tu8 = new Uint8Array(v.buffer, v.byteOffset, v.byteLength);
            bytes.u32(tu8.length);
            bytes.raw(tu8);
            return;
          }
        }
        if (Array.isArray(v)) {
          refs.set(v, next++);
          bytes.u8(28);
          bytes.u32(v.length);
          for (var i2 = 0; i2 < v.length; i2++) w(v[i2]);
          return;
        }
        refs.set(v, next++);
        bytes.u8(29);
        var keys;
        try {
          keys = Object.keys(v);
        } catch (e) {
          keys = [];
        }
        bytes.u32(keys.length);
        for (var i2 = 0; i2 < keys.length; i2++) {
          encodeString(bytes, keys[i2]);
          w(v[keys[i2]]);
        }
      }
      w(value);
      if (transferSet) {
        transferSet.forEach(function(ab) {
          if (!ab.detached) ab.transfer();
        });
      }
      return bytes.done();
    }
    function ByteReader(u8) {
      var i = 0;
      return {
        u8: function() {
          return u8[i++];
        },
        u32: function() {
          var v = u8[i] | u8[i + 1] << 8 | u8[i + 2] << 16 | u8[i + 3] << 24;
          i += 4;
          return v >>> 0;
        },
        f64: function() {
          var ab = new ArrayBuffer(8), f = new Float64Array(ab), u = new Uint8Array(ab);
          for (var j = 0; j < 8; j++) u[j] = u8[i + j];
          i += 8;
          return f[0];
        },
        str: function() {
          var n = this.u32();
          var s = utf8Decode(u8, i, n);
          i += n;
          return s;
        },
        bytes: function(n) {
          var out = new Uint8Array(n);
          for (var j = 0; j < n; j++) out[j] = u8[i + j];
          i += n;
          return out.buffer;
        }
      };
    }
    function deserializeFromBytes(buf) {
      var r = ByteReader(new Uint8Array(buf));
      var refs = [];
      function rd() {
        var tag = r.u8();
        switch (tag) {
          case 1:
            return null;
          case 2:
            return void 0;
          case 3:
            return true;
          case 4:
            return false;
          case 5:
            return r.u32() | 0;
          case 6:
            return r.f64();
          case 7:
            return r.str();
          case 8:
            return new Date(r.f64());
          case 9:
            return new RegExp(r.str(), r.str());
          case 10: {
            var nm = r.str(), ms = r.str();
            var e = new Error(ms);
            e.name = nm;
            refs.push(e);
            return e;
          }
          case 11: {
            var n = r.u32();
            var m = /* @__PURE__ */ new Map();
            refs.push(m);
            for (var i = 0; i < n; i++) m.set(rd(), rd());
            return m;
          }
          case 12: {
            var n = r.u32();
            var s = /* @__PURE__ */ new Set();
            refs.push(s);
            for (var i = 0; i < n; i++) s.add(rd());
            return s;
          }
          case 13: {
            var n = r.u32();
            var ab = r.bytes(n);
            refs.push(ab);
            return ab;
          }
          case 14: {
            var idx = refs.length;
            refs.push(null);
            var b = rd();
            var off = r.u32(), len = r.u32();
            var dv = new DataView(b, off, len);
            refs[idx] = dv;
            return dv;
          }
          default: {
            if (tag >= 15 && tag <= 25) {
              var Ctor = TA_CTORS[tag - 15];
              var n = r.u32();
              var ta = new Ctor(r.bytes(n));
              refs.push(ta);
              return ta;
            }
            if (tag === 26) {
              var type = r.str(), n = r.u32();
              var bl = new Blob([r.bytes(n)], { type });
              refs.push(bl);
              return bl;
            }
            if (tag === 27) {
              var nm = r.str(), type = r.str(), lm = r.f64(), n = r.u32();
              var fl = new File([r.bytes(n)], nm, { type, lastModified: lm });
              refs.push(fl);
              return fl;
            }
            if (tag === 28) {
              var n = r.u32();
              var a = [];
              refs.push(a);
              for (var i = 0; i < n; i++) a[i] = rd();
              return a;
            }
            if (tag === 29) {
              var n = r.u32();
              var o = {};
              refs.push(o);
              for (var i = 0; i < n; i++) {
                var k = r.str();
                o[k] = rd();
              }
              return o;
            }
            if (tag === 30) return refs[r.u32()];
            if (tag === 31) return BigInt(r.str());
            if (tag === 32) {
              var pid = r.u32(), ppeer = r.u32(), pth = r.str();
              var portRef;
              if (globalThis.__qwrt_port_from_ref__) {
                portRef = globalThis.__qwrt_port_from_ref__(
                  { id: pid, peerId: ppeer, peerThread: pth }
                );
              } else {
                throw new DOMException("MessagePort reference requires message-channel", "DataCloneError");
              }
              refs.push(portRef);
              return portRef;
            }
            throw new DOMException("Bad serialized data", "DataCloneError");
          }
        }
      }
      return rd();
    }
    globalThis.__qwrt_serialize__ = serializeToBytes;
    globalThis.__qwrt_deserialize__ = deserializeFromBytes;
  }

  // src/worker.js
  function setupWorker(pal2) {
    var self = globalThis;
    var workers = /* @__PURE__ */ new Map();
    function loadScript(url) {
      if (typeof url !== "string" || url.indexOf("file://") !== 0) {
        throw new Error("Worker: only file:// URLs are supported in v1");
      }
      return pal2.fsReadSync(url.slice("file://".length));
    }
    function Worker(url) {
      var code = loadScript(url);
      var id = pal2.spawnWorker(code);
      this._id = id;
      this._onmsg = null;
      this._onerror = null;
      workers.set(id, this);
      var w = this;
      Object.defineProperty(this, "onmessage", {
        get: function() {
          return w._onmsg;
        },
        set: function(fn) {
          w._onmsg = fn;
        },
        configurable: true
      });
      Object.defineProperty(this, "onerror", {
        get: function() {
          return w._onerror;
        },
        set: function(fn) {
          w._onerror = fn;
        },
        configurable: true
      });
    }
    Worker.prototype.postMessage = function(value, transfer) {
      var ports = [];
      var abTransfer;
      if (transfer && transfer.length) {
        abTransfer = [];
        for (var i = 0; i < transfer.length; i++) {
          var t = transfer[i];
          if (typeof MessagePort !== "undefined" && t instanceof MessagePort) {
            ports.push({ id: t._id, peerId: t._peerId, peerThread: t._peerThread === "local" ? "parent" : t._peerThread });
            t._detached = true;
            var peer = globalThis.__qwrt_lookup_port__(t._peerId);
            if (peer) peer._peerThread = this._id;
          } else {
            abTransfer.push(t);
          }
        }
        if (!abTransfer.length) abTransfer = void 0;
      }
      var dataBytes = __qwrt_serialize__(value, abTransfer);
      if (ports.length) {
        var wrapped = __qwrt_serialize__(
          { __qwrt_ports: ports, __qwrt_payload: dataBytes }
        );
        pal2.workerPost(this._id, wrapped);
      } else {
        pal2.workerPost(this._id, dataBytes);
      }
    };
    Worker.prototype.terminate = function() {
      pal2.workerTerminate(this._id);
      workers.delete(this._id);
    };
    globalThis.Worker = Worker;
    var hostDispatch = self.__qwrt_dispatch__;
    globalThis.__qwrt_dispatch__ = function(data, source) {
      if (source === 0) {
        hostDispatch(data, source);
        return;
      }
      var d;
      try {
        d = __qwrt_deserialize__(data);
      } catch (err) {
        reportError(err);
        return;
      }
      if (globalThis.__qwrt_deliver_port_msg__ && globalThis.__qwrt_deliver_port_msg__(d)) return;
      var w = workers.get(source);
      if (!w) return;
      if (d && typeof d === "object" && d.__qwrt_ports) {
        var ports = [];
        try {
          for (var i = 0; i < d.__qwrt_ports.length; i++) {
            ports.push(globalThis.__qwrt_port_from_ref__(d.__qwrt_ports[i]));
          }
        } catch (err) {
          reportError(err);
          return;
        }
        var inner;
        try {
          inner = __qwrt_deserialize__(d.__qwrt_payload);
        } catch (err) {
          reportError(err);
          return;
        }
        var ev2;
        try {
          ev2 = new MessageEvent("message", { data: inner, ports });
        } catch (err) {
          reportError(err);
          return;
        }
        var h2 = inner && inner.type === "error" ? w._onerror : w._onmsg;
        if (h2) {
          try {
            h2.call(self, ev2);
          } catch (err) {
            reportError(err);
          }
        }
        return;
      }
      var handler = d && d.type === "error" ? w._onerror : w._onmsg;
      if (!handler) return;
      var e;
      try {
        e = new MessageEvent("message", { data: d });
      } catch (err) {
        reportError(err);
        return;
      }
      try {
        handler.call(self, e);
      } catch (err) {
        reportError(err);
      }
    };
  }

  // src/context.js
  function setupContext(pal2) {
    var _pristine = /* @__PURE__ */ Object.create(null);
    var names = Object.keys(globalThis);
    for (var i = 0; i < names.length; i++) _pristine[names[i]] = 1;
    var _infra = {
      __qwrt_ctx_capture__: 1,
      __qwrt_ctx_restore__: 1,
      qwrtContext: 1
    };
    globalThis.__qwrt_ctx_capture__ = function() {
      var props = {};
      var skipped = [];
      var keys = Object.keys(globalThis);
      for (var i2 = 0; i2 < keys.length; i2++) {
        var n = keys[i2];
        if (_pristine[n] || _infra[n]) continue;
        var v;
        try {
          v = globalThis[n];
        } catch (e) {
          continue;
        }
        try {
          props[n] = __qwrt_serialize__(v);
        } catch (e) {
          skipped.push(n);
        }
      }
      return __qwrt_serialize__({ props, skipped });
    };
    globalThis.__qwrt_ctx_restore__ = function(bytes) {
      var rec = __qwrt_deserialize__(bytes);
      var p = rec && rec.props || {};
      var keys = Object.keys(p);
      for (var i2 = 0; i2 < keys.length; i2++) {
        var n = keys[i2];
        if (_infra[n]) continue;
        var v;
        try {
          v = __qwrt_deserialize__(p[n]);
        } catch (e) {
          continue;
        }
        try {
          globalThis[n] = v;
        } catch (e) {
        }
      }
      return rec && rec.skipped || [];
    };
    globalThis.qwrtContext = {
      spawn: function(s) {
        return pal2.contextSpawn(String(s));
      },
      suspend: function(id, p) {
        return pal2.contextSuspend(Number(id), String(p));
      },
      resume: function(id, s, p) {
        return pal2.contextResume(Number(id), String(s), String(p));
      },
      destroy: function(id) {
        return pal2.contextDestroy(Number(id));
      }
    };
  }

  // src/index.js
  setupConsole(pal);
  setupPerformance(pal);
  setupTimers(pal);
  setupEventTarget();
  setupAbort();
  setupErrorEvents();
  setupURL();
  setupEncoding(pal);
  setupFetch(pal);
  setupMessageChannel(pal);
  setupBroadcastChannel();
  setupCacheStorage();
  setupEventSource(pal);
  setupWebSocket(pal);
  setupHttpServer(pal);
  setupHostMessaging(pal);
  setupStreams(pal);
  setupBlobFileFormData();
  setupURLPattern();
  setupNavigatorReportError();
  setupFS(pal);
  setupStorage(pal);
  setupTextEncoding(pal);
  setupCrypto(pal);
  setupCryptoSubtle(pal);
  setupStructuredClone();
  setupWorker(pal);
  setupContext(pal);
  if (typeof globalThis.queueMicrotask !== "function") {
    globalThis.queueMicrotask = function(callback) {
      if (typeof callback !== "function") {
        throw new TypeError("queueMicrotask requires a function argument");
      }
      Promise.resolve().then(callback);
    };
  }
})(__native_inject__);

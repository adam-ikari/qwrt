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
    const performance = {
      /**
       * Returns a high-resolution timestamp in milliseconds.
       */
      now: function() {
        return nowMs();
      },
      /**
       * Get the time origin (approximation - time when runtime started)
       * Since we don't track this precisely, we use 0 as baseline.
       */
      get timeOrigin() {
        return 0;
      },
      /**
       * Create a named performance mark.
       */
      mark: function(name, options) {
        if (typeof name !== "string" || name === "") {
          throw new TypeError("Mark name must be a non-empty string");
        }
        marks.set(name, {
          name,
          entryType: "mark",
          startTime: nowMs(),
          duration: 0
        });
      },
      /**
       * Create a named performance measure between two marks.
       */
      measure: function(name, startMark, endMark) {
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
      },
      /**
       * Remove a mark by name.
       */
      clearMarks: function(name) {
        if (name) {
          marks.delete(name);
        } else {
          marks.clear();
        }
      },
      /**
       * Remove measures by name.
       */
      clearMeasures: function(name) {
        if (name) {
          for (let i = measures.length - 1; i >= 0; i--) {
            if (measures[i].name === name) {
              measures.splice(i, 1);
            }
          }
        } else {
          measures.length = 0;
        }
      },
      /**
       * Get all performance entries.
       */
      getEntries: function() {
        const result = [];
        marks.forEach((entry) => result.push({ ...entry }));
        measures.forEach((entry) => result.push({ ...entry }));
        return result.sort((a, b) => a.startTime - b.startTime);
      },
      /**
       * Get entries by name.
       */
      getEntriesByName: function(name, type) {
        return this.getEntries().filter(
          (entry) => entry.name === name && (!type || entry.entryType === type)
        );
      },
      /**
       * Get entries by type.
       */
      getEntriesByType: function(type) {
        return this.getEntries().filter((entry) => entry.entryType === type);
      }
    };
    globalThis.performance = performance;
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
        delay
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
    class URL {
      constructor(url, base) {
        let baseUrl = null;
        if (base) {
          baseUrl = base instanceof URL ? base : new URL(base);
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
          new URL(url, base);
          return true;
        } catch (e) {
          return false;
        }
      }
    }
    globalThis.URL = URL;
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
      if (init !== void 0 && init !== null) {
        if (init instanceof Headers) {
          init._map.forEach(function(value, key) {
            this._map.set(key, value);
          }.bind(this));
        } else if (typeof init === "object") {
          if (Symbol && Symbol.iterator && init[Symbol.iterator]) {
            var items = Array.from(init);
            for (var i = 0; i < items.length; i++) {
              var pair = items[i];
              if (!Array.isArray(pair) || pair.length < 2) {
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
    function Request(input, init) {
      init = init || {};
      if (input instanceof Request) {
        this._method = init.method || input.method;
        this._url = input.url;
        this._headers = new Headers(init.headers || input.headers);
        this._body = init.body !== void 0 ? init.body : input._body;
        this._signal = init.signal || input.signal;
      } else {
        this._method = init.method || "GET";
        this._url = String(input);
        this._headers = new Headers(init.headers);
        this._body = init.body !== void 0 ? init.body : null;
        this._signal = init.signal || null;
      }
      this._bodyUsed = false;
      if (!/^[A-Z]+$/.test(this._method)) {
        throw new TypeError("Invalid HTTP method: " + this._method);
      }
    }
    Object.defineProperty(Request.prototype, "method", {
      get: function() {
        return this._method;
      }
    });
    Object.defineProperty(Request.prototype, "url", {
      get: function() {
        return this._url;
      }
    });
    Object.defineProperty(Request.prototype, "headers", {
      get: function() {
        return this._headers;
      }
    });
    Object.defineProperty(Request.prototype, "body", {
      get: function() {
        return this._body;
      }
    });
    Object.defineProperty(Request.prototype, "bodyUsed", {
      get: function() {
        return this._bodyUsed;
      }
    });
    Object.defineProperty(Request.prototype, "signal", {
      get: function() {
        return this._signal;
      }
    });
    Request.prototype.clone = function() {
      if (this._bodyUsed) {
        throw new TypeError("Cannot clone a Request whose body has been used");
      }
      return new Request(this);
    };
    Request.prototype.text = function() {
      if (this._bodyUsed) {
        throw new TypeError("Body has already been used");
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
    function Response(body, init) {
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
    Object.defineProperty(Response.prototype, "status", {
      get: function() {
        return this._status;
      }
    });
    Object.defineProperty(Response.prototype, "statusText", {
      get: function() {
        return this._statusText;
      }
    });
    Object.defineProperty(Response.prototype, "ok", {
      get: function() {
        return this._status >= 200 && this._status <= 299;
      }
    });
    Object.defineProperty(Response.prototype, "headers", {
      get: function() {
        return this._headers;
      }
    });
    Object.defineProperty(Response.prototype, "body", {
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
    Object.defineProperty(Response.prototype, "bodyUsed", {
      get: function() {
        return this._bodyUsed;
      }
    });
    Object.defineProperty(Response.prototype, "type", {
      get: function() {
        return this._type;
      }
    });
    Object.defineProperty(Response.prototype, "url", {
      get: function() {
        return this._url;
      }
    });
    Object.defineProperty(Response.prototype, "redirected", {
      get: function() {
        return this._redirected;
      }
    });
    Response.prototype.clone = function() {
      if (this._bodyUsed) {
        throw new TypeError("Cannot clone a Response whose body has been used");
      }
      if (this._bodyStream) {
        throw new TypeError("Cannot clone a streaming Response");
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
    Response.prototype._readStreamFully = function() {
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
    Response.prototype.json = function() {
      return this.text().then(function(text) {
        return JSON.parse(text);
      });
    };
    Response.prototype.arrayBuffer = function() {
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
    Response.prototype.blob = function() {
      return this.text();
    };
    Response.error = function() {
      var response = new Response(null, {
        status: 0,
        statusText: ""
      });
      response._type = "error";
      return response;
    };
    Response.redirect = function(url, status) {
      if (status === void 0) status = 302;
      if (status < 300 || status > 399) {
        throw new RangeError("Invalid redirect status: " + status);
      }
      var response = new Response(null, {
        status,
        headers: { location: url }
      });
      response._type = "opaqueredirect";
      return response;
    };
    Response.json = function(data, init) {
      init = init || {};
      var body = JSON.stringify(data);
      var headers = new Headers(init.headers);
      if (!headers.has("content-type")) {
        headers.set("content-type", "application/json");
      }
      return new Response(body, {
        status: init.status !== void 0 ? init.status : 200,
        statusText: init.statusText || "",
        headers
      });
    };
    function fetch(input, init) {
      return new Promise(function(resolve, reject) {
        var request;
        try {
          request = new Request(input, init);
        } catch (e) {
          reject(e);
          return;
        }
        if (request.signal && request.signal.aborted) {
          reject(new DOMException("The operation was aborted.", "AbortError"));
          return;
        }
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
        var resolvedResponse = null;
        var streamController = null;
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
        if (typeof pal2.httpRequestStream !== "function") {
          if (aborted) {
            return;
          }
          if (request.signal && onAbort) {
            request.signal.removeEventListener("abort", onAbort);
          }
          var p = pal2.httpRequest(request.url, request.method, headersJson, bodyStr);
          Promise.resolve(p).then(function(data) {
            if (aborted) return;
            var bodyBytes = stringToUint8Array(data || "");
            var res = new Response(bodyBytes, {
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
          var headers = new Headers(parsedHeaders);
          resolvedResponse = new Response(readableStream, {
            status,
            statusText: STATUS_TEXTS[status] || "",
            headers,
            url: request.url
          });
          resolve(resolvedResponse);
        }
        function onData(chunk) {
          if (aborted) return;
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
          if (request.signal && onAbort) {
            request.signal.removeEventListener("abort", onAbort);
          }
          if (aborted) return;
          if (streamController) {
            if (errorStatus !== 0) {
              streamController.error(new TypeError("fetch failed with status: " + errorStatus));
            } else {
              streamController.close();
            }
          }
        }
        pal2.httpRequestStream(request.url, request.method, headersJson, bodyStr, onHeaders, onData, onEnd);
      });
    }
    globalThis.Headers = Headers;
    globalThis.Request = Request;
    globalThis.Response = Response;
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
      var encoded = this.encode(src);
      var len = Math.min(encoded.length, dst.length);
      for (var i = 0; i < len; i++) dst[i] = encoded[i];
      return { read: src.length, written: len };
    };
    function TextDecoder2(encoding, options) {
      this.encoding = (encoding || "utf-8").toLowerCase();
      this.fatal = options && options.fatal || false;
      this.ignoreBOM = options && options.ignoreBOM || false;
      this._buffer = null;
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
      var str = "";
      var i = 0;
      var lastComplete = 0;
      while (i < allBytes.length) {
        var lead = allBytes[i];
        var want = utf8LeadLen(lead);
        if (want < 0) {
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
    var crypto2 = {
      getRandomValues: function getRandomValues(typedArray) {
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
      },
      randomUUID: function randomUUID() {
        var bytes = new Uint8Array(16);
        this.getRandomValues(bytes);
        bytes[6] = bytes[6] & 15 | 64;
        bytes[8] = bytes[8] & 63 | 128;
        var hex = Array.from(bytes, function(b) {
          return b.toString(16).padStart(2, "0");
        }).join("");
        return hex.slice(0, 8) + "-" + hex.slice(8, 12) + "-" + hex.slice(12, 16) + "-" + hex.slice(16, 20) + "-" + hex.slice(20);
      },
      subtle: void 0
      // Not implemented
    };
    globalThis.crypto = crypto2;
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
  function setupMessageChannel() {
    if (typeof globalThis.EventTarget !== "function") {
      throw new Error("MessagePort requires EventTarget to be loaded first");
    }
    class MessageEvent extends Event {
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
    class MessagePort extends EventTarget {
      constructor() {
        super();
        this._entangledPort = null;
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
      postMessage(message, transfer) {
        if (!this._entangledPort) return;
        var data;
        try {
          data = typeof globalThis.structuredClone === "function" ? globalThis.structuredClone(message, transfer ? { transfer } : void 0) : JSON.parse(JSON.stringify(message));
        } catch (e) {
          var errorEvent = new MessageEvent("messageerror", { data: e });
          this._entangledPort.dispatchEvent(errorEvent);
          return;
        }
        var event = new MessageEvent("message", { data, ports: [] });
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
    class MessageChannel {
      constructor() {
        this._port1 = new MessagePort();
        this._port2 = new MessagePort();
        this._port1._entangledPort = this._port2;
        this._port2._entangledPort = this._port1;
      }
      get port1() {
        return this._port1;
      }
      get port2() {
        return this._port2;
      }
    }
    globalThis.MessageChannel = MessageChannel;
    globalThis.MessagePort = MessagePort;
    globalThis.MessageEvent = MessageEvent;
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
        });
      }
      releaseLock() {
        if (this._stream._reader !== this) return;
        this._stream._reader = null;
        this._isClosed = true;
      }
    }
    class ReadableStream {
      constructor(underlyingSource, strategy) {
        underlyingSource = underlyingSource || {};
        this._state = "readable";
        this._reader = null;
        this._queue = [];
        this._hwm = strategy && strategy.highWaterMark || 1;
        this._storedError = null;
        this._pendingReads = [];
        this._controller = new ReadableStreamDefaultController(this);
        var source = underlyingSource;
        this._cancel = source.cancel || ReadableStreamUnderlyingSourceDefaultCancel;
        this._pull = source.pull || ReadableStreamUnderlyingSourceDefaultPull;
        if (source.start) {
          source.start(this._controller);
        }
      }
      _notifyReaders() {
        while (this._pendingReads.length > 0) {
          if (this._queue.length > 0) {
            var entry = this._pendingReads.shift();
            var chunk = this._queue.shift();
            entry.resolve({ done: false, value: chunk });
          } else if (this._state === "closed") {
            var entry = this._pendingReads.shift();
            entry.resolve({ done: true, value: void 0 });
          } else if (this._state === "errored") {
            var entry = this._pendingReads.shift();
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
        if (this._state === "readable" && this._queue.length < this._hwm && this._pull) {
          try {
            var result = this._pull(this._controller);
            if (result && typeof result.catch === "function") {
              result.catch(function(e) {
                this._controller.error(e);
              }.bind(this));
            }
          } catch (e) {
            this._controller.error(e);
          }
        }
      }
      get locked() {
        return this._reader !== null;
      }
      getReader() {
        if (this._reader) {
          throw new TypeError("ReadableStream already locked");
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
            },
            pull: function(controller) {
              pullAndDispatch();
            },
            cancel: function() {
            }
          });
        }
        var branch1 = createBranch();
        var branch2 = createBranch();
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
          try {
            reader.releaseLock();
          } catch (x) {
          }
          try {
            writer.abort(e);
          } catch (x) {
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
        var buffers = [];
        var totalSize = 0;
        if (blobParts) {
          for (var i = 0; i < blobParts.length; i++) {
            var part = blobParts[i];
            var bytes;
            if (part instanceof Blob2) {
              bytes = part._getBytes();
            } else if (part instanceof ArrayBuffer) {
              bytes = new Uint8Array(part);
            } else if (ArrayBuffer.isView(part)) {
              bytes = new Uint8Array(part.buffer, part.byteOffset, part.byteLength);
            } else if (typeof part === "string") {
              bytes = new Uint8Array(part.length);
              for (var j = 0; j < part.length; j++) {
                bytes[j] = part.charCodeAt(j);
              }
            } else {
              var str = String(part);
              bytes = new Uint8Array(str.length);
              for (var j = 0; j < str.length; j++) {
                bytes[j] = str.charCodeAt(j);
              }
            }
            buffers.push(bytes);
            totalSize += bytes.length;
          }
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
        start = start || 0;
        if (start < 0) start = Math.max(this._size + start, 0);
        if (start > this._size) start = this._size;
        end = end === void 0 ? this._size : end;
        if (end < 0) end = Math.max(this._size + end, 0);
        if (end > this._size) end = this._size;
        var sliceLen = end > start ? end - start : 0;
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
          result.set(buf.subarray(localStart, localEnd), offset);
          offset += copyLen;
          globalStart = 0;
        }
        return new Blob2([result], { type: contentType || "" });
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
      var s = String(type).toLowerCase().trim();
      if (s === "") return "";
      if (s.indexOf("/") === -1) return "";
      return s;
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
            regexStr += "(:(.+))";
          } else if (modifier === "*") {
            regexStr += "(:(.*))";
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
      userAgent: "qwrt/1.0 (WinterCG)",
      language: "en-US",
      platform: "wintercg",
      hardwareConcurrency: 1,
      onLine: true,
      maxTouchPoints: 0
    };
    globalThis.navigator = navigator;
    globalThis.self = globalThis;
    globalThis.reportError = function reportError(error) {
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
  }

  // src/structured-clone.js
  function setupStructuredClone() {
    globalThis.structuredClone = function structuredClone(value, options) {
      var seen = /* @__PURE__ */ new Map();
      return clone(value, seen, options);
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
      if (value instanceof ArrayBuffer) {
        var result = value.slice(0);
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
  setupMessageChannel();
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
  if (typeof globalThis.queueMicrotask !== "function") {
    globalThis.queueMicrotask = function(callback) {
      if (typeof callback !== "function") {
        throw new TypeError("queueMicrotask requires a function argument");
      }
      Promise.resolve().then(callback);
    };
  }
})(__pal_inject__);

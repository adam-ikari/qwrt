/**
 * qwrt polyfill: AbortController and AbortSignal
 *
 * Standard AbortController implementation for cancelable operations.
 * Pure JS - no PAL primitives needed.
 *
 * Depends on EventTarget (must be loaded after event-target.js).
 */

export function setupAbort() {
  // Ensure EventTarget is available
  if (typeof globalThis.EventTarget !== 'function') {
    throw new Error('AbortController requires EventTarget to be loaded first');
  }

  /**
   * AbortSignal class
   *
   * A signal object that can be watched for abort events.
   * Extends EventTarget for addEventListener support.
   */
  class AbortSignal extends EventTarget {
    constructor() {
      super();
      this._aborted = false;
      this._reason = undefined;
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
          this._reason || 'The operation was aborted',
          'AbortError'
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

      // Dispatch abort event
      const event = new Event('abort');
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
        signal._abort(new DOMException('The operation timed out', 'TimeoutError'));
      }, ms);

      // Clean up timer if signal is aborted manually
      signal.addEventListener('abort', function() {
        clearTimeout(timer);
      });

      return signal;
    }

    /**
     * Static method to create a signal that aborts when any of the given signals abort.
     */
    static any(signals) {
      if (!Array.isArray(signals)) {
        throw new TypeError('signals must be an array');
      }

      const result = new AbortSignal();

      for (const signal of signals) {
        if (!(signal instanceof AbortSignal)) {
          throw new TypeError('All signals must be AbortSignal instances');
        }

        if (signal.aborted) {
          // Already aborted - abort result immediately
          result._abort(signal.reason);
          return result;
        }

        signal.addEventListener('abort', function() {
          result._abort(signal.reason);
        });
      }

      return result;
    }
  }

  /**
   * AbortController class
   *
   * Controller that can be used to abort async operations.
   */
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

  // DOMException polyfill (if not already defined)
  if (typeof globalThis.DOMException === 'undefined') {
    class DOMException extends Error {
      constructor(message, name) {
        super(message);
        this.name = name || 'Error';
        this.code = DOMException._codes[this.name] || 0;
      }
    }

    // Error code constants
    DOMException._codes = {
      'IndexSizeError': 1,
      'DOMStringSizeError': 2,
      'HierarchyRequestError': 3,
      'WrongDocumentError': 4,
      'InvalidCharacterError': 5,
      'NoDataAllowedError': 6,
      'NoModificationAllowedError': 7,
      'NotFoundError': 8,
      'NotSupportedError': 9,
      'InUseAttributeError': 10,
      'InvalidStateError': 11,
      'SyntaxError': 12,
      'InvalidModificationError': 13,
      'NamespaceError': 14,
      'InvalidAccessError': 15,
      'ValidationError': 16,
      'TypeMismatchError': 17,
      'SecurityError': 18,
      'NetworkError': 19,
      'AbortError': 20,
      'URLMismatchError': 21,
      'QuotaExceededError': 22,
      'TimeoutError': 23,
      'InvalidNodeTypeError': 24,
      'DataCloneError': 25
    };

    globalThis.DOMException = DOMException;
  }

  // Register on globalThis
  globalThis.AbortController = AbortController;
  globalThis.AbortSignal = AbortSignal;
}

/**
 * qwrt polyfill: structuredClone (enhanced)
 *
 * Replaces the basic JSON.parse(JSON.stringify()) implementation
 * with proper structured clone that handles:
 *   - TypedArrays (all types)
 *   - ArrayBuffer / DataView
 *   - Blob / File
 *   - Error (and subtypes)
 *   - Map / Set
 *   - Date
 *   - RegExp
 *   - Circular references
 *   - Infinity / NaN / -0
 *
 * TC55/ECMA-429 requires structuredClone to handle these types.
 *
 * Pure JS - no PAL primitives needed.
 */

export function setupStructuredClone() {

  /**
   * structuredClone(value, options)
   *
   * Deep clones a value using the structured clone algorithm.
   * Handles circular references and special JS types.
   */
  globalThis.structuredClone = function structuredClone(value, options) {
    var seen = new Map();
    return clone(value, seen, options);
  };

  function clone(value, seen, options) {
    // Primitives: return as-is (handles null, undefined, boolean, number, string, bigint, symbol)
    if (value === null || value === undefined) return value;
    var type = typeof value;
    if (type === 'boolean' || type === 'number' || type === 'string' || type === 'bigint') {
      return value;
    }
    if (type === 'symbol') {
      throw new DOMException('Symbols cannot be cloned', 'DataCloneError');
    }

    // Check for circular reference
    if (typeof value === 'object' || typeof value === 'function') {
      if (seen.has(value)) {
        return seen.get(value);
      }
    }

    // Handle functions — cannot be cloned
    if (typeof value === 'function') {
      throw new DOMException('Functions cannot be cloned', 'DataCloneError');
    }

    // Date
    if (value instanceof Date) {
      return new Date(value.getTime());
    }

    // RegExp
    if (value instanceof RegExp) {
      return new RegExp(value.source, value.flags);
    }

    // Error types
    if (value instanceof Error) {
      var Ctor = value.constructor;
      if (Ctor === Error || Ctor === TypeError || Ctor === RangeError ||
          Ctor === SyntaxError || Ctor === URIError || Ctor === ReferenceError ||
          Ctor === EvalError) {
        var err = new Ctor(value.message);
        err.stack = value.stack;
        return err;
      }
      // DOMException
      if (typeof DOMException === 'function' && value instanceof DOMException) {
        return new DOMException(value.message, value.name);
      }
      // Generic Error
      var err = new Error(value.message);
      err.name = value.name;
      err.stack = value.stack;
      return err;
    }

    // Map
    if (value instanceof Map) {
      var result = new Map();
      seen.set(value, result);
      value.forEach(function(v, k) {
        result.set(clone(k, seen, options), clone(v, seen, options));
      });
      return result;
    }

    // Set
    if (value instanceof Set) {
      var result = new Set();
      seen.set(value, result);
      value.forEach(function(v) {
        result.add(clone(v, seen, options));
      });
      return result;
    }

    // ArrayBuffer
    if (value instanceof ArrayBuffer) {
      var result = value.slice(0);
      seen.set(value, result);
      return result;
    }

    // DataView
    if (value instanceof DataView) {
      var buf = clone(value.buffer, seen, options);
      return new DataView(buf, value.byteOffset, value.byteLength);
    }

    // TypedArrays
    if (value instanceof Int8Array) return cloneTypedArray(value, Int8Array, seen);
    if (value instanceof Uint8Array) return cloneTypedArray(value, Uint8Array, seen);
    if (value instanceof Uint8ClampedArray) return cloneTypedArray(value, Uint8ClampedArray, seen);
    if (value instanceof Int16Array) return cloneTypedArray(value, Int16Array, seen);
    if (value instanceof Uint16Array) return cloneTypedArray(value, Uint16Array, seen);
    if (value instanceof Int32Array) return cloneTypedArray(value, Int32Array, seen);
    if (value instanceof Uint32Array) return cloneTypedArray(value, Uint32Array, seen);
    if (value instanceof Float32Array) return cloneTypedArray(value, Float32Array, seen);
    if (value instanceof Float64Array) return cloneTypedArray(value, Float64Array, seen);
    if (typeof BigInt64Array !== 'undefined' && value instanceof BigInt64Array)
      return cloneTypedArray(value, BigInt64Array, seen);
    if (typeof BigUint64Array !== 'undefined' && value instanceof BigUint64Array)
      return cloneTypedArray(value, BigUint64Array, seen);

    // Blob
    if (typeof Blob !== 'undefined' && value instanceof Blob) {
      return new Blob([value], { type: value.type });
    }

    // File
    if (typeof File !== 'undefined' && value instanceof File) {
      return new File([value], value.name, { type: value.type, lastModified: value.lastModified });
    }

    // Array
    if (Array.isArray(value)) {
      var result = [];
      seen.set(value, result);
      for (var i = 0; i < value.length; i++) {
        result[i] = clone(value[i], seen, options);
      }
      return result;
    }

    // Plain object
    if (value.constructor === Object || !value.constructor) {
      var result = {};
      seen.set(value, result);
      var keys = Object.keys(value);
      for (var i = 0; i < keys.length; i++) {
        result[keys[i]] = clone(value[keys[i]], seen, options);
      }
      return result;
    }

    // Objects with custom constructor — try to clone as plain object
    // (structured clone spec: only certain types are cloneable)
    var result = {};
    seen.set(value, result);
    try {
      var keys = Object.keys(value);
      for (var i = 0; i < keys.length; i++) {
        result[keys[i]] = clone(value[keys[i]], seen, options);
      }
    } catch (e) {
      // If we can't enumerate, just return empty
    }
    return result;
  }

  function cloneTypedArray(value, Ctor, seen) {
    var result = new Ctor(value);
    seen.set(value, result);
    return result;
  }
}

/**
 * qwrt polyfill: Blob, File, FormData
 *
 * TC55/ECMA-429 requires these for file/data handling.
 *
 * Pure JS implementation — no PAL primitives needed.
 */

export function setupBlobFileFormData() {

  /**
   * Blob
   *
   * Represents raw binary data with a content type.
   */
  class Blob {
    constructor(blobParts, options) {
      options = options || {};

      // Flatten blobParts into a single byte array
      var buffers = [];
      var totalSize = 0;

      if (blobParts) {
        for (var i = 0; i < blobParts.length; i++) {
          var part = blobParts[i];
          var bytes;
          if (part instanceof Blob) {
            bytes = part._getBytes();
          } else if (part instanceof ArrayBuffer) {
            bytes = new Uint8Array(part);
          } else if (ArrayBuffer.isView(part)) {
            bytes = new Uint8Array(part.buffer, part.byteOffset, part.byteLength);
          } else if (typeof part === 'string') {
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
      this._type = normalizeType(options.type || '');
    }

    get size() { return this._size; }
    get type() { return this._type; }

    slice(start, end, contentType) {
      start = start || 0;
      if (start < 0) start = Math.max(this._size + start, 0);
      if (start > this._size) start = this._size;
      end = end === undefined ? this._size : end;
      if (end < 0) end = Math.max(this._size + end, 0);
      if (end > this._size) end = this._size;

      var sliceLen = end > start ? end - start : 0;

      // Extract the slice bytes
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

      return new Blob([result], { type: contentType || '' });
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
      return obj instanceof Blob;
    }
  }

  /**
   * File
   *
   * Extends Blob with name and lastModified.
   */
  class File extends Blob {
    constructor(fileBits, fileName, options) {
      options = options || {};
      super(fileBits, options);
      this._name = String(fileName);
      this._lastModified = options.lastModified || Date.now();
    }

    get name() { return this._name; }
    get lastModified() { return this._lastModified; }
  }

  /**
   * FormData
   *
   * Key-value store supporting string and Blob/File values.
   */
  class FormData {
    constructor() {
      this._entries = [];
    }

    append(name, value, filename) {
      this._entries.push({
        name: String(name),
        value: this._normalizeValue(value, filename),
      });
    }

    delete(name) {
      this._entries = this._entries.filter(function(entry) {
        return entry.name !== name;
      });
    }

    get(name) {
      var entry = this._entries.find(function(e) { return e.name === name; });
      return entry ? entry.value : null;
    }

    getAll(name) {
      return this._entries
        .filter(function(e) { return e.name === name; })
        .map(function(e) { return e.value; });
    }

    has(name) {
      return this._entries.some(function(e) { return e.name === name; });
    }

    set(name, value, filename) {
      // Remove existing entries with this name
      var found = false;
      var newEntries = [];
      for (var i = 0; i < this._entries.length; i++) {
        if (this._entries[i].name === name) {
          if (!found) {
            newEntries.push({
              name: String(name),
              value: this._normalizeValue(value, filename),
            });
            found = true;
          }
          // Skip duplicates
        } else {
          newEntries.push(this._entries[i]);
        }
      }
      if (!found) {
        newEntries.push({
          name: String(name),
          value: this._normalizeValue(value, filename),
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
      return this._entries.map(function(e) { return e.name; })[Symbol.iterator]();
    }

    values() {
      return this._entries.map(function(e) { return e.value; })[Symbol.iterator]();
    }

    entries() {
      return this._entries.map(function(e) { return [e.name, e.value]; })[Symbol.iterator]();
    }

    _normalizeValue(value, filename) {
      if (value instanceof Blob) {
        if (!(value instanceof File) && filename) {
          // Wrap Blob as File with the given filename
          return new File([value], filename, { type: value.type });
        }
        return value;
      }
      return String(value);
    }
  }

  // Helper: normalize MIME type
  function normalizeType(type) {
    // Remove any parameters, lower-case
    var s = String(type).toLowerCase().trim();
    if (s === '') return '';
    // Basic validation: must contain a /
    if (s.indexOf('/') === -1) return '';
    return s;
  }

  // Helper: decode Uint8Array to string (UTF-8)
  function decodeUint8Array(bytes) {
    return new TextDecoder().decode(bytes);
  }

  globalThis.Blob = Blob;
  globalThis.File = File;
  globalThis.FormData = FormData;
}

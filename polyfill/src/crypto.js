/**
 * qwrt Polyfill - crypto.getRandomValues
 *
 * Provides crypto.getRandomValues() and crypto.randomUUID().
 * Uses pal.randomBytes() for cryptographically secure random generation.
 *
 * Mounted on globalThis.crypto
 */

export function setupCrypto(pal) {
  var crypto = {
    getRandomValues: function getRandomValues(typedArray) {
      if (!(typedArray instanceof Uint8Array) &&
          !(typedArray instanceof Uint16Array) &&
          !(typedArray instanceof Uint32Array) &&
          !(typedArray instanceof Int8Array) &&
          !(typedArray instanceof Int16Array) &&
          !(typedArray instanceof Int32Array)) {
        throw new TypeError('Argument must be a TypedArray');
      }

      var totalBytes = typedArray.length * typedArray.BYTES_PER_ELEMENT;
      var ab = pal.randomBytes(totalBytes);
      var src = new Uint8Array(ab);

      // Copy bytes into the typed array
      var dst = new Uint8Array(typedArray.buffer, typedArray.byteOffset, totalBytes);
      dst.set(src);

      return typedArray;
    },

    randomUUID: function randomUUID() {
      // Version 4 UUID using getRandomValues
      var bytes = new Uint8Array(16);
      this.getRandomValues(bytes);
      bytes[6] = (bytes[6] & 0x0F) | 0x40;  // version 4
      bytes[8] = (bytes[8] & 0x3F) | 0x80;  // variant 1
      var hex = Array.from(bytes, function(b) { return b.toString(16).padStart(2, '0'); }).join('');
      return hex.slice(0, 8) + '-' + hex.slice(8, 12) + '-' + hex.slice(12, 16) + '-' + hex.slice(16, 20) + '-' + hex.slice(20);
    },

    subtle: undefined,  // Not implemented
  };

  globalThis.crypto = crypto;
}

/**
 * qwrt polyfill: atob / btoa
 *
 * Base64 encoding/decoding functions.
 * Pure JS - no PAL primitives needed.
 *
 * Implements the standard atob() and btoa() functions as defined in
 * the HTML Living Standard.
 */

export function setupEncoding(pal) {
  var useNativeBtoa = typeof pal.nativeBtoa === 'function';
  var useNativeAtob = typeof pal.nativeAtob === 'function';

  const BASE64_CHARS = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
  const BASE64_DECODE = {};
  for (let i = 0; i < BASE64_CHARS.length; i++) {
    BASE64_DECODE[BASE64_CHARS[i]] = i;
  }
  BASE64_DECODE['='] = 0;

  globalThis.btoa = function(binaryString) {
    if (binaryString === null || binaryString === undefined) {
      throw new TypeError('btoa requires a string argument');
    }

    if (useNativeBtoa) {
      return pal.nativeBtoa(String(binaryString));
    }

    binaryString = String(binaryString);

    for (let i = 0; i < binaryString.length; i++) {
      const code = binaryString.charCodeAt(i);
      if (code > 255) {
        throw new Error(
          "Failed to execute 'btoa': The string to be encoded contains characters outside of the Latin1 range."
        );
      }
    }

    let result = '';
    let i = 0;
    const len = binaryString.length;

    while (i < len) {
      let byteCount = 0;
      const a = binaryString.charCodeAt(i++);
      byteCount++;
      const b = i < len ? (byteCount++, binaryString.charCodeAt(i++)) : 0;
      const c = i < len ? (byteCount++, binaryString.charCodeAt(i++)) : 0;

      const triplet = (a << 16) | (b << 8) | c;

      result += BASE64_CHARS[(triplet >> 18) & 0x3F];
      result += BASE64_CHARS[(triplet >> 12) & 0x3F];
      result += byteCount >= 2 ? BASE64_CHARS[(triplet >> 6) & 0x3F] : '=';
      result += byteCount >= 3 ? BASE64_CHARS[triplet & 0x3F] : '=';
    }

    return result;
  };

  globalThis.atob = function(base64String) {
    if (base64String === null || base64String === undefined) {
      throw new TypeError('atob requires a string argument');
    }

    if (useNativeAtob) {
      return pal.nativeAtob(String(base64String));
    }

    base64String = String(base64String);
    base64String = base64String.replace(/\s/g, '');

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

    let result = '';
    let i = 0;
    const len = base64String.length;

    while (i < len) {
      const a = BASE64_DECODE[base64String[i++]];
      const b = BASE64_DECODE[base64String[i++]];
      const c = BASE64_DECODE[base64String[i++]];
      const d = BASE64_DECODE[base64String[i++]];

      const triplet = (a << 18) | (b << 12) | (c << 6) | d;

      result += String.fromCharCode((triplet >> 16) & 0xFF);

      if (base64String[i - 2] !== '=') {
        result += String.fromCharCode((triplet >> 8) & 0xFF);
      }

      if (base64String[i - 1] !== '=') {
        result += String.fromCharCode(triplet & 0xFF);
      }
    }

    return result;
  };
}

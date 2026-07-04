/**
 * qwrt Polyfill - TextEncoder + TextDecoder
 *
 * Pure JS implementation of the Web Encoding API with native acceleration.
 * Handles UTF-8 encoding/decoding including surrogate pairs.
 *
 * When pal.nativeEncodeUtf8/nativeDecodeUtf8 are available (textcodec extension),
 * uses native C for significantly faster encode/decode on large strings.
 */

export function setupTextEncoding(pal) {
  var useNativeEncode = typeof pal.nativeEncodeUtf8 === 'function';
  var useNativeDecode = typeof pal.nativeDecodeUtf8 === 'function';

  function TextEncoder() {
    this.encoding = 'utf-8';
  }

  TextEncoder.prototype.encode = function encode(input) {
    var str = input === undefined ? '' : String(input);
    if (useNativeEncode) {
      return pal.nativeEncodeUtf8(str);
    }
    var bytes = [];
    for (var i = 0; i < str.length; i++) {
      var code = str.charCodeAt(i);
      if (code < 0x80) {
        bytes.push(code);
      } else if (code < 0x800) {
        bytes.push(0xC0 | (code >> 6), 0x80 | (code & 0x3F));
      } else if (code >= 0xD800 && code <= 0xDBFF) {
        var hi = code;
        var lo = str.charCodeAt(++i);
        var codePoint = ((hi - 0xD800) << 10) + (lo - 0xDC00) + 0x10000;
        bytes.push(
          0xF0 | (codePoint >> 18),
          0x80 | ((codePoint >> 12) & 0x3F),
          0x80 | ((codePoint >> 6) & 0x3F),
          0x80 | (codePoint & 0x3F)
        );
      } else {
        bytes.push(0xE0 | (code >> 12), 0x80 | ((code >> 6) & 0x3F), 0x80 | (code & 0x3F));
      }
    }
    return new Uint8Array(bytes);
  };

  TextEncoder.prototype.encodeInto = function encodeInto(src, dst) {
    var encoded = this.encode(src);
    var len = Math.min(encoded.length, dst.length);
    for (var i = 0; i < len; i++) dst[i] = encoded[i];
    return { read: src.length, written: len };
  };

  function TextDecoder(encoding, options) {
    this.encoding = (encoding || 'utf-8').toLowerCase();
    this.fatal = (options && options.fatal) || false;
    this.ignoreBOM = (options && options.ignoreBOM) || false;
  }

  TextDecoder.prototype.decode = function decode(input, options) {
    var bytes = (input instanceof Uint8Array) ? input : new Uint8Array(input || new Uint8Array(0));
    if (useNativeDecode) {
      return pal.nativeDecodeUtf8(bytes);
    }
    var str = '';
    var i = 0;
    while (i < bytes.length) {
      var byte = bytes[i++];
      if (byte < 0x80) {
        str += String.fromCharCode(byte);
      } else if (byte < 0xE0) {
        str += String.fromCharCode(((byte & 0x1F) << 6) | (bytes[i++] & 0x3F));
      } else if (byte < 0xF0) {
        str += String.fromCharCode(
          ((byte & 0x0F) << 12) | ((bytes[i++] & 0x3F) << 6) | (bytes[i++] & 0x3F)
        );
      } else {
        var codePoint = ((byte & 0x07) << 18) | ((bytes[i++] & 0x3F) << 12) |
          ((bytes[i++] & 0x3F) << 6) | (bytes[i++] & 0x3F);
        str += String.fromCodePoint(codePoint);
      }
    }
    return str;
  };

  globalThis.TextEncoder = TextEncoder;
  globalThis.TextDecoder = TextDecoder;
}
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
    this._buffer = null;  /* accumulated incomplete multibyte bytes for stream:true */
  }

  /* Return the number of continuation bytes needed (0-3) for a lead byte, or
   * -1 if the lead byte is invalid. */
  function utf8LeadLen(byte) {
    if (byte < 0x80) return 0;
    if (byte < 0xC0) return -1;    /* continuation byte at wrong position */
    if (byte < 0xE0) return 2;     /* 2-byte sequence */
    if (byte < 0xF0) return 3;     /* 3-byte sequence */
    if (byte < 0xF8) return 4;     /* 4-byte sequence */
    return -1;
  }

  TextDecoder.prototype.decode = function decode(input, options) {
    var streamMode = options && options.stream;
    var bytes = (input instanceof Uint8Array) ? input : new Uint8Array(input || new Uint8Array(0));

    /* Native decode is fast but doesn't support stream mode (buffer across
     * calls) or proper U+FFFD replacement for incomplete/invalid sequences.
     * Fall through to JS for stream mode; use native only for one-shot decode. */
    if (false && useNativeDecode && !streamMode && !(this._buffer && this._buffer.length > 0)) {
      /* useNativeDecode is intentionally unused — JS path handles errors correctly */
      return pal.nativeDecodeUtf8(bytes);
    }

    /* Prepend any buffered incomplete bytes from a previous stream:true call */
    var allBytes;
    if (this._buffer && this._buffer.length > 0) {
      allBytes = new Uint8Array(this._buffer.length + bytes.length);
      allBytes.set(new Uint8Array(this._buffer), 0);
      allBytes.set(bytes, this._buffer.length);
      this._buffer = null;
    } else {
      allBytes = bytes;
    }

    var str = '';
    var i = 0;
    var lastComplete = 0;
    while (i < allBytes.length) {
      var lead = allBytes[i];
      var want = utf8LeadLen(lead);
      if (want < 0) {
        str += '�';
        i++;
        continue;
      }
      if (want === 0) {
        str += String.fromCharCode(lead);
        i++;
        continue;
      }
      /* Multi-byte: need want bytes total (lead + want-1 continuations). All
       * continuation bytes must be present and valid (0x80-0xBF). If the
       * sequence is incomplete or invalid, emit one U+FFFD for the lead byte
       * and continue parsing at the next byte that could be a lead byte. */
      if (i + want > allBytes.length) {
        /* Incomplete at end of input */
        if (streamMode) {
          var remaining = allBytes.length - i;
          this._buffer = [];
          for (var k = i; k < allBytes.length; k++) this._buffer.push(allBytes[k]);
          break;
        }
        /* Not stream mode: emit one U+FFFD for the lead byte. Skip any
         * remaining continuation bytes (they belong to this failed
         * sequence, not standalone) and continue parsing. */
        str += '�';
        i++;  /* skip lead */
        while (i < allBytes.length && (allBytes[i] & 0xC0) === 0x80) i++;
        continue;
      }
      /* Verify continuation bytes */
      var ok = true;
      for (var j = 1; j < want; j++) {
        if ((allBytes[i + j] & 0xC0) !== 0x80) { ok = false; break; }
      }
      if (!ok) {
        str += '�';
        i++;  /* skip lead */
        while (i < allBytes.length && (allBytes[i] & 0xC0) === 0x80) i++;
        continue;
      }
      /* Decode */
      if (want === 2) {
        str += String.fromCharCode(((lead & 0x1F) << 6) | (allBytes[i+1] & 0x3F));
      } else if (want === 3) {
        str += String.fromCharCode(
          ((lead & 0x0F) << 12) | ((allBytes[i+1] & 0x3F) << 6) | (allBytes[i+2] & 0x3F)
        );
      } else {
        var codepoint = ((lead & 0x07) << 18) | ((allBytes[i+1] & 0x3F) << 12) |
          ((allBytes[i+2] & 0x3F) << 6) | (allBytes[i+3] & 0x3F);
        str += String.fromCodePoint(codepoint);
      }
      i += want;
    }

    return str;
  };

  globalThis.TextEncoder = TextEncoder;
  globalThis.TextDecoder = TextDecoder;
}
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
    var label = (encoding || 'utf-8').toLowerCase();
    this.fatal = (options && options.fatal) || false;
    this.ignoreBOM = (options && options.ignoreBOM) || false;
    this._buffer = null;  /* accumulated incomplete multibyte bytes for stream:true */

    /* Resolve encoding label to canonical name per WHATWG Encoding Standard.
     * Only UTF-8 is always supported; Latin-1 and replacement require the
     * compile-time QWRT_WITH_NONUTF_ENCODINGS option. */
    if (label === 'unicode-1-1-utf-8' || label === 'utf-8' || label === 'utf8') {
      this.encoding = 'utf-8';
      this._decoder = 'utf8';
    } else if (label === 'replacement') {
        /* 'replacement' is a fatal-only encoding per WHATWG spec */
        throw new RangeError('The "replacement" label is not a valid encoding label');
      } else if (QWRT_WITH_NONUTF_ENCODINGS) {
      if (label === 'iso-8859-1' || label === 'iso_8859-1' || label === 'latin1' ||
          label === 'l1' || label === 'ibm819' || label === 'cp819' ||
          label === 'csisolatin1' || label === 'iso-ir-100' || label === 'windows-28591') {
        this.encoding = 'windows-1252';
        this._decoder = 'latin1';
      } else if (this.fatal) {
        throw new RangeError('Encoding "' + encoding + '" not supported');
      } else {
        /* Non-fatal: fall back to UTF-8 for unknown labels */
        this.encoding = 'utf-8';
        this._decoder = 'utf8';
      }
    } else {
      /* Non-UTF encodings not compiled in */
      if (this.fatal) {
        throw new RangeError('Encoding "' + encoding + '" not supported');
      }
      this.encoding = 'utf-8';
      this._decoder = 'utf8';
    }
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

    /* Non-UTF-8 decoders */
    if (this._decoder === 'latin1') {
      var str = '';
      for (var i = 0; i < bytes.length; i++) {
        str += String.fromCharCode(bytes[i] & 0xFF);
      }
      return str;
    }

    /* UTF-8 decode follows */
    /* Native decode is fast but doesn't support stream mode or proper error
     * handling. Always use the JS path for correctness. */
    if (false && useNativeDecode && !streamMode && !(this._buffer && this._buffer.length > 0)) {
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
      /* Lead-specific continuation range check (RFC 3629 / WHATWG Encoding).
       * Overlong sequences and out-of-range continuations are caught here.
       * Must run BEFORE the truncated check so overlong sequences produce
       * per-byte U+FFFD (not just one for the lead). */
      var ok = true;
      if (want >= 2 && i + 1 < allBytes.length) {
        var b1 = allBytes[i + 1];
        if ((lead === 0xE0 && b1 < 0xA0) ||
            (lead === 0xED && b1 > 0x9F) ||
            (lead === 0xF0 && b1 < 0x90) ||
            (lead === 0xF4 && b1 > 0x8F)) {
          ok = false;
        }
      }
      if (!ok) {
        str += '�';
        i++;  /* skip lead; continuation bytes caught by want<0 */
        continue;
      }
      /* Multi-byte: need want bytes total (lead + want-1 continuations). */
      if (i + want > allBytes.length) {
        /* Incomplete at end of input */
        if (streamMode) {
          /* Buffer the lead + any available continuation bytes (up to
           * want total). Remaining bytes (e.g. 'A' after [0xF0,0x9F])
           * must continue to be processed. */
          var bufWant = Math.min(want, allBytes.length - i);
          this._buffer = [];
          for (var k = i; k < i + bufWant; k++) this._buffer.push(allBytes[k]);
          i += bufWant;
          if (i >= allBytes.length) break;
          continue;
        }
        /* Not stream mode: emit one U+FFFD for the lead. If the
         * available bytes after this are all valid continuation bytes,
         * skip them (they belong to this failed lead). Otherwise each
         * gets its own U+FFFD (overlong/invalid case). */
        str += '�';
        i++;  /* skip lead */
        var availCont = 0;
        while (i + availCont < allBytes.length &&
               (allBytes[i + availCont] & 0xC0) === 0x80) {
          availCont++;
        }
        if (availCont > 0 && availCont <= want - 1) {
          i += availCont;  /* skip all valid continuation bytes */
        }
        continue;
      }
      /* Verify generic continuation byte format */
      var ok = true;
      for (var j = 1; j < want && i + j < allBytes.length; j++) {
        if ((allBytes[i + j] & 0xC0) !== 0x80) { ok = false; break; }
      }
      if (!ok) {
        str += '�';
        i++;  /* skip lead; continuation bytes caught by want<0 */
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
/**
 * qwrt polyfill: atob / btoa
 *
 * Base64 encoding/decoding functions.
 * JS query-table implementation with per-call delegation to the native
 * textcodec primitives when available.
 *
 * Implements the standard atob() and btoa() functions as defined in
 * the HTML Living Standard.
 */

export function setupEncoding(pal) {
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

    binaryString = String(binaryString);

    /* nativeBtoa 由 textcodec 扩展在 polyfill 注入之后注册（context.c 注入
     * vs ext init），one-shot typeof 探测在 setup 时恒为 false，必须每次
     * 调用探测（模式同 text-encoding.js:24 的 nativeEncodeUtf8）。
     * 本循环同时完成两件事：
     *   1. Latin1 范围校验（>0xFF 抛 InvalidCharacterError，规范语义）；
     *   2. asciiOnly 判定 —— C 版 nativeBtoa 经 JS_ToCStringLen 拿到的是
     *      UTF-8 字节，0x80-0xFF 码点被展开为 2 字节（é → C3 A9），与 btoa
     *      "每码点即一字节"的二进制串语义不符。仅纯 ASCII 输入下 UTF-8 展开
     *      为恒等映射、C 输出与 JS 查表等价，才允许委托；含高位字节走 JS。 */
    var asciiOnly = true;
    for (let i = 0; i < binaryString.length; i++) {
      const code = binaryString.charCodeAt(i);
      if (code > 255) {
        if (typeof DOMException === 'function') {
          throw new DOMException(
            "Failed to execute 'btoa': The string to be encoded contains characters outside of the Latin1 range.",
            'InvalidCharacterError');
        }
        throw new Error(
          "Failed to execute 'btoa': The string to be encoded contains characters outside of the Latin1 range."
        );
      }
      if (code > 0x7F) asciiOnly = false;
    }

    if (asciiOnly && typeof pal.nativeBtoa === 'function') {
      return pal.nativeBtoa(binaryString);
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

    base64String = String(base64String);
    base64String = base64String.replace(/\s/g, '');

    if (base64String.length % 4 !== 0) {
      throw new Error(
        "Failed to execute 'atob': The string to be decoded is not correctly encoded."
      );
    }
    /* 委托前空串短路：C 版 nativeAtob 对空输入走 js_malloc(ctx, 0)
     * （quickjs 零字节恒返回 NULL）→ 误报 InternalError: out of memory；
     * 规范语义为空输入 → 空输出。 */
    if (base64String.length === 0) {
      return '';
    }

    /* '=' 只能作为尾部 padding（最多两个、位置正确）；非法 base64 → 抛错。
     * 前置校验在委托前执行：C 版 nativeAtob 只做宽松校验（非法 padding
     * 组合如 "=A==" 不报错），严格语义以 JS 为准。 */
    const validChars = /^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/;
    if (!validChars.test(base64String)) {
      if (typeof DOMException === 'function') {
        throw new DOMException(
          "Failed to execute 'atob': The string to be decoded is not correctly encoded.",
          'InvalidCharacterError');
      }
      throw new Error(
        "Failed to execute 'atob': The string to be decoded is not correctly encoded."
      );
    }

    /* 同 btoa：textcodec 扩展注册晚于 polyfill 注入，需每次调用探测。 */
    if (typeof pal.nativeAtob === 'function') {
      return pal.nativeAtob(base64String);
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

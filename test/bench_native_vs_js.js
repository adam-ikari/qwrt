/**
 * Native vs JS Performance Benchmark (Direct PAL calls)
 *
 * Measures native C functions directly via pal.* (synchronous),
 * and pure JS fallback by calling the internal JS implementations.
 *
 * This avoids the crypto.subtle Promise wrapper overhead and
 * the TextEncoder closure caching issue.
 */

function now_ns() {
  if (typeof __pal__ !== 'undefined' && typeof __pal__.hrtime === 'function') {
    return __pal__.hrtime();
  }
  if (typeof performance !== 'undefined') {
    var t = performance.now();
    if (t > 0) return t * 1e6;
  }
  return Date.now() * 1e6;
}

function bench(label, fn, iters) {
  fn(); /* warmup */
  var t0 = now_ns();
  for (var i = 0; i < iters; i++) fn();
  var elapsed_ns = now_ns() - t0;
  var elapsed_ms = elapsed_ns / 1e6;
  var per_op_us = elapsed_ms * 1000 / iters;
  return { label: label, iters: iters, elapsed_ms: +elapsed_ms.toFixed(1), per_op_us: +per_op_us.toFixed(1) };
}

function printResult(r) {
  console.log('  ' + r.label.padEnd(55) + r.per_op_us.toFixed(1).padStart(8) + ' us/op  (' + r.elapsed_ms.toFixed(0) + ' ms)');
}

var _p = (typeof __pal__ !== 'undefined') ? __pal__ : null;

/* ================================================================
 * Test data
 * ================================================================ */

var shortStr = 'Hello, World!';
var longStr = '';
for (var i = 0; i < 10000; i++) longStr += 'The quick brown fox jumps over the lazy dog. ';
var shortBytes = new TextEncoder().encode(shortStr);
var longBytes = new TextEncoder().encode(longStr);

var key16 = new Uint8Array(16);
var key32 = new Uint8Array(32);
var iv16 = new Uint8Array(16);
for (var i = 0; i < 16; i++) key16[i] = i;
for (var i = 0; i < 32; i++) key32[i] = i;
for (var i = 0; i < 16; i++) iv16[i] = i;

var results = {};

/* ================================================================
 * SHA-256 Digest (direct native call vs JS implementation)
 * ================================================================ */

console.log('\n=== SHA-256 Digest ===');

/* NAT: call pal.nativeDigest directly (synchronous) */
var r_nat_short = bench('SHA-256 native (13 bytes)', function() {
  _p.nativeDigest('SHA-256', shortBytes);
}, 5000);
var r_nat_1k = bench('SHA-256 native (1KB)', function() {
  _p.nativeDigest('SHA-256', new Uint8Array(1024));
}, 2000);
var r_nat_long = bench('SHA-256 native (440KB)', function() {
  _p.nativeDigest('SHA-256', longBytes);
}, 200);

printResult(r_nat_short);
printResult(r_nat_1k);
printResult(r_nat_long);

/* JS: call crypto.subtle.digest via JS fallback (disable native temporarily) */
var origDigest = _p.nativeDigest;
_p.nativeDigest = undefined;

/* For digest, crypto.subtle.digest is synchronous internally — the Promise
 * wraps a synchronous computation. We can measure by forcing microtask resolution. */

/* Use a direct measurement: compute the hash synchronously via JS path */
/* crypto.subtle.digest returns a Promise, but the computation happens synchronously
 * inside the Promise constructor. Let's measure via direct call. */

var r_js_short = bench('SHA-256 JS  (13 bytes)', function() {
  crypto.subtle.digest('SHA-256', shortBytes);
}, 200);
var r_js_long = bench('SHA-256 JS  (440KB)', function() {
  crypto.subtle.digest('SHA-256', longBytes);
}, 5);

_p.nativeDigest = origDigest;

printResult(r_js_short);
printResult(r_js_long);

results.sha256_short = { js: r_js_short.per_op_us, native: r_nat_short.per_op_us, speedup: +(r_js_short.per_op_us / r_nat_short.per_op_us).toFixed(1) };
results.sha256_1k = { js: null, native: r_nat_1k.per_op_us };
results.sha256_long = { js: r_js_long.per_op_us, native: r_nat_long.per_op_us, speedup: +(r_js_long.per_op_us / r_nat_long.per_op_us).toFixed(1) };

console.log('  Speedup (short): ' + results.sha256_short.speedup + 'x');
console.log('  Speedup (long):  ' + results.sha256_long.speedup + 'x');

/* ================================================================
 * HMAC-SHA256 (direct native call)
 * ================================================================ */

console.log('\n=== HMAC-SHA256 ===');

var r_nat_hmac_short = bench('HMAC-SHA256 native (13 bytes)', function() {
  _p.nativeHmac('SHA-256', key32, shortBytes);
}, 5000);
var r_nat_hmac_long = bench('HMAC-SHA256 native (440KB)', function() {
  _p.nativeHmac('SHA-256', key32, longBytes);
}, 100);

printResult(r_nat_hmac_short);
printResult(r_nat_hmac_long);

/* JS HMAC: disable native and use crypto.subtle */
var origHmac = _p.nativeHmac;
_p.nativeHmac = undefined;

var r_js_hmac_long = bench('HMAC-SHA256 JS  (440KB)', function() {
  crypto.subtle.sign({ name: 'HMAC', hash: 'SHA-256' }, { _data: key32, type: 'secret', algorithm: { name: 'HMAC', hash: 'SHA-256' } }, longBytes);
}, 5);

_p.nativeHmac = origHmac;

printResult(r_js_hmac_long);

results.hmac_short = { native: r_nat_hmac_short.per_op_us };
results.hmac_long = { js: r_js_hmac_long.per_op_us, native: r_nat_hmac_long.per_op_us, speedup: +(r_js_hmac_long.per_op_us / r_nat_hmac_long.per_op_us).toFixed(1) };
console.log('  Speedup (long): ' + results.hmac_long.speedup + 'x');

/* ================================================================
 * AES-CBC (direct native call)
 * ================================================================ */

console.log('\n=== AES-CBC ===');

var r_nat_aes_enc = bench('AES-CBC encrypt native (440KB)', function() {
  _p.nativeAesEncrypt(longBytes, key16, iv16, 'AES-CBC');
}, 50);
var encrypted = _p.nativeAesEncrypt(longBytes, key16, iv16, 'AES-CBC');
var r_nat_aes_dec = bench('AES-CBC decrypt native (440KB)', function() {
  _p.nativeAesDecrypt(encrypted, key16, iv16, 'AES-CBC');
}, 50);

printResult(r_nat_aes_enc);
printResult(r_nat_aes_dec);

/* JS AES-CBC */
var origAesEnc = _p.nativeAesEncrypt;
_p.nativeAesEncrypt = undefined;

var r_js_aes_enc = bench('AES-CBC encrypt JS  (440KB)', function() {
  crypto.subtle.encrypt({ name: 'AES-CBC', iv: iv16 }, { _data: key16, type: 'secret', algorithm: { name: 'AES-CBC' } }, longBytes);
}, 3);

_p.nativeAesEncrypt = origAesEnc;

printResult(r_js_aes_enc);

results.aes_cbc_enc = { js: r_js_aes_enc.per_op_us, native: r_nat_aes_enc.per_op_us, speedup: +(r_js_aes_enc.per_op_us / r_nat_aes_enc.per_op_us).toFixed(1) };
console.log('  Speedup: ' + results.aes_cbc_enc.speedup + 'x');

/* ================================================================
 * PBKDF2 (direct native call)
 * ================================================================ */

console.log('\n=== PBKDF2 (1000 iterations, SHA-256) ===');

var salt16 = new Uint8Array(16);
for (var i = 0; i < 16; i++) salt16[i] = i;

var r_nat_pbkdf2 = bench('PBKDF2 native (1000 iter, 32 bytes)', function() {
  _p.nativePbkdf2(key16, salt16, 1000, 'SHA-256', 32);
}, 200);
var r_nat_pbkdf2_10k = bench('PBKDF2 native (10000 iter, 32 bytes)', function() {
  _p.nativePbkdf2(key16, salt16, 10000, 'SHA-256', 32);
}, 20);

printResult(r_nat_pbkdf2);
printResult(r_nat_pbkdf2_10k);

/* JS PBKDF2 */
var origPbkdf2 = _p.nativePbkdf2;
_p.nativePbkdf2 = undefined;

var r_js_pbkdf2 = bench('PBKDF2 JS  (1000 iter, 32 bytes)', function() {
  crypto.subtle.deriveBits({ name: 'PBKDF2', salt: salt16, iterations: 1000, hash: 'SHA-256' }, { _data: key16, type: 'secret', algorithm: { name: 'PBKDF2' } }, 256);
}, 5);

_p.nativePbkdf2 = origPbkdf2;

printResult(r_js_pbkdf2);

results.pbkdf2 = { js: r_js_pbkdf2.per_op_us, native: r_nat_pbkdf2.per_op_us, speedup: +(r_js_pbkdf2.per_op_us / r_nat_pbkdf2.per_op_us).toFixed(1) };
console.log('  Speedup: ' + results.pbkdf2.speedup + 'x');

/* ================================================================
 * TextEncoder (direct native call)
 * ================================================================ */

console.log('\n=== TextEncoder ===');

if (_p.nativeEncodeUtf8) {
  var r_nat_enc_short = bench('TextEncoder native (13 bytes)', function() {
    _p.nativeEncodeUtf8(shortStr);
  }, 50000);
  var r_nat_enc_long = bench('TextEncoder native (440KB)', function() {
    _p.nativeEncodeUtf8(longStr);
  }, 50);

  printResult(r_nat_enc_short);
  printResult(r_nat_enc_long);
}

/* JS TextEncoder fallback: manual UTF-8 encode */
var r_js_enc_short = bench('TextEncoder JS fallback (13 bytes)', function() {
  var str = shortStr;
  var bytes = [];
  for (var i = 0; i < str.length; i++) {
    var code = str.charCodeAt(i);
    if (code < 0x80) bytes.push(code);
    else if (code < 0x800) bytes.push(0xC0 | (code >> 6), 0x80 | (code & 0x3F));
    else bytes.push(0xE0 | (code >> 12), 0x80 | ((code >> 6) & 0x3F), 0x80 | (code & 0x3F));
  }
  new Uint8Array(bytes);
}, 50000);
var r_js_enc_long = bench('TextEncoder JS fallback (440KB)', function() {
  var str = longStr;
  var bytes = [];
  for (var i = 0; i < str.length; i++) {
    var code = str.charCodeAt(i);
    if (code < 0x80) bytes.push(code);
    else if (code < 0x800) bytes.push(0xC0 | (code >> 6), 0x80 | (code & 0x3F));
    else bytes.push(0xE0 | (code >> 12), 0x80 | ((code >> 6) & 0x3F), 0x80 | (code & 0x3F));
  }
  new Uint8Array(bytes);
}, 10);

printResult(r_js_enc_short);
printResult(r_js_enc_long);

if (_p.nativeEncodeUtf8) {
  results.textencoder_short = { js: r_js_enc_short.per_op_us, native: r_nat_enc_short.per_op_us, speedup: +(r_js_enc_short.per_op_us / r_nat_enc_short.per_op_us).toFixed(1) };
  results.textencoder_long = { js: r_js_enc_long.per_op_us, native: r_nat_enc_long.per_op_us, speedup: +(r_js_enc_long.per_op_us / r_nat_enc_long.per_op_us).toFixed(1) };
  console.log('  Speedup (short): ' + results.textencoder_short.speedup + 'x');
  console.log('  Speedup (long):  ' + results.textencoder_long.speedup + 'x');
}

/* ================================================================
 * TextDecoder (direct native call)
 * ================================================================ */

console.log('\n=== TextDecoder ===');

if (_p.nativeDecodeUtf8) {
  var r_nat_dec_short = bench('TextDecoder native (13 bytes)', function() {
    _p.nativeDecodeUtf8(shortBytes);
  }, 50000);
  var r_nat_dec_long = bench('TextDecoder native (440KB)', function() {
    _p.nativeDecodeUtf8(longBytes);
  }, 50);

  printResult(r_nat_dec_short);
  printResult(r_nat_dec_long);
}

/* JS TextDecoder fallback */
var r_js_dec_short = bench('TextDecoder JS fallback (13 bytes)', function() {
  var bytes = shortBytes;
  var str = '';
  var i = 0;
  while (i < bytes.length) {
    var byte = bytes[i++];
    if (byte < 0x80) str += String.fromCharCode(byte);
    else if (byte < 0xE0) str += String.fromCharCode(((byte & 0x1F) << 6) | (bytes[i++] & 0x3F));
    else str += String.fromCharCode(((byte & 0x0F) << 12) | ((bytes[i++] & 0x3F) << 6) | (bytes[i++] & 0x3F));
  }
}, 50000);
var r_js_dec_long = bench('TextDecoder JS fallback (440KB)', function() {
  var bytes = longBytes;
  var str = '';
  var i = 0;
  while (i < bytes.length) {
    var byte = bytes[i++];
    if (byte < 0x80) str += String.fromCharCode(byte);
    else if (byte < 0xE0) str += String.fromCharCode(((byte & 0x1F) << 6) | (bytes[i++] & 0x3F));
    else str += String.fromCharCode(((byte & 0x0F) << 12) | ((bytes[i++] & 0x3F) << 6) | (bytes[i++] & 0x3F));
  }
}, 10);

printResult(r_js_dec_short);
printResult(r_js_dec_long);

if (_p.nativeDecodeUtf8) {
  results.textdecoder_short = { js: r_js_dec_short.per_op_us, native: r_nat_dec_short.per_op_us, speedup: +(r_js_dec_short.per_op_us / r_nat_dec_short.per_op_us).toFixed(1) };
  results.textdecoder_long = { js: r_js_dec_long.per_op_us, native: r_nat_dec_long.per_op_us, speedup: +(r_js_dec_long.per_op_us / r_nat_dec_long.per_op_us).toFixed(1) };
  console.log('  Speedup (short): ' + results.textdecoder_short.speedup + 'x');
  console.log('  Speedup (long):  ' + results.textdecoder_long.speedup + 'x');
}

/* ================================================================
 * btoa / atob (direct native call)
 * ================================================================ */

console.log('\n=== btoa / atob ===');

var btoaInput = '';
for (var i = 0; i < 3000; i++) btoaInput += 'Hello World! ';

if (_p.nativeBtoa) {
  var r_nat_btoa = bench('btoa native (39KB)', function() {
    _p.nativeBtoa(btoaInput);
  }, 2000);
  var btoaOutput = _p.nativeBtoa(btoaInput);
  var r_nat_atob = bench('atob native (52KB)', function() {
    _p.nativeAtob(btoaOutput);
  }, 2000);

  printResult(r_nat_btoa);
  printResult(r_nat_atob);
}

/* JS btoa/atob fallback */
var B64_CHARS = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';

var r_js_btoa = bench('btoa JS fallback (39KB)', function() {
  var s = btoaInput;
  var result = '';
  for (var i = 0; i < s.length; i++) {
    var code = s.charCodeAt(i);
    if (code > 255) throw new Error('btoa: char out of range');
  }
  var i = 0;
  while (i < s.length) {
    var byteCount = 0;
    var a = s.charCodeAt(i++); byteCount++;
    var b = i < s.length ? (byteCount++, s.charCodeAt(i++)) : 0;
    var c = i < s.length ? (byteCount++, s.charCodeAt(i++)) : 0;
    var triplet = (a << 16) | (b << 8) | c;
    result += B64_CHARS[(triplet >> 18) & 0x3F];
    result += B64_CHARS[(triplet >> 12) & 0x3F];
    result += byteCount >= 2 ? B64_CHARS[(triplet >> 6) & 0x3F] : '=';
    result += byteCount >= 3 ? B64_CHARS[triplet & 0x3F] : '=';
  }
}, 100);

var btoaOutputJS = btoa(btoaInput);

var r_js_atob = bench('atob JS fallback (52KB)', function() {
  var s = btoaOutputJS;
  s = s.replace(/\s/g, '');
  var B64_DECODE = {};
  for (var i = 0; i < B64_CHARS.length; i++) B64_DECODE[B64_CHARS[i]] = i;
  B64_DECODE['='] = 0;
  var result = '';
  var i = 0;
  while (i < s.length) {
    var a = B64_DECODE[s[i++]];
    var b = B64_DECODE[s[i++]];
    var c = B64_DECODE[s[i++]];
    var d = B64_DECODE[s[i++]];
    var triplet = (a << 18) | (b << 12) | (c << 6) | d;
    result += String.fromCharCode((triplet >> 16) & 0xFF);
    if (s[i-2] !== '=') result += String.fromCharCode((triplet >> 8) & 0xFF);
    if (s[i-1] !== '=') result += String.fromCharCode(triplet & 0xFF);
  }
}, 100);

printResult(r_js_btoa);
printResult(r_js_atob);

if (_p.nativeBtoa) {
  results.btoa = { js: r_js_btoa.per_op_us, native: r_nat_btoa.per_op_us, speedup: +(r_js_btoa.per_op_us / r_nat_btoa.per_op_us).toFixed(1) };
  results.atob = { js: r_js_atob.per_op_us, native: r_nat_atob.per_op_us, speedup: +(r_js_atob.per_op_us / r_nat_atob.per_op_us).toFixed(1) };
  console.log('  btoa Speedup: ' + results.btoa.speedup + 'x');
  console.log('  atob Speedup: ' + results.atob.speedup + 'x');
}

/* ================================================================
 * Summary
 * ================================================================ */

console.log('\n========================================');
console.log('  Native Extension Speedup Summary');
console.log('========================================');

var allKeys = Object.keys(results);
for (var i = 0; i < allKeys.length; i++) {
  var k = allKeys[i];
  var r = results[k];
  if (r.speedup) {
    console.log('  ' + k.padEnd(25) + r.speedup.toFixed(1).padStart(6) + 'x  (JS ' + r.js.toFixed(0) + ' us → NAT ' + r.native.toFixed(0) + ' us)');
  } else {
    console.log('  ' + k.padEnd(25) + '  NAT: ' + r.native.toFixed(0) + ' us/op');
  }
}

console.log('\n=== JSON ===');
console.log(JSON.stringify(results));
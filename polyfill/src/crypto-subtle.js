/**
 * qwrt polyfill: crypto.subtle
 *
 * TC55/ECMA-429 requires crypto.subtle with at minimum:
 *   - digest (SHA-1, SHA-256, SHA-384, SHA-512)
 *   - importKey / sign / verify (HMAC)
 *   - importKey (for PBKDF2)
 *
 * Supported beyond the minimum:
 *   - HMAC generateKey (with algorithm.length in bits)
 *   - AES-CBC / AES-GCM / AES-CTR encrypt/decrypt/generateKey
 *   - AES-KW wrap/unwrap (RFC 3394)
 *   - PBKDF2 deriveBits/deriveKey
 *   - HKDF deriveBits/deriveKey (SHA-1..SHA-512)
 *   - ECDSA P-256/P-384/P-521 generateKey/importKey/exportKey(jwk)/sign/verify
 *   - ECDH deriveBits/deriveKey (P-256/P-384/P-521)
 *
 * All operations delegate to native C functions via pal.nativeDigest,
 * pal.nativeHmac, pal.nativeAesEncrypt, pal.nativeAesDecrypt,
 * pal.nativePbkdf2, pal.nativeHkdf, pal.nativeAesKwWrap/Unwrap,
 * pal.nativeEcGenerate, pal.nativeEcdh, pal.nativeEcdsaSign/Verify.
 * These are registered by the crypto extension (ext_crypto.c, gated by
 * QWRT_WITH_CRYPTO_EXT).
 */

export function setupCryptoSubtle(pal) {
  /* Expose the installer on the pal object. The crypto extension's init hook
   * (ext_crypto.c) calls pal.__installCryptoSubtle__() after registering its
   * native hooks. If the extension is absent, the installer is never called
   * and crypto.subtle stays undefined. */
  pal.__installCryptoSubtle__ = function() {
    installCryptoSubtle(pal);
  };
}

/* Build and attach the SubtleCrypto + CryptoKey to globalThis.crypto. Called
 * lazily by the extension's init hook (via pal.__installCryptoSubtle__) so it
 * only runs when the native crypto hooks are present. */
function installCryptoSubtle(pal) {

  // ================================================================
  // Helper functions
  // ================================================================

  function toUint8Array(data) {
    if (data instanceof Uint8Array) return data;
    if (data instanceof ArrayBuffer) return new Uint8Array(data);
    if (ArrayBuffer.isView(data)) return new Uint8Array(data.buffer, data.byteOffset, data.byteLength);
    throw new TypeError('Expected ArrayBuffer or TypedArray');
  }

  function toArrayBuffer(u8) {
    return u8.buffer.slice(u8.byteOffset, u8.byteOffset + u8.byteLength);
  }

  var B64_CHARS = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';

  function base64UrlEncode(bytes) {
    var str = '';
    for (var i = 0; i < bytes.length; i += 3) {
      var b0 = bytes[i], b1 = i + 1 < bytes.length ? bytes[i + 1] : 0, b2 = i + 2 < bytes.length ? bytes[i + 2] : 0;
      str += B64_CHARS[b0 >> 2];
      str += B64_CHARS[((b0 & 3) << 4) | (b1 >> 4)];
      str += i + 1 < bytes.length ? B64_CHARS[((b1 & 15) << 2) | (b2 >> 6)] : '=';
      str += i + 2 < bytes.length ? B64_CHARS[b2 & 63] : '=';
    }
    return str.replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
  }

  function base64UrlDecode(str) {
    str = str.replace(/-/g, '+').replace(/_/g, '/');
    while (str.length % 4) str += '=';
    var chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
    var bytes = [];
    for (var i = 0; i < str.length; i += 4) {
      var c0 = chars.indexOf(str[i]), c1 = chars.indexOf(str[i+1]);
      var c2 = chars.indexOf(str[i+2]), c3 = chars.indexOf(str[i+3]);
      bytes.push((c0 << 2) | (c1 >> 4));
      if (c2 !== -1 && str[i+2] !== '=') bytes.push(((c1 & 15) << 4) | (c2 >> 2));
      if (c3 !== -1 && str[i+3] !== '=') bytes.push(((c2 & 3) << 6) | c3);
    }
    return new Uint8Array(bytes);
  }

  // ================================================================
  // CryptoKey
  // ================================================================

  class CryptoKey {
    constructor(type, algorithm, extractable, usages, data) {
      this._type = type;
      this._algorithm = algorithm;
      this._extractable = extractable;
      this._usages = usages;
      this._data = data;
    }

    get type() { return this._type; }
    get algorithm() { return this._algorithm; }
    get extractable() { return this._extractable; }
    get usages() { return this._usages; }
  }

  // ================================================================
  // SubtleCrypto
  // ================================================================

  class SubtleCrypto {
    constructor() {}

    digest(algorithm, data) {
      return new Promise(function(resolve, reject) {
        var name = typeof algorithm === 'string' ? algorithm : algorithm.name;

        if (typeof pal.nativeDigest !== 'function') {
          reject(new DOMException('Crypto extension not available', 'NotSupportedError'));
          return;
        }

        try {
          var result = pal.nativeDigest(name, toUint8Array(data));
          resolve(toArrayBuffer(result));
        } catch (e) {
          reject(e);
        }
      });
    }
    importKey(format, keyData, algorithm, extractable, keyUsages) {
      return new Promise(function(resolve, reject) {
        var algoName = typeof algorithm === 'string' ? algorithm : algorithm.name;
        var data;

        if (algoName === 'ECDSA' || algoName === 'ECDH') {
          var curve = typeof algorithm === 'object' && algorithm ? algorithm.namedCurve : undefined;
          if (!curve) { reject(new TypeError('namedCurve required')); return; }
          var coordLen = curve === 'P-256' ? 32 : (curve === 'P-384' ? 48 : 66);

          if (format === 'raw') {
            /* public key: uncompressed point 0x04 || x || y */
            var raw = toUint8Array(keyData);
            if (raw.length !== coordLen * 2 + 1 || raw[0] !== 4) {
              reject(new DOMException('Invalid EC public key', 'DataError'));
              return;
            }
            resolve(new CryptoKey('public', { name: algoName, namedCurve: curve }, extractable, keyUsages, raw));
            return;
          }

          if (format === 'jwk') {
            if (!keyData || keyData.crv !== curve) {
              reject(new DOMException('Invalid JWK key data', 'DataError'));
              return;
            }
            if (keyData.k) {
              /* private key: d, zero-padded to coordLen */
              var d = base64UrlDecode(keyData.k);
              var dd = new Uint8Array(coordLen);
              dd.set(d.length > coordLen ? d.subarray(0, coordLen) : d, coordLen - d.length);
              resolve(new CryptoKey('private', { name: algoName, namedCurve: curve }, extractable, keyUsages, dd));
              return;
            }
            if (keyData.x && keyData.y) {
              var x = base64UrlDecode(keyData.x);
              var y = base64UrlDecode(keyData.y);
              var pub = new Uint8Array(coordLen * 2 + 1);
              pub[0] = 4;
              pub.set(x.length > coordLen ? x.subarray(0, coordLen) : x, 1 + coordLen - x.length);
              pub.set(y.length > coordLen ? y.subarray(0, coordLen) : y, 1 + 2 * coordLen - y.length);
              resolve(new CryptoKey('public', { name: algoName, namedCurve: curve }, extractable, keyUsages, pub));
              return;
            }
            reject(new DOMException('Invalid JWK EC key', 'DataError'));
            return;
          }

          reject(new DOMException('Unsupported key format: ' + format, 'NotSupportedError'));
          return;
        }

        if (format === 'raw') {
          if (keyData instanceof ArrayBuffer) {
            data = new Uint8Array(keyData);
          } else if (ArrayBuffer.isView(keyData)) {
            data = new Uint8Array(keyData.buffer, keyData.byteOffset, keyData.byteLength);
          } else {
            reject(new TypeError('Invalid keyData'));
            return;
          }
        } else if (format === 'jwk') {
          if (!keyData || !keyData.k) {
            reject(new TypeError('Invalid JWK key data'));
            return;
          }
          data = base64UrlDecode(keyData.k);
        } else {
          reject(new DOMException('Unsupported key format: ' + format, 'NotSupportedError'));
          return;
        }

        resolve(new CryptoKey(
          'secret',
          { name: algoName },
          extractable,
          keyUsages,
          data
        ));
      });
    }
    sign(algorithm, key, data) {
      return new Promise(function(resolve, reject) {
        var algoName = typeof algorithm === 'string' ? algorithm : algorithm.name;

        if (algoName === 'ECDSA') {
          var hashAlgo = algorithm.hash ? (typeof algorithm.hash === 'string' ? algorithm.hash : algorithm.hash.name) : undefined;
          if (!hashAlgo) { reject(new TypeError('hash required for ECDSA sign')); return; }
          if (key.type !== 'private' || typeof pal.nativeEcdsaSign !== 'function') {
            reject(new DOMException('Crypto extension not available or wrong key type', 'NotSupportedError'));
            return;
          }
          try {
            var sig = pal.nativeEcdsaSign(hashAlgo, key.algorithm.namedCurve, key._data, toUint8Array(data));
            resolve(toArrayBuffer(sig));
          } catch (e) { reject(e); }
          return;
        }

        if (algoName === 'HMAC') {
          var hashAlgo = algorithm.hash ? (typeof algorithm.hash === 'string' ? algorithm.hash : algorithm.hash.name) : 'SHA-256';

          if (typeof pal.nativeHmac !== 'function') {
            reject(new DOMException('Crypto extension not available', 'NotSupportedError'));
            return;
          }

          try {
            var result = pal.nativeHmac(hashAlgo, key._data, toUint8Array(data));
            resolve(toArrayBuffer(result));
          } catch (e) {
            reject(e);
          }
          return;
        }

        reject(new DOMException('Unsupported algorithm: ' + algoName, 'NotSupportedError'));
      });
    }

    verify(algorithm, key, signature, data) {
      return new Promise(function(resolve, reject) {
        var algoName = typeof algorithm === 'string' ? algorithm : algorithm.name;

        if (algoName === 'ECDSA') {
          var hashAlgo = algorithm.hash ? (typeof algorithm.hash === 'string' ? algorithm.hash : algorithm.hash.name) : undefined;
          if (!hashAlgo) { reject(new TypeError('hash required for ECDSA verify')); return; }
          if (key.type !== 'public' || typeof pal.nativeEcdsaVerify !== 'function') {
            reject(new DOMException('Crypto extension not available or wrong key type', 'NotSupportedError'));
            return;
          }
          try {
            resolve(pal.nativeEcdsaVerify(hashAlgo, key.algorithm.namedCurve, key._data,
                                          toUint8Array(signature), toUint8Array(data)));
          } catch (e) { reject(e); }
          return;
        }

        if (algoName === 'HMAC') {
          this.sign(algorithm, key, data).then(function(computed) {
            var sig = toUint8Array(signature);
            var comp = new Uint8Array(computed);
            if (sig.length !== comp.length) { resolve(false); return; }
            var diff = 0;
            for (var i = 0; i < sig.length; i++) {
              diff |= sig[i] ^ comp[i];
            }
            resolve(diff === 0);
          }, reject);
          return;
        }

        reject(new DOMException('Unsupported algorithm: ' + algoName, 'NotSupportedError'));
      }.bind(this));
    }

    encrypt(algorithm, key, data) {
      return new Promise(function(resolve, reject) {
        var algoName = typeof algorithm === 'string' ? algorithm : algorithm.name;
        var plaintext = toUint8Array(data);

        if (typeof pal.nativeAesEncrypt !== 'function') {
          reject(new DOMException('Crypto extension not available', 'NotSupportedError'));
          return;
        }

        try {
          if (algoName === 'AES-CBC') {
            var iv = toUint8Array(algorithm.iv);
            var result = pal.nativeAesEncrypt(plaintext, key._data, iv, 'AES-CBC');
            resolve(toArrayBuffer(result));
            return;
          }
          if (algoName === 'AES-GCM') {
            var iv = toUint8Array(algorithm.iv);
            var aad = algorithm.additionalData ? toUint8Array(algorithm.additionalData) : undefined;
            var tagLen = algorithm.tagLength !== undefined ? algorithm.tagLength / 8 : 16;
            var result = pal.nativeAesEncrypt(plaintext, key._data, iv, 'AES-GCM', aad, tagLen);
            resolve(toArrayBuffer(result));
            return;
          }
          if (algoName === 'AES-CTR') {
            var counter = toUint8Array(algorithm.counter);
            var result = pal.nativeAesEncrypt(plaintext, key._data, counter, 'AES-CTR');
            resolve(toArrayBuffer(result));
            return;
          }
        } catch (e) {
          reject(e);
          return;
        }

        reject(new DOMException('Unsupported algorithm: ' + algoName, 'NotSupportedError'));
      });
    }

    decrypt(algorithm, key, data) {
      return new Promise(function(resolve, reject) {
        var algoName = typeof algorithm === 'string' ? algorithm : algorithm.name;
        var ciphertext = toUint8Array(data);

        if (typeof pal.nativeAesDecrypt !== 'function') {
          reject(new DOMException('Crypto extension not available', 'NotSupportedError'));
          return;
        }

        try {
          if (algoName === 'AES-CBC') {
            var iv = toUint8Array(algorithm.iv);
            var result = pal.nativeAesDecrypt(ciphertext, key._data, iv, 'AES-CBC');
            resolve(toArrayBuffer(result));
            return;
          }
          if (algoName === 'AES-GCM') {
            var iv = toUint8Array(algorithm.iv);
            var aad = algorithm.additionalData ? toUint8Array(algorithm.additionalData) : undefined;
            var tagLen = algorithm.tagLength !== undefined ? algorithm.tagLength / 8 : 16;
            var result = pal.nativeAesDecrypt(ciphertext, key._data, iv, 'AES-GCM', aad, tagLen);
            resolve(toArrayBuffer(result));
            return;
          }
          if (algoName === 'AES-CTR') {
            var counter = toUint8Array(algorithm.counter);
            var result = pal.nativeAesDecrypt(ciphertext, key._data, counter, 'AES-CTR');
            resolve(toArrayBuffer(result));
            return;
          }
        } catch (e) {
          reject(e);
          return;
        }

        reject(new DOMException('Unsupported algorithm: ' + algoName, 'NotSupportedError'));
      });
    }

    generateKey(algorithm, extractable, keyUsages) {
      return new Promise(function(resolve, reject) {
        var algoName = typeof algorithm === 'string' ? algorithm : algorithm.name;

        if (algoName === 'HMAC') {
          var hashAlgo = algorithm.hash ? (typeof algorithm.hash === 'string' ? algorithm.hash : algorithm.hash.name) : 'SHA-256';
          /* WebCrypto: algorithm.length is in BITS (HMAC key length). Default to
           * the hash output length in bytes when omitted (32 for SHA-256). Divide
           * by 8 to get bytes; clamp to >=1. */
          var lengthBits = algorithm.length !== undefined ? algorithm.length : 0;
          var lengthBytes;
          if (lengthBits > 0) {
            lengthBytes = Math.ceil(lengthBits / 8);
          } else {
            lengthBytes = hashAlgo === 'SHA-1' ? 20 : (hashAlgo === 'SHA-512' ? 64 : 32);
          }
          var keyBytes = new Uint8Array(lengthBytes);
          crypto.getRandomValues(keyBytes);
          resolve(new CryptoKey('secret', { name: 'HMAC', hash: hashAlgo }, extractable, keyUsages, keyBytes));
          return;
        }

        if (algoName === 'ECDSA' || algoName === 'ECDH') {
          var curve = algorithm.namedCurve;
          if (curve !== 'P-256' && curve !== 'P-384' && curve !== 'P-521') {
            reject(new DOMException('Unsupported namedCurve', 'NotSupportedError'));
            return;
          }
          if (typeof pal.nativeEcGenerate !== 'function') {
            reject(new DOMException('Crypto extension not available', 'NotSupportedError'));
            return;
          }
          try {
            var packed = new Uint8Array(pal.nativeEcGenerate(curve));
            var privLen = (packed[0] << 24) | (packed[1] << 16) | (packed[2] << 8) | packed[3];
            var priv = packed.slice(4, 4 + privLen);
            var pub = packed.slice(4 + privLen);
            var usages = keyUsages.filter(function(u) {
              return algoName === 'ECDSA' ? (u === 'sign' || u === 'verify') : (u === 'deriveKey' || u === 'deriveBits');
            });
            var pubKey = new CryptoKey('public', { name: algoName, namedCurve: curve }, extractable, usages.filter(function(u){ return u === 'verify' || u === 'deriveKey' || u === 'deriveBits'; }), pub);
            var privKey = new CryptoKey('private', { name: algoName, namedCurve: curve }, extractable, usages.filter(function(u){ return u !== 'verify'; }), priv);
            privKey._pub = pub;
            resolve({ publicKey: pubKey, privateKey: privKey });
          } catch (e) { reject(e); }
          return;
        }

        if (algoName === 'AES-CBC' || algoName === 'AES-GCM' || algoName === 'AES-CTR' || algoName === 'AES-KW') {
          var length = algorithm.length || 128;
          if (length !== 128 && length !== 192 && length !== 256) {
            reject(new DOMException('Invalid AES key length', 'OperationError'));
            return;
          }
          var keyBytes = new Uint8Array(length / 8);
          crypto.getRandomValues(keyBytes);
          resolve(new CryptoKey('secret', { name: algoName, length: length }, extractable, keyUsages, keyBytes));
          return;
        }

        reject(new DOMException('Unsupported algorithm: ' + algoName, 'NotSupportedError'));
      });
    }

    exportKey(format, key) {
      return new Promise(function(resolve, reject) {
        if (!key.extractable) {
          reject(new DOMException('Key is not extractable', 'InvalidAccessError'));
          return;
        }

        var algoName = key.algorithm.name;

        if (algoName === 'ECDSA' || algoName === 'ECDH') {
          var curve = key.algorithm.namedCurve;
          var coordLen = curve === 'P-256' ? 32 : (curve === 'P-384' ? 48 : 66);
          if (format === 'raw') {
            if (key.type !== 'public') {
              reject(new DOMException('raw export requires a public key', 'NotSupportedError'));
              return;
            }
            resolve(toArrayBuffer(key._data));
            return;
          }
          if (format === 'jwk') {
            var jwk = { kty: 'EC', crv: curve, ext: true, key_ops: key.usages };
            if (key.type === 'private') {
              jwk.d = base64UrlEncode(key._data);
              var pub = key._pub || new Uint8Array(0);
              if (pub.length === coordLen * 2 + 1) {
                jwk.x = base64UrlEncode(pub.subarray(1, 1 + coordLen));
                jwk.y = base64UrlEncode(pub.subarray(1 + coordLen));
              }
            } else {
              if (key._data.length !== coordLen * 2 + 1) {
                reject(new DOMException('Invalid public key', 'DataError'));
                return;
              }
              jwk.x = base64UrlEncode(key._data.subarray(1, 1 + coordLen));
              jwk.y = base64UrlEncode(key._data.subarray(1 + coordLen));
            }
            resolve(jwk);
            return;
          }
          reject(new DOMException('Unsupported export format: ' + format, 'NotSupportedError'));
          return;
        }

        if (format === 'raw') {
          resolve(toArrayBuffer(key._data));
          return;
        }

        if (format === 'jwk') {
          var jwk = {
            kty: 'oct',
            k: base64UrlEncode(key._data),
            alg: algoName === 'HMAC' ? 'HS' + (key.algorithm.hash ? key.algorithm.hash.replace('SHA-', '') : '256') : algoName,
            ext: true,
            key_ops: key.usages,
          };
          resolve(jwk);
          return;
        }

        reject(new DOMException('Unsupported export format: ' + format, 'NotSupportedError'));
      });
    }


    wrapKey(format, key, wrappingKey, wrapAlgorithm) {
      return new Promise(function(resolve, reject) {
        var wrapName = typeof wrapAlgorithm === 'string' ? wrapAlgorithm : wrapAlgorithm.name;

        if (wrapName === 'AES-KW') {
          if (format !== 'raw') {
            reject(new DOMException('AES-KW supports raw format only', 'NotSupportedError'));
            return;
          }
          if (typeof pal.nativeAesKwWrap !== 'function') {
            reject(new DOMException('Crypto extension not available', 'NotSupportedError'));
            return;
          }
          try {
            resolve(toArrayBuffer(pal.nativeAesKwWrap(wrappingKey._data, toUint8Array(key._data))));
          } catch (e) { reject(e); }
          return;
        }

        if (wrapName !== 'AES-GCM' && wrapName !== 'AES-CBC') {
          reject(new DOMException('Unsupported wrap algorithm: ' + wrapName, 'NotSupportedError'));
          return;
        }
        if (typeof pal.nativeAesEncrypt !== 'function') {
          reject(new DOMException('Crypto extension not available', 'NotSupportedError'));
          return;
        }

        var plaintext;
        try {
          if (format === 'raw') {
            plaintext = toUint8Array(key._data);
          } else if (format === 'jwk') {
            var jwk = {
              kty: 'oct',
              k: base64UrlEncode(key._data),
              alg: key.algorithm.name === 'HMAC' ? 'HS' + (key.algorithm.hash ? key.algorithm.hash.replace('SHA-', '') : '256') : key.algorithm.name,
              ext: key.extractable,
              key_ops: key.usages,
            };
            plaintext = new TextEncoder().encode(JSON.stringify(jwk));
          } else {
            reject(new DOMException('Unsupported wrap format: ' + format, 'NotSupportedError'));
            return;
          }
        } catch (e) {
          reject(e);
          return;
        }

        try {
          if (wrapName === 'AES-GCM') {
            var iv = toUint8Array(wrapAlgorithm.iv);
            var aad = wrapAlgorithm.additionalData ? toUint8Array(wrapAlgorithm.additionalData) : undefined;
            var tagLen = wrapAlgorithm.tagLength !== undefined ? wrapAlgorithm.tagLength / 8 : 16;
            resolve(toArrayBuffer(pal.nativeAesEncrypt(plaintext, wrappingKey._data, iv, 'AES-GCM', aad, tagLen)));
            return;
          }
          if (wrapName === 'AES-CBC') {
            var iv = toUint8Array(wrapAlgorithm.iv);
            resolve(toArrayBuffer(pal.nativeAesEncrypt(plaintext, wrappingKey._data, iv, 'AES-CBC')));
            return;
          }
        } catch (e) {
          reject(e);
          return;
        }
      });
    }

    unwrapKey(format, wrappedKey, unwrappingKey, unwrapAlgorithm, unwrappedKeyAlgorithm, extractable, keyUsages) {
      return new Promise(function(resolve, reject) {
        var unwrapName = typeof unwrapAlgorithm === 'string' ? unwrapAlgorithm : unwrapAlgorithm.name;

        if (unwrapName === 'AES-KW') {
          if (format !== 'raw') {
            reject(new DOMException('AES-KW supports raw format only', 'NotSupportedError'));
            return;
          }
          if (typeof pal.nativeAesKwUnwrap !== 'function') {
            reject(new DOMException('Crypto extension not available', 'NotSupportedError'));
            return;
          }
          try {
            var keyBytes = new Uint8Array(pal.nativeAesKwUnwrap(unwrappingKey._data, toUint8Array(wrappedKey)));
            resolve(new CryptoKey('secret', unwrappedKeyAlgorithm, extractable, keyUsages, keyBytes));
          } catch (e) { reject(e); }
          return;
        }

        if (unwrapName !== 'AES-GCM' && unwrapName !== 'AES-CBC') {
          reject(new DOMException('Unsupported unwrap algorithm: ' + unwrapName, 'NotSupportedError'));
          return;
        }
        if (typeof pal.nativeAesDecrypt !== 'function') {
          reject(new DOMException('Crypto extension not available', 'NotSupportedError'));
          return;
        }

        var plaintext;
        try {
          if (unwrapName === 'AES-GCM') {
            var iv = toUint8Array(unwrapAlgorithm.iv);
            var aad = unwrapAlgorithm.additionalData ? toUint8Array(unwrapAlgorithm.additionalData) : undefined;
            var tagLen = unwrapAlgorithm.tagLength !== undefined ? unwrapAlgorithm.tagLength / 8 : 16;
            plaintext = pal.nativeAesDecrypt(toUint8Array(wrappedKey), unwrappingKey._data, iv, 'AES-GCM', aad, tagLen);
          } else {
            var iv = toUint8Array(unwrapAlgorithm.iv);
            plaintext = pal.nativeAesDecrypt(toUint8Array(wrappedKey), unwrappingKey._data, iv, 'AES-CBC');
          }
        } catch (e) {
          reject(e);
          return;
        }

        try {
          if (format === 'raw') {
            resolve(new CryptoKey('secret', unwrappedKeyAlgorithm, extractable, keyUsages, new Uint8Array(plaintext)));
            return;
          }
          if (format === 'jwk') {
            var json = JSON.parse(new TextDecoder().decode(plaintext));
            if (!json || !json.k) {
              reject(new DOMException('Invalid JWK', 'DataError'));
              return;
            }
            resolve(new CryptoKey('secret', unwrappedKeyAlgorithm, extractable, keyUsages, base64UrlDecode(json.k)));
            return;
          }
        } catch (e) {
          reject(e);
          return;
        }

        reject(new DOMException('Unsupported unwrap format: ' + format, 'NotSupportedError'));
      });
    }

    deriveBits(algorithm, key, length) {
      return new Promise(function(resolve, reject) {
        var algoName = typeof algorithm === 'string' ? algorithm : algorithm.name;

        if (algoName === 'PBKDF2') {
          var salt = toUint8Array(algorithm.salt);
          var iterations = algorithm.iterations;
          var hashAlgo = algorithm.hash ? (typeof algorithm.hash === 'string' ? algorithm.hash : algorithm.hash.name) : 'SHA-1';

          if (typeof pal.nativePbkdf2 !== 'function') {
            reject(new DOMException('Crypto extension not available', 'NotSupportedError'));
            return;
          }

          try {
            var dkLen = Math.ceil(length / 8);
            var result = pal.nativePbkdf2(key._data, salt, iterations, hashAlgo, dkLen);
            resolve(toArrayBuffer(result));
          } catch (e) {
            reject(e);
          }
          return;
        }
        if (algoName === 'HKDF') {
          var salt = algorithm.salt ? toUint8Array(algorithm.salt) : new Uint8Array(0);
          var info = algorithm.info ? toUint8Array(algorithm.info) : new Uint8Array(0);
          var hashAlgo = algorithm.hash ? (typeof algorithm.hash === 'string' ? algorithm.hash : algorithm.hash.name) : undefined;
          if (!hashAlgo) { reject(new TypeError('hash required for HKDF')); return; }

          if (typeof pal.nativeHkdf !== 'function') {
            reject(new DOMException('Crypto extension not available', 'NotSupportedError'));
            return;
          }

          try {
            var dkLen = Math.ceil(length / 8);
            var result = pal.nativeHkdf(hashAlgo, key._data, salt, info, dkLen);
            resolve(toArrayBuffer(result));
          } catch (e) {
            reject(e);
          }
          return;
        }

        if (algoName === 'ECDH') {
          if (key.type !== 'private') {
            reject(new DOMException('ECDH requires a private key', 'InvalidAccessError'));
            return;
          }
          var pub = algorithm.public;
          if (!pub || pub.type !== 'public') {
            reject(new TypeError('public key required for ECDH'));
            return;
          }
          if (typeof pal.nativeEcdh !== 'function') {
            reject(new DOMException('Crypto extension not available', 'NotSupportedError'));
            return;
          }

          try {
            var secret = pal.nativeEcdh(key.algorithm.namedCurve, key._data, pub._data);
            resolve(toArrayBuffer(secret));
          } catch (e) {
            reject(e);
          }
          return;
        }

        reject(new DOMException('Unsupported algorithm: ' + algoName, 'NotSupportedError'));
      });
    }

    deriveKey(algorithm, key, derivedKeyType, extractable, keyUsages) {
      var self = this;
      return new Promise(function(resolve, reject) {
        var bitsLength = (typeof derivedKeyType === 'string' ? 256 : (derivedKeyType.length || 256));
        self.deriveBits(algorithm, key, bitsLength).then(function(bits) {
          var data = new Uint8Array(bits);
          var algoName = typeof derivedKeyType === 'string' ? derivedKeyType : derivedKeyType.name;
          resolve(new CryptoKey('secret', { name: algoName }, extractable, keyUsages, data));
        }, reject);
      });
    }
  }

  // ================================================================
  // Wire up crypto.subtle
  // ================================================================

  if (!globalThis.crypto) {
    globalThis.crypto = {};
  }

  globalThis.crypto.subtle = new SubtleCrypto();
  globalThis.CryptoKey = CryptoKey;
  globalThis.SubtleCrypto = SubtleCrypto;
}

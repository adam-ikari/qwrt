/**
 * qwrt polyfill: crypto.subtle
 *
 * TC55/ECMA-429 requires crypto.subtle with at minimum:
 *   - digest (SHA-1, SHA-256, SHA-384, SHA-512)
 *   - importKey / sign / verify (HMAC)
 *   - importKey (for PBKDF2)
 *
 * All operations delegate to native C functions via pal.nativeDigest,
 * pal.nativeHmac, pal.nativeAesEncrypt, pal.nativeAesDecrypt,
 * pal.nativePbkdf2. These are registered by the crypto extension
 * (ext_crypto.c, gated by QWRT_WITH_CRYPTO_EXT).
 *
 * No JS fallback: this module exports an *installer* function
 * (installCryptoSubtle) but does NOT call it. The crypto extension's init
 * hook calls it after registering its native hooks — so when the extension
 * is not compiled in (QWRT_WITH_CRYPTO_EXT=OFF), crypto.subtle is never
 * installed and `typeof crypto.subtle === 'undefined'` reports the truth,
 * rather than leaving a shim that always rejects.
 *
 * Depends on: pal.nativeDigest, pal.nativeHmac, pal.nativeAesEncrypt,
 *             pal.nativeAesDecrypt, pal.nativePbkdf2
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

        if (algoName === 'AES-CBC' || algoName === 'AES-GCM' || algoName === 'AES-CTR') {
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

        if (format === 'raw') {
          resolve(toArrayBuffer(key._data));
          return;
        }

        if (format === 'jwk') {
          var jwk = {
            kty: 'oct',
            k: base64UrlEncode(key._data),
            alg: key.algorithm.name === 'HMAC' ? 'HS' + (key.algorithm.hash ? key.algorithm.hash.replace('SHA-', '') : '256') : key.algorithm.name,
            ext: true,
            key_ops: key.usages,
          };
          resolve(jwk);
          return;
        }

        reject(new DOMException('Unsupported export format: ' + format, 'NotSupportedError'));
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
}

# crypto API

The Web Crypto API subset as defined by WinterCG. Provides cryptographically strong random number generation and the SubtleCrypto interface.

## Globals

| Global | Type | Description |
|--------|------|-------------|
| `crypto` | `Crypto` | Crypto interface with `getRandomValues` and `subtle` |
| `crypto.subtle` | `SubtleCrypto` | Promise-based cryptographic operations |
| `CryptoKey` | class | Cryptographic key representation |

## crypto.getRandomValues()

Fills a typed array with cryptographically strong random values.

```js
// Fill with random bytes
let bytes = new Uint8Array(32);
crypto.getRandomValues(bytes);

// Fill with random 32-bit integers
let ints = new Uint32Array(16);
crypto.getRandomValues(ints);

// Use as random ID
let id = Array.from(bytes, b => b.toString(16).padStart(2, '0')).join('');
```

Supported array types: `Int8Array`, `Uint8Array`, `Uint8ClampedArray`, `Int16Array`, `Uint16Array`, `Int32Array`, `Uint32Array`.

Throws `QuotaExceededError` if more than 65536 bytes requested.

The underlying PAL method is synchronous — random bytes come from `/dev/urandom` (Linux), `getentropy()` (macOS), or hardware RNG (ESP32).

## crypto.subtle — SubtleCrypto

All SubtleCrypto methods return `Promise`s. Available algorithms depend on whether `QWRT_WITH_CRYPTO_EXT` is enabled at build time.

### `crypto.subtle.digest(algorithm, data)`

Compute a cryptographic hash.

```js
let data = new TextEncoder().encode('hello world');

// SHA-256 (always available)
let hash = await crypto.subtle.digest('SHA-256', data);
// returns ArrayBuffer

// SHA-512
let hash512 = await crypto.subtle.digest('SHA-512', data);
```

| Algorithm | Availability |
|-----------|-------------|
| `SHA-1` | Always (via mbedTLS) |
| `SHA-256` | Always (via mbedTLS) |
| `SHA-384` | Always (via mbedTLS) |
| `SHA-512` | Always (via mbedTLS) |

### `crypto.subtle.encrypt(algorithm, key, data)` / `decrypt(algorithm, key, data)`

AES encryption/decryption. Available when `QWRT_WITH_CRYPTO_EXT=ON`.

```js
let key = await crypto.subtle.generateKey(
    { name: 'AES-CBC', length: 256 },
    false, ['encrypt', 'decrypt']
);

let iv = crypto.getRandomValues(new Uint8Array(16));
let plaintext = new TextEncoder().encode('secret message');

let ciphertext = await crypto.subtle.encrypt(
    { name: 'AES-CBC', iv: iv },
    key, plaintext
);

let decrypted = await crypto.subtle.decrypt(
    { name: 'AES-CBC', iv: iv },
    key, ciphertext
);
```

| Algorithm | Modes | Key Sizes |
|-----------|-------|-----------|
| `AES-CBC` | encrypt, decrypt | 128, 192, 256 |
| `AES-CTR` | encrypt, decrypt | 128, 192, 256 |

### `crypto.subtle.generateKey(algorithm, extractable, keyUsages)`

Generate a new cryptographic key.

```js
let key = await crypto.subtle.generateKey(
    {
        name: 'HMAC',
        hash: 'SHA-256'
    },
    false,  // not extractable
    ['sign', 'verify']
);
```

### `crypto.subtle.importKey(format, keyData, algorithm, extractable, keyUsages)`

Import a key from external data.

```js
let rawKey = hexToBytes('0123456789abcdef0123456789abcdef');
let key = await crypto.subtle.importKey(
    'raw',
    rawKey,
    { name: 'HMAC', hash: 'SHA-256' },
    false,
    ['sign', 'verify']
);
```

Formats: `"raw"` for symmetric keys.

### `crypto.subtle.sign(algorithm, key, data)` / `verify(algorithm, key, signature, data)`

HMAC signing and verification.

```js
let key = await crypto.subtle.generateKey(
    { name: 'HMAC', hash: 'SHA-256' },
    false, ['sign', 'verify']
);

let data = new TextEncoder().encode('message');
let signature = await crypto.subtle.sign('HMAC', key, data);
let valid = await crypto.subtle.verify('HMAC', key, signature, data);
console.log('Valid:', valid); // true
```

| Algorithm | Hash Options |
|-----------|-------------|
| `HMAC` | SHA-1, SHA-256, SHA-384, SHA-512 |

### PBKDF2 Key Derivation

Available when `QWRT_WITH_CRYPTO_EXT` provides PBKDF2 support.

```js
let password = new TextEncoder().encode('password');
let salt = crypto.getRandomValues(new Uint8Array(16));

let key = await crypto.subtle.importKey(
    'raw', password, 'PBKDF2', false, ['deriveBits']
);

let derived = await crypto.subtle.deriveBits(
    { name: 'PBKDF2', salt: salt, iterations: 100000, hash: 'SHA-256' },
    key, 256
);
```

## Without CRYPTO_EXT

When `QWRT_WITH_CRYPTO_EXT=OFF`, only `crypto.getRandomValues()` is available. `crypto.subtle` exists but all methods throw `NotSupportedError`.

## Notes

- Key extraction (`extractable: true`) is NOT supported — all keys are non-extractable
- `AES-GCM` is not yet supported
- `ECDH`/`ECDSA` (asymmetric) are not yet supported
- No `crypto.randomUUID()` — use `getRandomValues` to build UUIDs manually

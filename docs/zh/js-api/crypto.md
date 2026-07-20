---
title: crypto
description: Qwrt.js 中的 Web Crypto API —— crypto.subtle 支持 SHA-256/384/512、HMAC、PBKDF2、AES-GCM 以及 crypto.getRandomValues。
---

# crypto API

WinterTC 定义的 Web Crypto API 子集。提供密码学强度的随机数生成和 SubtleCrypto 接口。

## 全局对象

| 全局对象 | 类型 | 描述 |
|--------|------|-------------|
| `crypto` | `Crypto` | Crypto 接口，包含 `getRandomValues` 和 `subtle` |
| `crypto.subtle` | `SubtleCrypto` | 基于 Promise 的密码学操作 |
| `CryptoKey` | class | 密码学密钥表示 |

## crypto.getRandomValues()

用密码学强度的随机值填充一个类型化数组。

```js
// 填充随机字节
let bytes = new Uint8Array(32);
crypto.getRandomValues(bytes);

// 填充随机 32 位整数
let ints = new Uint32Array(16);
crypto.getRandomValues(ints);

// 用作随机 ID
let id = Array.from(bytes, b => b.toString(16).padStart(2, '0')).join('');
```

支持的数组类型：`Int8Array`、`Uint8Array`、`Uint8ClampedArray`、`Int16Array`、`Uint16Array`、`Int32Array`、`Uint32Array`。

如果请求超过 65536 字节，抛出 `QuotaExceededError`。

底层 PAL 方法是同步的——随机字节来自 `/dev/urandom`（Linux）、`getentropy()`（macOS）或硬件 RNG（ESP32）。

## crypto.subtle — SubtleCrypto

所有 SubtleCrypto 方法返回 `Promise`。可用的算法取决于构建时是否启用了 `QWRT_WITH_CRYPTO_EXT`。

### `crypto.subtle.digest(algorithm, data)`

计算密码学哈希。

```js
let data = new TextEncoder().encode('hello world');

// SHA-256（始终可用）
let hash = await crypto.subtle.digest('SHA-256', data);
// 返回 ArrayBuffer

// SHA-512
let hash512 = await crypto.subtle.digest('SHA-512', data);
```

| 算法 | 可用性 |
|-----------|-------------|
| `SHA-1` | 始终可用（通过 mbedTLS） |
| `SHA-256` | 始终可用（通过 mbedTLS） |
| `SHA-384` | 始终可用（通过 mbedTLS） |
| `SHA-512` | 始终可用（通过 mbedTLS） |

### `crypto.subtle.encrypt(algorithm, key, data)` / `decrypt(algorithm, key, data)`

AES 加密/解密。当 `QWRT_WITH_CRYPTO_EXT=ON` 时可用。

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

| 算法 | 模式 | 密钥大小 |
|-----------|-------|-----------|
| `AES-CBC` | encrypt, decrypt | 128, 192, 256 |
| `AES-CTR` | encrypt, decrypt | 128, 192, 256 |

### `crypto.subtle.generateKey(algorithm, extractable, keyUsages)`

生成新的密码学密钥。

```js
let key = await crypto.subtle.generateKey(
    {
        name: 'HMAC',
        hash: 'SHA-256'
    },
    false,  // 不可提取
    ['sign', 'verify']
);
```

### `crypto.subtle.importKey(format, keyData, algorithm, extractable, keyUsages)`

从外部数据导入密钥。

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

格式：`"raw"` 用于对称密钥。

### `crypto.subtle.sign(algorithm, key, data)` / `verify(algorithm, key, signature, data)`

HMAC 签名与验证。

```js
let key = await crypto.subtle.generateKey(
    { name: 'HMAC', hash: 'SHA-256' },
    false, ['sign', 'verify']
);

let data = new TextEncoder().encode('message');
let signature = await crypto.subtle.sign('HMAC', key, data);
let valid = await crypto.subtle.verify('HMAC', key, signature, data);
console.log('有效:', valid); // true
```

| 算法 | 哈希选项 |
|-----------|-------------|
| `HMAC` | SHA-1, SHA-256, SHA-384, SHA-512 |

### PBKDF2 密钥派生

当 `QWRT_WITH_CRYPTO_EXT` 提供 PBKDF2 支持时可用。

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

## 无 CRYPTO_EXT 时

当 `QWRT_WITH_CRYPTO_EXT=OFF` 时，只有 `crypto.getRandomValues()` 可用。`crypto.subtle` 存在但所有方法抛出 `NotSupportedError`。

## 注意事项

- 密钥提取（`extractable: true`）不被支持——所有密钥均不可提取
- `AES-GCM` 尚不支持
- `ECDH`/`ECDSA`（非对称）尚不支持
- 不支持 `crypto.randomUUID()` —— 请使用 `getRandomValues` 手动构建 UUID
# crypto — WebCrypto 摘要与加解密

演示 WinterTC 兼容的 `crypto.subtle`（mbedTLS 后端）：SHA-256 摘要、
AES-GCM 对称加解密、密钥导出。

## 运行

```bash
./build/qzjs examples/crypto/crypto.js
```

## 期望输出

```
SHA-256(hello qzjs) = 60df0fbbb14f3704f22ceba4e178f140…
AES-GCM 密文长度: 40 bytes
解密回文: secret message from qzjs
key 导出长度: 32 bytes
```

密文 40 字节 = 明文 24 字节（`'secret message from qzjs'`）+ GCM 认证标签
16 字节；IV 不随密文输出，由调用方携带。`key 导出长度 32` 对应 AES-256。

## 要点

- 全部走 `crypto.subtle`（Promise 风格），与浏览器代码可直接互换。
- `generateKey(..., extractable=true, ...)` 才能 `exportKey('raw', key)`；
  导出结果可序列化传给宿主或另一运行时。
- `iv` 由 `crypto.getRandomValues` 生成——示例为可复现性在同一进程内复用，
  生产代码每次加密必须换新 IV。
- 后端能力不止本例：ECDSA/ECDH（P-256/384/521）、HKDF、AES-KW wrap/unwrap
  均已实现，见 [JS API: crypto](/js-api/crypto)。

## 相关文档

- [JS API: crypto](/js-api/crypto) — 完整算法清单与签名
- [JS API: encoding](/js-api/encoding) — `TextEncoder` / `TextDecoder`

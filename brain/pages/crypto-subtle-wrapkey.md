---
id: crypto-subtle-wrapkey
title: "crypto.subtle wrapKey/unwrapKey（AES-GCM/CBC wrapping）"
category: decision
status: active
tags: [crypto, webcrypto, wrapkey]
created: "2026-08-19T04:08:54"
updated: "2026-08-19T04:08:54"
---

<!-- compiled_truth -->
- **背景**：crypto.subtle 此前缺 `wrapKey`/`unwrapKey`（WebCrypto 标准方法）。C 层（ext_crypto.c）提供 `nativeAesEncrypt`/`nativeAesDecrypt`（AES-CBC/GCM/CTR），**无 AES-KW（RFC 3394）、无 RSA**。
- **决策**：wrapKey/unwrapKey 用 **AES-GCM**（默认，16B tag）或 **AES-CBC** 作为 wrap 算法（WebCrypto 规范允许这些算法用于 key wrapping）；format 支持 `raw` / `jwk`（secret key 类）。不用 AES-KW（C 层未实现、需 ECB 基础，收益低）。
- **实现（polyfill/src/crypto-subtle.js，2026-08-19）**：
  - `wrapKey(format, key, wrappingKey, wrapAlgorithm)`：raw → 取 `key._data` 字节；jwk → 构造 JWK（kty/k/alg/ext/key_ops，复用 exportKey 逻辑）序列化 JSON → `nativeAesEncrypt(..., 'AES-GCM'|'AES-CBC')` → ArrayBuffer。
  - `unwrapKey(format, wrappedKey, unwrappingKey, unwrapAlgorithm, unwrappedKeyAlgorithm, extractable, keyUsages)`：`nativeAesDecrypt` 解密 → raw 直接重建 CryptoKey；jwk 解析 JSON 取 `k` 再 base64UrlDecode 重建。
- **算法正确性验证**：补 AES-CBC/GCM/CTR `encrypt→decrypt` 往返 gtest（AesGcmRoundTrip/AesCbcRoundTrip/AesCtrRoundTrip），确认既有实现正确（GCM 输出 = 明文 + 16B tag，ctLen 校验）。
- **测试**：test_crypto_subtle_gtest 8→13 用例（+WrapUnwrapKeyRoundTrip：raw wrap/unwrap 后 unwrapped key 可加解密 + wrappedLen=32B；+WrapUnwrapJwk：jwk 往返 exportKey 一致）。13/13 过；polyfill 19/19、worker 12/12、fetch_stream 2/2 等全量回归通过。
- **坑**：CryptoKey.algorithm.hash 是字符串（'SHA-256'）而非 {name} 对象——沿用既有约定（见 [[crypto-subtle-gtest]]）。


## Timeline

- time: 2026-08-19T04:08:54
  kind: decision
  summary: "Created this page: crypto.subtle wrapKey/unwrapKey（AES-GCM/CBC wrapping）"
  source: session 2026-08-19
  affects: [crypto-subtle-wrapkey]

- time: 2026-08-19T04:08:54
  kind: decision
  summary: Rewrote compiled_truth to the new best understanding
  source: brain update-truth
  affects: [crypto-subtle-wrapkey]

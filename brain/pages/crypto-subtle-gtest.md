---
id: crypto-subtle-gtest
title: "crypto.subtle gtest 测试套件"
category: decision
status: active
tags: [crypto, gtest, webcrypto]
created: "2026-08-15T05:51:39"
updated: "2026-08-15T05:51:39"
---

<!-- compiled_truth -->
## 决策
crypto.subtle（digest/HMAC/PBKDF2）的功能测试采用 gtest 框架（test/test_crypto_subtle_gtest.cpp，8 用例），不用 WPT runner（test/wpt_runner.c 是 wip 骨架，未接入 CMake）。理由：与项目现有 13 个 gtest 套件统一，mock_libuv 下确定性离线运行。

## 已知细节（避免踩坑）
- polyfill 的 CryptoKey.algorithm.hash 是**字符串**（'SHA-256'）而非 WebCrypto 规范的 {name:...} 对象——断言时用 `k.algorithm.hash.name || k.algorithm.hash` 兼容（与 WPT 测试 subtle-digest.any.js 一致）。
- crypto.subtle 由 ext_crypto.c（QWRT_WITH_CRYPTO_EXT=ON）init hook 安装到全局；所有操作返回 Promise，测试用全局变量 + host_poll_until_value 轮询。
- 测试通过 add_qwrt_gtest(test_crypto_subtle_gtest) 注册，QWRT_WITH_CRYPTO_EXT 条件守卫。

## 验证
./build/test/test_crypto_subtle_gtest: 8/8 PASSED；ctest 15/15 PASSED。


## Timeline

- time: 2026-08-15T05:51:39
  kind: decision
  summary: "Created this page: crypto.subtle gtest 测试套件"
  source: "用户要求 gtest 框架"
  affects: [crypto-subtle-gtest]

- time: 2026-08-15T05:51:39
  kind: decision
  summary: "crypto.subtle 用 gtest 测试，而非 WPT runner"
  source: brain update-truth
  affects: [crypto-subtle-gtest]

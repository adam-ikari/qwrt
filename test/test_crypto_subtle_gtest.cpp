// test_crypto_subtle_gtest.cpp — crypto.subtle 宿主契约测试（执行模型 A / mock_libuv）
//
// 覆盖 test/wpt/crypto/subtle-digest.any.js 的测试向量：SHA-256 digest、
// HMAC sign/verify/importKey/exportKey/generateKey、PBKDF2 deriveBits。
// crypto.subtle 由 crypto 扩展（ext_crypto.c，QWRT_WITH_CRYPTO_EXT=ON）在
// runtime 初始化时安装，所有操作返回 Promise，测试用全局变量 + 轮询断言。
#include "test_host.h"
#include <cstdio>
#include <cstring>

class CryptoSubtleTest : public ::testing::Test {
protected:
    HostCtx *h = nullptr;

    void SetUp() override {
        h = host_create();
        ASSERT_NE(nullptr, h);
    }
    void TearDown() override { host_destroy(h); }
};

// 异步执行 code（结果写全局 var），轮询直到 var 的 JSON 值包含 expected。
static bool poll_until(HostCtx *h, const char *var, const char *code,
                       const char *expected, std::string *out) {
    std::string v;
    if (!host_eval(h, code, &v)) return false;
    return host_poll_until_value(h, var, expected, out);
}

// ================================================================
// crypto.subtle.digest — SHA-256 已知向量
// ================================================================

TEST_F(CryptoSubtleTest, DigestExists) {
    std::string v;
    ASSERT_TRUE(host_value(h,
        "JSON.stringify({obj: typeof crypto.subtle, fn: typeof crypto.subtle.digest})", &v));
    EXPECT_NE(std::string::npos, v.find("\"obj\":\"object\""));
    EXPECT_NE(std::string::npos, v.find("\"fn\":\"function\""));
}

// SHA-256 of empty input — e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
TEST_F(CryptoSubtleTest, DigestSHA256Empty) {
    std::string v;
    ASSERT_TRUE(poll_until(h, "_d",
        "var _d = null;\n"
        "crypto.subtle.digest('SHA-256', new Uint8Array(0)).then(function(buf){\n"
        "  var s=''; var u=new Uint8Array(buf);\n"
        "  for(var i=0;i<u.length;i++) s += u[i].toString(16).padStart(2,'0');\n"
        "  _d = s;\n"
        "});\n"
        "'go'",
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", &v));
}

// SHA-256 of "hello" — 2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824
TEST_F(CryptoSubtleTest, DigestSHA256Hello) {
    std::string v;
    ASSERT_TRUE(poll_until(h, "_d",
        "var _d = null;\n"
        "crypto.subtle.digest('SHA-256', new TextEncoder().encode('hello')).then(function(buf){\n"
        "  var s=''; var u=new Uint8Array(buf);\n"
        "  for(var i=0;i<u.length;i++) s += u[i].toString(16).padStart(2,'0');\n"
        "  _d = s;\n"
        "});\n"
        "'go'",
        "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824", &v));
}

// ================================================================
// HMAC — generateKey / sign / verify
// ================================================================

TEST_F(CryptoSubtleTest, HmacSignVerifyRoundTrip) {
    std::string v;
    ASSERT_TRUE(poll_until(h, "_r",
        "var _r = null;\n"
        "crypto.subtle.generateKey({name:'HMAC', hash:'SHA-256'}, false, ['sign','verify'])\n"
        "  .then(function(k){\n"
        "    var msg = new TextEncoder().encode('test message');\n"
        "    return crypto.subtle.sign({name:'HMAC'}, k, msg).then(function(sig){\n"
        "      return crypto.subtle.verify({name:'HMAC'}, k, sig, msg).then(function(ok){\n"
        "        _r = JSON.stringify({type:k.type, usages:k.usages, sigLen:sig.byteLength, ok:ok});\n"
        "      });\n"
        "    });\n"
        "  });\n"
        "'go'",
        "\"type\":\"secret\"", &v));
    EXPECT_NE(std::string::npos, v.find("\"usages\":[\"sign\",\"verify\"]"));
    EXPECT_NE(std::string::npos, v.find("\"sigLen\":32"));
    EXPECT_NE(std::string::npos, v.find("\"ok\":true"));
}

TEST_F(CryptoSubtleTest, HmacVerifyRejectsTampered) {
    std::string v;
    ASSERT_TRUE(poll_until(h, "_r",
        "var _r = null;\n"
        "crypto.subtle.generateKey({name:'HMAC', hash:'SHA-256'}, false, ['sign','verify'])\n"
        "  .then(function(k){\n"
        "    var msg = new TextEncoder().encode('test message');\n"
        "    return crypto.subtle.sign({name:'HMAC'}, k, msg).then(function(sig){\n"
        "      var tampered = new Uint8Array(msg); tampered[0] ^= 0xff;\n"
        "      return crypto.subtle.verify({name:'HMAC'}, k, sig, tampered).then(function(ok){\n"
        "        _r = JSON.stringify({ok:ok});\n"
        "      });\n"
        "    });\n"
        "  });\n"
        "'go'",
        "\"ok\":false", &v));
}

// generateKey 产出的 CryptoKey 属性（algorithm.name / extractable）
TEST_F(CryptoSubtleTest, GenerateKeyCryptoKeyAttrs) {
    std::string v;
    ASSERT_TRUE(poll_until(h, "_a",
        "var _a = null;\n"
        "crypto.subtle.generateKey({name:'HMAC', hash:'SHA-256'}, false, ['sign'])\n"
        "  .then(function(k){\n"
        "    var hashName = k.algorithm.hash.name || k.algorithm.hash;\n"
        "    _a = JSON.stringify({name:k.algorithm.name, hash:hashName, extractable:k.extractable});\n"
        "  });\n"
        "'go'",
        "\"name\":\"HMAC\"", &v));
    EXPECT_NE(std::string::npos, v.find("\"hash\":\"SHA-256\""));
    EXPECT_NE(std::string::npos, v.find("\"extractable\":false"));
}

// importKey(raw) + exportKey(raw) 往返
TEST_F(CryptoSubtleTest, HmacImportExportKey) {
    std::string v;
    ASSERT_TRUE(poll_until(h, "_e",
        "var _e = null;\n"
        "crypto.subtle.importKey('raw', new TextEncoder().encode('secret key'),\n"
        "  {name:'HMAC', hash:'SHA-256'}, true, ['sign'])\n"
        "  .then(function(k){ return crypto.subtle.exportKey('raw', k); })\n"
        "  .then(function(exp){ _e = new TextDecoder().decode(exp); });\n"
        "'go'",
        "secret key", &v));
}

// ================================================================
// PBKDF2 — importKey(raw) + deriveBits
// ================================================================

TEST_F(CryptoSubtleTest, Pbkdf2DeriveBits) {
    std::string v;
    ASSERT_TRUE(poll_until(h, "_p",
        "var _p = null;\n"
        "crypto.subtle.importKey('raw', new TextEncoder().encode('password'),\n"
        "  'PBKDF2', false, ['deriveBits'])\n"
        "  .then(function(k){\n"
        "    var salt = new Uint8Array(8);\n"
        "    return crypto.subtle.deriveBits(\n"
        "      {name:'PBKDF2', salt:salt, iterations:1000, hash:'SHA-256'}, k, 128)\n"
        "      .then(function(bits){\n"
        "        var s=''; var u=new Uint8Array(bits);\n"
        "        for(var i=0;i<u.length;i++) s += u[i].toString(16).padStart(2,'0');\n"
        "        _p = JSON.stringify({len:bits.byteLength, hex:s});\n"
        "      });\n"
        "  });\n"
        "'go'",
        "\"len\":16", &v));
    // 输出非平凡（不是全 0）
    EXPECT_NE(std::string::npos, v.find("\"hex\":\""));
    size_t hexpos = v.find("\"hex\":\"");
    ASSERT_NE(std::string::npos, hexpos);
    std::string hex = v.substr(hexpos + 7, 32);
    EXPECT_NE(std::string(32, '0'), hex);
}

// ================================================================
// AES encrypt/decrypt round-trips (algorithm correctness)
// ================================================================

TEST_F(CryptoSubtleTest, AesGcmRoundTrip) {
    std::string v;
    ASSERT_TRUE(poll_until(h, "_r",
        "var _r = null;\n"
        "crypto.subtle.generateKey({name:'AES-GCM', length:256}, false, ['encrypt','decrypt'])\n"
        "  .then(function(k){\n"
        "    var msg = new TextEncoder().encode('aes-gcm roundtrip payload');\n"
        "    var iv = new Uint8Array(12);\n"
        "    return crypto.subtle.encrypt({name:'AES-GCM', iv:iv}, k, msg).then(function(ct){\n"
        "      return crypto.subtle.decrypt({name:'AES-GCM', iv:iv}, k, ct).then(function(pt){\n"
        "        _r = JSON.stringify({txt:new TextDecoder().decode(pt), ctLen:ct.byteLength});\n"
        "      });\n"
        "    });\n"
        "  });\n"
        "'go'",
        "\"txt\":\"aes-gcm roundtrip payload\"", &v));
    /* GCM 输出 = 明文 + 16 字节 tag */
    EXPECT_NE(std::string::npos, v.find("\"ctLen\":41")) << "got: " << v;
}

TEST_F(CryptoSubtleTest, AesCbcRoundTrip) {
    std::string v;
    ASSERT_TRUE(poll_until(h, "_r",
        "var _r = null;\n"
        "crypto.subtle.generateKey({name:'AES-CBC', length:256}, false, ['encrypt','decrypt'])\n"
        "  .then(function(k){\n"
        "    var msg = new TextEncoder().encode('aes-cbc roundtrip payload');\n"
        "    var iv = new Uint8Array(16);\n"
        "    return crypto.subtle.encrypt({name:'AES-CBC', iv:iv}, k, msg).then(function(ct){\n"
        "      return crypto.subtle.decrypt({name:'AES-CBC', iv:iv}, k, ct).then(function(pt){\n"
        "        _r = new TextDecoder().decode(pt);\n"
        "      });\n"
        "    });\n"
        "  });\n"
        "'go'",
        "aes-cbc roundtrip payload", &v));
}

TEST_F(CryptoSubtleTest, AesCtrRoundTrip) {
    std::string v;
    ASSERT_TRUE(poll_until(h, "_r",
        "var _r = null;\n"
        "crypto.subtle.generateKey({name:'AES-CTR', length:256}, false, ['encrypt','decrypt'])\n"
        "  .then(function(k){\n"
        "    var msg = new TextEncoder().encode('aes-ctr roundtrip payload');\n"
        "    var counter = new Uint8Array(16);\n"
        "    return crypto.subtle.encrypt({name:'AES-CTR', counter:counter}, k, msg).then(function(ct){\n"
        "      return crypto.subtle.decrypt({name:'AES-CTR', counter:counter}, k, ct).then(function(pt){\n"
        "        _r = new TextDecoder().decode(pt);\n"
        "      });\n"
        "    });\n"
        "  });\n"
        "'go'",
        "aes-ctr roundtrip payload", &v));
}

// ================================================================
// wrapKey / unwrapKey (raw + jwk formats, AES-GCM wrapping)
// ================================================================

TEST_F(CryptoSubtleTest, WrapUnwrapKeyRoundTrip) {
    std::string v;
    ASSERT_TRUE(poll_until(h, "_w",
        "var _w = null;\n"
        "var _kek = null;\n"
        "crypto.subtle.generateKey({name:'AES-GCM', length:256}, false, ['wrapKey','unwrapKey'])\n"
        "  .then(function(kek){\n"
        "    _kek = kek;\n"
        "    return crypto.subtle.generateKey({name:'AES-GCM', length:128}, true, ['encrypt','decrypt']);\n"
        "  })\n"
        "  .then(function(target){\n"
        "    var iv = new Uint8Array(12);\n"
        "    return crypto.subtle.wrapKey('raw', target, _kek, {name:'AES-GCM', iv:iv}).then(function(wrapped){\n"
        "      return crypto.subtle.unwrapKey('raw', wrapped, _kek, {name:'AES-GCM', iv:iv},\n"
        "        {name:'AES-GCM', length:128}, true, ['encrypt','decrypt']).then(function(unwrapped){\n"
        "          var msg = new TextEncoder().encode('unwrapped key works');\n"
        "          var iv2 = new Uint8Array(12);\n"
        "          return crypto.subtle.encrypt({name:'AES-GCM', iv:iv2}, unwrapped, msg).then(function(ct){\n"
        "            return crypto.subtle.decrypt({name:'AES-GCM', iv:iv2}, unwrapped, ct).then(function(pt){\n"
        "              _w = JSON.stringify({wrappedLen:wrapped.byteLength, txt:new TextDecoder().decode(pt),\n"
        "                type:unwrapped.type, extractable:unwrapped.extractable});\n"
        "            });\n"
        "          });\n"
        "        });\n"
        "    });\n"
        "  });\n"
        "'go'",
        "\"txt\":\"unwrapped key works\"", &v));
    EXPECT_NE(std::string::npos, v.find("\"type\":\"secret\"")) << "got: " << v;
    EXPECT_NE(std::string::npos, v.find("\"extractable\":true")) << "got: " << v;
    /* 128-bit key raw 16B + GCM tag 16B = 32B wrapped */
    EXPECT_NE(std::string::npos, v.find("\"wrappedLen\":32")) << "got: " << v;
}

TEST_F(CryptoSubtleTest, WrapUnwrapJwk) {
    std::string v;
    ASSERT_TRUE(poll_until(h, "_j",
        "var _j = null;\n"
        "var _kek = null;\n"
        "crypto.subtle.generateKey({name:'AES-GCM', length:256}, false, ['wrapKey','unwrapKey'])\n"
        "  .then(function(kek){\n"
        "    _kek = kek;\n"
        "    return crypto.subtle.importKey('raw', new TextEncoder().encode('0123456789abcdef'),\n"
        "      {name:'AES-GCM', length:128}, true, ['encrypt']);\n"
        "  })\n"
        "  .then(function(target){\n"
        "    var iv = new Uint8Array(12);\n"
        "    return crypto.subtle.wrapKey('jwk', target, _kek, {name:'AES-GCM', iv:iv}).then(function(wrapped){\n"
        "      return crypto.subtle.unwrapKey('jwk', wrapped, _kek, {name:'AES-GCM', iv:iv},\n"
        "        {name:'AES-GCM', length:128}, true, ['encrypt']).then(function(unwrapped){\n"
        "          return crypto.subtle.exportKey('raw', unwrapped).then(function(raw){\n"
        "            _j = JSON.stringify({txt:new TextDecoder().decode(raw), type:unwrapped.type});\n"
        "          });\n"
        "        });\n"
        "    });\n"
        "  });\n"
        "'go'",
        "\"txt\":\"0123456789abcdef\"", &v));
    EXPECT_NE(std::string::npos, v.find("\"type\":\"secret\"")) << "got: " << v;
}

/*
 * qwrt Crypto Extension
 *
 * Native crypto.subtle operations using mbedTLS.
 * Registers pal.nativeDigest, pal.nativeHmac, pal.nativeAesEncrypt,
 * pal.nativeAesDecrypt, pal.nativePbkdf2 on the JS pal object.
 *
 * When QWRT_WITH_CRYPTO_EXT is defined, uses mbedTLS for real crypto.
 * When not defined, the extension compiles but does nothing —
 * crypto.subtle will fall back to the JS implementation.
 */

#include "qwrt_internal.h"

#ifdef QWRT_WITH_CRYPTO_EXT

#include <mbedtls/md.h>
#include <mbedtls/cipher.h>
#include <mbedtls/pkcs5.h>
#include <string.h>

/* ================================================================
 * Helper: extract byte buffer from JS ArrayBuffer/TypedArray
 * ================================================================ */

static int crypto_extract_buffer(JSContext *ctx, JSValueConst val,
                                const uint8_t **out_bytes, size_t *out_len)
{
    size_t byte_len = 0;
    const uint8_t *bytes = NULL;

    bytes = JS_GetUint8Array(ctx, &byte_len, val);
    if (bytes) {
        *out_bytes = bytes;
        *out_len = byte_len;
        return 0;
    }

    bytes = JS_GetArrayBuffer(ctx, &byte_len, val);
    if (bytes) {
        *out_bytes = bytes;
        *out_len = byte_len;
        return 0;
    }

    return -1;
}

/* Helper: create Uint8Array copy from raw bytes */
static JSValue crypto_new_uint8array(JSContext *ctx, const uint8_t *data, size_t len)
{
    return JS_NewUint8ArrayCopy(ctx, data, len);
}

/* ================================================================
 * pal.nativeDigest(algorithm, data) -> Uint8Array
 *
 * algorithm: "SHA-1", "SHA-256", "SHA-384", "SHA-512"
 * data: ArrayBuffer or Uint8Array
 * ================================================================ */

static JSValue js_pal_native_digest(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "nativeDigest requires 2 arguments");
    }

    const char *algo = JS_ToCString(ctx, argv[0]);
    if (!algo) return JS_EXCEPTION;

    const uint8_t *data;
    size_t data_len;
    if (crypto_extract_buffer(ctx, argv[1], &data, &data_len) < 0) {
        JS_FreeCString(ctx, algo);
        return JS_ThrowTypeError(ctx, "nativeDigest: data must be ArrayBuffer or Uint8Array");
    }

    mbedtls_md_type_t md_type;
    if (strcmp(algo, "SHA-1") == 0)       md_type = MBEDTLS_MD_SHA1;
    else if (strcmp(algo, "SHA-256") == 0) md_type = MBEDTLS_MD_SHA256;
    else if (strcmp(algo, "SHA-384") == 0) md_type = MBEDTLS_MD_SHA384;
    else if (strcmp(algo, "SHA-512") == 0) md_type = MBEDTLS_MD_SHA512;
    else {
        JS_FreeCString(ctx, algo);
        return JS_ThrowTypeError(ctx, "nativeDigest: unsupported algorithm '%s'", algo);
    }
    JS_FreeCString(ctx, algo);

    const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(md_type);
    if (!md_info) return JS_ThrowTypeError(ctx, "nativeDigest: algorithm not available");

    size_t out_len = mbedtls_md_get_size(md_info);
    uint8_t *out_buf = (uint8_t *)js_malloc(ctx, out_len);
    if (!out_buf) return JS_ThrowOutOfMemory(ctx);

    if (mbedtls_md(md_info, data, data_len, out_buf) != 0) {
        js_free(ctx, out_buf);
        return JS_ThrowTypeError(ctx, "nativeDigest: hash computation failed");
    }

    JSValue result = crypto_new_uint8array(ctx, out_buf, out_len);
    js_free(ctx, out_buf);
    return result;
}

/* ================================================================
 * pal.nativeHmac(hashAlgo, key, data) -> Uint8Array
 *
 * hashAlgo: "SHA-1", "SHA-256", "SHA-384", "SHA-512"
 * key: ArrayBuffer or Uint8Array
 * data: ArrayBuffer or Uint8Array
 * ================================================================ */

static JSValue js_pal_native_hmac(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 3) {
        return JS_ThrowTypeError(ctx, "nativeHmac requires 3 arguments");
    }

    const char *hash_algo = JS_ToCString(ctx, argv[0]);
    if (!hash_algo) return JS_EXCEPTION;

    const uint8_t *key;
    size_t key_len;
    if (crypto_extract_buffer(ctx, argv[1], &key, &key_len) < 0) {
        JS_FreeCString(ctx, hash_algo);
        return JS_ThrowTypeError(ctx, "nativeHmac: key must be ArrayBuffer or Uint8Array");
    }

    const uint8_t *data;
    size_t data_len;
    if (crypto_extract_buffer(ctx, argv[2], &data, &data_len) < 0) {
        JS_FreeCString(ctx, hash_algo);
        return JS_ThrowTypeError(ctx, "nativeHmac: data must be ArrayBuffer or Uint8Array");
    }

    mbedtls_md_type_t md_type;
    if (strcmp(hash_algo, "SHA-1") == 0)       md_type = MBEDTLS_MD_SHA1;
    else if (strcmp(hash_algo, "SHA-256") == 0) md_type = MBEDTLS_MD_SHA256;
    else if (strcmp(hash_algo, "SHA-384") == 0) md_type = MBEDTLS_MD_SHA384;
    else if (strcmp(hash_algo, "SHA-512") == 0) md_type = MBEDTLS_MD_SHA512;
    else {
        JS_FreeCString(ctx, hash_algo);
        return JS_ThrowTypeError(ctx, "nativeHmac: unsupported hash '%s'", hash_algo);
    }
    JS_FreeCString(ctx, hash_algo);

    const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(md_type);
    if (!md_info) return JS_ThrowTypeError(ctx, "nativeHmac: hash not available");

    size_t out_len = mbedtls_md_get_size(md_info);
    uint8_t *out_buf = (uint8_t *)js_malloc(ctx, out_len);
    if (!out_buf) return JS_ThrowOutOfMemory(ctx);

    if (mbedtls_md_hmac(md_info, key, key_len, data, data_len, out_buf) != 0) {
        js_free(ctx, out_buf);
        return JS_ThrowTypeError(ctx, "nativeHmac: HMAC computation failed");
    }

    JSValue result = crypto_new_uint8array(ctx, out_buf, out_len);
    js_free(ctx, out_buf);
    return result;
}

/* ================================================================
 * pal.nativeAesEncrypt(data, key, iv, algo, aad, tagLen) -> Uint8Array
 * pal.nativeAesDecrypt(data, key, iv, algo, aad, tagLen) -> Uint8Array
 *
 * algo: "AES-CBC", "AES-GCM", "AES-CTR"
 * data, key, iv, aad: ArrayBuffer or Uint8Array (aad can be undefined)
 * tagLen: number (for GCM, typically 16)
 * ================================================================ */

static JSValue js_pal_native_aes_crypt(JSContext *ctx, JSValueConst argv[],
                                       int encrypt)
{
    const uint8_t *data;
    size_t data_len;
    if (crypto_extract_buffer(ctx, argv[0], &data, &data_len) < 0) {
        return JS_ThrowTypeError(ctx, "nativeAes: data must be ArrayBuffer or Uint8Array");
    }

    const uint8_t *key;
    size_t key_len;
    if (crypto_extract_buffer(ctx, argv[1], &key, &key_len) < 0) {
        return JS_ThrowTypeError(ctx, "nativeAes: key must be ArrayBuffer or Uint8Array");
    }

    const uint8_t *iv;
    size_t iv_len;
    if (crypto_extract_buffer(ctx, argv[2], &iv, &iv_len) < 0) {
        return JS_ThrowTypeError(ctx, "nativeAes: iv must be ArrayBuffer or Uint8Array");
    }

    const char *algo = JS_ToCString(ctx, argv[3]);
    if (!algo) return JS_EXCEPTION;

    const uint8_t *aad = NULL;
    size_t aad_len = 0;
    if (!JS_IsUndefined(argv[4]) && !JS_IsNull(argv[4])) {
        if (crypto_extract_buffer(ctx, argv[4], &aad, &aad_len) < 0) {
            JS_FreeCString(ctx, algo);
            return JS_ThrowTypeError(ctx, "nativeAes: aad must be ArrayBuffer or Uint8Array");
        }
    }

    int32_t tag_len = 16;
    if (!JS_IsUndefined(argv[5])) {
        JS_ToInt32(ctx, &tag_len, argv[5]);
    }

    mbedtls_cipher_mode_t mode;
    if (strcmp(algo, "AES-CBC") == 0)      mode = MBEDTLS_MODE_CBC;
    else if (strcmp(algo, "AES-GCM") == 0) mode = MBEDTLS_MODE_GCM;
    else if (strcmp(algo, "AES-CTR") == 0) mode = MBEDTLS_MODE_CTR;
    else {
        JS_FreeCString(ctx, algo);
        return JS_ThrowTypeError(ctx, "nativeAes: unsupported mode '%s'", algo);
    }
    JS_FreeCString(ctx, algo);

    const mbedtls_cipher_info_t *cipher_info =
        mbedtls_cipher_info_from_values(MBEDTLS_CIPHER_ID_AES, (int)key_len * 8, mode);
    if (!cipher_info) {
        return JS_ThrowTypeError(ctx, "nativeAes: unsupported key length or mode");
    }

    mbedtls_cipher_context_t cipher_ctx;
    mbedtls_cipher_init(&cipher_ctx);

    if (mbedtls_cipher_setup(&cipher_ctx, cipher_info) != 0) {
        return JS_ThrowTypeError(ctx, "nativeAes: cipher setup failed");
    }

    if (mbedtls_cipher_setkey(&cipher_ctx, key, (int)key_len * 8,
                              encrypt ? MBEDTLS_ENCRYPT : MBEDTLS_DECRYPT) != 0) {
        mbedtls_cipher_free(&cipher_ctx);
        return JS_ThrowTypeError(ctx, "nativeAes: setkey failed");
    }

    /* CBC requires explicit padding mode (mbedTLS doesn't default it) */
    if (mode == MBEDTLS_MODE_CBC) {
        if (mbedtls_cipher_set_padding_mode(&cipher_ctx, MBEDTLS_PADDING_PKCS7) != 0) {
            mbedtls_cipher_free(&cipher_ctx);
            return JS_ThrowTypeError(ctx, "nativeAes: set padding failed");
        }
    }

    /* For GCM, output = ciphertext + tag (encrypt) or input = ciphertext + tag (decrypt) */
    size_t out_len;
    if (mode == MBEDTLS_MODE_GCM) {
        if (encrypt) {
            out_len = data_len + tag_len;
        } else {
            if (data_len < (size_t)tag_len) {
                mbedtls_cipher_free(&cipher_ctx);
                return JS_ThrowTypeError(ctx, "nativeAes: GCM data too short for tag");
            }
            out_len = data_len - tag_len;
        }
    } else if (mode == MBEDTLS_MODE_CBC) {
        /* CBC: output includes padding */
        out_len = data_len + mbedtls_cipher_get_block_size(&cipher_ctx);
    } else {
        out_len = data_len;
    }

    uint8_t *out_buf = (uint8_t *)js_malloc(ctx, out_len + 16);
    if (!out_buf) {
        mbedtls_cipher_free(&cipher_ctx);
        return JS_ThrowOutOfMemory(ctx);
    }

    size_t olen = 0;
    int ret = 0;

    if (mode == MBEDTLS_MODE_GCM) {
        if (encrypt) {
            ret = mbedtls_cipher_auth_encrypt_ext(&cipher_ctx,
                iv, iv_len, aad, aad_len,
                data, data_len,
                out_buf, out_len, &olen,
                tag_len);
        } else {
            ret = mbedtls_cipher_auth_decrypt_ext(&cipher_ctx,
                iv, iv_len, aad, aad_len,
                data, data_len,
                out_buf, out_len, &olen,
                tag_len);
        }
    } else {
        /* CBC / CTR: use standard cipher API */
        if (mbedtls_cipher_set_iv(&cipher_ctx, iv, iv_len) != 0) {
            js_free(ctx, out_buf);
            mbedtls_cipher_free(&cipher_ctx);
            return JS_ThrowTypeError(ctx, "nativeAes: setiv failed");
        }

        if (mbedtls_cipher_reset(&cipher_ctx) != 0) {
            js_free(ctx, out_buf);
            mbedtls_cipher_free(&cipher_ctx);
            return JS_ThrowTypeError(ctx, "nativeAes: reset failed");
        }

        if (mode == MBEDTLS_MODE_CBC || mode == MBEDTLS_MODE_CTR) {
            size_t olen1 = 0, olen2 = 0;
            ret = mbedtls_cipher_update(&cipher_ctx, data, data_len, out_buf, &olen1);
            if (ret == 0) {
                ret = mbedtls_cipher_finish(&cipher_ctx, out_buf + olen1, &olen2);
                olen = olen1 + olen2;
            } else {
                olen = olen1;
            }
        }
    }

    mbedtls_cipher_free(&cipher_ctx);

    if (ret != 0) {
        js_free(ctx, out_buf);
        if (!encrypt && mode == MBEDTLS_MODE_GCM) {
            return JS_ThrowTypeError(ctx, "nativeAes: GCM authentication failed");
        }
        return JS_ThrowTypeError(ctx, "nativeAes: cipher operation failed (%d)", ret);
    }

    JSValue result = crypto_new_uint8array(ctx, out_buf, olen);
    js_free(ctx, out_buf);
    return result;
}

static JSValue js_pal_native_aes_encrypt(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc;
    return js_pal_native_aes_crypt(ctx, argv, 1);
}

static JSValue js_pal_native_aes_decrypt(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc;
    return js_pal_native_aes_crypt(ctx, argv, 0);
}

/* ================================================================
 * pal.nativePbkdf2(password, salt, iterations, hashAlgo, dkLen) -> Uint8Array
 *
 * password, salt: ArrayBuffer or Uint8Array
 * iterations: number
 * hashAlgo: "SHA-1", "SHA-256", "SHA-384", "SHA-512"
 * dkLen: number (derived key length in bytes)
 * ================================================================ */

static JSValue js_pal_native_pbkdf2(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 5) {
        return JS_ThrowTypeError(ctx, "nativePbkdf2 requires 5 arguments");
    }

    const uint8_t *password;
    size_t password_len;
    if (crypto_extract_buffer(ctx, argv[0], &password, &password_len) < 0) {
        return JS_ThrowTypeError(ctx, "nativePbkdf2: password must be ArrayBuffer or Uint8Array");
    }

    const uint8_t *salt;
    size_t salt_len;
    if (crypto_extract_buffer(ctx, argv[1], &salt, &salt_len) < 0) {
        return JS_ThrowTypeError(ctx, "nativePbkdf2: salt must be ArrayBuffer or Uint8Array");
    }

    int32_t iterations;
    if (JS_ToInt32(ctx, &iterations, argv[2])) return JS_EXCEPTION;

    const char *hash_algo = JS_ToCString(ctx, argv[3]);
    if (!hash_algo) return JS_EXCEPTION;

    int32_t dk_len;
    if (JS_ToInt32(ctx, &dk_len, argv[4])) {
        JS_FreeCString(ctx, hash_algo);
        return JS_EXCEPTION;
    }

    mbedtls_md_type_t md_type;
    if (strcmp(hash_algo, "SHA-1") == 0)       md_type = MBEDTLS_MD_SHA1;
    else if (strcmp(hash_algo, "SHA-256") == 0) md_type = MBEDTLS_MD_SHA256;
    else if (strcmp(hash_algo, "SHA-384") == 0) md_type = MBEDTLS_MD_SHA384;
    else if (strcmp(hash_algo, "SHA-512") == 0) md_type = MBEDTLS_MD_SHA512;
    else {
        JS_FreeCString(ctx, hash_algo);
        return JS_ThrowTypeError(ctx, "nativePbkdf2: unsupported hash '%s'", hash_algo);
    }
    JS_FreeCString(ctx, hash_algo);

    const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(md_type);
    if (!md_info) return JS_ThrowTypeError(ctx, "nativePbkdf2: hash not available");

    uint8_t *out_buf = (uint8_t *)js_malloc(ctx, dk_len);
    if (!out_buf) return JS_ThrowOutOfMemory(ctx);

    if (mbedtls_pkcs5_pbkdf2_hmac_ext(md_type,
                                       password, password_len,
                                       salt, salt_len,
                                       iterations, dk_len,
                                       out_buf) != 0) {
        js_free(ctx, out_buf);
        return JS_ThrowTypeError(ctx, "nativePbkdf2: computation failed");
    }

    JSValue result = crypto_new_uint8array(ctx, out_buf, dk_len);
    js_free(ctx, out_buf);
    return result;
}

/* ================================================================
 * Extension hooks
 * ================================================================ */

static int crypto_ext_init(qwrt_ext_t *ext, qwrt_t *rt)
{
    JSContext *ctx = qwrt_get_jsctx(rt);
    if (!ctx) return -1;

    JSValue global = JS_GetGlobalObject(ctx);

    JSValue pal = JS_GetPropertyStr(ctx, global, "pal");
    if (JS_IsUndefined(pal) || JS_IsException(pal)) {
        JS_FreeValue(ctx, pal);
        pal = JS_GetPropertyStr(ctx, global, "__pal__");
    }

    if (JS_IsUndefined(pal) || JS_IsException(pal)) {
        JS_FreeValue(ctx, pal);
        JS_FreeValue(ctx, global);
        return -1;
    }

    JS_SetPropertyStr(ctx, pal, "nativeDigest",
        JS_NewCFunction(ctx, js_pal_native_digest, "nativeDigest", 2));
    JS_SetPropertyStr(ctx, pal, "nativeHmac",
        JS_NewCFunction(ctx, js_pal_native_hmac, "nativeHmac", 3));
    JS_SetPropertyStr(ctx, pal, "nativeAesEncrypt",
        JS_NewCFunction(ctx, js_pal_native_aes_encrypt, "nativeAesEncrypt", 6));
    JS_SetPropertyStr(ctx, pal, "nativeAesDecrypt",
        JS_NewCFunction(ctx, js_pal_native_aes_decrypt, "nativeAesDecrypt", 6));
    JS_SetPropertyStr(ctx, pal, "nativePbkdf2",
        JS_NewCFunction(ctx, js_pal_native_pbkdf2, "nativePbkdf2", 5));

    JS_FreeValue(ctx, pal);
    JS_FreeValue(ctx, global);

    (void)ext;
    return 0;
}

static void crypto_ext_destroy(qwrt_ext_t *ext, qwrt_t *rt)
{
    (void)ext; (void)rt;
}

static int crypto_ext_suspend(qwrt_ext_t *ext, qwrt_t *rt)
{
    (void)ext; (void)rt;
    return 0;
}

static int crypto_ext_resume(qwrt_ext_t *ext, qwrt_t *rt)
{
    (void)ext; (void)rt;
    return 0;
}

#endif /* QWRT_WITH_CRYPTO_EXT */

/* ================================================================
 * Extension definition
 * ================================================================ */

const qwrt_ext_t qwrt_crypto_ext = {
    .name = "crypto",
    .version = 1,
#ifdef QWRT_WITH_CRYPTO_EXT
    .init = crypto_ext_init,
    .destroy = crypto_ext_destroy,
    .suspend = crypto_ext_suspend,
    .resume = crypto_ext_resume,
#else
    .init = NULL,
    .destroy = NULL,
    .suspend = NULL,
    .resume = NULL,
#endif
    .user_data = NULL,
};

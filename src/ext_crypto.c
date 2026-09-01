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

#if QWRT_WITH_CRYPTO_EXT

#include <mbedtls/md.h>
#include <mbedtls/cipher.h>
#include <mbedtls/pkcs5.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/ecp.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/hkdf.h>
#include <mbedtls/nist_kw.h>
#include <string.h>
#include <stdlib.h>

/* ================================================================
 * Per-runtime RNG for EC key generation / signing.
 * The entropy/DRBG contexts live on qwrt_t (one per runtime) — qwrt runs
 * JS single-threaded per runtime, but multiple runtimes (e.g. workers on
 * their own threads) must not share one CTR_DRBG without synchronization,
 * so the old process-level singleton was a data race. Freed in
 * crypto_ext_destroy.
 * ================================================================ */

static mbedtls_ctr_drbg_context *ec_rng(JSContext *ctx)
{
    qwrt_t *rt = qwrt_get_rt_from_ctx(ctx);
    if (!rt) return NULL;
    if (!rt->ec_rng_ready) {
        if (!rt->ec_entropy)
            rt->ec_entropy = malloc(sizeof(mbedtls_entropy_context));
        if (!rt->ec_drbg)
            rt->ec_drbg = malloc(sizeof(mbedtls_ctr_drbg_context));
        if (!rt->ec_entropy || !rt->ec_drbg)
            return NULL;
        mbedtls_entropy_init((mbedtls_entropy_context *)rt->ec_entropy);
        mbedtls_ctr_drbg_init((mbedtls_ctr_drbg_context *)rt->ec_drbg);
        if (mbedtls_ctr_drbg_seed((mbedtls_ctr_drbg_context *)rt->ec_drbg,
                                  mbedtls_entropy_func,
                                  rt->ec_entropy, NULL, 0) != 0)
            return NULL;
        rt->ec_rng_ready = 1;
    }
    return (mbedtls_ctr_drbg_context *)rt->ec_drbg;
}

/* Map WebCrypto curve name to mbedTLS group + coordinate size. */
static int ec_curve_from_name(const char *name, mbedtls_ecp_group_id *gid,
                              size_t *coord_len)
{
    if (strcmp(name, "P-256") == 0) {
        *gid = MBEDTLS_ECP_DP_SECP256R1; *coord_len = 32;
    } else if (strcmp(name, "P-384") == 0) {
        *gid = MBEDTLS_ECP_DP_SECP384R1; *coord_len = 48;
    } else if (strcmp(name, "P-521") == 0) {
        *gid = MBEDTLS_ECP_DP_SECP521R1; *coord_len = 66;
    } else {
        return -1;
    }
    return 0;
}

/* Write big-endian mpi into out, zero-padded to exactly len bytes. */
static int mpi_write_padded(const mbedtls_mpi *x, uint8_t *out, size_t len)
{
    size_t sz = mbedtls_mpi_size(x);
    if (sz > len) return -1;
    memset(out, 0, len - sz);
    return mbedtls_mpi_write_binary(x, out + (len - sz), sz);
}


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
    if (!JS_IsUndefined(argv[5]) && !JS_IsNull(argv[5])) {
        if (JS_ToInt32(ctx, &tag_len, argv[5]) != 0) {
            return JS_ThrowTypeError(ctx, "nativeAes: invalid tagLen");
        }
    }
    if (tag_len < 1 || tag_len > 16) {
        return JS_ThrowTypeError(ctx, "nativeAes: tagLen must be in [1,16]");
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
 * pal.nativeHkdf(hashAlgo, ikm, salt, info, length) -> Uint8Array
 *
 * RFC 5869 HKDF. hashAlgo: "SHA-1".."SHA-512". length in bytes.
 * ================================================================ */

static JSValue js_pal_native_hkdf(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 5) {
        return JS_ThrowTypeError(ctx, "nativeHkdf requires 5 arguments");
    }

    const char *hash_algo = JS_ToCString(ctx, argv[0]);
    if (!hash_algo) return JS_EXCEPTION;

    const uint8_t *ikm, *salt = NULL, *info = NULL;
    size_t ikm_len, salt_len = 0, info_len = 0;
    int bad = crypto_extract_buffer(ctx, argv[1], &ikm, &ikm_len) < 0 ||
              crypto_extract_buffer(ctx, argv[2], &salt, &salt_len) < 0 ||
              crypto_extract_buffer(ctx, argv[3], &info, &info_len) < 0;
    JS_FreeCString(ctx, hash_algo);
    if (bad) {
        return JS_ThrowTypeError(ctx, "nativeHkdf: ikm/salt/info must be ArrayBuffer or Uint8Array");
    }

    mbedtls_md_type_t md_type;
    if (strcmp(hash_algo, "SHA-1") == 0)       md_type = MBEDTLS_MD_SHA1;
    else if (strcmp(hash_algo, "SHA-256") == 0) md_type = MBEDTLS_MD_SHA256;
    else if (strcmp(hash_algo, "SHA-384") == 0) md_type = MBEDTLS_MD_SHA384;
    else if (strcmp(hash_algo, "SHA-512") == 0) md_type = MBEDTLS_MD_SHA512;
    else {
        return JS_ThrowTypeError(ctx, "nativeHkdf: unsupported hash '%s'", hash_algo);
    }

    int32_t dk_len;
    if (JS_ToInt32(ctx, &dk_len, argv[4]) || dk_len < 0) {
        return JS_ThrowTypeError(ctx, "nativeHkdf: invalid length");
    }

    const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(md_type);
    if (!md_info) return JS_ThrowTypeError(ctx, "nativeHkdf: hash not available");

    uint8_t *out_buf = (uint8_t *)js_malloc(ctx, (size_t)dk_len);
    if (!out_buf) return JS_ThrowOutOfMemory(ctx);

    if (mbedtls_hkdf(md_info, salt, salt_len, ikm, ikm_len,
                     info, info_len, out_buf, (size_t)dk_len) != 0) {
        js_free(ctx, out_buf);
        return JS_ThrowTypeError(ctx, "nativeHkdf: computation failed");
    }

    JSValue result = crypto_new_uint8array(ctx, out_buf, (size_t)dk_len);
    js_free(ctx, out_buf);
    return result;
}

/* ================================================================
 * pal.nativeAesKw(key, data, wrap) -> Uint8Array
 *
 * RFC 3394 AES Key Wrap (nist_kw). key: 128/192/256-bit KEK;
 * data: key material to wrap (multiple of 8 bytes) / wrapped to unwrap.
 * ================================================================ */

static JSValue js_pal_native_aes_kw(JSContext *ctx, JSValueConst argv[],
                                    int wrap)
{
    const uint8_t *key, *data;
    size_t key_len, data_len;
    if (crypto_extract_buffer(ctx, argv[0], &key, &key_len) < 0 ||
        crypto_extract_buffer(ctx, argv[1], &data, &data_len) < 0) {
        return JS_ThrowTypeError(ctx, "nativeAesKw: key/data must be ArrayBuffer or Uint8Array");
    }

    if (key_len != 16 && key_len != 24 && key_len != 32) {
        return JS_ThrowTypeError(ctx, "nativeAesKw: invalid KEK length");
    }
    if (data_len < 16 || (data_len % 8) != 0) {
        return JS_ThrowTypeError(ctx, "nativeAesKw: data must be >=16 bytes, multiple of 8");
    }

    mbedtls_nist_kw_context kw;
    mbedtls_nist_kw_init(&kw);
    if (mbedtls_nist_kw_setkey(&kw, MBEDTLS_CIPHER_ID_AES,
                               key, (unsigned int)key_len * 8, wrap) != 0) {
        mbedtls_nist_kw_free(&kw);
        return JS_ThrowTypeError(ctx, "nativeAesKw: setkey failed");
    }

    uint8_t *out_buf = (uint8_t *)js_malloc(ctx, data_len + 8);
    if (!out_buf) {
        mbedtls_nist_kw_free(&kw);
        return JS_ThrowOutOfMemory(ctx);
    }
    size_t olen = 0;
    int ret = wrap
        ? mbedtls_nist_kw_wrap(&kw, MBEDTLS_KW_MODE_KW, data, data_len,
                               out_buf, &olen, data_len + 8)
        : mbedtls_nist_kw_unwrap(&kw, MBEDTLS_KW_MODE_KW, data, data_len,
                                 out_buf, &olen, data_len + 8);
    mbedtls_nist_kw_free(&kw);
    if (ret != 0) {
        js_free(ctx, out_buf);
        return JS_ThrowTypeError(ctx, wrap ? "nativeAesKw: wrap failed"
                                           : "nativeAesKw: unwrap failed (integrity check)");
    }

    JSValue result = crypto_new_uint8array(ctx, out_buf, olen);
    js_free(ctx, out_buf);
    return result;
}

static JSValue js_pal_native_aes_kw_wrap(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc;
    return js_pal_native_aes_kw(ctx, argv, 1);
}

static JSValue js_pal_native_aes_kw_unwrap(JSContext *ctx, JSValueConst this_val,
                                           int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc;
    return js_pal_native_aes_kw(ctx, argv, 0);
}

/* ================================================================
 * pal.nativeEcGenerate(curve) -> Uint8Array [ privLen(4BE) | priv | pub(65/97/133B) ]
 * ================================================================ */

static JSValue js_pal_native_ec_generate(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc;
    const char *curve = JS_ToCString(ctx, argv[0]);
    if (!curve) return JS_EXCEPTION;

    mbedtls_ecp_group_id gid;
    size_t coord_len;
    if (ec_curve_from_name(curve, &gid, &coord_len) != 0) {
        JS_FreeCString(ctx, curve);
        return JS_ThrowTypeError(ctx, "nativeEcGenerate: unsupported curve '%s'", curve);
    }
    JS_FreeCString(ctx, curve);

    mbedtls_ctr_drbg_context *rng = ec_rng(ctx);
    if (!rng) return JS_ThrowTypeError(ctx, "nativeEcGenerate: RNG init failed");

    mbedtls_ecp_group grp;
    mbedtls_mpi d;
    mbedtls_ecp_point Q;
    mbedtls_ecp_group_init(&grp);
    mbedtls_mpi_init(&d);
    mbedtls_ecp_point_init(&Q);

    JSValue result = JS_EXCEPTION;
    size_t pub_len = coord_len * 2 + 1;
    size_t priv_len = coord_len;
    uint8_t *buf = (uint8_t *)js_malloc(ctx, 4 + priv_len + pub_len);
    if (!buf) {
        result = JS_ThrowOutOfMemory(ctx);
        goto out;
    }

    if (mbedtls_ecp_group_load(&grp, gid) != 0 ||
        mbedtls_ecp_gen_keypair(&grp, &d, &Q,
                                mbedtls_ctr_drbg_random, rng) != 0) {
        result = JS_ThrowTypeError(ctx, "nativeEcGenerate: keygen failed");
        goto out;
    }

    buf[0] = (uint8_t)(priv_len >> 24); buf[1] = (uint8_t)(priv_len >> 16);
    buf[2] = (uint8_t)(priv_len >> 8);  buf[3] = (uint8_t)priv_len;
    if (mpi_write_padded(&d, buf + 4, priv_len) != 0 ||
        mbedtls_ecp_point_write_binary(&grp, &Q, MBEDTLS_ECP_PF_UNCOMPRESSED,
                                       &pub_len, buf + 4 + priv_len, pub_len) != 0) {
        result = JS_ThrowTypeError(ctx, "nativeEcGenerate: serialization failed");
        js_free(ctx, buf);
        goto out;
    }

    result = crypto_new_uint8array(ctx, buf, 4 + priv_len + pub_len);
    js_free(ctx, buf);
out:
    mbedtls_ecp_point_free(&Q);
    mbedtls_mpi_free(&d);
    mbedtls_ecp_group_free(&grp);
    return result;
}

/* ================================================================
 * pal.nativeEcdh(curve, privD, peerQ) -> Uint8Array (shared secret, x)
 *
 * RFC 7748-style: returns big-endian x coordinate as the secret.
 * ================================================================ */

static JSValue js_pal_native_ecdh(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc;
    const char *curve = JS_ToCString(ctx, argv[0]);
    if (!curve) return JS_EXCEPTION;

    mbedtls_ecp_group_id gid;
    size_t coord_len;
    if (ec_curve_from_name(curve, &gid, &coord_len) != 0) {
        JS_FreeCString(ctx, curve);
        return JS_ThrowTypeError(ctx, "nativeEcdh: unsupported curve '%s'", curve);
    }
    JS_FreeCString(ctx, curve);

    const uint8_t *d_bytes, *q_bytes;
    size_t d_len, q_len;
    if (crypto_extract_buffer(ctx, argv[1], &d_bytes, &d_len) < 0 ||
        crypto_extract_buffer(ctx, argv[2], &q_bytes, &q_len) < 0) {
        return JS_ThrowTypeError(ctx, "nativeEcdh: priv/peer must be ArrayBuffer or Uint8Array");
    }

    mbedtls_ctr_drbg_context *rng = ec_rng(ctx);
    if (!rng) return JS_ThrowTypeError(ctx, "nativeEcdh: RNG init failed");

    mbedtls_ecp_group grp;
    mbedtls_mpi d, z;
    mbedtls_ecp_point Q;
    mbedtls_ecp_group_init(&grp);
    mbedtls_mpi_init(&d);
    mbedtls_mpi_init(&z);
    mbedtls_ecp_point_init(&Q);

    JSValue result = JS_EXCEPTION;
    uint8_t *out_buf = NULL;

    if (mbedtls_ecp_group_load(&grp, gid) != 0 ||
        mbedtls_mpi_read_binary(&d, d_bytes, d_len) != 0 ||
        mbedtls_ecp_point_read_binary(&grp, &Q, q_bytes, q_len) != 0) {
        result = JS_ThrowTypeError(ctx, "nativeEcdh: invalid key data");
        goto out;
    }

    if (mbedtls_ecdh_compute_shared(&grp, &z, &Q, &d,
                                    mbedtls_ctr_drbg_random, rng) != 0) {
        result = JS_ThrowTypeError(ctx, "nativeEcdh: compute_shared failed");
        goto out;
    }

    out_buf = (uint8_t *)js_malloc(ctx, coord_len);
    if (!out_buf) {
        result = JS_ThrowOutOfMemory(ctx);
        goto out;
    }
    if (mpi_write_padded(&z, out_buf, coord_len) != 0) {
        result = JS_ThrowTypeError(ctx, "nativeEcdh: secret too large");
        goto out;
    }
    result = crypto_new_uint8array(ctx, out_buf, coord_len);

out:
    js_free(ctx, out_buf);
    mbedtls_ecp_point_free(&Q);
    mbedtls_mpi_free(&z);
    mbedtls_mpi_free(&d);
    mbedtls_ecp_group_free(&grp);
    return result;
}

/* ================================================================
 * pal.nativeEcdsaSign(hashAlgo, curve, privD, data) -> Uint8Array
 * pal.nativeEcdsaVerify(hashAlgo, curve, pubQ, sig, data) -> boolean
 *
 * ECDSA over P-256/P-384/P-521. Signature = raw r||s (padded), the
 * JS layer converts to/from JWS-style concat; digest computed here.
 * ================================================================ */

static JSValue js_pal_native_ecdsa(JSContext *ctx, JSValueConst argv[],
                                   int sign)
{
    const char *hash_algo = JS_ToCString(ctx, argv[0]);
    if (!hash_algo) return JS_EXCEPTION;

    mbedtls_md_type_t md_type;
    if (strcmp(hash_algo, "SHA-1") == 0)       md_type = MBEDTLS_MD_SHA1;
    else if (strcmp(hash_algo, "SHA-256") == 0) md_type = MBEDTLS_MD_SHA256;
    else if (strcmp(hash_algo, "SHA-384") == 0) md_type = MBEDTLS_MD_SHA384;
    else if (strcmp(hash_algo, "SHA-512") == 0) md_type = MBEDTLS_MD_SHA512;
    else {
        JS_FreeCString(ctx, hash_algo);
        return JS_ThrowTypeError(ctx, "nativeEcdsa: unsupported hash '%s'", hash_algo);
    }
    JS_FreeCString(ctx, hash_algo);

    const char *curve = JS_ToCString(ctx, argv[1]);
    if (!curve) return JS_EXCEPTION;
    mbedtls_ecp_group_id gid;
    size_t coord_len;
    int cres = ec_curve_from_name(curve, &gid, &coord_len);
    JS_FreeCString(ctx, curve);
    if (cres != 0) {
        return JS_ThrowTypeError(ctx, "nativeEcdsa: unsupported curve");
    }

    const uint8_t *key_bytes, *data_bytes, *sig_bytes = NULL;
    size_t key_len, data_len, sig_len = 0;
    if (crypto_extract_buffer(ctx, argv[2], &key_bytes, &key_len) < 0 ||
        crypto_extract_buffer(ctx, argv[sign ? 3 : 4], &data_bytes, &data_len) < 0 ||
        (!sign && crypto_extract_buffer(ctx, argv[3], &sig_bytes, &sig_len) < 0)) {
        return JS_ThrowTypeError(ctx, "nativeEcdsa: buffers required");
    }

    mbedtls_ctr_drbg_context *rng = ec_rng(ctx);
    if (!rng) return JS_ThrowTypeError(ctx, "nativeEcdsa: RNG init failed");

    mbedtls_ecp_group grp;
    mbedtls_mpi d, r, s;
    mbedtls_ecp_point Q;
    mbedtls_ecp_group_init(&grp);
    mbedtls_mpi_init(&d); mbedtls_mpi_init(&r); mbedtls_mpi_init(&s);
    mbedtls_ecp_point_init(&Q);

    JSValue result = JS_EXCEPTION;
    const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(md_type);
    uint8_t hash[MBEDTLS_MD_MAX_SIZE];
    if (!md_info || mbedtls_md(md_info, data_bytes, data_len, hash) != 0) {
        result = JS_ThrowTypeError(ctx, "nativeEcdsa: digest failed");
        goto out;
    }

    if (mbedtls_ecp_group_load(&grp, gid) != 0) {
        result = JS_ThrowTypeError(ctx, "nativeEcdsa: group load failed");
        goto out;
    }

    if (sign) {
        if (mbedtls_mpi_read_binary(&d, key_bytes, key_len) != 0 ||
            mbedtls_ecp_mul(&grp, &Q, &d, &grp.G, mbedtls_ctr_drbg_random, rng) != 0 ||
            mbedtls_ecdsa_sign_det_ext(&grp, &r, &s, &d, hash,
                                       mbedtls_md_get_size(md_info), md_type,
                                       mbedtls_ctr_drbg_random, rng) != 0) {
            result = JS_ThrowTypeError(ctx, "nativeEcdsa: sign failed");
            goto out;
        }
        uint8_t *sig_buf = (uint8_t *)js_malloc(ctx, coord_len * 2);
        if (!sig_buf) {
            result = JS_ThrowOutOfMemory(ctx);
            goto out;
        }
        if (mpi_write_padded(&r, sig_buf, coord_len) != 0 ||
            mpi_write_padded(&s, sig_buf + coord_len, coord_len) != 0) {
            js_free(ctx, sig_buf);
            result = JS_ThrowTypeError(ctx, "nativeEcdsa: sig serialization failed");
            goto out;
        }
        result = crypto_new_uint8array(ctx, sig_buf, coord_len * 2);
        js_free(ctx, sig_buf);
    } else {
        if (key_len != coord_len * 2 + 1 || sig_len != coord_len * 2 ||
            mbedtls_ecp_point_read_binary(&grp, &Q, key_bytes, key_len) != 0 ||
            mbedtls_mpi_read_binary(&r, sig_bytes, coord_len) != 0 ||
            mbedtls_mpi_read_binary(&s, sig_bytes + coord_len, coord_len) != 0) {
            result = JS_ThrowTypeError(ctx, "nativeEcdsa: invalid pubkey/signature");
            goto out;
        }
        if (mbedtls_ecdsa_verify(&grp, hash, mbedtls_md_get_size(md_info), &Q, &r, &s) != 0) {
            result = JS_FALSE;
            goto out;
        }
        result = JS_TRUE;
    }

out:
    mbedtls_ecp_point_free(&Q);
    mbedtls_mpi_free(&r); mbedtls_mpi_free(&s); mbedtls_mpi_free(&d);
    mbedtls_ecp_group_free(&grp);
    return result;
}

static JSValue js_pal_native_ecdsa_sign(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc;
    return js_pal_native_ecdsa(ctx, argv, 1);
}

static JSValue js_pal_native_ecdsa_verify(JSContext *ctx, JSValueConst this_val,
                                          int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc;
    return js_pal_native_ecdsa(ctx, argv, 0);
}

/* ================================================================
 * Extension hooks
 * ================================================================ */

static int crypto_ext_init(qwrt_ext_t *ext, qwrt_t *rt)
{
    JSContext *ctx = qwrt_get_active_jsctx(rt);
    if (!ctx) return -1;

    JSValue global = JS_GetGlobalObject(ctx);

    JSValue pal = JS_GetPropertyStr(ctx, global, "pal");
    if (JS_IsUndefined(pal) || JS_IsException(pal)) {
        JS_FreeValue(ctx, pal);
        pal = JS_GetPropertyStr(ctx, global, "__native__");
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
    JS_SetPropertyStr(ctx, pal, "nativeHkdf",
        JS_NewCFunction(ctx, js_pal_native_hkdf, "nativeHkdf", 5));
    JS_SetPropertyStr(ctx, pal, "nativeAesKwWrap",
        JS_NewCFunction(ctx, js_pal_native_aes_kw_wrap, "nativeAesKwWrap", 2));
    JS_SetPropertyStr(ctx, pal, "nativeAesKwUnwrap",
        JS_NewCFunction(ctx, js_pal_native_aes_kw_unwrap, "nativeAesKwUnwrap", 2));
    JS_SetPropertyStr(ctx, pal, "nativeEcGenerate",
        JS_NewCFunction(ctx, js_pal_native_ec_generate, "nativeEcGenerate", 1));
    JS_SetPropertyStr(ctx, pal, "nativeEcdh",
        JS_NewCFunction(ctx, js_pal_native_ecdh, "nativeEcdh", 3));
    JS_SetPropertyStr(ctx, pal, "nativeEcdsaSign",
        JS_NewCFunction(ctx, js_pal_native_ecdsa_sign, "nativeEcdsaSign", 4));
    JS_SetPropertyStr(ctx, pal, "nativeEcdsaVerify",
        JS_NewCFunction(ctx, js_pal_native_ecdsa_verify, "nativeEcdsaVerify", 5));
    /* Install crypto.subtle / CryptoKey on globalThis.crypto. The polyfill
     * exposes this as pal.__installCryptoSubtle__ but does NOT call it — so
     * when this extension is not compiled in, crypto.subtle stays undefined
     * rather than being a shim that always rejects. Called here (after the
     * native hooks are registered) so the SubtleCrypto methods can bind to
     * them at call time. */
    JSValue installer = JS_GetPropertyStr(ctx, pal, "__installCryptoSubtle__");
    if (JS_IsFunction(ctx, installer)) {
        JSValue ret = JS_Call(ctx, installer, JS_UNDEFINED, 0, NULL);
        JS_FreeValue(ctx, ret);
    }
    JS_FreeValue(ctx, installer);

    JS_FreeValue(ctx, pal);
    JS_FreeValue(ctx, global);

    (void)ext;
    return 0;
}

static void crypto_ext_destroy(qwrt_ext_t *ext, qwrt_t *rt)
{
    (void)ext;
    /* Release the per-runtime EC RNG (if it was ever seeded). */
    if (rt->ec_entropy) {
        mbedtls_entropy_free((mbedtls_entropy_context *)rt->ec_entropy);
        free(rt->ec_entropy);
        rt->ec_entropy = NULL;
    }
    if (rt->ec_drbg) {
        mbedtls_ctr_drbg_free((mbedtls_ctr_drbg_context *)rt->ec_drbg);
        free(rt->ec_drbg);
        rt->ec_drbg = NULL;
    }
    rt->ec_rng_ready = 0;
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
#if QWRT_WITH_CRYPTO_EXT
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

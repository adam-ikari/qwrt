#ifndef QWRT_EXT_CRYPTO_H
#define QWRT_EXT_CRYPTO_H

#include "qwrt/qwrt.h"

/* Crypto extension — native SHA/HMAC/AES/PBKDF2 via mbedTLS.
 *
 * When compiled with QWRT_WITH_CRYPTO_EXT, registers pal.nativeDigest,
 * pal.nativeHmac, pal.nativeAesEncrypt, pal.nativeAesDecrypt, and
 * pal.nativePbkdf2 on the JS pal object, enabling crypto.subtle to
 * use native implementations instead of pure JS.
 *
 * When not compiled, the extension is inert — crypto.subtle falls
 * back to its JS implementation.
 *
 * To use: add &qwrt_crypto_ext to config.extensions array,
 * or it will be auto-registered when QWRT_WITH_CRYPTO_EXT is defined.
 */

extern const qwrt_ext_t qwrt_crypto_ext;

#endif /* QWRT_EXT_CRYPTO_H */

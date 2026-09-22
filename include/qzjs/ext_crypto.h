#ifndef QZ_EXT_CRYPTO_H
#define QZ_EXT_CRYPTO_H

#include "qzjs/qzjs.h"

/* Crypto extension — native SHA/HMAC/AES/PBKDF2 via mbedTLS.
 *
 * When compiled with QZ_WITH_CRYPTO_EXT, registers pal.nativeDigest,
 * pal.nativeHmac, pal.nativeAesEncrypt, pal.nativeAesDecrypt, and
 * pal.nativePbkdf2 on the JS pal object, enabling crypto.subtle to
 * use native implementations instead of pure JS.
 *
 * When not compiled, the extension is inert — crypto.subtle falls
 * back to its JS implementation.
 *
 * Registered automatically when QZ_WITH_CRYPTO_EXT is on (it's in the
 * default QZ_EXTENSIONS set; see qz_ext_registry.h). No runtime
 * registration.
 */

extern const qz_ext_t qz_crypto_ext;

#endif /* QZ_EXT_CRYPTO_H */

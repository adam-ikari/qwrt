#ifndef AM_EXT_CRYPTO_H
#define AM_EXT_CRYPTO_H

#include "amoib/amoib.h"

/* Crypto extension — native SHA/HMAC/AES/PBKDF2 via mbedTLS.
 *
 * When compiled with AM_WITH_CRYPTO_EXT, registers pal.nativeDigest,
 * pal.nativeHmac, pal.nativeAesEncrypt, pal.nativeAesDecrypt, and
 * pal.nativePbkdf2 on the JS pal object, enabling crypto.subtle to
 * use native implementations instead of pure JS.
 *
 * When not compiled, the extension is inert — crypto.subtle falls
 * back to its JS implementation.
 *
 * Registered automatically when AM_WITH_CRYPTO_EXT is on (it's in the
 * default AM_EXTENSIONS set; see am_ext_registry.h). No runtime
 * registration.
 */

extern const am_ext_t am_crypto_ext;

#endif /* AM_EXT_CRYPTO_H */

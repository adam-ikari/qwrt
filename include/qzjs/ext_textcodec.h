#ifndef QZ_EXT_TEXTCODEC_H
#define QZ_EXT_TEXTCODEC_H

#include "qzjs/qzjs.h"

/* TextCodec extension — native UTF-8 encode/decode and Base64.
 *
 * When compiled with QZ_WITH_TEXTCODEC, registers pal.nativeEncodeUtf8,
 * pal.nativeBtoa, and pal.nativeAtob on the JS pal
 * object, enabling TextEncoder/TextDecoder and atob/btoa to use native
 * implementations instead of pure JS.
 *
 * When not compiled, the extension is inert — the JS polyfill fallbacks
 * are used instead.
 */

extern const qz_ext_t qz_textcodec_ext;

#endif /* QZ_EXT_TEXTCODEC_H */

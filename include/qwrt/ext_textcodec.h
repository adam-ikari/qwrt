#ifndef QWRT_EXT_TEXTCODEC_H
#define QWRT_EXT_TEXTCODEC_H

#include "qwrt/qwrt.h"

/* TextCodec extension — native UTF-8 encode/decode and Base64.
 *
 * When compiled with QWRT_WITH_TEXTCODEC, registers pal.nativeEncodeUtf8,
 * pal.nativeDecodeUtf8, pal.nativeBtoa, and pal.nativeAtob on the JS pal
 * object, enabling TextEncoder/TextDecoder and atob/btoa to use native
 * implementations instead of pure JS.
 *
 * When not compiled, the extension is inert — the JS polyfill fallbacks
 * are used instead.
 */

extern const qwrt_ext_t qwrt_textcodec_ext;

#endif /* QWRT_EXT_TEXTCODEC_H */

#ifndef AM_EXT_TEXTCODEC_H
#define AM_EXT_TEXTCODEC_H

#include "amoib/amoib.h"

/* TextCodec extension — native UTF-8 encode/decode and Base64.
 *
 * When compiled with AM_WITH_TEXTCODEC, registers pal.nativeEncodeUtf8,
 * pal.nativeBtoa, and pal.nativeAtob on the JS pal
 * object, enabling TextEncoder/TextDecoder and atob/btoa to use native
 * implementations instead of pure JS.
 *
 * When not compiled, the extension is inert — the JS polyfill fallbacks
 * are used instead.
 */

extern const am_ext_t am_textcodec_ext;

#endif /* AM_EXT_TEXTCODEC_H */

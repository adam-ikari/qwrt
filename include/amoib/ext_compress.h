#ifndef AM_EXT_COMPRESS_H
#define AM_EXT_COMPRESS_H

#include "amoib/amoib.h"

/* Compression extension — native DEFLATE/gzip via miniz.
 *
 * When compiled with AM_WITH_COMPRESS, registers pal.nativeCompress
 * and pal.nativeDecompress on the JS pal object, enabling
 * CompressionStream and DecompressionStream in the polyfill.
 *
 * When not compiled, the extension is inert — no compression
 * functions are registered and CompressionStream/DecompressionStream
 * will throw "Compression extension not available".
 *
 * Registered automatically when AM_WITH_COMPRESS is on (it's in the default
 * AM_EXTENSIONS set; see am_ext_registry.h). No runtime registration.
 */

extern const am_ext_t am_compress_ext;

#endif /* AM_EXT_COMPRESS_H */

#ifndef QZ_EXT_COMPRESS_H
#define QZ_EXT_COMPRESS_H

#include "qzjs/qzjs.h"

/* Compression extension — native DEFLATE/gzip via miniz.
 *
 * When compiled with QZ_WITH_COMPRESS, registers pal.nativeCompress
 * and pal.nativeDecompress on the JS pal object, enabling
 * CompressionStream and DecompressionStream in the polyfill.
 *
 * When not compiled, the extension is inert — no compression
 * functions are registered and CompressionStream/DecompressionStream
 * will throw "Compression extension not available".
 *
 * Registered automatically when QZ_WITH_COMPRESS is on (it's in the default
 * QZ_EXTENSIONS set; see qz_ext_registry.h). No runtime registration.
 */

extern const qz_ext_t qz_compress_ext;

#endif /* QZ_EXT_COMPRESS_H */

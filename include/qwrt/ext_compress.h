#ifndef QWRT_EXT_COMPRESS_H
#define QWRT_EXT_COMPRESS_H

#include "qwrt/qwrt.h"

/* Compression extension — native DEFLATE/gzip via miniz.
 *
 * When compiled with QWRT_WITH_COMPRESS, registers pal.nativeCompress
 * and pal.nativeDecompress on the JS pal object, enabling
 * CompressionStream and DecompressionStream in the polyfill.
 *
 * When not compiled, the extension is inert — no compression
 * functions are registered and CompressionStream/DecompressionStream
 * will throw "Compression extension not available".
 *
 * Registered automatically when QWRT_WITH_COMPRESS is on (it's in the default
 * QWRT_EXTENSIONS set; see qwrt_ext_registry.h). No runtime registration.
 */

extern const qwrt_ext_t qwrt_compress_ext;

#endif /* QWRT_EXT_COMPRESS_H */
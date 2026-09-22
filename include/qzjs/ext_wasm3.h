#ifndef QZ_EXT_WASM3_H
#define QZ_EXT_WASM3_H

#include "qzjs/qzjs.h"

/* wasm3 extension — enables WebAssembly API with pure sandbox model.
 *
 * WASM modules have NO access to system APIs: no filesystem, no network,
 * no host functions. Only pure computation + linear memory.
 *
 * Purpose: CPU-intensive compute acceleration (crypto, compression,
 * image processing, math).
 *
 * wasm3 is the WASM engine (QZ_WITH_WASM3, default ON). When compiled in,
 * it's in the default QZ_EXTENSIONS set (see qz_ext_registry.h), so
 * WebAssembly is available out of the box. Without wasm3 compiled in, the
 * WebAssembly global is not available.
 */
extern const qz_ext_t qz_wasm3_ext;

#endif /* QZ_EXT_WASM3_H */

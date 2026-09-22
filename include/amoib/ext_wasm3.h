#ifndef AM_EXT_WASM3_H
#define AM_EXT_WASM3_H

#include "amoib/amoib.h"

/* wasm3 extension — enables WebAssembly API with pure sandbox model.
 *
 * WASM modules have NO access to system APIs: no filesystem, no network,
 * no host functions. Only pure computation + linear memory.
 *
 * Purpose: CPU-intensive compute acceleration (crypto, compression,
 * image processing, math).
 *
 * wasm3 is the WASM engine (AM_WITH_WASM3, default ON). When compiled in,
 * it's in the default AM_EXTENSIONS set (see am_ext_registry.h), so
 * WebAssembly is available out of the box. Without wasm3 compiled in, the
 * WebAssembly global is not available.
 */
extern const am_ext_t am_wasm3_ext;

#endif /* AM_EXT_WASM3_H */

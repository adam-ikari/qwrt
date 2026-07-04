#ifndef QWRT_EXT_WASM3_H
#define QWRT_EXT_WASM3_H

#include "qwrt/qwrt.h"

/* wasm3 extension — enables WebAssembly API with pure sandbox model.
 *
 * WASM modules have NO access to system APIs: no filesystem, no network,
 * no host functions. Only pure computation + linear memory.
 *
 * Purpose: CPU-intensive compute acceleration (crypto, compression,
 * image processing, math).
 *
 * Include this extension in qwrt_config_t.extensions to grant
 * WebAssembly access to a context. Without this extension (or ext_wamr),
 * the WebAssembly global is not available.
 *
 * Current status: provides JS API surface. WASM engine integration
 * pending — throws "engine not linked" until wasm3 is linked.
 *
 * Usage:
 *   const qwrt_ext_t *exts[] = { &qwrt_wasm3_ext, NULL };
 *   config.extensions = exts;
 */
extern const qwrt_ext_t qwrt_wasm3_ext;

#endif /* QWRT_EXT_WASM3_H */

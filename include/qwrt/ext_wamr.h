#ifndef QWRT_EXT_WAMR_H
#define QWRT_EXT_WAMR_H

#include "qwrt/qwrt.h"

/* WAMR extension — enables WebAssembly API via WAMR engine (pure sandbox).
 *
 * WASM modules have NO access to system APIs: no filesystem, no network,
 * no host functions. Only pure computation + linear memory.
 *
 * Purpose: CPU-intensive compute acceleration using the WAMR engine
 * (supports AOT compilation for better performance than wasm3).
 *
 * Include this extension in qwrt_config_t.extensions to grant
 * WebAssembly access to a context. Without this extension (or ext_wasm3),
 * the WebAssembly global is not available.
 *
 * Current status: provides JS API surface. WASM engine integration
 * pending — throws "engine not linked" until WAMR is linked.
 *
 * Usage:
 *   const qwrt_ext_t *exts[] = { &qwrt_wamr_ext, NULL };
 *   config.extensions = exts;
 */
extern const qwrt_ext_t qwrt_wamr_ext;

#endif /* QWRT_EXT_WAMR_H */

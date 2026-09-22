#ifndef QZ_EXT_WAMR_H
#define QZ_EXT_WAMR_H

#include "qzjs/qzjs.h"

/* WAMR extension — enables WebAssembly API via WAMR engine (pure sandbox).
 *
 * WASM modules have NO access to system APIs: no filesystem, no network,
 * no host functions. Only pure computation + linear memory.
 *
 * Purpose: CPU-intensive compute acceleration using the WAMR engine
 * (supports AOT compilation for better performance than wasm3).
 *
 * WAMR is the default WASM engine (QZ_WITH_WAMR, default ON); it's in the
 * default QZ_EXTENSIONS set (see qz_ext_registry.h), so WebAssembly is
 * available out of the box. No runtime registration. Without WAMR or wasm3
 * compiled in, the WebAssembly global is not available.
 *
 * Note: pinned to WAMR-1.3.3, WebAssembly.Instance.exports is left empty
 * (1.3.3 has no export-enumeration API). See ext_wamr.c.
 */
extern const qz_ext_t qz_wamr_ext;

#endif /* QZ_EXT_WAMR_H */

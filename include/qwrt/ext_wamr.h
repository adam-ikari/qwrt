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
 * WAMR is the default WASM engine (QWRT_WITH_WAMR, default ON); it's in the
 * default QWRT_EXTENSIONS set (see qwrt_ext_registry.h), so WebAssembly is
 * available out of the box. No runtime registration. Without WAMR or wasm3
 * compiled in, the WebAssembly global is not available.
 *
 * Note: pinned to WAMR-1.3.3, WebAssembly.Instance.exports is left empty
 * (1.3.3 has no export-enumeration API). See ext_wamr.c.
 */
extern const qwrt_ext_t qwrt_wamr_ext;

#endif /* QWRT_EXT_WAMR_H */

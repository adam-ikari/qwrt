/*
 * pal_wasm.h — WASM/Emscripten PAL for qwrt
 *
 * Creates a PAL backed by browser Web APIs when qwrt runs in WebAssembly.
 *
 *   #include <pal_wasm.h>
 *   qwrt_pal_t *pal = pal_wasm_create();
 *   qwrt_t *rt = qwrt_create(&(qwrt_config_t){ .pal = pal });
 */

#ifndef QWRT_PAL_WASM_H
#define QWRT_PAL_WASM_H

#include <qwrt/qwrt.h>

#ifdef __EMSCRIPTEN__

qwrt_pal_t *pal_wasm_create(void);
void pal_wasm_destroy_pal(qwrt_pal_t *pal);

#endif /* __EMSCRIPTEN__ */

#endif /* QWRT_PAL_WASM_H */
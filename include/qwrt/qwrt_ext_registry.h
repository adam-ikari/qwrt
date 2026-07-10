/*
 * qwrt extension registry - the single compile-time control point for which
 * extensions a qwrt build registers.
 *
 * Build-time selection (no runtime registration, no config.extensions field):
 *   - QWRT_EXTENSIONS expands to a comma-separated list of `const qwrt_ext_t *`
 *     (a NULL slot is a disabled built-in, skipped at init). Order = register
 *     order. Trailing comma optional (C99 allows it).
 *   - A parent project overrides it non-invasively via CMake:
 *       set(QWRT_EXTENSIONS "QWRT_DEFAULT_EXTENSIONS, &my_foo_ext")
 *       add_subdirectory(deps/qwrt)
 *     or on the command line: -DQWRT_EXTENSIONS="&qwrt_compress_ext".
 *   - QWRT_WITH_* (CMake) controls whether an extension's CODE is compiled into
 *     the library; QWRT_EXTENSIONS controls which compiled-in extensions are
 *     REGISTERED. The conditional macro below emits a NULL slot for a built-in
 *     whose QWRT_WITH_* is off, so the default set never references an
 *     undefined symbol.
 *
 * Adding a user extension non-invasively: compile its source into the qwrt
 * target (CMake QWRT_EXTRA_SOURCES) so &my_foo_ext is visible to context.c,
 * and list it in QWRT_EXTENSIONS.
 */
#ifndef QWRT_EXT_REGISTRY_H
#define QWRT_EXT_REGISTRY_H

#include <qwrt/qwrt.h>
#include <qwrt/ext_compress.h>
#include <qwrt/ext_crypto.h>
#include <qwrt/ext_textcodec.h>
#include <qwrt/ext_wamr.h>
#include <qwrt/ext_wasm3.h>

/* QWRT_EXT_IF_WITH(FEATURE, ptr): when QWRT_WITH_<FEATURE> == 1, expands to
 * "ptr,"; when 0, expands to "NULL," (a disabled-slot placeholder). It always
 * emits "non-empty + comma" - never an empty token - because a ", ," (empty
 * element) in a C99 array initializer is a compile error, while a trailing
 * comma is legal. Disabled built-ins thus become NULL slots that
 * qwrt_ext_init_all skips.
 *
 * Three levels of indirection are required so the FEATURE name expands to the
 * VALUE of QWRT_WITH_<FEATURE> (0/1) BEFORE token-pasting into
 * QWRT_EXT_IF_WITH0/1 - two levels fail because ## suppresses argument
 * expansion. (Prototype-verified under -std=c99 -Wall -Wextra -Werror.) */
#define QWRT_EXT_IF_WITH0(ptr)   NULL,
#define QWRT_EXT_IF_WITH1(ptr)   ptr,
#define QWRT_EXT_IF_WITH_3(bit, ptr) QWRT_EXT_IF_WITH##bit(ptr)
#define QWRT_EXT_IF_WITH_2(bit, ptr) QWRT_EXT_IF_WITH_3(bit, ptr)
#define QWRT_EXT_IF_WITH_1(feature, ptr) QWRT_EXT_IF_WITH_2(QWRT_WITH_##feature, ptr)
#define QWRT_EXT_IF_WITH(feature, ptr) QWRT_EXT_IF_WITH_1(feature, ptr)

/* qwrt's built-in default set. Whichever WASM engine is compiled in (WAMR by
 * default, or wasm3 when QWRT_WITH_WASM3=ON / QWRT_WITH_WAMR=OFF) is listed
 * here; the two are mutually exclusive at the CMake level. */
#define QWRT_DEFAULT_EXTENSIONS \
    QWRT_EXT_IF_WITH(COMPRESS,   &qwrt_compress_ext) \
    QWRT_EXT_IF_WITH(CRYPTO_EXT, &qwrt_crypto_ext)   \
    QWRT_EXT_IF_WITH(TEXTCODEC,  &qwrt_textcodec_ext) \
    QWRT_EXT_IF_WITH(WAMR,       &qwrt_wamr_ext)      \
    QWRT_EXT_IF_WITH(WASM3,      &qwrt_wasm3_ext)

/* The effective extension set. Overridable by:
 *   (a) parent-project CMake variable (recommended, non-invasive);
 *   (b) a translation-unit #define before including this header.
 * If undefined, the default set is used. Register order = expansion order. */
#ifndef QWRT_EXTENSIONS
#  define QWRT_EXTENSIONS QWRT_DEFAULT_EXTENSIONS
#endif

#endif /* QWRT_EXT_REGISTRY_H */

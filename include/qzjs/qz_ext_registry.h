/*
 * qzjs extension registry - the single compile-time control point for which
 * extensions a qzjs build registers.
 *
 * Build-time selection (no runtime registration, no config.extensions field):
 *   - QZ_EXTENSIONS expands to a comma-separated list of `const qz_ext_t *`
 *     (a NULL slot is a disabled built-in, skipped at init). Order = register
 *     order. Trailing comma optional (C99 allows it).
 *   - QZ_DEFAULT_EXTENSIONS itself ends in a trailing comma, so append with a
 *     SPACE, never a comma — "QZ_DEFAULT_EXTENSIONS, &my_foo_ext" expands to
 *     "ptr, , &my_foo_ext", an empty array element and a C99 compile error.
 *   - A parent project overrides it non-invasively via CMake:
 *       set(QZ_EXTENSIONS "QZ_DEFAULT_EXTENSIONS &my_foo_ext")
 *       add_subdirectory(deps/qzjs)
 *     or on the command line: -DQZ_EXTENSIONS="&qz_compress_ext".
 *   - QZ_WITH_* (CMake) controls whether an extension's CODE is compiled into
 *     the library; QZ_EXTENSIONS controls which compiled-in extensions are
 *     REGISTERED. The conditional macro below emits a NULL slot for a built-in
 *     whose QZ_WITH_* is off, so the default set never references an
 *     undefined symbol.
 *
 * Adding a user extension non-invasively: compile its source into the qzjs
 * target (CMake QZ_EXTRA_SOURCES) so &my_foo_ext is visible to context.c,
 * and list it in QZ_EXTENSIONS.
 */
#ifndef QZ_EXT_REGISTRY_H
#define QZ_EXT_REGISTRY_H

#include <qzjs/qzjs.h>
#include <qzjs/ext_compress.h>
#include <qzjs/ext_crypto.h>
#include <qzjs/ext_textcodec.h>
#include <qzjs/ext_wamr.h>
#include <qzjs/ext_wasm3.h>

/* QZ_EXT_IF_WITH(FEATURE, ptr): when QZ_WITH_<FEATURE> == 1, expands to
 * "ptr,"; when 0, expands to "NULL," (a disabled-slot placeholder). It always
 * emits "non-empty + comma" - never an empty token - because a ", ," (empty
 * element) in a C99 array initializer is a compile error, while a trailing
 * comma is legal. Disabled built-ins thus become NULL slots that
 * qz_ext_init_all skips.
 *
 * Three levels of indirection are required so the FEATURE name expands to the
 * VALUE of QZ_WITH_<FEATURE> (0/1) BEFORE token-pasting into
 * QZ_EXT_IF_WITH0/1 - two levels fail because ## suppresses argument
 * expansion. (Prototype-verified under -std=c99 -Wall -Wextra -Werror.) */
#define QZ_EXT_IF_WITH0(ptr)   NULL,
#define QZ_EXT_IF_WITH1(ptr)   ptr,
#define QZ_EXT_IF_WITH_3(bit, ptr) QZ_EXT_IF_WITH##bit(ptr)
#define QZ_EXT_IF_WITH_2(bit, ptr) QZ_EXT_IF_WITH_3(bit, ptr)
#define QZ_EXT_IF_WITH_1(feature, ptr) QZ_EXT_IF_WITH_2(QZ_WITH_##feature, ptr)
#define QZ_EXT_IF_WITH(feature, ptr) QZ_EXT_IF_WITH_1(feature, ptr)

/* qzjs's built-in default set. wamr is the default WASM engine (Fast Interp +
 * AOT); wasm3 is the alternative (CMake enforces QZ_WITH_WAMR and
 * QZ_WITH_WASM3 are mutually exclusive, so exactly one slot below is
 * non-NULL — whichever engine the build selected gets registered). */
#define QZ_DEFAULT_EXTENSIONS \
    QZ_EXT_IF_WITH(COMPRESS,   &qz_compress_ext) \
    QZ_EXT_IF_WITH(CRYPTO_EXT, &qz_crypto_ext)   \
    QZ_EXT_IF_WITH(TEXTCODEC,  &qz_textcodec_ext) \
    QZ_EXT_IF_WITH(WAMR,       &qz_wamr_ext) \
    QZ_EXT_IF_WITH(WASM3,      &qz_wasm3_ext)

/* The effective extension set. Overridable by:
 *   (a) parent-project CMake variable (recommended, non-invasive);
 *   (b) a translation-unit #define before including this header.
 * If undefined, the default set is used. Register order = expansion order. */
#ifndef QZ_EXTENSIONS
#  define QZ_EXTENSIONS QZ_DEFAULT_EXTENSIONS
#endif

#endif /* QZ_EXT_REGISTRY_H */

/*
 * amoib extension registry - the single compile-time control point for which
 * extensions a amoib build registers.
 *
 * Build-time selection (no runtime registration, no config.extensions field):
 *   - AM_EXTENSIONS expands to a comma-separated list of `const am_ext_t *`
 *     (a NULL slot is a disabled built-in, skipped at init). Order = register
 *     order. Trailing comma optional (C99 allows it).
 *   - A parent project overrides it non-invasively via CMake:
 *       set(AM_EXTENSIONS "AM_DEFAULT_EXTENSIONS, &my_foo_ext")
 *       add_subdirectory(deps/amoib)
 *     or on the command line: -DAM_EXTENSIONS="&am_compress_ext".
 *   - AM_WITH_* (CMake) controls whether an extension's CODE is compiled into
 *     the library; AM_EXTENSIONS controls which compiled-in extensions are
 *     REGISTERED. The conditional macro below emits a NULL slot for a built-in
 *     whose AM_WITH_* is off, so the default set never references an
 *     undefined symbol.
 *
 * Adding a user extension non-invasively: compile its source into the amoib
 * target (CMake AM_EXTRA_SOURCES) so &my_foo_ext is visible to context.c,
 * and list it in AM_EXTENSIONS.
 */
#ifndef AM_EXT_REGISTRY_H
#define AM_EXT_REGISTRY_H

#include <amoib/amoib.h>
#include <amoib/ext_compress.h>
#include <amoib/ext_crypto.h>
#include <amoib/ext_textcodec.h>
#include <amoib/ext_wamr.h>
#include <amoib/ext_wasm3.h>

/* AM_EXT_IF_WITH(FEATURE, ptr): when AM_WITH_<FEATURE> == 1, expands to
 * "ptr,"; when 0, expands to "NULL," (a disabled-slot placeholder). It always
 * emits "non-empty + comma" - never an empty token - because a ", ," (empty
 * element) in a C99 array initializer is a compile error, while a trailing
 * comma is legal. Disabled built-ins thus become NULL slots that
 * am_ext_init_all skips.
 *
 * Three levels of indirection are required so the FEATURE name expands to the
 * VALUE of AM_WITH_<FEATURE> (0/1) BEFORE token-pasting into
 * AM_EXT_IF_WITH0/1 - two levels fail because ## suppresses argument
 * expansion. (Prototype-verified under -std=c99 -Wall -Wextra -Werror.) */
#define AM_EXT_IF_WITH0(ptr)   NULL,
#define AM_EXT_IF_WITH1(ptr)   ptr,
#define AM_EXT_IF_WITH_3(bit, ptr) AM_EXT_IF_WITH##bit(ptr)
#define AM_EXT_IF_WITH_2(bit, ptr) AM_EXT_IF_WITH_3(bit, ptr)
#define AM_EXT_IF_WITH_1(feature, ptr) AM_EXT_IF_WITH_2(AM_WITH_##feature, ptr)
#define AM_EXT_IF_WITH(feature, ptr) AM_EXT_IF_WITH_1(feature, ptr)

/* amoib's built-in default set. wamr is the default WASM engine (Fast Interp +
 * AOT); wasm3 is the alternative (CMake enforces AM_WITH_WAMR and
 * AM_WITH_WASM3 are mutually exclusive, so exactly one slot below is
 * non-NULL — whichever engine the build selected gets registered). */
#define AM_DEFAULT_EXTENSIONS \
    AM_EXT_IF_WITH(COMPRESS,   &am_compress_ext) \
    AM_EXT_IF_WITH(CRYPTO_EXT, &am_crypto_ext)   \
    AM_EXT_IF_WITH(TEXTCODEC,  &am_textcodec_ext) \
    AM_EXT_IF_WITH(WAMR,       &am_wamr_ext) \
    AM_EXT_IF_WITH(WASM3,      &am_wasm3_ext)

/* The effective extension set. Overridable by:
 *   (a) parent-project CMake variable (recommended, non-invasive);
 *   (b) a translation-unit #define before including this header.
 * If undefined, the default set is used. Register order = expansion order. */
#ifndef AM_EXTENSIONS
#  define AM_EXTENSIONS AM_DEFAULT_EXTENSIONS
#endif

#endif /* AM_EXT_REGISTRY_H */

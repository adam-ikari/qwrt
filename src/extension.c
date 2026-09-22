/*
 * amoib Extension Lifecycle
 *
 * Extension init/destroy/suspend/resume hooks for multi-context support.
 *
 * Extensions come from a compile-time table (am_default_exts[] in context.c,
 * driven by the AM_EXTENSIONS macro). The table is NOT NULL-terminated -
 * disabled built-ins appear as NULL slots that are skipped here - so iteration
 * is by count (ctx->extensions_count), not by a NULL sentinel.
 */

#include "am_internal.h"
#include <stdlib.h>

/* Iterate the extension table by count, skipping NULL slots (disabled
 * built-ins). `action` runs for each non-NULL extension. */
#define AM_EXT_FOR_EACH(ctx, ext) \
    for (int _i = 0; _i < (ctx)->extensions_count && \
                     ((ext) = (ctx)->extensions[_i], 1); _i++) \
        if ((ext) != NULL)

/* ================================================================
 * Extension init - iterate by count, call init hooks
 * ================================================================ */

int am_ext_init_all(am_t *rt, am_ctx_t *ctx)
{
    if (!rt || !ctx || !ctx->extensions) {
        return 0;  /* No extensions is not an error */
    }

    const am_ext_t *ext;
    AM_EXT_FOR_EACH(ctx, ext) {
        if (ext->init) {
            if (ext->init((am_ext_t *)ext, rt) < 0) {
                return -1;  /* Init failed */
            }
        }
    }

    return 0;
}

/* ================================================================
 * Extension destroy - iterate, call destroy hooks
 * ================================================================ */

void am_ext_destroy_all(am_t *rt, am_ctx_t *ctx)
{
    if (!rt || !ctx || !ctx->extensions) {
        return;
    }

    const am_ext_t *ext;
    AM_EXT_FOR_EACH(ctx, ext) {
        if (ext->destroy) {
            ext->destroy((am_ext_t *)ext, rt);
        }
    }
}

/* ================================================================
 * Extension suspend - iterate, call suspend hooks
 * ================================================================ */

int am_ext_suspend_all(am_t *rt, am_ctx_t *ctx)
{
    if (!rt || !ctx || !ctx->extensions) {
        return 0;
    }

    const am_ext_t *ext;
    AM_EXT_FOR_EACH(ctx, ext) {
        if (ext->suspend) {
            if (ext->suspend((am_ext_t *)ext, rt) < 0) {
                return -1;  /* Suspend failed */
            }
        }
    }

    return 0;
}

/* ================================================================
 * Extension resume - iterate, call resume hooks
 * ================================================================ */

int am_ext_resume_all(am_t *rt, am_ctx_t *ctx)
{
    if (!rt || !ctx || !ctx->extensions) {
        return 0;
    }

    const am_ext_t *ext;
    AM_EXT_FOR_EACH(ctx, ext) {
        if (ext->resume) {
            if (ext->resume((am_ext_t *)ext, rt) < 0) {
                return -1;  /* Resume failed */
            }
        }
    }

    return 0;
}

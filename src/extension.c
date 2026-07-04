/*
 * qwrt Extension Lifecycle
 *
 * Extension init/destroy/suspend/resume hooks for multi-context support.
 */

#include "qwrt_internal.h"
#include <stdlib.h>

/* ================================================================
 * Extension init — iterate NULL-terminated array, call init hooks
 * ================================================================ */

int qwrt_ext_init_all(qwrt_t *rt, qwrt_ctx_t *ctx)
{
    if (!rt || !ctx || !ctx->extensions) {
        return 0;  /* No extensions is not an error */
    }

    for (const qwrt_ext_t **extp = ctx->extensions; *extp != NULL; extp++) {
        const qwrt_ext_t *ext = *extp;
        if (ext->init) {
            if (ext->init((qwrt_ext_t *)ext, rt) < 0) {
                return -1;  /* Init failed */
            }
        }
    }

    return 0;
}

/* ================================================================
 * Extension destroy — iterate, call destroy hooks
 * ================================================================ */

void qwrt_ext_destroy_all(qwrt_t *rt, qwrt_ctx_t *ctx)
{
    if (!rt || !ctx || !ctx->extensions) {
        return;
    }

    for (const qwrt_ext_t **extp = ctx->extensions; *extp != NULL; extp++) {
        const qwrt_ext_t *ext = *extp;
        if (ext->destroy) {
            ext->destroy((qwrt_ext_t *)ext, rt);
        }
    }
}

/* ================================================================
 * Extension suspend — iterate, call suspend hooks
 * ================================================================ */

int qwrt_ext_suspend_all(qwrt_t *rt, qwrt_ctx_t *ctx)
{
    if (!rt || !ctx || !ctx->extensions) {
        return 0;
    }

    for (const qwrt_ext_t **extp = ctx->extensions; *extp != NULL; extp++) {
        const qwrt_ext_t *ext = *extp;
        if (ext->suspend) {
            if (ext->suspend((qwrt_ext_t *)ext, rt) < 0) {
                return -1;  /* Suspend failed */
            }
        }
    }

    return 0;
}

/* ================================================================
 * Extension resume — iterate, call resume hooks
 * ================================================================ */

int qwrt_ext_resume_all(qwrt_t *rt, qwrt_ctx_t *ctx)
{
    if (!rt || !ctx || !ctx->extensions) {
        return 0;
    }

    for (const qwrt_ext_t **extp = ctx->extensions; *extp != NULL; extp++) {
        const qwrt_ext_t *ext = *extp;
        if (ext->resume) {
            if (ext->resume((qwrt_ext_t *)ext, rt) < 0) {
                return -1;  /* Resume failed */
            }
        }
    }

    return 0;
}

/* ================================================================
 * Register a single extension on a context at runtime.
 * Appends the extension to the context's extensions array and calls init.
 * Returns 0 on success, -1 on failure (init failed or alloc failed).
 * ================================================================ */

int qwrt_ext_register(qwrt_t *rt, qwrt_ctx_t *ctx, const qwrt_ext_t *ext)
{
    if (!rt || !ctx || !ext) {
        return -1;
    }

    /* Count existing extensions */
    int count = 0;
    if (ctx->extensions) {
        for (; ctx->extensions[count] != NULL; count++) {}
    }

    /* Allocate new array: count + 1 (new ext) + 1 (NULL terminator).
     * If the existing array was not dynamically allocated (e.g. from
     * config->extensions on the stack), we must copy rather than realloc. */
    const qwrt_ext_t **new_arr;
    if (ctx->extensions_dynamic && ctx->extensions) {
        new_arr = (const qwrt_ext_t **)realloc(
            (void *)ctx->extensions,
            sizeof(const qwrt_ext_t *) * (size_t)(count + 2));
    } else {
        new_arr = (const qwrt_ext_t **)malloc(
            sizeof(const qwrt_ext_t *) * (size_t)(count + 2));
        if (new_arr && ctx->extensions) {
            memcpy(new_arr, ctx->extensions,
                   sizeof(const qwrt_ext_t *) * (size_t)count);
        }
    }
    if (!new_arr) {
        return -1;
    }

    /* Append the new extension and NULL terminator */
    new_arr[count] = ext;
    new_arr[count + 1] = NULL;
    ctx->extensions = new_arr;
    ctx->extensions_dynamic = 1;

    /* Call init hook — the context must be the active one so
     * qwrt_get_jsctx(rt) works during init. */
    if (ext->init) {
        if (ext->init((qwrt_ext_t *)ext, rt) < 0) {
            /* Init failed — rollback */
            if (count == 0) {
                free(new_arr);
                ctx->extensions = NULL;
            } else {
                new_arr[count] = NULL;
            }
            return -1;
        }
    }

    return 0;
}

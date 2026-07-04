/*
 * qwrt Context Lifecycle
 *
 * Context creation, destruction, and helper functions for multi-context support.
 */

#include "qwrt_internal.h"
#include "qwrt/ext_compress.h"
#include "qwrt/ext_crypto.h"
#include "qwrt/ext_textcodec.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ================================================================
 * Context getter helpers
 * ================================================================ */

qwrt_ctx_t *qwrt_get_active_ctx(qwrt_t *rt)
{
    if (!rt || rt->magic != QWRT_MAGIC) {
        return NULL;
    }
    if (rt->active_ctx_id < 0 || rt->active_ctx_id >= QWRT_MAX_CONTEXTS) {
        return NULL;
    }
    return rt->contexts[rt->active_ctx_id];
}

JSContext *qwrt_get_active_jsctx(qwrt_t *rt)
{
    qwrt_ctx_t *ctx = qwrt_get_active_ctx(rt);
    if (!ctx) {
        return NULL;
    }
    return ctx->jsctx;
}

qwrt_ctx_t *qwrt_get_ctx_by_id(qwrt_t *rt, int context_id)
{
    if (!rt || rt->magic != QWRT_MAGIC) {
        return NULL;
    }
    if (context_id < 0 || context_id >= QWRT_MAX_CONTEXTS) {
        return NULL;
    }
    return rt->contexts[context_id];
}

/* ================================================================
 * Context resource cleanup
 * ================================================================ */

void qwrt_ctx_cleanup_resources(qwrt_t *rt, qwrt_ctx_t *ctx)
{
    if (!rt || !ctx) {
        return;
    }

    JSContext *jsctx = ctx->jsctx;
    const qwrt_pal_t *pal = ctx->pal;

    /* Stop all active timers and free associated resources */
    for (int i = 0; i < ctx->handle_count; i++) {
        if (ctx->handles[i] != NULL) {
            if (pal && pal->timer_stop) {
                pal->timer_stop((qwrt_pal_t *)pal, ctx->handles[i]);
            }
            ctx->handles[i] = NULL;
        }

        /* Free timer resolve JSValue */
        if (jsctx && !JS_IsUndefined(ctx->timer_resolves[i])) {
            JS_FreeValue(jsctx, ctx->timer_resolves[i]);
            ctx->timer_resolves[i] = JS_UNDEFINED;
        }

        /* Free timer cbd allocation */
        if (jsctx && ctx->timer_cbds[i] != NULL) {
            qwrt_free_cb_data(jsctx, ctx->timer_cbds[i]);
            ctx->timer_cbds[i] = NULL;
        }
    }

    ctx->handle_count = 0;
}

/* ================================================================
 * Context creation
 * ================================================================ */

qwrt_ctx_t *qwrt_ctx_create(qwrt_t *rt, const qwrt_config_t *config)
{
    if (!rt || rt->magic != QWRT_MAGIC) {
        return NULL;
    }
    if (!config || !config->pal) {
        return NULL;
    }

    /* Find a free context slot */
    int slot = -1;
    for (int i = 0; i < QWRT_MAX_CONTEXTS; i++) {
        if (rt->contexts[i] == NULL) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return NULL;  /* No free slots */
    }

    /* Allocate qwrt_ctx_t */
    qwrt_ctx_t *ctx = (qwrt_ctx_t *)calloc(1, sizeof(qwrt_ctx_t));
    if (!ctx) {
        return NULL;
    }

    /* Create JSContext under shared JSRuntime */
    ctx->jsctx = JS_NewContext(rt->jsrt);
    if (!ctx->jsctx) {
        free(ctx);
        return NULL;
    }

    /* Initialize context fields */
    ctx->context_id = slot;
    ctx->active = 0;
    ctx->suspended = 0;
    ctx->pal = config->pal;
    ctx->handle_count = 0;
    ctx->extensions = config->extensions;

    /* Use the built-in default polyfill */
    ctx->polyfill = qwrt_default_polyfill;
    ctx->polyfill_len = qwrt_default_polyfill_len;

    /* Initialize handle arrays to zero/NULL */
    for (int i = 0; i < QWRT_MAX_HANDLES; i++) {
        ctx->handles[i] = NULL;
        ctx->timer_resolves[i] = JS_UNDEFINED;
        ctx->timer_cbds[i] = NULL;
    }

    /* Register in runtime's context table BEFORE polyfill injection,
     * so that bridge functions (pal.hrtime etc.) can find the context
     * via get_ctx_from_jsctx during polyfill evaluation. */
    rt->contexts[slot] = ctx;
    rt->context_count++;
    int prev_active = rt->active_ctx_id;
    rt->active_ctx_id = slot;

    /* Inject polyfill bytecode.
     * If caller didn't provide a custom polyfill, the default is used
     * (set above when saving ctx->polyfill). */
    {
        JSValue pal_obj = qwrt_create_pal_object_ctx(rt, ctx);
        if (JS_IsException(pal_obj)) {
            rt->active_ctx_id = prev_active;
            rt->contexts[slot] = NULL;
            rt->context_count--;
            JS_FreeContext(ctx->jsctx);
            free(ctx);
            return NULL;
        }
        JSValue global = JS_GetGlobalObject(ctx->jsctx);
        JS_SetPropertyStr(ctx->jsctx, global, "__pal_inject__", pal_obj);
        JS_FreeValue(ctx->jsctx, global);

        /* Load and evaluate bytecode */
        JSValue obj = JS_ReadObject(ctx->jsctx, ctx->polyfill, ctx->polyfill_len, JS_READ_OBJ_BYTECODE);
        int bc_ok = 1;
        if (JS_IsException(obj)) {
            bc_ok = 0;
        } else {
            JSValue val = JS_EvalFunction(ctx->jsctx, obj);
            if (JS_IsException(val)) {
                JS_FreeValue(ctx->jsctx, val);
                bc_ok = 0;
            } else {
                JS_FreeValue(ctx->jsctx, val);
            }
        }

        /* Remove __pal_inject__ from globalThis but keep the pal object
         * accessible as __pal__ for extension init hooks to register
         * functions on it. The polyfill's closures reference the same
         * object, so extensions adding properties here are visible. */
        global = JS_GetGlobalObject(ctx->jsctx);
        JSAtom inject_atom = JS_NewAtom(ctx->jsctx, "__pal_inject__");
        JSValue pal_ref = JS_GetProperty(ctx->jsctx, global, inject_atom);
        JS_DeleteProperty(ctx->jsctx, global, inject_atom, 0);
        JS_FreeAtom(ctx->jsctx, inject_atom);
        if (!JS_IsUndefined(pal_ref) && !JS_IsException(pal_ref)) {
            JS_SetPropertyStr(ctx->jsctx, global, "__pal__", pal_ref);
        } else {
            JS_FreeValue(ctx->jsctx, pal_ref);
        }
        JS_FreeValue(ctx->jsctx, global);

        if (!bc_ok) {
            rt->active_ctx_id = prev_active;
            rt->contexts[slot] = NULL;
            rt->context_count--;
            JS_FreeContext(ctx->jsctx);
            free(ctx);
            return NULL;
        }
    }

    /* Remove QuickJS built-in WebAssembly — it can parse but not execute
     * WASM, which is misleading. WASM extensions (wasm3, wamr) register
     * their own working WebAssembly implementation in their init hook.
     * If no WASM extension is present, WebAssembly is simply unavailable. */
    {
        JSValue global = JS_GetGlobalObject(ctx->jsctx);
        JSAtom wasm_atom = JS_NewAtom(ctx->jsctx, "WebAssembly");
        JS_DeleteProperty(ctx->jsctx, global, wasm_atom, 0);
        JS_FreeAtom(ctx->jsctx, wasm_atom);
        JS_FreeValue(ctx->jsctx, global);
    }

    /* Auto-register compression extension if not already in the list.
     * This happens before qwrt_ext_init_all so the init hook runs. */
#ifdef QWRT_WITH_COMPRESS
    {
        int found = 0;
        if (ctx->extensions) {
            for (int i = 0; ctx->extensions[i] != NULL; i++) {
                if (ctx->extensions[i] == &qwrt_compress_ext) {
                    found = 1;
                    break;
                }
            }
        }
        if (!found) {
            qwrt_ext_register(rt, ctx, &qwrt_compress_ext);
        }
    }
#endif

    /* Auto-register crypto extension if not already in the list. */
#ifdef QWRT_WITH_CRYPTO_EXT
    {
        int found = 0;
        if (ctx->extensions) {
            for (int i = 0; ctx->extensions[i] != NULL; i++) {
                if (ctx->extensions[i] == &qwrt_crypto_ext) {
                    found = 1;
                    break;
                }
            }
        }
        if (!found) {
            qwrt_ext_register(rt, ctx, &qwrt_crypto_ext);
        }
    }
#endif

    /* Auto-register textcodec extension if not already in the list. */
#ifdef QWRT_WITH_TEXTCODEC
    {
        int found = 0;
        if (ctx->extensions) {
            for (int i = 0; ctx->extensions[i] != NULL; i++) {
                if (ctx->extensions[i] == &qwrt_textcodec_ext) {
                    found = 1;
                    break;
                }
            }
        }
        if (!found) {
            qwrt_ext_register(rt, ctx, &qwrt_textcodec_ext);
        }
    }
#endif

    /* Call extension init hooks */
    if (qwrt_ext_init_all(rt, ctx) < 0) {
        /* Undo registration on failure */
        rt->active_ctx_id = prev_active;
        rt->contexts[slot] = NULL;
        rt->context_count--;
        qwrt_ctx_cleanup_resources(rt, ctx);
        JS_FreeContext(ctx->jsctx);
        free(ctx);
        return NULL;
    }

    return ctx;
}

/* ================================================================
 * Context destruction
 * ================================================================ */

void qwrt_ctx_destroy(qwrt_t *rt, qwrt_ctx_t *ctx)
{
    if (!rt || !ctx) {
        return;
    }

    int context_id = ctx->context_id;

    /* Call extension destroy hooks */
    qwrt_ext_destroy_all(rt, ctx);

    /* Cleanup resources (timers, handles, etc.) */
    qwrt_ctx_cleanup_resources(rt, ctx);

    /* Free the JSContext */
    if (ctx->jsctx) {
        JS_FreeContext(ctx->jsctx);
        ctx->jsctx = NULL;
    }

    /* Remove from runtime table */
    rt->contexts[context_id] = NULL;
    rt->context_count--;

    /* Clear active_ctx_id if this was the active context */
    if (rt->active_ctx_id == context_id) {
        rt->active_ctx_id = -1;
    }

    /* Free dynamically allocated extensions array */
    if (ctx->extensions_dynamic && ctx->extensions) {
        free((void *)ctx->extensions);
        ctx->extensions = NULL;
    }

    /* Free the ctx struct */
    free(ctx);
}

/*
 * qwrt Web WASM Extension — browser-native WebAssembly engine
 *
 * When qwrt runs in a browser (Emscripten/WASM), this extension bridges
 * the WebAssembly JS API directly to the browser's native implementation.
 * No WAMR, no wasm3 — the browser provides everything.
 *
 * This is a zero-code extension — all WASM execution happens in the
 * browser's native engine. We just register the JS API surface.
 *
 * Compile with: QWRT_HAS_WEB_WASM (mutually exclusive with WAMR/wasm3)
 */

#include "qwrt_internal.h"
#include <string.h>

#ifdef QWRT_HAS_WEB_WASM

/*
 * All WebAssembly.* methods are provided by the browser natively.
 * We just need to ensure the global WebAssembly object is available.
 * In a browser environment, it already exists — we just pass it through.
 *
 * The extension registers nothing — the browser already has WebAssembly.
 * But we need to make sure it's accessible from qwrt's JS context.
 * In practice, Emscripten's globalThis already has it.
 */

static int web_wasm_ext_init(qwrt_ext_t *ext, qwrt_t *rt)
{
    JSContext *ctx = qwrt_get_jsctx(rt);
    if (!ctx) return -1;

    /*
     * In the browser, WebAssembly already exists on globalThis.
     * Emscripten preserves global objects, so no action needed.
     * The browser provides: validate, compile, instantiate,
     * Module, Instance, Memory, Table, Global, CompileError,
     * LinkError, RuntimeError — all natively.
     */

    /* Verify WebAssembly is available */
    JSValue global = JS_GetGlobalObject(ctx);
    JSAtom wasm_atom = JS_NewAtom(ctx, "WebAssembly");
    int has_wasm = JS_HasProperty(ctx, global, wasm_atom);
    JS_FreeAtom(ctx, wasm_atom);
    JS_FreeValue(ctx, global);

    if (!has_wasm) {
        /*
         * WebAssembly not available — this shouldn't happen in a browser.
         * Create a stub that throws meaningful errors.
         */
        JSValue wasm_obj = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, wasm_obj, "validate",
            JS_NewCFunction(ctx, web_wasm_throw_not_available, "validate", 1));
        JS_SetPropertyStr(ctx, wasm_obj, "compile",
            JS_NewCFunction(ctx, web_wasm_throw_not_available, "compile", 1));
        JS_SetPropertyStr(ctx, wasm_obj, "instantiate",
            JS_NewCFunction(ctx, web_wasm_throw_not_available, "instantiate", 2));

        JSValue ctor = JS_NewCFunction2(ctx, web_wasm_throw_not_available,
            "Module", 1, JS_CFUNC_constructor, 0);
        JS_SetPropertyStr(ctx, wasm_obj, "Module", ctor);
        JS_SetPropertyStr(ctx, wasm_obj, "Instance", ctor);
        JS_SetPropertyStr(ctx, wasm_obj, "Memory", ctor);
        JS_SetPropertyStr(ctx, wasm_obj, "Table", ctor);
        JS_SetPropertyStr(ctx, wasm_obj, "Global", ctor);

        JS_SetPropertyStr(ctx, global, "WebAssembly", wasm_obj);
    }

    (void)ext;
    return 0;
}

static void web_wasm_ext_destroy(qwrt_ext_t *ext, qwrt_t *rt)
{
    (void)ext;
    (void)rt;
    /* Browser manages WebAssembly lifecycle */
}

static int web_wasm_ext_suspend(qwrt_ext_t *ext, qwrt_t *rt)
{
    (void)ext; (void)rt;
    return 0;
}

static int web_wasm_ext_resume(qwrt_ext_t *ext, qwrt_t *rt)
{
    (void)ext; (void)rt;
    return 0;
}

#else /* !QWRT_HAS_WEB_WASM — stub */

static JSValue web_wasm_throw_not_available(JSContext *ctx, JSValueConst this_val,
    int argc, JSValueConst *argv)
{
    (void)this_val; (void)argc; (void)argv;
    return JS_ThrowTypeError(ctx,
        "WebAssembly: browser-native engine not linked. "
        "Compile with QWRT_HAS_WEB_WASM for browser environments.");
}

static int web_wasm_ext_init(qwrt_ext_t *ext, qwrt_t *rt)
{
    JSContext *ctx = qwrt_get_jsctx(rt);
    if (!ctx) return -1;

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue wasm_obj = JS_NewObject(ctx);

    JS_SetPropertyStr(ctx, wasm_obj, "validate",
        JS_NewCFunction(ctx, web_wasm_throw_not_available, "validate", 1));
    JS_SetPropertyStr(ctx, wasm_obj, "compile",
        JS_NewCFunction(ctx, web_wasm_throw_not_available, "compile", 1));
    JS_SetPropertyStr(ctx, wasm_obj, "instantiate",
        JS_NewCFunction(ctx, web_wasm_throw_not_available, "instantiate", 2));

    JSValue ctor = JS_NewCFunction2(ctx, web_wasm_throw_not_available,
        "Module", 1, JS_CFUNC_constructor, 0);
    JS_SetPropertyStr(ctx, wasm_obj, "Module", ctor);
    JS_SetPropertyStr(ctx, wasm_obj, "Instance", ctor);
    JS_SetPropertyStr(ctx, wasm_obj, "Memory", ctor);
    JS_SetPropertyStr(ctx, wasm_obj, "Table", ctor);
    JS_SetPropertyStr(ctx, wasm_obj, "Global", ctor);

    JS_SetPropertyStr(ctx, global, "WebAssembly", wasm_obj);
    JS_FreeValue(ctx, global);

    (void)ext;
    return 0;
}

static void web_wasm_ext_destroy(qwrt_ext_t *ext, qwrt_t *rt)
{
    (void)ext; (void)rt;
}

static int web_wasm_ext_suspend(qwrt_ext_t *ext, qwrt_t *rt)
{
    (void)ext; (void)rt;
    return 0;
}

static int web_wasm_ext_resume(qwrt_ext_t *ext, qwrt_t *rt)
{
    (void)ext; (void)rt;
    return 0;
}

#endif /* QWRT_HAS_WEB_WASM */

/* ================================================================
 * Extension definition
 * ================================================================ */

const qwrt_ext_t qwrt_web_wasm_ext = {
    .name = "web_wasm",
    .init = web_wasm_ext_init,
    .destroy = web_wasm_ext_destroy,
    .suspend = web_wasm_ext_suspend,
    .resume = web_wasm_ext_resume,
    .user_data = NULL,
};
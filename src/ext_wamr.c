/*
 * qwrt WAMR Extension
 *
 * WASM runtime using WAMR (WebAssembly Micro Runtime) engine.
 * Pure sandbox model — WASM modules have NO access to system APIs:
 * no filesystem, no network, no host functions. Only pure
 * computation + linear memory.
 *
 * When QWRT_HAS_WAMR is defined, uses real WAMR engine.
 * Otherwise, provides stub JS API surface that throws on use.
 */

#include "qwrt_internal.h"
#include <string.h>

#ifdef QWRT_HAS_WAMR
#include "wasm_export.h"

/* ================================================================
 * WAMR per-extension state
 * ================================================================ */

typedef struct wamr_state_t {
    int initialized;
} wamr_state_t;

static wamr_state_t g_wamr_state;

/* ================================================================
 * Opaque JS object helpers — wrap WAMR handles for GC
 * ================================================================ */

typedef struct wamr_module_wrap_t {
    wasm_module_t module;
    uint8_t *wasm_buf;
    uint32_t wasm_buf_size;
} wamr_module_wrap_t;

typedef struct wamr_instance_wrap_t {
    wasm_module_inst_t module_inst;
    wasm_exec_env_t exec_env;
    JSValue module_obj;  /* keep module alive — unload happens after instance dies */
} wamr_instance_wrap_t;


static void wamr_module_finalizer(JSRuntime *jsrt, JSValue val)
{
    qwrt_t *rt = qwrt_get_rt_from_jsrt(jsrt);
    if (!rt) return;
    wamr_module_wrap_t *wrap = (wamr_module_wrap_t *)JS_GetOpaque(val, rt->wamr_module_class_id);
    if (wrap) {
        if (wrap->module) {
            wasm_runtime_unload(wrap->module);
            wrap->module = NULL;
        }
        if (wrap->wasm_buf) {
            wasm_runtime_free(wrap->wasm_buf);
            wrap->wasm_buf = NULL;
        }
        js_free_rt(jsrt, wrap);
    }
}

static void wamr_instance_finalizer(JSRuntime *jsrt, JSValue val)
{
    qwrt_t *rt = qwrt_get_rt_from_jsrt(jsrt);
    if (!rt) return;
    wamr_instance_wrap_t *wrap = (wamr_instance_wrap_t *)JS_GetOpaque(val, rt->wamr_instance_class_id);
    if (wrap) {
        if (wrap->exec_env) {
            wasm_runtime_destroy_exec_env(wrap->exec_env);
            wrap->exec_env = NULL;
        }
        if (wrap->module_inst) {
            wasm_runtime_deinstantiate(wrap->module_inst);
            wrap->module_inst = NULL;
        }
        JS_FreeValueRT(jsrt, wrap->module_obj);
        js_free_rt(jsrt, wrap);
    }
}

static void wamr_register_classes(qwrt_t *rt, JSContext *ctx)
{
    JSRuntime *jsrt = JS_GetRuntime(ctx);

    /* Module class */
    JS_NewClassID(jsrt, &rt->wamr_module_class_id);
    JSClassDef module_class = {
        .class_name = "WebAssembly.Module",
        .finalizer = wamr_module_finalizer,
    };
    JS_NewClass(jsrt, rt->wamr_module_class_id, &module_class);

    /* Instance class */
    JS_NewClassID(jsrt, &rt->wamr_instance_class_id);
    JSClassDef instance_class = {
        .class_name = "WebAssembly.Instance",
        .finalizer = wamr_instance_finalizer,
    };
    JS_NewClass(jsrt, rt->wamr_instance_class_id, &instance_class);
}

/* ================================================================
 * Helper: extract byte buffer from ArrayBuffer or TypedArray
 * ================================================================ */

static int wamr_extract_buffer(JSContext *ctx, JSValueConst val,
                               uint8_t **out_bytes, size_t *out_len)
{
    size_t byte_len = 0;
    uint8_t *bytes = NULL;

    if (JS_IsArrayBuffer(val)) {
        bytes = JS_GetArrayBuffer(ctx, &byte_len, val);
    } else {
        /* Try as TypedArray — get the underlying buffer */
        JSValue ab = JS_GetPropertyStr(ctx, val, "buffer");
        if (JS_IsException(ab) || !JS_IsArrayBuffer(ab)) {
            JS_FreeValue(ctx, ab);
            return -1;
        }
        size_t ab_len;
        uint8_t *ab_bytes = JS_GetArrayBuffer(ctx, &ab_len, ab);
        JS_FreeValue(ctx, ab);

        int64_t byte_offset = 0;
        JSValue offset_val = JS_GetPropertyStr(ctx, val, "byteOffset");
        if (!JS_IsException(offset_val)) {
            JS_ToInt64(ctx, &byte_offset, offset_val);
        }
        JS_FreeValue(ctx, offset_val);

        int64_t view_len = 0;
        JSValue len_val = JS_GetPropertyStr(ctx, val, "byteLength");
        if (!JS_IsException(len_val)) {
            JS_ToInt64(ctx, &view_len, len_val);
        }
        JS_FreeValue(ctx, len_val);

        if (byte_offset < 0 || (size_t)byte_offset + (size_t)view_len > ab_len) {
            return -1;
        }

        bytes = ab_bytes + byte_offset;
        byte_len = (size_t)view_len;
    }

    if (!bytes) return -1;
    *out_bytes = bytes;
    *out_len = byte_len;
    return 0;
}

/* ================================================================
 * WebAssembly.validate(bufferSource) -> bool
 * ================================================================ */

static JSValue wamr_wasm_validate(JSContext *ctx, JSValueConst this_val,
                                  int argc, JSValueConst *argv)
{
    qwrt_t *rt = qwrt_get_rt_from_ctx(ctx);
    if (!rt) return JS_NULL;
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "WebAssembly.validate requires at least 1 argument");
    }

    uint8_t *bytes;
    size_t byte_len;
    if (wamr_extract_buffer(ctx, argv[0], &bytes, &byte_len) < 0) {
        return JS_ThrowTypeError(ctx, "WebAssembly.validate: argument must be ArrayBuffer or TypedArray");
    }

    char error_buf[128] = {0};
    wasm_module_t module = wasm_runtime_load(bytes, (uint32_t)byte_len,
                                             error_buf, sizeof(error_buf));
    if (module) {
        wasm_runtime_unload(module);
        return JS_TRUE;
    }
    return JS_FALSE;
}

/* ================================================================
 * WebAssembly.compile(bufferSource) -> Promise<Module>
 * ================================================================ */

static JSValue wamr_wasm_compile(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "WebAssembly.compile requires at least 1 argument");
    }

    /* Call new Module(bufferSource) */
    JSValue module_ctor = JS_GetPropertyStr(ctx, this_val, "Module");
    JSValue result = JS_CallConstructor(ctx, module_ctor, 1, argv);
    JS_FreeValue(ctx, module_ctor);

    if (JS_IsException(result)) {
        return result;
    }

    /* Wrap in a resolved promise */
    JSValue resolving_funcs[2];
    JSValue promise = JS_NewPromiseCapability(ctx, resolving_funcs);
    if (JS_IsException(promise)) {
        JS_FreeValue(ctx, result);
        return JS_EXCEPTION;
    }

    JS_Call(ctx, resolving_funcs[0], JS_UNDEFINED, 1, &result);
    JS_FreeValue(ctx, result);
    JS_FreeValue(ctx, resolving_funcs[0]);
    JS_FreeValue(ctx, resolving_funcs[1]);

    return promise;
}

/* ================================================================
 * WebAssembly.instantiate(bufferSource|Module, importObject) -> Promise
 * ================================================================ */

static JSValue wamr_wasm_instantiate(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv)
{
    qwrt_t *rt = qwrt_get_rt_from_ctx(ctx);
    if (!rt) return JS_NULL;
    (void)this_val;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "WebAssembly.instantiate requires at least 1 argument");
    }

    JSValue instance_ctor = JS_GetPropertyStr(ctx, this_val, "Instance");
    int is_module_arg = (JS_GetOpaque(argv[0], rt->wamr_module_class_id) != NULL);

    JSValue instance_result;
    JSValue module_result = JS_UNDEFINED;

    if (is_module_arg) {
        /* Module arg — just instantiate */
        JSValue inst_args[2] = { argv[0], argc >= 2 ? argv[1] : JS_UNDEFINED };
        instance_result = JS_CallConstructor(ctx, instance_ctor, argc >= 2 ? 2 : 1, inst_args);
    } else {
        /* Buffer arg — compile + instantiate */
        JSValue module_ctor = JS_GetPropertyStr(ctx, this_val, "Module");
        module_result = JS_CallConstructor(ctx, module_ctor, 1, argv);
        JS_FreeValue(ctx, module_ctor);

        if (JS_IsException(module_result)) {
            JS_FreeValue(ctx, instance_ctor);
            return module_result;
        }

        JSValue inst_args[2] = { module_result, argc >= 2 ? argv[1] : JS_UNDEFINED };
        instance_result = JS_CallConstructor(ctx, instance_ctor, argc >= 2 ? 2 : 1, inst_args);
    }

    JS_FreeValue(ctx, instance_ctor);

    if (JS_IsException(instance_result)) {
        JS_FreeValue(ctx, module_result);
        return instance_result;
    }

    /* Wrap in a resolved promise */
    JSValue resolving_funcs[2];
    JSValue promise = JS_NewPromiseCapability(ctx, resolving_funcs);
    if (JS_IsException(promise)) {
        JS_FreeValue(ctx, instance_result);
        JS_FreeValue(ctx, module_result);
        return JS_EXCEPTION;
    }

    if (is_module_arg) {
        JS_Call(ctx, resolving_funcs[0], JS_UNDEFINED, 1, &instance_result);
        JS_FreeValue(ctx, instance_result);
    } else {
        /* Buffer+instantiate returns {module, instance} */
        JSValue obj = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, obj, "module", module_result);
        JS_SetPropertyStr(ctx, obj, "instance", instance_result);
        JS_Call(ctx, resolving_funcs[0], JS_UNDEFINED, 1, &obj);
        JS_FreeValue(ctx, obj);
        /* module_result and instance_result were consumed by JS_SetPropertyStr,
         * which transferred ownership to the obj. Do not free them again. */
    }

    JS_FreeValue(ctx, resolving_funcs[0]);
    JS_FreeValue(ctx, resolving_funcs[1]);

    return promise;
}

/* ================================================================
 * WebAssembly.Module constructor
 * ================================================================ */

static JSValue wamr_module_constructor(JSContext *ctx, JSValueConst new_target,
                                       int argc, JSValueConst *argv)
{
    qwrt_t *rt = qwrt_get_rt_from_ctx(ctx);
    if (!rt) return JS_NULL;
    (void)new_target;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "WebAssembly.Module requires at least 1 argument");
    }

    /* If already a Module, return copy */
    wamr_module_wrap_t *existing = (wamr_module_wrap_t *)JS_GetOpaque(argv[0], rt->wamr_module_class_id);
    if (existing) {
        return JS_DupValue(ctx, argv[0]);
    }

    uint8_t *bytes;
    size_t byte_len;
    if (wamr_extract_buffer(ctx, argv[0], &bytes, &byte_len) < 0) {
        return JS_ThrowTypeError(ctx, "WebAssembly.Module: argument must be ArrayBuffer or TypedArray");
    }

    if (byte_len == 0) {
        return JS_ThrowTypeError(ctx, "WebAssembly.Module: empty buffer");
    }

    /* Copy the wasm binary — WAMR needs it to remain valid */
    uint8_t *wasm_buf = (uint8_t *)wasm_runtime_malloc((uint32_t)byte_len);
    if (!wasm_buf) {
        return JS_ThrowOutOfMemory(ctx);
    }
    memcpy(wasm_buf, bytes, byte_len);

    char error_buf[128] = {0};
    wasm_module_t module = wasm_runtime_load(wasm_buf, (uint32_t)byte_len,
                                             error_buf, sizeof(error_buf));
    if (!module) {
        wasm_runtime_free(wasm_buf);
        return JS_ThrowTypeError(ctx, "WebAssembly.Module: %s", error_buf);
    }

    wamr_module_wrap_t *wrap = (wamr_module_wrap_t *)js_malloc(ctx, sizeof(wamr_module_wrap_t));
    if (!wrap) {
        wasm_runtime_unload(module);
        wasm_runtime_free(wasm_buf);
        return JS_ThrowOutOfMemory(ctx);
    }
    wrap->module = module;
    wrap->wasm_buf = wasm_buf;
    wrap->wasm_buf_size = (uint32_t)byte_len;

    JSValue obj = JS_NewObjectClass(ctx, rt->wamr_module_class_id);
    if (JS_IsException(obj)) {
        wasm_runtime_unload(module);
        wasm_runtime_free(wasm_buf);
        js_free(ctx, wrap);
        return JS_EXCEPTION;
    }
    JS_SetOpaque(obj, wrap);

    return obj;
}

/* ================================================================
 * WebAssembly.Instance constructor
 * ================================================================ */

static JSValue wamr_instance_constructor(JSContext *ctx, JSValueConst new_target,
                                         int argc, JSValueConst *argv)
{
    qwrt_t *rt = qwrt_get_rt_from_ctx(ctx);
    if (!rt) return JS_NULL;
    (void)new_target;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "WebAssembly.Instance requires at least 1 argument");
    }

    wamr_module_wrap_t *mod_wrap = (wamr_module_wrap_t *)JS_GetOpaque(argv[0], rt->wamr_module_class_id);
    if (!mod_wrap || !mod_wrap->module) {
        return JS_ThrowTypeError(ctx, "WebAssembly.Instance: first argument must be a WebAssembly.Module");
    }

    /* Instantiate */
    char error_buf[128] = {0};
    wasm_module_inst_t module_inst = wasm_runtime_instantiate(mod_wrap->module,
                                                              64 * 1024,
                                                              8 * 1024 * 1024,
                                                              error_buf, sizeof(error_buf));
    if (!module_inst) {
        return JS_ThrowTypeError(ctx, "WebAssembly.Instance: %s",
                                 error_buf[0] ? error_buf : "instantiation failed");
    }

    /* Create exec env */
    wasm_exec_env_t exec_env = wasm_runtime_create_exec_env(module_inst, 64 * 1024);
    if (!exec_env) {
        wasm_runtime_deinstantiate(module_inst);
        return JS_ThrowOutOfMemory(ctx);
    }

    wamr_instance_wrap_t *wrap = (wamr_instance_wrap_t *)js_malloc(ctx, sizeof(wamr_instance_wrap_t));
    if (!wrap) {
        wasm_runtime_destroy_exec_env(exec_env);
        wasm_runtime_deinstantiate(module_inst);
        return JS_ThrowOutOfMemory(ctx);
    }
    wrap->module_inst = module_inst;
    wrap->exec_env = exec_env;
    wrap->module_obj = JS_DupValue(ctx, argv[0]);  /* keep module alive until instance dies */

    JSValue obj = JS_NewObjectClass(ctx, rt->wamr_instance_class_id);
    if (JS_IsException(obj)) {
        wasm_runtime_destroy_exec_env(exec_env);
        wasm_runtime_deinstantiate(module_inst);
        js_free(ctx, wrap);
        return JS_EXCEPTION;
    }
    JS_SetOpaque(obj, wrap);

    /* Build the exports object.
     *
     * WAMR-1.3.3 (the pinned release) has NO export-enumeration API - only
     * wasm_runtime_lookup_function (name-based), and no memory/global
     * introspection (wasm_runtime_get_default_memory etc. are unreleased).
     * The export-introspection API ext_wamr.c originally used
     * (wasm_runtime_get_export_count / get_export_type / wasm_export_t /
     * WASM_IMPORT_EXPORT_KIND_*) exists only on WAMR's unreleased main branch.
     * Pinned to a release tag, we cannot enumerate exports, so `exports` is
     * left empty here. (This drops introspection only; exported-function
     * invocation is not implemented for wamr regardless.) wasm3's ext has
     * full enumeration; WAMR's is limited on 1.3.3.
     *
     * TODO: when WAMR ships the export-enumeration API in a tagged release,
     * restore the enumeration here. */
    JSValue exports = JS_NewObject(ctx);

    JS_SetPropertyStr(ctx, obj, "exports", exports);

    return obj;
}

/* ================================================================
 * WebAssembly.Memory constructor
 * ================================================================ */

/* Finalizer for the standalone WebAssembly.Memory ArrayBuffer (js_mallocz'd
 * buffer owned by the array buffer). Frees via the QuickJS allocator. */
static void wamr_arraybuffer_free(JSRuntime *rt, void *opaque, void *ptr)
{
    (void)opaque;
    if (ptr) {
        js_free_rt(rt, ptr);
    }
}

static JSValue wamr_memory_constructor(JSContext *ctx, JSValueConst new_target,
                                       int argc, JSValueConst *argv)
{
    (void)new_target;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "WebAssembly.Memory requires a descriptor");
    }

    JSValue initial_val = JS_GetPropertyStr(ctx, argv[0], "initial");
    int32_t initial_pages = 1;
    if (!JS_IsException(initial_val)) {
        JS_ToInt32(ctx, &initial_pages, initial_val);
    }
    if (initial_pages < 1) {
        JS_FreeValue(ctx, initial_val);
        return JS_ThrowRangeError(ctx, "WebAssembly.Memory: initial must be >= 1");
    }
    JS_FreeValue(ctx, initial_val);

    JSValue maximum_val = JS_GetPropertyStr(ctx, argv[0], "maximum");
    int32_t maximum_pages = -1;
    if (!JS_IsUndefined(maximum_val) && !JS_IsException(maximum_val)) {
        JS_ToInt32(ctx, &maximum_pages, maximum_val);
    }
    JS_FreeValue(ctx, maximum_val);

    size_t byte_len = (size_t)initial_pages * 65536;
    uint8_t *mem = (uint8_t *)js_mallocz(ctx, byte_len);
    if (!mem) {
        return JS_ThrowOutOfMemory(ctx);
    }

    JSValue obj = JS_NewObject(ctx);
    /* JS_NewArrayBuffer takes ownership of `mem`; the finalizer frees it via
     * the QuickJS allocator when the ArrayBuffer is GC'd (without it, mem
     * leaks - JS_NewArrayBuffer with a NULL finalizer never frees external
     * memory). */
    JSValue ab = JS_NewArrayBuffer(ctx, mem, byte_len, wamr_arraybuffer_free,
                                   NULL, 0);
    JS_SetPropertyStr(ctx, obj, "buffer", ab);

    /* Store initial/maximum for grow() */
    JS_SetPropertyStr(ctx, obj, "_initial", JS_NewInt32(ctx, initial_pages));
    if (maximum_pages >= 0) {
        JS_SetPropertyStr(ctx, obj, "_maximum", JS_NewInt32(ctx, maximum_pages));
    }

    return obj;
}

/* ================================================================
 * WebAssembly.Table constructor
 * ================================================================ */

static JSValue wamr_table_constructor(JSContext *ctx, JSValueConst new_target,
                                      int argc, JSValueConst *argv)
{
    (void)new_target;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "WebAssembly.Table requires a descriptor");
    }

    JSValue initial_val = JS_GetPropertyStr(ctx, argv[0], "initial");
    int32_t initial = 0;
    if (!JS_IsException(initial_val)) {
        JS_ToInt32(ctx, &initial, initial_val);
    }
    JS_FreeValue(ctx, initial_val);

    JSValue element_val = JS_GetPropertyStr(ctx, argv[0], "element");
    const char *element = JS_ToCString(ctx, element_val);
    JS_FreeValue(ctx, element_val);

    JSValue obj = JS_NewObject(ctx);
    JSValue array = JS_NewArray(ctx);
    for (int32_t i = 0; i < initial; i++) {
        JS_SetPropertyInt64(ctx, array, i, JS_NULL);
    }
    JS_SetPropertyStr(ctx, obj, "length", JS_NewInt32(ctx, initial));
    JS_SetPropertyStr(ctx, obj, "_array", array);
    if (element) {
        JS_SetPropertyStr(ctx, obj, "_element", JS_NewString(ctx, element));
        JS_FreeCString(ctx, element);
    }

    return obj;
}

/* ================================================================
 * WebAssembly.Global constructor
 * ================================================================ */

static JSValue wamr_global_constructor(JSContext *ctx, JSValueConst new_target,
                                       int argc, JSValueConst *argv)
{
    (void)new_target;
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "WebAssembly.Global requires a descriptor");
    }

    JSValue value_val;
    if (argc >= 2) {
        value_val = JS_DupValue(ctx, argv[1]);
    } else {
        value_val = JS_NewFloat64(ctx, 0);
    }

    JSValue mutable_val = JS_GetPropertyStr(ctx, argv[0], "mutable");
    int is_mutable = 0;
    if (JS_IsBool(mutable_val)) {
        is_mutable = JS_ToBool(ctx, mutable_val);
    }
    JS_FreeValue(ctx, mutable_val);

    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "value", value_val);
    JS_SetPropertyStr(ctx, obj, "mutable", JS_NewBool(ctx, is_mutable));

    return obj;
}

/* ================================================================
 * Extension hooks
 * ================================================================ */

static int wamr_ext_init(qwrt_ext_t *ext, qwrt_t *rt)
{
    JSContext *ctx = qwrt_get_jsctx(rt);
    if (!ctx) return -1;

    /* Initialize WAMR runtime (once) */
    if (!g_wamr_state.initialized) {
        if (!wasm_runtime_init()) {
            return -1;
        }
        g_wamr_state.initialized = 1;
    }

    /* Register JS classes for Module/Instance */
    wamr_register_classes(rt, ctx);

    JSValue global = JS_GetGlobalObject(ctx);

    /* Check if WebAssembly already exists */
    JSAtom wasm_atom = JS_NewAtom(ctx, "WebAssembly");
    int has_wasm = JS_HasProperty(ctx, global, wasm_atom);
    JS_FreeAtom(ctx, wasm_atom);

    if (has_wasm) {
        JS_FreeValue(ctx, global);
        return 0;
    }

    /* Create WebAssembly object */
    JSValue wasm_obj = JS_NewObject(ctx);
    if (JS_IsException(wasm_obj)) {
        JS_FreeValue(ctx, global);
        return -1;
    }

    /* Add methods */
    JS_SetPropertyStr(ctx, wasm_obj, "validate",
        JS_NewCFunction(ctx, wamr_wasm_validate, "validate", 1));
    JS_SetPropertyStr(ctx, wasm_obj, "compile",
        JS_NewCFunction(ctx, wamr_wasm_compile, "compile", 1));
    JS_SetPropertyStr(ctx, wasm_obj, "instantiate",
        JS_NewCFunction(ctx, wamr_wasm_instantiate, "instantiate", 2));

    /* Create constructors */
    JSValue module_ctor = JS_NewCFunction2(ctx, wamr_module_constructor,
                                           "Module", 1, JS_CFUNC_constructor, 0);
    JSValue instance_ctor = JS_NewCFunction2(ctx, wamr_instance_constructor,
                                             "Instance", 1, JS_CFUNC_constructor, 0);
    JSValue memory_ctor = JS_NewCFunction2(ctx, wamr_memory_constructor,
                                           "Memory", 1, JS_CFUNC_constructor, 0);
    JSValue table_ctor = JS_NewCFunction2(ctx, wamr_table_constructor,
                                          "Table", 1, JS_CFUNC_constructor, 0);
    JSValue global_ctor = JS_NewCFunction2(ctx, wamr_global_constructor,
                                           "Global", 1, JS_CFUNC_constructor, 0);

    JS_SetPropertyStr(ctx, wasm_obj, "Module", module_ctor);
    JS_SetPropertyStr(ctx, wasm_obj, "Instance", instance_ctor);
    JS_SetPropertyStr(ctx, wasm_obj, "Memory", memory_ctor);
    JS_SetPropertyStr(ctx, wasm_obj, "Table", table_ctor);
    JS_SetPropertyStr(ctx, wasm_obj, "Global", global_ctor);

    /* CompileError, LinkError, RuntimeError */
    JSValue compile_error = JS_NewError(ctx);
    JS_SetPropertyStr(ctx, wasm_obj, "CompileError", compile_error);

    JSValue link_error = JS_NewError(ctx);
    JS_SetPropertyStr(ctx, wasm_obj, "LinkError", link_error);

    JSValue runtime_error = JS_NewError(ctx);
    JS_SetPropertyStr(ctx, wasm_obj, "RuntimeError", runtime_error);

    /* Register WebAssembly on global */
    JS_SetPropertyStr(ctx, global, "WebAssembly", wasm_obj);

    JS_FreeValue(ctx, global);

    (void)ext;
    return 0;
}

static void wamr_ext_destroy(qwrt_ext_t *ext, qwrt_t *rt)
{
    (void)ext;
    /* Reset class IDs so JS_NewClassID allocates fresh ones for the next runtime */
    rt->wamr_module_class_id = 0;
    rt->wamr_instance_class_id = 0;
}

static int wamr_ext_suspend(qwrt_ext_t *ext, qwrt_t *rt)
{
    (void)ext;
    (void)rt;
    return 0;
}

static int wamr_ext_resume(qwrt_ext_t *ext, qwrt_t *rt)
{
    (void)ext;
    (void)rt;
    return 0;
}

#else /* !QWRT_HAS_WAMR — stub implementation */

/* ================================================================
 * Stub WebAssembly implementation — throws "engine not linked"
 * ================================================================ */

static JSValue wamr_throw_not_linked(JSContext *ctx, JSValueConst this_val,
                                     int argc, JSValueConst *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_ThrowTypeError(ctx, "WebAssembly WAMR engine not linked");
}

static const JSCFunctionListEntry wamr_module_funcs[] = {
    JS_CFUNC_DEF("validate", 1, wamr_throw_not_linked),
    JS_CFUNC_DEF("compile", 1, wamr_throw_not_linked),
    JS_CFUNC_DEF("instantiate", 1, wamr_throw_not_linked),
};

static JSValue wamr_stub_constructor(JSContext *ctx, JSValueConst new_target,
                                     int argc, JSValueConst *argv)
{
    (void)new_target;
    (void)argc;
    (void)argv;
    return JS_ThrowTypeError(ctx, "WebAssembly: WAMR engine not linked");
}

static int wamr_ext_init(qwrt_ext_t *ext, qwrt_t *rt)
{
    JSContext *ctx = qwrt_get_jsctx(rt);
    if (!ctx) return -1;

    JSValue global = JS_GetGlobalObject(ctx);

    JSAtom wasm_atom = JS_NewAtom(ctx, "WebAssembly");
    int has_wasm = JS_HasProperty(ctx, global, wasm_atom);
    JS_FreeAtom(ctx, wasm_atom);

    if (has_wasm) {
        JS_FreeValue(ctx, global);
        return 0;
    }

    JSValue wasm_obj = JS_NewObject(ctx);
    if (JS_IsException(wasm_obj)) {
        JS_FreeValue(ctx, global);
        return -1;
    }

    JS_SetPropertyFunctionList(ctx, wasm_obj, wamr_module_funcs,
                               sizeof(wamr_module_funcs) / sizeof(wamr_module_funcs[0]));

    JSValue module_ctor = JS_NewCFunction2(ctx, wamr_stub_constructor,
                                           "Module", 1, JS_CFUNC_constructor, 0);
    JSValue instance_ctor = JS_NewCFunction2(ctx, wamr_stub_constructor,
                                             "Instance", 1, JS_CFUNC_constructor, 0);
    JSValue memory_ctor = JS_NewCFunction2(ctx, wamr_stub_constructor,
                                           "Memory", 1, JS_CFUNC_constructor, 0);
    JSValue table_ctor = JS_NewCFunction2(ctx, wamr_stub_constructor,
                                          "Table", 1, JS_CFUNC_constructor, 0);
    JSValue global_ctor = JS_NewCFunction2(ctx, wamr_stub_constructor,
                                           "Global", 1, JS_CFUNC_constructor, 0);

    JS_SetPropertyStr(ctx, wasm_obj, "Module", module_ctor);
    JS_SetPropertyStr(ctx, wasm_obj, "Instance", instance_ctor);
    JS_SetPropertyStr(ctx, wasm_obj, "Memory", memory_ctor);
    JS_SetPropertyStr(ctx, wasm_obj, "Table", table_ctor);
    JS_SetPropertyStr(ctx, wasm_obj, "Global", global_ctor);

    JSValue compile_error = JS_NewError(ctx);
    JS_SetPropertyStr(ctx, wasm_obj, "CompileError", compile_error);

    JSValue link_error = JS_NewError(ctx);
    JS_SetPropertyStr(ctx, wasm_obj, "LinkError", link_error);

    JSValue runtime_error = JS_NewError(ctx);
    JS_SetPropertyStr(ctx, wasm_obj, "RuntimeError", runtime_error);

    JS_SetPropertyStr(ctx, global, "WebAssembly", wasm_obj);

    JS_FreeValue(ctx, global);

    (void)ext;
    return 0;
}

static void wamr_ext_destroy(qwrt_ext_t *ext, qwrt_t *rt)
{
    (void)ext;
    (void)rt;
}

static int wamr_ext_suspend(qwrt_ext_t *ext, qwrt_t *rt)
{
    (void)ext;
    (void)rt;
    return 0;
}

static int wamr_ext_resume(qwrt_ext_t *ext, qwrt_t *rt)
{
    (void)ext;
    (void)rt;
    return 0;
}

#endif /* QWRT_HAS_WAMR */

/* ================================================================
 * Extension definition
 * ================================================================ */

const qwrt_ext_t qwrt_wamr_ext = {
    .name = "wamr",
    .init = wamr_ext_init,
    .destroy = wamr_ext_destroy,
    .suspend = wamr_ext_suspend,
    .resume = wamr_ext_resume,
    .user_data = NULL,
};

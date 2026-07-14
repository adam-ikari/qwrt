/*
 * qwrt Core Runtime
 *
 * Main implementation: create/destroy/tick/eval/call
 * Multi-context support: reset/spawn/suspend/resume/destroy_ctx
 */

#include "qwrt_internal.h"
#include <stdlib.h>
#include <string.h>

#ifdef QWRT_DEBUG_SUPPORT
#include "qwrt/qwrt_debug_dap.h"
#endif

/* ================================================================
 * qwrt_create - Create a new qwrt runtime with initial context
 * ================================================================ */

qwrt_t *qwrt_create(const qwrt_config_t *config)
{
    if (!config || !config->pal) {
        return NULL;
    }

    qwrt_pal_t *pal = (qwrt_pal_t *)config->pal;
    qwrt_t *rt = NULL;

    /* Allocate qwrt_t via pal->mem_alloc if available, else malloc */
    if (pal->mem_alloc) {
        rt = (qwrt_t *)pal->mem_alloc(pal, sizeof(qwrt_t));
    } else {
        rt = (qwrt_t *)malloc(sizeof(qwrt_t));
    }

    if (!rt) {
        return NULL;
    }

    /* Zero-initialize the struct */
    memset(rt, 0, sizeof(qwrt_t));

    /* Set magic sentinel for opaque pointer validation */
    rt->magic = QWRT_MAGIC;

    /* Create JSRuntime (shared across all contexts) */
    rt->jsrt = JS_NewRuntime();
    if (!rt->jsrt) {
        if (pal->mem_free) {
            pal->mem_free(pal, rt);
        } else {
            free(rt);
        }
        return NULL;
    }

    /* Set runtime opaque to the qwrt_t pointer - critical for bridge.c */
    JS_SetRuntimeOpaque(rt->jsrt, rt);

    /* Initialize context table */
    rt->context_count = 0;
    rt->active_ctx_id = -1;
    rt->debug = config->debug;
    rt->host_data = config->host_data;  /* readable by ext init via qwrt_get_runtime_data */
    rt->has_pending_jobs = 0;
    rt->deferred_cb_head = NULL;
    rt->deferred_cb_tail = NULL;

    for (int i = 0; i < QWRT_MAX_CONTEXTS; i++) {
        rt->contexts[i] = NULL;
    }

    /* Create initial context */
    qwrt_ctx_t *ctx = qwrt_ctx_create(rt, config);
    if (!ctx) {
        JS_FreeRuntime(rt->jsrt);
        if (pal->mem_free) {
            pal->mem_free(pal, rt);
        } else {
            free(rt);
        }
        return NULL;
    }

    /* Set initial context as active */
    ctx->active = 1;
    rt->active_ctx_id = ctx->context_id;

#ifdef QWRT_DEBUG_SUPPORT
    /* Auto-attach the DAP debugger when enabled. Two activation paths, both
     * funneling into qwrt_dap_attach + qwrt_dap_configure:
     *   - env var QWRT_DEBUG=1  (host code unchanged — just run with the env)
     *   - config->debug enable-bit (bit 1; bit 0 is the legacy verbose-log flag)
     * qwrt_dap_configure blocks here reading DAP initialize/setBreakpoints/
     * configurationDone from the client, so breakpoints are armed before the
     * host's first qwrt_eval. stop_on_entry pauses at the first opcode. */
    {
        int enable = 0;
        const char *env = getenv("QWRT_DEBUG");
        if (env && (env[0] == '1' || env[0] == 't' || env[0] == 'T'))
            enable = 1;
        if (config->debug & 0x2)  /* bit 1 = debug-enable */
            enable = 1;
        if (enable) {
            qwrt_dap_config_t dcfg;
            dcfg.stop_on_entry = 1;
            dcfg.in = NULL;   /* stdin */
            dcfg.out = NULL;  /* stdout */
            if (qwrt_dap_attach(rt, &dcfg) == 0) {
                qwrt_dap_configure(rt);  /* blocks until configurationDone */
            }
        }
    }
#endif

    return rt;
}

/* ================================================================
 * qwrt_destroy - Destroy a qwrt runtime and all contexts
 * ================================================================ */

void qwrt_destroy(qwrt_t *rt)
{
    if (!rt || rt->magic != QWRT_MAGIC) {
        return;
    }

    qwrt_pal_t *pal = NULL;
    JSContext *drain_ctx = NULL;

    /* Get PAL + a valid JSContext from the first context. The JSContext is
     * needed to drain deferred PAL callbacks (they call JS functions, which
     * require a valid context) before we tear everything down. */
    if (rt->context_count > 0) {
        for (int i = 0; i < QWRT_MAX_CONTEXTS; i++) {
            if (rt->contexts[i]) {
                pal = (qwrt_pal_t *)rt->contexts[i]->pal;
                drain_ctx = rt->contexts[i]->jsctx;
                break;
            }
        }
    }

    /* Drain deferred PAL callbacks + pending JS jobs BEFORE freeing contexts
     * and the runtime. PAL callbacks (libuv, etc.) and Promise reactions can
     * keep JS closures/values reachable; if we skip this, JS_FreeRuntime
     * aborts on a non-empty gc_obj_list (the test_ace_core teardown crash:
     * 51/51 pass, then JS_FreeRuntime: Assertion 'list_empty(&rt->gc_obj_list)'
     * failed). Mirrors the drain loop in qwrt_tick(). */
    if (rt->jsrt && drain_ctx) {
        int jobs_processed = 0;
        do {
            jobs_processed = 0;

            /* Step 1: Drain deferred PAL callbacks (call JS from a valid
             * context, in the qwrt_destroy stack frame). */
            while (rt->deferred_cb_head) {
                struct pal_deferred_cb *cb = rt->deferred_cb_head;
                rt->deferred_cb_head = cb->next;
                if (!rt->deferred_cb_head) rt->deferred_cb_tail = NULL;

                cb->fn(cb->data);
                free(cb);
                jobs_processed++;
            }

            /* Step 2: Execute pending JS jobs (Promise reactions enqueued by
             * step 1). The returned context is unused here. */
            JSContext *job_ctx = NULL;
            int ret;
            while ((ret = JS_ExecutePendingJob(rt->jsrt, &job_ctx)) > 0) {
                jobs_processed++;
            }
            /* ret == 0: drained; ret < 0: job error — continue teardown. */
        } while (jobs_processed > 0);
    }

    /* Destroy all contexts */
    for (int i = 0; i < QWRT_MAX_CONTEXTS; i++) {
        if (rt->contexts[i]) {
            qwrt_ctx_destroy(rt, rt->contexts[i]);
        }
    }

    /* Free the JSRuntime (gc_obj_list is now empty) */
    if (rt->jsrt) {
        JS_FreeRuntime(rt->jsrt);
    }

#ifdef QWRT_DEBUG_SUPPORT
    /* Detach the DAP debugger session if one was attached. */
    if (rt->dbg_session) {
        qwrt_dap_detach(rt);
        rt->dbg_session = NULL;
    }
#endif

    /* Clear magic before freeing */
    rt->magic = 0;

    /* Free the qwrt_t struct */
    if (pal && pal->mem_free) {
        pal->mem_free(pal, rt);
    } else {
        free(rt);
    }
}

/* ================================================================
 * qwrt_tick - Process pending JS jobs
 * ================================================================ */

int qwrt_tick(qwrt_t *rt)
{
    if (!rt || !rt->jsrt) {
        return -1;
    }

    JSContext *ctx1;
    int ret;
    int jobs_processed = 0;

    /* Clear the flag before processing - bridge callbacks set it */
    rt->has_pending_jobs = 0;

    /* Loop until both deferred callbacks and JS jobs are exhausted.
     * JS promise jobs can trigger new PAL calls (e.g. storage.set),
     * which enqueue new deferred callbacks. We must keep draining
     * both queues until neither has work. */
    do {
        jobs_processed = 0;

        /* Step 1: Drain deferred PAL callbacks (libuv callbacks enqueue here).
         * These call JS functions (resolve/reject promises, stream callbacks)
         * from the qwrt_tick stack frame, which IS a valid QuickJS context. */
        while (rt->deferred_cb_head) {
            struct pal_deferred_cb *cb = rt->deferred_cb_head;
            rt->deferred_cb_head = cb->next;
            if (!rt->deferred_cb_head) rt->deferred_cb_tail = NULL;

            cb->fn(cb->data);
            free(cb);
            jobs_processed++;
        }

        /* Step 2: Execute pending JS jobs (Promise reactions enqueued by step 1) */
        while ((ret = JS_ExecutePendingJob(rt->jsrt, &ctx1)) > 0) {
            jobs_processed++;
        }

        /* ret == 0: no more jobs, ret < 0: error */
        if (ret < 0) {
            return -1;
        }
    } while (jobs_processed > 0);

    return 0;
}

/* ================================================================
 * qwrt_defer_callback - Enqueue a callback to be executed in
 * qwrt_tick's stack frame. Used by libuv PAL callbacks that need
 * to call JS functions (JS_Call only works from a valid JS context).
 * ================================================================ */

void qwrt_defer_callback(qwrt_t *rt, void (*fn)(void *data), void *data)
{
    if (!rt || !fn) return;

    struct pal_deferred_cb *cb = malloc(sizeof(*cb));
    if (!cb) {
        if (rt->debug) {
            fprintf(stderr, "[qwrt] deferred callback allocation failed\n");
        }
        return;
    }
    cb->fn = fn;
    cb->data = data;
    cb->next = NULL;

    if (rt->deferred_cb_tail) {
        rt->deferred_cb_tail->next = cb;
    } else {
        rt->deferred_cb_head = cb;
    }
    rt->deferred_cb_tail = cb;
    rt->has_pending_jobs = 1;
}

/* ================================================================
 * qwrt_eval_bytecode - Evaluate pre-compiled QuickJS bytecode
 * ================================================================ */

int qwrt_eval_bytecode(qwrt_t *rt, const uint8_t *bytecode, size_t len,
                       char **result)
{
    if (!rt || !bytecode || len == 0) {
        if (result) *result = NULL;
        return -1;
    }

    if (result) *result = NULL;

    JSContext *ctx = qwrt_get_active_jsctx(rt);
    if (!ctx) {
        return -1;
    }

    JSValue obj = JS_ReadObject(ctx, bytecode, len, JS_READ_OBJ_BYTECODE);
    if (JS_IsException(obj)) {
        if (rt->debug) {
            JSValue exc = JS_GetException(ctx);
            const char *str = JS_ToCString(ctx, exc);
            fprintf(stderr, "[qwrt] bytecode load error: %s\n", str);
            JS_FreeCString(ctx, str);
            JS_FreeValue(ctx, exc);
        }
        JS_FreeValue(ctx, obj);
        return -1;
    }

    /* Evaluate the bytecode object (handles both modules and scripts) */
    JSValue val = JS_EvalFunction(ctx, obj);
    if (JS_IsException(val)) {
        if (rt->debug) {
            JSValue exc = JS_GetException(ctx);
            const char *str = JS_ToCString(ctx, exc);
            fprintf(stderr, "[qwrt] bytecode eval error: %s\n", str);
            JS_FreeCString(ctx, str);
            JS_FreeValue(ctx, exc);
        }
        JS_FreeValue(ctx, val);
        return -1;
    }

    if (result != NULL) {
        JSValue res = val;
        int res_is_separate = 0;
        /* Modules return a Promise — run pending jobs so it resolves,
         * then extract the fulfillment value */
        if (JS_IsPromise(val)) {
            /* Run pending jobs to advance Promise state */
            qwrt_tick(rt);
            JSPromiseStateEnum state = JS_PromiseState(ctx, val);
            if (state == JS_PROMISE_FULFILLED) {
                res = JS_PromiseResult(ctx, val);
                res_is_separate = 1;
            } else if (state == JS_PROMISE_REJECTED) {
                /* Extract rejection reason */
                res = JS_PromiseResult(ctx, val);
                res_is_separate = 1;
            }
            /* Pending: stringify the Promise itself (gives undefined) */
        }
        JSValue json_val = JS_JSONStringify(ctx, res, JS_UNDEFINED, JS_UNDEFINED);
        if (JS_IsException(json_val)) {
            if (res_is_separate) JS_FreeValue(ctx, res);
            JS_FreeValue(ctx, val);
            JS_FreeValue(ctx, json_val);
            return -1;
        }
        const char *json_str = JS_ToCString(ctx, json_val);
        if (json_str) {
            *result = strdup(json_str);
            JS_FreeCString(ctx, json_str);
        } else {
            *result = NULL;
        }
        JS_FreeValue(ctx, json_val);
        if (res_is_separate) JS_FreeValue(ctx, res);
    }

    JS_FreeValue(ctx, val);
    return 0;
}

/* ================================================================
 * qwrt_eval - Evaluate JS code
 * ================================================================ */

int qwrt_eval(qwrt_t *rt, const char *code, char **result)
{
    if (!rt || !code) {
        return -1;
    }

    JSContext *ctx = qwrt_get_active_jsctx(rt);
    if (!ctx) {
        return -1;
    }

    /* Evaluate the code */
    JSValue val = JS_Eval(ctx, code, strlen(code), "<eval>", JS_EVAL_TYPE_GLOBAL);

    if (JS_IsException(val)) {
        JS_FreeValue(ctx, val);
        return -1;
    }

    /* If result is requested, convert to JSON string */
    if (result != NULL) {
        JSValue json_val = JS_JSONStringify(ctx, val, JS_UNDEFINED, JS_UNDEFINED);
        if (JS_IsException(json_val)) {
            JS_FreeValue(ctx, val);
            JS_FreeValue(ctx, json_val);
            return -1;
        }

        const char *json_str = JS_ToCString(ctx, json_val);
        if (json_str) {
            /* Duplicate the string for caller ownership */
            *result = strdup(json_str);
            JS_FreeCString(ctx, json_str);
        } else {
            *result = NULL;
        }

        JS_FreeValue(ctx, json_val);
    }

    JS_FreeValue(ctx, val);
    return 0;
}

/* ================================================================
 * qwrt_call - Call a JS function by name
 * ================================================================ */

int qwrt_call(qwrt_t *rt, const char *func, const char *args_json, char **result)
{
    if (!rt || !func) {
        return -1;
    }

    JSContext *ctx = qwrt_get_active_jsctx(rt);
    if (!ctx) {
        return -1;
    }

    JSValue global = JS_GetGlobalObject(ctx);

    /* Look up the function by name */
    JSValue func_val = JS_GetPropertyStr(ctx, global, func);
    JS_FreeValue(ctx, global);

    if (JS_IsException(func_val) || JS_IsUndefined(func_val)) {
        JS_FreeValue(ctx, func_val);
        return -1;
    }

    /* Check if it's actually a function */
    if (!JS_IsFunction(ctx, func_val)) {
        JS_FreeValue(ctx, func_val);
        return -1;
    }

    /* Parse args_json as a JSON array */
    int argc = 0;
    JSValue *args = NULL;
    JSValue args_array = JS_UNDEFINED;

    if (args_json != NULL && *args_json != '\0') {
        args_array = JS_ParseJSON(ctx, args_json, strlen(args_json), "<args>");
        if (JS_IsException(args_array)) {
            JS_FreeValue(ctx, func_val);
            JS_FreeValue(ctx, args_array);
            return -1;
        }

        /* Get array length */
        JSValue len_val = JS_GetPropertyStr(ctx, args_array, "length");
        if (JS_IsException(len_val)) {
            JS_FreeValue(ctx, func_val);
            JS_FreeValue(ctx, args_array);
            JS_FreeValue(ctx, len_val);
            return -1;
        }

        int32_t len;
        if (JS_ToInt32(ctx, &len, len_val) < 0) {
            JS_FreeValue(ctx, func_val);
            JS_FreeValue(ctx, args_array);
            JS_FreeValue(ctx, len_val);
            return -1;
        }
        JS_FreeValue(ctx, len_val);

        argc = len;

        /* Allocate args array (C99 compatible - no VLA) */
        if (argc > 0) {
            args = (JSValue *)malloc(sizeof(JSValue) * (size_t)argc);
            if (!args) {
                JS_FreeValue(ctx, func_val);
                JS_FreeValue(ctx, args_array);
                return -1;
            }

            /* Extract elements from the JSON array */
            for (int i = 0; i < argc; i++) {
                JSAtom atom = JS_NewAtomUInt32(ctx, (uint32_t)i);
                args[i] = JS_GetProperty(ctx, args_array, atom);
                JS_FreeAtom(ctx, atom);
                if (JS_IsException(args[i])) {
                    /* Free already extracted args */
                    for (int j = 0; j < i; j++) {
                        JS_FreeValue(ctx, args[j]);
                    }
                    free(args);
                    JS_FreeValue(ctx, func_val);
                    JS_FreeValue(ctx, args_array);
                    return -1;
                }
            }
        }
    }

    /* Call the function */
    JSValue call_result = JS_Call(ctx, func_val, JS_UNDEFINED, argc, args);

    /* Free args */
    if (args != NULL) {
        for (int i = 0; i < argc; i++) {
            JS_FreeValue(ctx, args[i]);
        }
        free(args);
    }

    /* Free args_array if we created it */
    if (!JS_IsUndefined(args_array)) {
        JS_FreeValue(ctx, args_array);
    }

    JS_FreeValue(ctx, func_val);

    if (JS_IsException(call_result)) {
        JS_FreeValue(ctx, call_result);
        return -1;
    }

    /* If result is requested, convert to JSON string */
    if (result != NULL) {
        JSValue json_val = JS_JSONStringify(ctx, call_result, JS_UNDEFINED, JS_UNDEFINED);
        if (JS_IsException(json_val)) {
            JS_FreeValue(ctx, call_result);
            JS_FreeValue(ctx, json_val);
            return -1;
        }

        const char *json_str = JS_ToCString(ctx, json_val);
        if (json_str) {
            *result = strdup(json_str);
            JS_FreeCString(ctx, json_str);
        } else {
            *result = NULL;
        }

        JS_FreeValue(ctx, json_val);
    }

    JS_FreeValue(ctx, call_result);
    return 0;
}

/* ================================================================
 * qwrt_free - Free memory
 * ================================================================ */

void qwrt_free(void *ptr)
{
    if (ptr) {
        free(ptr);
    }
}

/* ================================================================
 * Multi-context API
 * ================================================================ */

/* qwrt_reset - destroy active context, create new one with new config */
int qwrt_reset(qwrt_t *rt, const qwrt_config_t *config)
{
    if (!rt || rt->magic != QWRT_MAGIC) {
        return -1;
    }
    if (!config || !config->pal) {
        return -1;
    }

    /* Get the active context */
    qwrt_ctx_t *old_ctx = qwrt_get_active_ctx(rt);
    if (!old_ctx) {
        return -1;
    }

    /* Create new context first (so we don't lose the runtime if it fails) */
    qwrt_ctx_t *new_ctx = qwrt_ctx_create(rt, config);
    if (!new_ctx) {
        return -1;
    }

    /* Destroy the old context */
    qwrt_ctx_destroy(rt, old_ctx);

    /* Set new context as active */
    new_ctx->active = 1;
    rt->active_ctx_id = new_ctx->context_id;

    return 0;
}

/* qwrt_spawn - create new context in suspended state, return context_id */
int qwrt_spawn(qwrt_t *rt, const qwrt_config_t *config)
{
    if (!rt || rt->magic != QWRT_MAGIC) {
        return -1;
    }
    if (!config || !config->pal) {
        return -1;
    }

    /* Create new context */
    qwrt_ctx_t *ctx = qwrt_ctx_create(rt, config);
    if (!ctx) {
        return -1;
    }

    /* Mark as suspended (not active) */
    ctx->suspended = 1;
    ctx->active = 0;

    return ctx->context_id;
}

/* qwrt_suspend - call ext_suspend_all on active context, mark suspended */
int qwrt_suspend(qwrt_t *rt)
{
    if (!rt || rt->magic != QWRT_MAGIC) {
        return -1;
    }

    qwrt_ctx_t *ctx = qwrt_get_active_ctx(rt);
    if (!ctx) {
        return -1;
    }

    /* Call extension suspend hooks */
    if (qwrt_ext_suspend_all(rt, ctx) < 0) {
        return -1;
    }

    /* Mark as suspended and clear active */
    ctx->suspended = 1;
    ctx->active = 0;
    rt->active_ctx_id = -1;

    return 0;
}

/* qwrt_resume - auto-suspend current active context, mark target as active,
 * call ext_resume_all */
int qwrt_resume(qwrt_t *rt, int context_id)
{
    if (!rt || rt->magic != QWRT_MAGIC) {
        return -1;
    }

    qwrt_ctx_t *target_ctx = qwrt_get_ctx_by_id(rt, context_id);
    if (!target_ctx) {
        return -1;
    }

    /* Suspend current active context if any */
    if (rt->active_ctx_id >= 0) {
        qwrt_ctx_t *current_ctx = qwrt_get_active_ctx(rt);
        if (current_ctx && current_ctx != target_ctx) {
            if (qwrt_ext_suspend_all(rt, current_ctx) < 0) {
                return -1;
            }
            current_ctx->suspended = 1;
            current_ctx->active = 0;
        }
    }

    /* Call extension resume hooks on target */
    if (qwrt_ext_resume_all(rt, target_ctx) < 0) {
        return -1;
    }

    /* Mark target as active */
    target_ctx->suspended = 0;
    target_ctx->active = 1;
    rt->active_ctx_id = context_id;

    return 0;
}

/* qwrt_destroy_ctx - destroy a specific context (fail if only 1 remains) */
int qwrt_destroy_ctx(qwrt_t *rt, int context_id)
{
    if (!rt || rt->magic != QWRT_MAGIC) {
        return -1;
    }

    /* Don't allow destroying the last context */
    if (rt->context_count <= 1) {
        return -1;
    }

    qwrt_ctx_t *ctx = qwrt_get_ctx_by_id(rt, context_id);
    if (!ctx) {
        return -1;
    }

    qwrt_ctx_destroy(rt, ctx);
    return 0;
}

/* qwrt_get_active_ctx_id - return active context id, or -1 if none */
int qwrt_get_active_ctx_id(qwrt_t *rt)
{
    if (!rt || rt->magic != QWRT_MAGIC) {
        return -1;
    }
    return rt->active_ctx_id;
}

/* qwrt_get_jsctx - return active JSContext cast to struct JSContext* */
struct JSContext *qwrt_get_jsctx(qwrt_t *rt)
{
    if (!rt || rt->magic != QWRT_MAGIC) {
        return NULL;
    }
    return (struct JSContext *)qwrt_get_active_jsctx(rt);
}

/* qwrt_get_runtime_data / qwrt_set_runtime_data - per-runtime host data. */
void *qwrt_get_runtime_data(qwrt_t *rt)
{
    if (!rt || rt->magic != QWRT_MAGIC) {
        return NULL;
    }
    return rt->host_data;
}

void qwrt_set_runtime_data(qwrt_t *rt, void *data)
{
    if (!rt || rt->magic != QWRT_MAGIC) {
        return;
    }
    rt->host_data = data;
}

/* ================================================================
 * Bytecode compilation
 * ================================================================ */

uint8_t *qwrt_compile(qwrt_t *rt, const char *code, size_t code_len,
                      size_t *out_len)
{
    JSContext *ctx = qwrt_get_active_jsctx(rt);
    if (!ctx || !code || !out_len) return NULL;

    *out_len = 0;

    JSValue obj = JS_Eval(ctx, code, code_len, "<compile>",
                          JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(obj)) return NULL;

    size_t len = 0;
    uint8_t *buf = JS_WriteObject(ctx, &len, obj, JS_WRITE_OBJ_BYTECODE);
    JS_FreeValue(ctx, obj);

    if (!buf) return NULL;
    *out_len = len;
    return buf;
}

uint8_t *qwrt_compile_module(qwrt_t *rt, const char *code, size_t code_len,
                             size_t *out_len)
{
    JSContext *ctx = qwrt_get_active_jsctx(rt);
    if (!ctx || !code || !out_len) return NULL;

    *out_len = 0;

    JSValue obj = JS_Eval(ctx, code, code_len, "<compile>",
                          JS_EVAL_FLAG_COMPILE_ONLY | JS_EVAL_TYPE_MODULE);
    if (JS_IsException(obj)) return NULL;

    size_t len = 0;
    uint8_t *buf = JS_WriteObject(ctx, &len, obj, JS_WRITE_OBJ_BYTECODE);
    JS_FreeValue(ctx, obj);

    if (!buf) return NULL;
    *out_len = len;
    return buf;
}

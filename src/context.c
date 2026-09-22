/*
 * amoib Context Lifecycle
 *
 * Context creation, destruction, and helper functions for multi-context support.
 */

#include "am_internal.h"
#include "amoib/am_ext_registry.h"   /* AM_EXTENSIONS table */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ================================================================
 * Compile-time extension table
 *
 * AM_EXTENSIONS expands to a list of `const am_ext_t *` (possibly with
 * NULL slots for built-ins whose AM_WITH_* is off). Not NULL-terminated
 * (NULL slots would be indistinguishable from a terminator); iterate by count
 * and skip NULL. Trailing comma is legal in a C99 array initializer. */
static const am_ext_t *am_default_exts[] = {
    AM_EXTENSIONS
};
static const int am_default_exts_count =
    (int)(sizeof(am_default_exts) / sizeof(am_default_exts[0]));

/* ================================================================
 * Context getter helpers
 * ================================================================ */

am_ctx_t *am_get_active_ctx(am_t *rt)
{
    if (!rt || rt->magic != AM_MAGIC) {
        return NULL;
    }
    if (rt->active_ctx_id < 0 || rt->active_ctx_id >= AM_MAX_CONTEXTS) {
        return NULL;
    }
    return rt->contexts[rt->active_ctx_id];
}

JSContext *am_get_active_jsctx(am_t *rt)
{
    am_ctx_t *ctx = am_get_active_ctx(rt);
    if (!ctx) {
        return NULL;
    }
    return ctx->jsctx;
}

am_ctx_t *am_get_ctx_by_id(am_t *rt, int context_id)
{
    if (!rt || rt->magic != AM_MAGIC) {
        return NULL;
    }
    if (context_id < 0 || context_id >= AM_MAX_CONTEXTS) {
        return NULL;
    }
    return rt->contexts[context_id];
}

/* ================================================================
 * Context resource cleanup
 * ================================================================ */

void am_ctx_cleanup_resources(am_t *rt, am_ctx_t *ctx)
{
    if (!rt || !ctx) {
        return;
    }

    /* Stop all active timers and free associated resources */
    for (int i = 0; i < ctx->handle_count; i++) {
        am_timer_cancel(ctx, i);
    }

    ctx->handle_count = 0;
}

/* ================================================================
 * Context creation
 * ================================================================ */

static am_ctx_t *am_ctx_create_at(am_t *rt, int slot);

am_ctx_t *am_ctx_create(am_t *rt, const am_config_t *config)
{
    if (!rt || rt->magic != AM_MAGIC) {
        return NULL;
    }
    (void)config;   /* ctx_create no longer takes a PAL; config lives on rt */
    /* Find a free context slot */
    for (int i = 0; i < AM_MAX_CONTEXTS; i++) {
        if (rt->contexts[i] == NULL) {
            return am_ctx_create_at(rt, i);
        }
    }
    return NULL;  /* No free slots */
}

/* 在指定槽位创建 context（Task 5 恢复挂起的 context 用）。调用方保证槽位空闲。 */
static am_ctx_t *am_ctx_create_at(am_t *rt, int slot)
{
    /* Allocate am_ctx_t */
    am_ctx_t *ctx = (am_ctx_t *)calloc(1, sizeof(am_ctx_t));
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
    ctx->suspended = 0;
    ctx->handle_count = 0;
    /* Extensions: point at the compile-time AM_EXTENSIONS table (no per-context
     * heap allocation). NULL slots (disabled built-ins) are skipped at init. */
    ctx->extensions = am_default_exts;
    ctx->extensions_count = am_default_exts_count;

    /* Polyfill bytecode: load once per runtime (lazily at first context
     * creation) and share across all contexts — modes A/B hold a heap copy,
     * mode C hands out the compile-time const array, mode D the host hook.
     * rt->polyfill is unloaded at runtime teardown (am_thread_teardown). */
    if (!rt->polyfill) {
        const uint8_t *data = NULL;
        size_t len = 0;
        void *owner = NULL;
        if (am_polyfill_load(&data, &len, &owner) != 0) {
            JS_FreeContext(ctx->jsctx);
            free(ctx);
            return NULL;
        }
        rt->polyfill = data;
        rt->polyfill_len = len;
        rt->polyfill_owner = owner;
    }
    ctx->polyfill = rt->polyfill;
    ctx->polyfill_len = rt->polyfill_len;

    /* Initialize handle arrays to zero/NULL */
    for (int i = 0; i < AM_MAX_HANDLES; i++) {
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
        JSValue pal_obj = am_create_pal_object_ctx(rt, ctx);
        if (JS_IsException(pal_obj)) {            rt->active_ctx_id = prev_active;
            rt->contexts[slot] = NULL;
            rt->context_count--;
            JS_FreeContext(ctx->jsctx);
            free(ctx);
            return NULL;
        }
        JSValue global = JS_GetGlobalObject(ctx->jsctx);
        JS_SetPropertyStr(ctx->jsctx, global, "__native_inject__", pal_obj);
        JS_FreeValue(ctx->jsctx, global);

        /* Load and evaluate bytecode */
        JSValue obj = JS_ReadObject(ctx->jsctx, ctx->polyfill, ctx->polyfill_len, JS_READ_OBJ_BYTECODE);
        int bc_ok = 1;
        if (JS_IsException(obj)) {            bc_ok = 0;
        } else {
            JSValue val = JS_EvalFunction(ctx->jsctx, obj);
            if (JS_IsException(val)) {                JS_FreeValue(ctx->jsctx, val);
                bc_ok = 0;
            } else {
                JS_FreeValue(ctx->jsctx, val);
            }
        }

        /* Remove __native_inject__ from globalThis but keep the native object
         * accessible as __native__ for extension init hooks to register
         * functions on it. The polyfill's closures reference the same
         * object, so extensions adding properties here are visible. */
        global = JS_GetGlobalObject(ctx->jsctx);
        JSAtom inject_atom = JS_NewAtom(ctx->jsctx, "__native_inject__");
        JSValue pal_ref = JS_GetProperty(ctx->jsctx, global, inject_atom);
        JS_DeleteProperty(ctx->jsctx, global, inject_atom, 0);
        JS_FreeAtom(ctx->jsctx, inject_atom);
        if (!JS_IsUndefined(pal_ref) && !JS_IsException(pal_ref)) {
            JS_SetPropertyStr(ctx->jsctx, global, "__native__", pal_ref);
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

    /* Remove QuickJS built-in WebAssembly - it can parse but not execute
     * WASM, which is misleading. The wasm3 extension registers its own working
     * WebAssembly implementation in its init hook. If no WASM extension is
     * present, WebAssembly is simply unavailable. */
    {
        JSValue global = JS_GetGlobalObject(ctx->jsctx);
        JSAtom wasm_atom = JS_NewAtom(ctx->jsctx, "WebAssembly");
        JS_DeleteProperty(ctx->jsctx, global, wasm_atom, 0);
        JS_FreeAtom(ctx->jsctx, wasm_atom);
        JS_FreeValue(ctx->jsctx, global);
    }

    /* Call extension init hooks */
    if (am_ext_init_all(rt, ctx) < 0) {
        /* Undo registration on failure */
        rt->active_ctx_id = prev_active;
        rt->contexts[slot] = NULL;
        rt->context_count--;
        am_ctx_cleanup_resources(rt, ctx);
        JS_FreeContext(ctx->jsctx);
        free(ctx);
        return NULL;
    }

    return ctx;
}

/* ================================================================
 * Context destruction
 * ================================================================ */

void am_ctx_destroy(am_t *rt, am_ctx_t *ctx)
{
    if (!rt || !ctx) {
        return;
    }

    int context_id = ctx->context_id;

    /* Call extension destroy hooks */
    am_ext_destroy_all(rt, ctx);

    /* Cleanup resources (timers, handles, etc.) */
    am_ctx_cleanup_resources(rt, ctx);

    /* G2：先排空 rt->job_list 中属于本 ctx 的 pending job（如未执行的
     * Promise 反应）。JS_FreeContext 不清 job 队列，残留 entry 会持悬垂
     * ctx 指针，下一轮 JS_ExecutePendingJob 即 UAF。 */
    if (ctx->jsctx && rt->jsrt) {
        JS_DrainPendingJobsForContext(rt->jsrt, ctx->jsctx);
    }
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

    /* Free the ctx struct */
    free(ctx);
}

/* ================================================================
 * Multi-context + soft suspend/resume (Task 5)
 *
 * 宿主只见主 context；spawn/suspend/resume/destroy 由 polyfill 的 amContext
 * 经 bridge 驱动。挂起 = 把目标 ctx 可克隆的全局属性收集成结构化克隆字节写盘，
 * 然后销毁该 JSContext；恢复 = 在原槽位重建 JSContext + 重注入 polyfill（
 * am_ctx_create_at 内做）+ 重 eval 脚本 + 反序列化状态回 globalThis。
 * 序列化/反序列化的可克隆判定在 JS 侧（context.js 的 __am_ctx_capture__ /
 * __am_ctx_restore__，复用 Task 4 的序列化内核）——C 只做边界转换。
 * ================================================================ */

/* 同步写盘（state_path）。挂起必须完整落盘后才能销毁 ctx，所以不用异步 IO。 */
static int am_write_file(const char *path, const char *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int ok = (fwrite(data, 1, len, f) == len) ? 0 : -1;
    if (fclose(f) != 0) ok = -1;
    return ok;
}

static int am_read_file(const char *path, char **out, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return -1; }
    rewind(f);
    char *buf = (char *)malloc(sz ? (size_t)sz : 1);
    if (!buf) { fclose(f); return -1; }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    *out = buf;
    *out_len = n;
    return 0;
}

/* 调目标 ctx 的 __am_ctx_restore__ 把状态字节写回其 globalThis */
static int am_ctx_restore_bytes(am_t *rt, am_ctx_t *cctx,
                                  const char *data, size_t len)
{
    AM_UNUSED(rt);
    JSContext *ctx = cctx->jsctx;
    JSValue g = JS_GetGlobalObject(ctx);
    JSValue fn = JS_GetPropertyStr(ctx, g, "__am_ctx_restore__");
    JS_FreeValue(ctx, g);
    if (!JS_IsFunction(ctx, fn)) {
        JS_FreeValue(ctx, fn);
        return AM_ERR_NOT_SUPPORTED;
    }
    JSValue b = JS_NewArrayBufferCopy(ctx, (const uint8_t *)data, len);
    JSValue args[1] = { b };
    JSValue r = JS_Call(ctx, fn, JS_UNDEFINED, 1, args);
    int ok = !JS_IsException(r);
    JS_FreeValue(ctx, r);
    JS_FreeValue(ctx, b);
    JS_FreeValue(ctx, fn);
    return ok ? AM_OK : AM_ERR_GENERIC;
}

int am_ctx_spawn(am_t *rt, const char *init_script)
{
    if (!rt || rt->magic != AM_MAGIC) return AM_ERR_INVALID_ARG;
    am_ctx_t *cctx = am_ctx_create(rt, &rt->config);
    if (!cctx) return AM_ERR_NO_MEMORY;
    int cid = cctx->context_id;
    /* am_eval_internal 在 active ctx 上执行——把 active 切到新 ctx，让 init
     * 脚本落到子 context（而不是留在主 context 上）。 */
    rt->active_ctx_id = cid;
    if (init_script && init_script[0]) {
        char *err = NULL;
        if (am_eval_internal(rt, init_script, &err) != 0) {
            free(err);
            am_ctx_destroy(rt, cctx);
            if (rt->contexts[0]) rt->active_ctx_id = 0;
            return AM_ERR_GENERIC;
        }
    }
    /* 恢复 active = 主 context（宿主只见主） */
    if (rt->contexts[0]) rt->active_ctx_id = 0;
    return cid;
}

int am_ctx_serialize(am_t *rt, int ctx_id, const char *state_path)
{
    if (!rt || rt->magic != AM_MAGIC) return AM_ERR_INVALID_ARG;
    am_ctx_t *cctx = am_get_ctx_by_id(rt, ctx_id);
    if (!cctx || !cctx->jsctx) return AM_ERR_NOT_FOUND;
    if (ctx_id == rt->active_ctx_id) return AM_ERR_BUSY;
    /* G3：挂起前让扩展保存状态（当前全部 no-op，为扩展状态持久化预留） */
    if (am_ext_suspend_all(rt, cctx) < 0) return AM_ERR_GENERIC;
    JSContext *ctx = cctx->jsctx;
    JSValue g = JS_GetGlobalObject(ctx);
    JSValue fn = JS_GetPropertyStr(ctx, g, "__am_ctx_capture__");
    JS_FreeValue(ctx, g);
    if (!JS_IsFunction(ctx, fn)) {
        JS_FreeValue(ctx, fn);
        return AM_ERR_NOT_SUPPORTED;
    }
    JSValue bytes = JS_Call(ctx, fn, JS_UNDEFINED, 0, NULL);
    JS_FreeValue(ctx, fn);
    if (JS_IsException(bytes)) {
        JS_FreeValue(ctx, bytes);
        return AM_ERR_GENERIC;
    }
    size_t len = 0;
    const uint8_t *data = JS_GetUint8Array(ctx, &len, bytes);
    if (!data) data = JS_GetArrayBuffer(ctx, &len, bytes);
    int rc = (data && am_write_file(state_path, (const char *)data, len) == 0)
             ? AM_OK : AM_ERR_IO;
    JS_FreeValue(ctx, bytes);
    /* G1：写盘成功后销毁 ctx 释放内存。槽位由 am_ctx_destroy 置 NULL，
     * 后续 am_ctx_rebuild 在该槽位重建；写盘失败保留 ctx 供宿主重试。 */
    if (rc == AM_OK) am_ctx_destroy(rt, cctx);
    return rc;
}

int am_ctx_rebuild(am_t *rt, int ctx_id, const char *script_ref, const char *state_path)
{
    if (!rt || rt->magic != AM_MAGIC) return AM_ERR_INVALID_ARG;
    if (ctx_id < 0 || ctx_id >= AM_MAX_CONTEXTS) return AM_ERR_INVALID_ARG;
    if (ctx_id == rt->active_ctx_id) return AM_ERR_BUSY;

    /* 目标槽若有残留（旧实例）先销毁 */
    if (rt->contexts[ctx_id]) {
        am_ctx_destroy(rt, rt->contexts[ctx_id]);
    }

    am_ctx_t *cctx = am_ctx_create_at(rt, ctx_id);
    if (!cctx) return AM_ERR_NO_MEMORY;

    int rc = AM_OK;
    /* 同上：script_ref 必须 eval 到重建出的子 ctx 上 */
    rt->active_ctx_id = ctx_id;
    if (script_ref && script_ref[0]) {
        char *err = NULL;
        if (am_eval_internal(rt, script_ref, &err) != 0) {
            free(err);
            rc = AM_ERR_GENERIC;
        }
    }
    if (rc == AM_OK && state_path && state_path[0]) {
        char *buf = NULL;
        size_t blen = 0;
        if (am_read_file(state_path, &buf, &blen) == 0) {
            rc = am_ctx_restore_bytes(rt, cctx, buf, blen);
            free(buf);
        } else {
            rc = AM_ERR_IO;
        }
    }
    /* G3：restore 成功后让扩展恢复状态（active 尚未切回主 ctx，钩子可在
     * 子 ctx 上安全 eval）。失败按整体恢复失败处理：走下方销毁路径。 */
    if (rc == AM_OK && am_ext_resume_all(rt, cctx) < 0) rc = AM_ERR_GENERIC;
    if (rt->contexts[0]) rt->active_ctx_id = 0;
    if (rc != AM_OK) {
        am_ctx_destroy(rt, cctx);
        return rc;
    }
    return ctx_id;
}

int am_ctx_destroy_id(am_t *rt, int ctx_id)
{
    if (!rt || rt->magic != AM_MAGIC) return AM_ERR_INVALID_ARG;
    if (ctx_id == rt->active_ctx_id) return AM_ERR_BUSY;
    am_ctx_t *cctx = am_get_ctx_by_id(rt, ctx_id);
    if (!cctx) return AM_ERR_NOT_FOUND;
    am_ctx_destroy(rt, cctx);
    return AM_OK;
}

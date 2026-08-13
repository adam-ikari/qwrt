/*
 * qwrt Core Runtime (执行模型 A)
 *
 * 宿主侧生命周期：qwrt_create（阻塞到内部线程 ready）/ qwrt_destroy（请求
 * 线程退出 → join）/ qwrt_post_message（线程安全入站）/ get/set_runtime_data /
 * qwrt_free。
 *
 * qwrt 线程侧内部函数（thread.c 调用）：qwrt_runtime_init 建 JSRuntime + 主
 * context（含 polyfill 注入 / 扩展 init / DAP attach）；qwrt_eval_internal 在
 * 活动 context 上 eval；qwrt_thread_teardown 回收（排空 jobs → 销毁 contexts
 * → JS_FreeRuntime → 关闭 loop）。
 */

#include "qwrt_internal.h"
#include <stdlib.h>
#include <string.h>

#ifdef QWRT_DEBUG_SUPPORT
#include "qwrt/qwrt_debug_dap.h"
#endif

/* ================================================================
 * 宿主侧 API
 * ================================================================ */

qwrt_t *qwrt_create(const qwrt_config_t *config)
{
    if (!config) return NULL;
    qwrt_t *rt = (qwrt_t *)calloc(1, sizeof *rt);
    if (!rt) return NULL;
    rt->magic = QWRT_MAGIC;
    rt->config = *config;
    if (config->initial_script)
        rt->config.initial_script = strdup(config->initial_script);
    uv_mutex_init(&rt->msg_mutex);
    uv_cond_init(&rt->ready_cond);
    uv_mutex_init(&rt->ready_mutex);

    if (uv_thread_create(&rt->thread, qwrt_thread_main, rt) != 0) {
        uv_cond_destroy(&rt->ready_cond);
        uv_mutex_destroy(&rt->ready_mutex);
        uv_mutex_destroy(&rt->msg_mutex);
        free((void *)rt->config.initial_script);
        free(rt);
        return NULL;
    }

    /* 阻塞到内部线程 ready（握手在 thread_main 末尾） */
    uv_mutex_lock(&rt->ready_mutex);
    while (!rt->thread_ready) uv_cond_wait(&rt->ready_cond, &rt->ready_mutex);
    uv_mutex_unlock(&rt->ready_mutex);
    if (rt->ready_err) {
        qwrt_destroy(rt);
        return NULL;
    }
    return rt;
}

int qwrt_post_message(qwrt_t *rt, const char *json, size_t len)
{
    if (!rt || rt->magic != QWRT_MAGIC || !json) return -1;
    return qwrt_msg_push(rt, json, len, QWRT_MSG_SRC_HOST);
}

void qwrt_destroy(qwrt_t *rt)
{
    if (!rt) return;
    if (rt->magic != QWRT_MAGIC) return;
    uv_mutex_lock(&rt->msg_mutex);
    rt->shutting_down = 1;
    uv_mutex_unlock(&rt->msg_mutex);
    uv_async_send(&rt->wake);          /* 唤醒可能阻塞的 uv_run */
    uv_thread_join(&rt->thread);       /* 等线程 teardown 完成 */
    uv_mutex_destroy(&rt->msg_mutex);
    uv_cond_destroy(&rt->ready_cond);
    uv_mutex_destroy(&rt->ready_mutex);
    free((void *)rt->config.initial_script);
    free(rt);
}

void *qwrt_get_runtime_data(qwrt_t *rt) { return rt ? rt->host_data : NULL; }
void  qwrt_set_runtime_data(qwrt_t *rt, void *data) { if (rt) rt->host_data = data; }
void  qwrt_free(void *ptr) { free(ptr); }

/* ================================================================
 * qwrt 线程侧内部函数（thread.c 调用）
 * ================================================================ */

int qwrt_runtime_init(qwrt_t *rt)
{
    /* Create JSRuntime (shared across all contexts) */
    rt->jsrt = JS_NewRuntime();
    if (!rt->jsrt) return -1;
    JS_SetRuntimeOpaque(rt->jsrt, rt);

    /* Initialize context table */
    rt->context_count = 0;
    rt->active_ctx_id = -1;
    for (int i = 0; i < QWRT_MAX_CONTEXTS; i++) rt->contexts[i] = NULL;

    /* Create initial context (ext init / polyfill 注入在 qwrt_ctx_create 内) */
    qwrt_ctx_t *ctx = qwrt_ctx_create(rt, &rt->config);
    if (!ctx) {
        JS_FreeRuntime(rt->jsrt);
        rt->jsrt = NULL;
        return -1;
    }
    ctx->active = 1;
    rt->active_ctx_id = ctx->context_id;

#ifdef QWRT_DEBUG_SUPPORT
    /* Auto-attach the DAP debugger when enabled (env QWRT_DEBUG=1 or
     * config->debug bit 1). qwrt_dap_configure blocks reading the DAP
     * initialize/setBreakpoints/configurationDone exchange, so breakpoints
     * are armed before the host's first message. */
    {
        int enable = 0;
        const char *env = getenv("QWRT_DEBUG");
        if (env && (env[0] == '1' || env[0] == 't' || env[0] == 'T'))
            enable = 1;
        if (rt->config.debug & 0x2)  /* bit 1 = debug-enable */
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

    return 0;
}

int qwrt_eval_internal(qwrt_t *rt, const char *script, char **err)
{
    JSContext *ctx = qwrt_get_active_jsctx(rt);
    if (!ctx) return -1;

    JSValue val = JS_Eval(ctx, script, strlen(script), "<initial>",
                          JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(val)) {
        if (err) {
            JSValue exc = JS_GetException(ctx);
            const char *msg = JS_ToCString(ctx, exc);
            *err = msg ? strdup(msg) : NULL;
            if (msg) JS_FreeCString(ctx, msg);
            JS_FreeValue(ctx, exc);
        }
        JS_FreeValue(ctx, val);
        return -1;
    }
    JS_FreeValue(ctx, val);
    return 0;
}

static void qwrt_close_walk_cb(uv_handle_t *h, void *arg)
{
    QWRT_UNUSED(arg);
    uv_close(h, NULL);
}

void qwrt_thread_teardown(qwrt_t *rt)
{
    /* 0) 先终止所有 worker（Task 4）。必须在排空队列/JSRuntime 之前：worker
     * 线程可能仍向本队列推消息（join 期间 msg_mutex 必须存活），且 JSRuntime
     * 释放后 worker 自己的 teardown 不再需要父侧任何状态。join 在本线程做，
     * 随后 free 结构。 */
    for (int i = 0; i < QWRT_MAX_WORKERS; i++) {
        qwrt_worker_t *w = rt->workers[i];
        if (w) {
            qwrt_worker_terminate(rt, w);
            uv_thread_join(&w->thread);
            rt->workers[i] = NULL;
            qwrt_worker_free(w);
        }
    }

    /* 1) 排空剩余入站队列（shutting_down 后 wake 回调不再派发） */
    qwrt_msg_t *m;
    while ((m = qwrt_msg_pop(rt)) != NULL) qwrt_msg_free(m);

    /* 2) 排空 pending JS jobs BEFORE freeing contexts/runtime，否则
     * JS_FreeRuntime 会在非空 gc_obj_list 上断言（Promise 反应引用着
     * 尚未执行的闭包/值）。 */
    if (rt->jsrt) {
        JSContext *job_ctx = NULL;
        int ret;
        while ((ret = JS_ExecutePendingJob(rt->jsrt, &job_ctx)) > 0) {}
    }

    /* 3) DAP detach 必须先于 contexts/JSRuntime 释放：qwrt_debug_detach 会调
     * JS_SetDebuggerHandler(jsrt, NULL)（在已释放的 runtime 上写即 UAF），
     * 并释放缓存的 paused-frame 快照（JS_FreeCallFrames 需要活的 ctx）。 */
#ifdef QWRT_DEBUG_SUPPORT
    if (rt->dbg_session) {
        qwrt_dap_detach(rt);
        rt->dbg_session = NULL;
    }
#endif

    /* 4) 销毁所有 contexts（内含扩展 destroy） */
    for (int i = 0; i < QWRT_MAX_CONTEXTS; i++) {
        if (rt->contexts[i]) qwrt_ctx_destroy(rt, rt->contexts[i]);
    }

    /* 5) 释放 JSRuntime（gc_obj_list 已空） */
    if (rt->jsrt) JS_FreeRuntime(rt->jsrt);

    /* 6) 释放 uv_io in-memory storage（key/value 均为堆分配） */
    for (int i = 0; i < rt->store_count; i++) {
        free(rt->store[i].key);
        free(rt->store[i].value);
    }
    free(rt->store);
    rt->store = NULL;
    rt->store_count = 0;

    /* 7) 关闭 loop：close 全部 handle → 处理 close 回调 → loop_close
     *（libuv 里 stop 过但仍 open 的 handle 也会让 uv_loop_close EBUSY，
     * 所以必须 walk-close 而非只关 wake）。 */
    uv_walk(&rt->loop, qwrt_close_walk_cb, NULL);
    uv_run(&rt->loop, UV_RUN_NOWAIT);
    uv_loop_close(&rt->loop);
}

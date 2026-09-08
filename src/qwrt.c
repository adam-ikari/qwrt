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
#include <sched.h>
#include <stdlib.h>
#include <string.h>

#ifdef QWRT_DEBUG_SUPPORT
#include "qwrt/qwrt_debug_dap.h"
#endif

#ifndef QWRT_USE_MOCK_LIBUV
#include "ipc_process.h"
#endif

/* ================================================================
 * 宿主侧 API
 * ================================================================ */

qwrt_t *qwrt_create(const qwrt_config_t *config)
{
    /* 禁用 libuv 的 io_uring：部分内核（如 PVE 6.17）在 io_uring_setup 后
     * 会破坏 futex/pthread_cond 唤醒，导致宿主线程的 cond_wait 永不返回。
     * 不覆盖宿主显式设置的 UV_USE_IO_URING。 */
    setenv("UV_USE_IO_URING", "0", 0);
    if (!config) return NULL;
    qwrt_t *rt = (qwrt_t *)calloc(1, sizeof *rt);
    if (!rt) return NULL;
    rt->magic = QWRT_MAGIC;
    rt->config = *config;
    if (config->initial_script)
        rt->config.initial_script = strdup(config->initial_script);
    /* lock-free MPSC queue: head == tail == sentinel (calloc zeroed stub's q.next) */
    rt->msg_head = &rt->msg_stub;
    rt->msg_tail = &rt->msg_stub;
    /* CTL-0：控制面回执表锁（生产者登记，qwrt 线程消费）。 */
    uv_mutex_init(&rt->ctl_lock);

    if (uv_thread_create(&rt->thread, qwrt_thread_main, rt) != 0) {
        free((void *)rt->config.initial_script);
        free(rt);
        return NULL;
    }

    /* Block until the internal thread is ready (atomically set at the end of
     * thread_main; spin + yield here, no futex/pthread_cond wakeup — on some
     * kernels (PVE 6.17) cond wakeups break after an fd is created). */
    while (!__atomic_load_n(&rt->thread_ready, __ATOMIC_ACQUIRE))
        sched_yield();
    if (rt->ready_err) {
        qwrt_destroy(rt);
        return NULL;
    }
    return rt;
}

int qwrt_post_message(qwrt_t *rt, const char *json, size_t len)
{
    if (!rt || rt->magic != QWRT_MAGIC || !json) return -1;
    return qwrt_msg_push(rt, json, len, QWRT_MSG_SRC_HOST, 0);
}


void qwrt_wait_idle(qwrt_t *rt)
{
    if (!rt || rt->magic != QWRT_MAGIC) return;
    __atomic_store_n(&rt->wait_idle, 1, __ATOMIC_RELEASE);
    uv_async_send(&rt->wake);          /* wake a blocked uv_run for idle detection */
    /* Block until the thread auto-exits on idle (loop empty of work).
     * qwrt_destroy must not be called before this returns — it would force
     * shutdown and cancel pending async work (e.g. a live timer). */
    uv_thread_join(&rt->thread);
    /* M-R1: record the join so a following qwrt_destroy skips it — bare
     * pthread_join on an already-joined handle is UB. */
    __atomic_store_n(&rt->thread_joined, 1, __ATOMIC_RELEASE);
}

void qwrt_destroy(qwrt_t *rt)
{
    if (!rt) return;
    if (rt->magic != QWRT_MAGIC) return;
    __atomic_store_n(&rt->shutting_down, 1, __ATOMIC_RELEASE);
    uv_async_send(&rt->wake);          /* wake a blocked uv_run */
    /* wait_idle already joined the (exited) thread — re-joining is UB. The
     * thread is gone at that point, so nothing to wait for. */
    if (!__atomic_load_n(&rt->thread_joined, __ATOMIC_ACQUIRE))
        uv_thread_join(&rt->thread);   /* 等线程 teardown 完成 */
    free((void *)rt->config.initial_script);
    free(rt);
}

void *qwrt_get_runtime_data(qwrt_t *rt) { return rt ? rt->host_data : NULL; }
void  qwrt_set_runtime_data(qwrt_t *rt, void *data) { if (rt) rt->host_data = data; }
void  qwrt_free(void *ptr) {
    if (!ptr) return;
    qwrt_t *rt = (qwrt_t *)ptr;
    free((void *)rt->config.initial_script);
    free(ptr);
}

/* ================================================================
 * qwrt 线程侧内部函数（thread.c 调用）
 * ================================================================ */

int qwrt_runtime_init(qwrt_t *rt)
{
    /* Create JSRuntime (shared across all contexts) */
    rt->jsrt = JS_NewRuntime();
    if (!rt->jsrt) return -1;
    /* CTL-0 §3.9：安装 runtime 级中断处理器——只读 ctl_interrupt 原子标志。 */
    JS_SetInterruptHandler(rt->jsrt, qwrt_ctl_interrupt_handler, rt);
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
    rt->active_ctx_id = ctx->context_id;

#ifdef QWRT_DEBUG_SUPPORT
    /* Auto-attach the DAP debugger when enabled (env QWRT_DEBUG=1 or
     * config->debug bit 1). qwrt_dap_configure blocks reading the DAP
     * initialize/setBreakpoints/configurationDone exchange, so breakpoints
     * are armed before the host's first message.
     *
     * 作用域语义（A4 多上下文断点）：DAP 走 stdio 单通道，一个进程只有一份
     * stdin/stdout，无法同时服务两个 runtime 的协议会话。因此 worker 运行时
     * （rt->worker_self != NULL，独立线程 + 独立 JSRuntime）不 auto-attach——
     * 否则它会与父 runtime 竞争读同一 stdin（父的 configure/on_stopped pump
     * 会吞掉 worker 的协议消息 → 死锁/错乱）。结果：断点只作用于 attach 的
     * 那个 runtime 的 source 文件执行；父 runtime 设的断点不影响 worker。 */
    {
        int enable = 0;
        if (rt->worker_self == NULL) {
            const char *env = getenv("QWRT_DEBUG");
            if (env && (env[0] == '1' || env[0] == 't' || env[0] == 'T'))
                enable = 1;
            if (rt->config.debug & 0x2)  /* bit 1 = debug-enable */
                enable = 1;
        }
        if (enable) {
            qwrt_dap_config_t dcfg;
            dcfg.stop_on_entry = 1;
            dcfg.in = NULL;   /* stdin */
            dcfg.out = NULL;  /* stdout */
            int rc = qwrt_dap_attach(rt, &dcfg);
            if (rc == 0) {
                qwrt_dap_configure(rt);  /* blocks until configurationDone */
            } else if (rc == -2) {
                /* M-R1 §13.2：stdio 已被同进程另一 runtime 认领。显式失败
                 * （qwrt_create 返回 NULL），不静默降级成无调试器运行——
                 * 与开放点 #2 的「不静默降级」裁决一致。 */
                rt->ready_err = -2;
            }
            /* rc == -1（qwrt_debug_attach 失败等其他错误）：维持旧行为，
             * 运行时继续无调试器运行。 */
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

/* 在活动 context 上执行预编译字节码（JS_ReadObject + JS_EvalFunction）。
 * 用于编译期注入的 JS（worker 启动垫片等），与 qwrt_eval_internal 的
 * 错误提取语义一致。 */
int qwrt_eval_bytecode_internal(qwrt_t *rt, const uint8_t *code, size_t len,
                                char **err)
{
    JSContext *ctx = qwrt_get_active_jsctx(rt);
    if (!ctx) return -1;

    JSValue obj = JS_ReadObject(ctx, code, len, JS_READ_OBJ_BYTECODE);
    if (JS_IsException(obj)) {
        if (err) {
            JSValue exc = JS_GetException(ctx);
            const char *msg = JS_ToCString(ctx, exc);
            *err = msg ? strdup(msg) : NULL;
            if (msg) JS_FreeCString(ctx, msg);
            JS_FreeValue(ctx, exc);
        }
        JS_FreeValue(ctx, obj);
        return -1;
    }
    JSValue val = JS_EvalFunction(ctx, obj);
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
    /* ctx/ext teardown (step 4) may already have closed some handles without
     * their close callbacks running
     * (the loop is not run again); walk still sees those handles in the queue,
     * so skip the ones already closing. */
    if (!uv_is_closing(h)) uv_close(h, NULL);
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
            /* 线程后端：join 已请求退出的 worker 线程（进程 worker 由 JS 层
             * processTerminate 管理——C 层 qwrt_worker_t 仅服务线程后端，
             * spawn 分层化 Phase C）。 */
            if (w->self)
                uv_thread_join(&w->thread);
            rt->workers[i] = NULL;
            qwrt_worker_free(w);
        }
    }

#ifndef QWRT_USE_MOCK_LIBUV
    /* 0.5) 终止残留的 pal.processSpawn 句柄（spawn 分层化, Phase B）：JS
     *      未显式 processTerminate 的句柄在此收尾——3-tier 杀掉子进程 + 释放
     *      proc 结构。onmsg 是 JS 回调函数引用：必须在此 JS_FreeValue（本步
     *      在 contexts 销毁 / JS_FreeRuntime 之前，h->ctx->jsctx 有效），否则
     *      残留引用使 JS_FreeRuntime 的 gc_obj_list 断言失败。 */
    for (int i = 0; i < QWRT_MAX_PROC_HANDLES; i++) {
        qwrt_proc_handle_t *h = &rt->proc_handles[i];
        if (h->live && h->proc) {
            qwrt_proc_terminate(h->proc, 0);
            qwrt_proc_free(h->proc);
        }
        if (h->live && h->ctx && h->ctx->jsctx)
            JS_FreeValue(h->ctx->jsctx, h->onmsg);
        h->onmsg = JS_UNDEFINED;
        h->ctx = NULL;
        h->proc = NULL;
        h->live = 0;
    }
#endif
    /* 1) drain any remaining inbound queue (pop already frees passed nodes;
     *    the final node stays on head) */
    qwrt_msg_t *m;
    while ((m = qwrt_msg_pop(rt)) != NULL) {}
    if (rt->msg_head != &rt->msg_stub) qwrt_msg_free(rt->msg_head);
    rt->msg_head = &rt->msg_stub;

    /* 1.5) abort in-flight streaming HTTP op（若存在）。uv_io_http_abort 会
     * 同步触发 on_end（JS_Call，bridge_stream_on_end 释放 bs）并关闭
     * tcp/timer 句柄；必须赶在销毁 contexts / 释放 JSRuntime 之前，否则
     * on_end 访问已释放的 ctx。abort 排的 JS job 由步骤 2 的循环消化。 */
    if (rt->active_stream) {
        uv_io_http_abort(rt);
    }

    /* 2) 排空 pending JS jobs BEFORE freeing contexts/runtime，否则
     * JS_FreeRuntime 会在非空 gc_obj_list 上断言（Promise 反应引用着
     * 尚未执行的闭包/值）。 */
    if (rt->jsrt) {
        JSContext *job_ctx = NULL;
        int ret;
        while ((ret = JS_ExecutePendingJob(rt->jsrt, &job_ctx)) > 0) {}
    }
    /* 2.5) 排空 libuv 已排队但未处理的 request 完成（work_done）。
     * 必须在销毁 contexts / 释放 JSRuntime 之前：完成回调（bridge_io_done）
     * 会 JS_Call resolve，需要活着的 ctx 与 jsrt。wait_idle 路径由
     * qwrt_loop_idle 的 active_reqs 检查保证 teardown 时无残留；此处兜底
     * 强制 shutdown（qwrt_destroy）的瞬时窗口。限轮防止在途慢请求
     * 导致 busy-spin（UV_RUN_NOWAIT 的返回值是 loop-alive，不是"处理数"）。 */
    for (int i = 0; i < 16; i++) {
        if (uv_run(&rt->loop, UV_RUN_NOWAIT) == 0) break;
        if (rt->jsrt) {
            JSContext *job_ctx = NULL;
            while (JS_ExecutePendingJob(rt->jsrt, &job_ctx) > 0) {}
        }
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
    /* CTL-0：回收未完成回执条目 + 销毁 ctl_lock。必须在 loop 关闭前：
     * 条目/锁不依赖 loop/JSRuntime。 */
    qwrt_ctl_teardown(rt);

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

    /* 6.5) 释放 Proxy-Authorization 缓存（uv_io_http_apply_proxy 分配）。
     * 所有 in-flight op 已在上文步骤 1.5 中止清理，无 op 再借用该指针。 */
    free(rt->proxy_auth_url);
    free(rt->proxy_auth_value);
    rt->proxy_auth_url = NULL;
    rt->proxy_auth_value = NULL;

    /* 6.7) 释放 polyfill 字节码缓存（qwrt_ctx_create_at 惰性加载，各 context
     * 共享同一份）。C mode 无堆分配（unload 是 no-op）；A/B/D 释放堆缓冲。 */
    qwrt_polyfill_unload(rt->polyfill_owner);
    rt->polyfill_owner = NULL;
    rt->polyfill = NULL;
    rt->polyfill_len = 0;

    /* 7) 关闭 loop：close 全部 handle → 处理 close 回调 → loop_close
     *（libuv 里 stop 过但仍 open 的 handle 也会让 uv_loop_close EBUSY，
     * 所以必须 walk-close 而非只关 wake）。 */
    uv_walk(&rt->loop, qwrt_close_walk_cb, NULL);
    uv_run(&rt->loop, UV_RUN_NOWAIT);
    uv_loop_close(&rt->loop);
}

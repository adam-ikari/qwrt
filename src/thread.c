/*
 * qwrt Thread Main Loop (执行模型 A)
 *
 * qwrt_t 自持一个内部线程：uv_loop_init + wake async 注册，JS 运行时由
 * qwrt_runtime_init 建好（含 polyfill 注入 / 扩展 init / DAP attach），然后
 * 用 mutex+cond 与宿主握手（qwrt_create 阻塞到 thread_ready）。此后线程在
 * uv_run(UV_RUN_ONCE) 里处理事件（wake 回调排空入站 FIFO 并派发 onmessage），
 * 事件间 flush 微任务。destroy 置 shutting_down 并 wake → 线程退出主循环 →
 * qwrt_thread_teardown 清理。
 *
 * PAL 时代"回调在线程上排队、qwrt_tick 回放"的 deferred 队列已删除：现在所有
 * libuv 回调（timer/io/wake）本来就跑在 qwrt 线程，直接 JS_Call 即可。
 */

#include "qwrt_internal.h"
#include <stdlib.h>
#include <string.h>

/* 在安全点排空 JS 微任务/待执行 job。返回本轮处理数。worker.c 也调用（其
 * 自己的线程主循环）。 */
int qwrt_flush_microtasks(qwrt_t *rt)
{
    int total = 0, n;
    JSContext *job_ctx = NULL;
    while ((n = JS_ExecutePendingJob(rt->jsrt, &job_ctx)) > 0) total += n;
    return total;
}

/* ── idle detection (qwrt_wait_idle support) ── */

/* uv_walk callback: any active handle other than the internal wake async → busy */
typedef struct {
    qwrt_t *rt;
    int busy;
} qwrt_idle_state_t;

static void qwrt_idle_walk_cb(uv_handle_t *h, void *arg)
{
    qwrt_idle_state_t *st = (qwrt_idle_state_t *)arg;
    if (h == (uv_handle_t *)&st->rt->wake) return;   /* exclude the internal wake async */
#ifdef QWRT_DEBUG_SUPPORT
    /* 调试器附着的周期 DAP 轮询 timer 不算"忙"——wait_idle 应照常退出，
     * 不能被它（一个恒活动的 50ms timer）永远判为 busy。 */
    if (st->rt->dap_timer_active && h == (uv_handle_t *)&st->rt->dap_timer)
        return;
#endif
    if (!uv_is_closing(h) && uv_is_active(h)) st->busy = 1;
}

/* no active handle besides wake async and an empty message queue → idle */
static int qwrt_loop_idle(qwrt_t *rt)
{
    if (qwrt_msg_has_pending(rt)) return 0;   /* inbound queue non-empty */
    qwrt_idle_state_t st;
    st.rt = rt;
    st.busy = 0;
    uv_walk(&rt->loop, qwrt_idle_walk_cb, &st);
    if (st.busy) return 0;
    /* fs/http 等 request 不是 handle，uv_walk 看不到。若线程池 work 在途或
     * 完成回调（work_done）仍在 loop 队列排队，idle 判定必须算"忙"，否则
     * wait_idle 会在 work_done 处理前进入 teardown，其回调（bridge_io_done）
     * 访问已释放的 JSRuntime → UAF。 */
    if (rt->loop.active_reqs.count != 0) return 0;
    return 1;
}

/* uv_async callback: runs on the qwrt thread; drains the inbound queue and dispatches onmessage */
static void qwrt_wake_cb(uv_async_t *a)
{
    qwrt_t *rt = (qwrt_t *)a->data;
    if (__atomic_load_n(&rt->shutting_down, __ATOMIC_ACQUIRE)) return;
    qwrt_msg_t *m;
    while ((m = qwrt_msg_pop(rt)) != NULL) {
        qwrt_dispatch_message(rt, m);   /* bridge.c 实现；JSRuntime 已就绪 */
        /* 节点不 free：pop 内部已释放旧 head；m 成为下次 pop 的 head */

        /* 每个消息派发后立即排空微任务：宿主常在消息回调（dispatch 内联的
         * message_cb）里立即回发下一条消息，若攒到 uv_run 返回后再统一冲刷，
         * 下一条消息的 JS 会在本消息 promise 副作用落定之前被派发（读到旧
         * 状态）。逐条冲刷保持"每条消息 = 一个任务，任务后微任务先跑完"的
         * 事件循环语义。 */
        qwrt_flush_microtasks(rt);
    }
}

void qwrt_thread_main(void *arg)
{
    qwrt_t *rt = (qwrt_t *)arg;
    int loop_inited = 0;
    rt->host_data = rt->config.host_data;
    rt->debug = rt->config.debug;

    if (uv_loop_init(&rt->loop) != 0) {
        rt->ready_err = -1;
    } else {
        loop_inited = 1;
        rt->wake.data = rt;
        if (uv_async_init(&rt->loop, &rt->wake, qwrt_wake_cb) != 0) {
            rt->ready_err = -1;
        } else if (qwrt_runtime_init(rt) != 0) {
            rt->ready_err = -1;
        } else if (rt->config.initial_script) {
            char *err = NULL;
            if (qwrt_eval_internal(rt, rt->config.initial_script, &err) != 0) {
                /* 异常：记录 ready_err，握手后线程退出；qwrt_create 返回 NULL */
                free(err);
                rt->ready_err = -1;
            }
        }
    }

    /* ready handshake: atomic store (host qwrt_create spins until it reads 1).
     * No mutex/cond: on PVE 6.17 kernels pthread_cond wakeups fail after an
     * fd is created. */
    __atomic_store_n(&rt->thread_ready, 1, __ATOMIC_RELEASE);

    if (rt->ready_err) {
        /* init 失败：清理后线程自己退出 */
        if (loop_inited) qwrt_thread_teardown(rt);
        return;
    }
    /* ==== 主循环 ==== */
    while (!__atomic_load_n(&rt->shutting_down, __ATOMIC_ACQUIRE)) {
        uv_run(&rt->loop, UV_RUN_ONCE);  /* 阻塞等事件；wake_cb 期间派发消息 */
        if (__atomic_load_n(&rt->shutting_down, __ATOMIC_ACQUIRE)) break;
        qwrt_flush_microtasks(rt);
        if (__atomic_load_n(&rt->wait_idle, __ATOMIC_ACQUIRE) && qwrt_loop_idle(rt)) {
            __atomic_store_n(&rt->shutting_down, 1, __ATOMIC_RELEASE);
            break;
        }
    }
    qwrt_thread_teardown(rt);
}

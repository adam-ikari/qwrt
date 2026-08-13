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

/* uv_async 回调：跑在 qwrt 线程，排空入站队列并派发 onmessage */
static void qwrt_wake_cb(uv_async_t *a)
{
    qwrt_t *rt = (qwrt_t *)a->data;
    if (rt->shutting_down) return;
    qwrt_msg_t *m;
    while ((m = qwrt_msg_pop(rt)) != NULL) {
        qwrt_dispatch_message(rt, m);   /* bridge.c 实现；JSRuntime 已就绪 */
        qwrt_msg_free(m);
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

    /* ready 握手：宿主 qwrt_create 在此解锁返回 */
    uv_mutex_lock(&rt->ready_mutex);
    rt->thread_ready = 1;
    uv_cond_signal(&rt->ready_cond);
    uv_mutex_unlock(&rt->ready_mutex);

    if (rt->ready_err) {
        /* init 失败：清理后线程自己退出 */
        if (loop_inited) qwrt_thread_teardown(rt);
        return;
    }

    /* ==== 主循环 ==== */
    while (!rt->shutting_down) {
        uv_run(&rt->loop, UV_RUN_ONCE);  /* 阻塞等事件；wake_cb 期间派发消息 */
        if (rt->shutting_down) break;
        qwrt_flush_microtasks(rt);
    }
    qwrt_thread_teardown(rt);
}

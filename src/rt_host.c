/*
 * qwrt Host-side main-RT channel backend (M-P2, QWRT_PROCESS_MODEL=ISOLATED)
 *
 * 宿主进程侧实现：qwrt_create 内部 spawn 主RT 进程（qwrt-rt --qwrt-rt-server）
 * 并完成 M-P1 握手；此后宿主与主RT 之间只有一条 uv_pipe 通道（socketpair）。
 * 宿主 C API 的签名与语义与线程后端逐一对应：
 *
 *   qwrt_post_message  → 宿主入站 FIFO（MPSC，宿主线程推）→ loop 线程装信封写通道
 *   message_cb         → loop 线程解信封 → 回调解出 payload（JSON 文本）
 *   qwrt_wait_idle     → 发 CONTROL{idle} → 主RT 排空后回 ack 并自身退出 → join
 *   qwrt_destroy       → 三级终止（§9.2）→ 收尸 → join → 释放
 *
 * 线程语义与线程后端一致：宿主 loop 线程独占通道读写，message_cb 仍跑在
 * 「qwrt 线程」（此处 = 宿主 loop 线程），宿主无需改变回调线程假设。
 *
 * Design: docs/plans/2026-09-04-multi-process-model.md §3.3, §6, §9.2, M-P2.
 */

#include "qwrt_internal.h"
#include "ipc_process.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <sched.h>

/* ── 初始脚本临时文件 ──
 * 与 M-P1 worker 脚本同机制：mkstemp 原子创建（无 TOCTOU），子进程读毕 unlink；
 * 本函数只负责写盘并交回路径，最后路径由调用方兜底 unlink（幂等）。 */
static char *host_write_script(const char *code)
{
    if (!code) return NULL;

    char tmpl[] = "/tmp/qwrt-rt-script-XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) return NULL;

    size_t len = strlen(code), off = 0;
    while (off < len) {
        ssize_t n = write(fd, code + off, len - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            close(fd);
            unlink(tmpl);
            return NULL;
        }
        off += (size_t)n;
    }
    if (close(fd) != 0) {
        unlink(tmpl);
        return NULL;
    }
    return strdup(tmpl);
}

/* ── 入站回调（loop 线程）──
 * 信封解码结果 → 宿主语义：
 *   payload == NULL → 主RT 已退出/死亡（EOF，proc 已收尸）；
 *   kind == MESSAGE → message_cb（与线程后端 wake 派发同语义）；
 *   kind == CONTROL → M-P2 协议（ready / idle ack）就地消化；其余 CONTROL
 *   （控制面回执等）同样交 message_cb，与线程后端一致。 */
static void host_proc_msg_cb(void *user, int8_t kind, int32_t source,
                             const uint8_t *payload, uint32_t len)
{
    qwrt_t *rt = (qwrt_t *)user;
    QWRT_UNUSED(source);

    if (!payload) {
        /* 主RT 退出：宿主 loop 收束，wait_idle/destroy 的 join 随之返回。
         * 未 ready 即死 → ready_err（qwrt_create 显式失败，不静默降级 §5.3）。 */
        if (!__atomic_load_n(&rt->thread_ready, __ATOMIC_ACQUIRE)) {
            rt->ready_err = -1;
            __atomic_store_n(&rt->thread_ready, 1, __ATOMIC_RELEASE);
        }
        __atomic_store_n(&rt->shutting_down, 1, __ATOMIC_RELEASE);
        return;
    }

    if (kind == IPC_ENV_KIND_CONTROL) {
        int val = 0;
        qwrt_ipc_ctl_kind_t k = qwrt_ipc_ctl_classify(payload, len, &val);
        if (k == QWRT_IPC_CTL_READY) {
            if (!val) rt->ready_err = -1;
            __atomic_store_n(&rt->thread_ready, 1, __ATOMIC_RELEASE);
            return;
        }
        if (k == QWRT_IPC_CTL_IDLE && val == 1) {
            __atomic_store_n(&rt->idle_ack, 1, __ATOMIC_RELEASE);
            return;
        }
    }

    if (rt->config.message_cb)
        rt->config.message_cb(rt, (const char *)payload, len, rt->host_data);
}

/* ── 出站唤醒（loop 线程）──
 * 排空宿主入站 FIFO → 装信封写通道。FIFO 有序性是 idle 协议的基础：CONTROL{idle}
 * 与宿主消息同队列同序，主RT 收到 idle 请求时其前面的宿主消息必然已入通道
 * （§6.1 的「未决写计数 W」由单通道有序性保证，无需显式计数）。 */
static void host_wake_cb(uv_async_t *a)
{
    qwrt_t *rt = (qwrt_t *)a->data;
    if (__atomic_load_n(&rt->shutting_down, __ATOMIC_ACQUIRE)) return;

    qwrt_msg_t *m;
    while ((m = qwrt_msg_pop(rt)) != NULL) {
        if (!rt->proc) break;
        /* 写失败（peer 已死/通道异常）不额外终止：读泵侧的 EOF 同样会收束
         * 本循环；线程后端的 post 也只是「入队即返回」。 */
        qwrt_proc_post(rt->proc, QWRT_IPC_HOST_ID, QWRT_IPC_MAIN_ID,
                       m->flags ? IPC_ENV_KIND_CONTROL : IPC_ENV_KIND_MESSAGE,
                       (const uint8_t *)m->data, (uint32_t)m->len);
    }
}

/* ── 宿主 loop 线程 ──
 * 只做通道 I/O（JS 在主RT 进程里跑）。与 qwrt_thread_main 的对应关系：
 * thread_ready/ready_err 握手、wait_idle 后 join、destroy 后 join 逐一保持。 */
static void host_thread_main(void *arg)
{
    qwrt_t *rt = (qwrt_t *)arg;
    rt->host_data = rt->config.host_data;

    while (!__atomic_load_n(&rt->shutting_down, __ATOMIC_ACQUIRE)) {
        uv_run(&rt->loop, UV_RUN_ONCE);
        if (__atomic_load_n(&rt->shutting_down, __ATOMIC_ACQUIRE)) break;
        /* 主RT 退出（idle 自退 / 崩溃）→ 通道 EOF → proc DEAD → 宿主收束。 */
        if (rt->proc && rt->proc->state == QWRT_PROC_DEAD) {
            __atomic_store_n(&rt->shutting_down, 1, __ATOMIC_RELEASE);
            break;
        }
    }

    /* 主RT 若仍活着（destroy 路径）：三级终止（§9.2）——CONTROL{shutdown} →
     * 超时 → SIGKILL + waitpid 收尸。等价于线程后端的 join：destroy 阻塞到
     * 主RT 真正退出（最坏 = 终止超时）。 */
    if (rt->proc) {
        qwrt_proc_terminate(rt->proc, QWRT_IPC_TERMINATE_TIMEOUT_MS);
        qwrt_proc_free(rt->proc);
        rt->proc = NULL;
    }
    if (!uv_is_closing((uv_handle_t *)&rt->wake))
        uv_close((uv_handle_t *)&rt->wake, NULL);
    uv_run(&rt->loop, UV_RUN_DEFAULT);   /* close 回调（含 proc reclaim）跑完 */
    uv_loop_close(&rt->loop);

    if (!__atomic_load_n(&rt->thread_ready, __ATOMIC_ACQUIRE)) {
        rt->ready_err = -1;
        __atomic_store_n(&rt->thread_ready, 1, __ATOMIC_RELEASE);
    }
}

/* 停 loop 线程并收尾（不释放 rt —— 调用方决定是 create 失败清理还是 destroy）。 */
static void host_stop_thread(qwrt_t *rt)
{
    if (!__atomic_load_n(&rt->shutting_down, __ATOMIC_ACQUIRE)) {
        __atomic_store_n(&rt->shutting_down, 1, __ATOMIC_RELEASE);
        uv_async_send(&rt->wake);
    }
    if (!__atomic_load_n(&rt->thread_joined, __ATOMIC_ACQUIRE))
        uv_thread_join(&rt->thread);
}

int qwrt_host_start(qwrt_t *rt)
{
    /* loop 必须先 init：qwrt_proc_spawn 用 parent->loop 做 uv_pipe_init。 */
    if (uv_loop_init(&rt->loop) != 0) return -1;

    char *tmp = host_write_script(rt->config.initial_script);
    if (rt->config.initial_script && !tmp) {
        uv_loop_close(&rt->loop);
        return -1;
    }

    char fd_arg[16];
    snprintf(fd_arg, sizeof fd_arg, "%d", QWRT_IPC_CHANNEL_FD);
    char *argv[10];
    int n = 0;
    argv[n++] = (char *)"qwrt-rt";
    argv[n++] = (char *)"--qwrt-rt-server";
    argv[n++] = (char *)"--parent-fd";
    argv[n++] = fd_arg;
    argv[n++] = (char *)"--worker-backend";
    argv[n++] = (char *)(rt->config.worker_backend == QWRT_WORKER_BACKEND_PROCESS
                             ? "process" : "thread");
    if (tmp) {
        argv[n++] = (char *)"--script";
        argv[n++] = tmp;
    }
    argv[n] = NULL;

    rt->proc = qwrt_proc_new();
    if (!rt->proc) {
        if (tmp) { unlink(tmp); free(tmp); }
        uv_loop_close(&rt->loop);
        return -1;
    }

    /* 伴随二进制 qwrt-rt 由 M-P1 的解析链定位（显式路径 → QWRT_RT_SERVER →
     * /proc/self/exe 同目录 → 编译期 QWRT_RT_PATH）；找不到 = 显式失败。 */
    int rc = qwrt_proc_spawn(rt, rt->proc, NULL, argv,
                             QWRT_IPC_ROLE_MAIN, QWRT_IPC_MAIN_ID, 1);
    if (tmp) { unlink(tmp); free(tmp); }   /* 子已读毕并 unlink；此处幂等兜底 */
    if (rc != 0) {
        qwrt_proc_free(rt->proc);
        rt->proc = NULL;
        uv_loop_close(&rt->loop);
        return -1;
    }

    /* 入站：信封 → host_proc_msg_cb（读泵在 loop 线程）。 */
    qwrt_proc_start_read_cb(rt->proc, host_proc_msg_cb, rt);

    int wake_inited = 0;
    rt->wake.data = rt;
    if (uv_async_init(&rt->loop, &rt->wake, host_wake_cb) != 0) goto fail;
    wake_inited = 1;
    if (uv_thread_create(&rt->thread, host_thread_main, rt) != 0) goto fail;

    /* 阻塞到主RT ready（CONTROL{ready}）：与线程后端 thread_ready 同一手语
     * （spin + sched_yield —— PVE 6.17 上 cond 唤醒不可靠）。 */
    while (!__atomic_load_n(&rt->thread_ready, __ATOMIC_ACQUIRE))
        sched_yield();
    if (rt->ready_err) {
        host_stop_thread(rt);
        return -1;
    }
    return 0;

fail:
    if (rt->proc) { qwrt_proc_free(rt->proc); rt->proc = NULL; }
    if (wake_inited && !uv_is_closing((uv_handle_t *)&rt->wake))
        uv_close((uv_handle_t *)&rt->wake, NULL);
    uv_run(&rt->loop, UV_RUN_DEFAULT);
    uv_loop_close(&rt->loop);
    return -1;
}

int qwrt_host_post(qwrt_t *rt, const char *json, size_t len)
{
    if (!rt || rt->magic != QWRT_MAGIC || !json) return -1;
    if (__atomic_load_n(&rt->shutting_down, __ATOMIC_ACQUIRE)) return -1;
    int rc = qwrt_msg_push(rt, json, len, QWRT_MSG_SRC_HOST, 0);
    if (rc == 0) uv_async_send(&rt->wake);
    return rc;
}

void qwrt_host_wait_idle(qwrt_t *rt)
{
    if (!rt || rt->magic != QWRT_MAGIC) return;
    __atomic_store_n(&rt->wait_idle, 1, __ATOMIC_RELEASE);
    /* CONTROL{idle} 走同一 FIFO：排在所有已投递宿主消息之后。 */
    if (qwrt_msg_push(rt, QWRT_IPC_CTL_IDLE_REQ,
                      strlen(QWRT_IPC_CTL_IDLE_REQ),
                      QWRT_MSG_SRC_HOST, 1) == 0)
        uv_async_send(&rt->wake);

    /* 阻塞至 ack 或 EOF（§6.1），再 join：主RT 排空后回 ack 并自身退出，
     * 宿主 loop 线程见 EOF 收束 → join 返回（对应线程后端 join RT 线程）。 */
    while (!__atomic_load_n(&rt->idle_ack, __ATOMIC_ACQUIRE) &&
           !__atomic_load_n(&rt->shutting_down, __ATOMIC_ACQUIRE))
        sched_yield();
    uv_thread_join(&rt->thread);
    __atomic_store_n(&rt->thread_joined, 1, __ATOMIC_RELEASE);
}

void qwrt_host_destroy(qwrt_t *rt)
{
    if (!rt || rt->magic != QWRT_MAGIC) return;
    host_stop_thread(rt);   /* 终止主RT + 收尸 + 关 loop；loop 已退则 join 立即返回 */
    free((void *)rt->config.initial_script);
    free(rt);
}

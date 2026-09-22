/*
 * amoib Host-side main-RT channel backend (M-P2, AM_PROCESS_MODEL=ISOLATED)
 *
 * 宿主进程侧实现：am_create 内部 spawn 主RT 进程（amoib-rt --amoib-rt-server）
 * 并完成 M-P1 握手；此后宿主与主RT 之间只有一条 uv_pipe 通道（socketpair）。
 * 宿主 C API 的签名与语义与线程后端逐一对应：
 *
 *   am_post_message  → 宿主入站 FIFO（MPSC，宿主线程推）→ loop 线程装信封写通道
 *   message_cb         → loop 线程解信封 → 回调解出 payload（JSON 文本）
 *   am_wait_idle     → 发 CONTROL{idle} → 主RT 排空后回 ack 并自身退出 → join
 *   am_destroy       → 三级终止（§9.2）→ 收尸 → join → 释放
 *
 * 线程语义与线程后端一致：宿主 loop 线程独占通道读写，message_cb 仍跑在
 * 「amoib 线程」（此处 = 宿主 loop 线程），宿主无需改变回调线程假设。
 *
 * Design: docs/plans/2026-09-04-multi-process-model.md §3.3, §6, §9.2, M-P2.
 */

#include "am_internal.h"
#include "ipc_process.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <sched.h>
#include <cJSON.h>

/* ── 跨层 liveness ping/pong（宿主→树中任意 worker，§8.2 path 寻址）──
 *
 * payload 格式（PING）：{"amoib":1,"ping":N,"tp":[...]}——"tp" = root-relative
 * 槽位链（与命令面 target_path 同一 §8.2 范式）；PONG 回显 "tp" 作过境标记：
 * 中间节点读泵见 tp 沿父通道上行转发（corr 保持），不吃进自身 ping_seq/pong_seq
 * 配对槽；转发失败回 {"amoib":1,"pfail":1,"corr":N}，宿主快速 -1。 */

int am_ping_path(am_t *rt, const int32_t *path, int path_len,
                   int32_t timeout_ms)
{
    if (!rt || rt->magic != AM_MAGIC || !path || path_len <= 0 ||
        path_len > AM_SELF_PATH_MAX)
        return -1;
    if (!rt->proc || rt->proc->state != AM_PROC_RUN) return -1;
    if (__atomic_load_n(&rt->shutting_down, __ATOMIC_ACQUIRE)) return -1;

    /* 组 PING payload：{"amoib":1,"ping":seq,"tp":[p0,p1,...]}。corr 复用
     * §4.1 既定槽位（同 am_ping），tp 是 payload 扩展字段——信封 schema
     * 零改动。tp[0] 作信封 target（根的直接子槽位，N-P3 同款）。 */
    int32_t seq = __atomic_add_fetch(&rt->ping_seq, 1, __ATOMIC_ACQ_REL);
    char msg[64];
    int off = snprintf(msg, sizeof msg, "{\"amoib\":1,\"ping\":%d,\"tp\":[",
                       seq);
    if (off < 0 || off >= (int)sizeof msg) return -1;
    for (int i = 0; i < path_len; i++) {
        int n = snprintf(msg + off, (size_t)((int)sizeof msg - off),
                         "%s%d", i ? "," : "", path[i]);
        if (n < 0 || off + n >= (int)sizeof msg) return -1;
        off += n;
    }
    if (off + 2 >= (int)sizeof msg) return -1;
    msg[off++] = ']';
    msg[off++] = '}';
    msg[off] = '\0';

    if (am_proc_post(rt->proc, AM_IPC_HOST_ID, path[0],
                       IPC_ENV_KIND_CONTROL, seq,
                       (const uint8_t *)msg, (uint32_t)off) < 0)
        return -1;

    int64_t deadline = am_now_ms() + timeout_ms;
    for (;;) {
        if (__atomic_load_n(&rt->pong_seq, __ATOMIC_ACQUIRE) >= seq)
            return 0;   /* deadline 内 PONG 命中 = 目标 loop 通畅 */
        if (__atomic_load_n(&rt->ping_fail, __ATOMIC_ACQUIRE) >= seq)
            return -1;  /* pfail：转发失败/路径不存在/通道死 */
        if (am_now_ms() >= deadline) return 1;   /* 超时 = 目标 loop 阻塞 */
        if (__atomic_load_n(&rt->shutting_down, __ATOMIC_ACQUIRE)) return -1;
        sched_yield();
    }
}


/* ── 初始脚本临时文件 ──
 * 与 M-P1 worker 脚本同机制：mkstemp 原子创建（无 TOCTOU），子进程读毕 unlink；
 * 本函数只负责写盘并交回路径，最后路径由调用方兜底 unlink（幂等）。 */
static char *host_write_script(const char *code)
{
    if (!code) return NULL;

    char tmpl[] = "/tmp/amoib-rt-script-XXXXXX";
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
                             int32_t corr,
                             const uint8_t *payload, uint32_t len)
{
    am_t *rt = (am_t *)user;
    AM_UNUSED(source);
    AM_UNUSED(corr);

    if (!payload) {
        /* 主RT 退出：宿主 loop 收束，wait_idle/destroy 的 join 随之返回。
         * 未 ready 即死 → ready_err（am_create 显式失败，不静默降级 §5.3）。
         * M-P4 §9.3 崩溃检测：已 ready 且属非预期退出（既非 idle 自退 ack、
         * 又非宿主主动 shutdown）→ message_cb 收 {"type":"error",...}，宿主
         * 据此决定重启还是报错退出；am_wait_idle 随之立即返回。 */
        int was_ready = __atomic_load_n(&rt->thread_ready, __ATOMIC_ACQUIRE);
        int expected = __atomic_load_n(&rt->idle_ack, __ATOMIC_ACQUIRE) ||
                       __atomic_load_n(&rt->shutting_down, __ATOMIC_ACQUIRE);
        if (!was_ready) {
            rt->ready_err = -1;
            __atomic_store_n(&rt->thread_ready, 1, __ATOMIC_RELEASE);
        }
        __atomic_store_n(&rt->shutting_down, 1, __ATOMIC_RELEASE);
        if (was_ready && !expected && rt->config.message_cb) {
            static const char *kExitErr =
                "{\"type\":\"error\",\"error\":\"main-runtime-process-exited-unexpectedly\"}";
            rt->config.message_cb(rt, kExitErr, strlen(kExitErr), rt->host_data);
        }
        return;
    }

    if (kind == IPC_ENV_KIND_CONTROL) {
        int val = 0;
        am_ipc_ctl_kind_t k = am_ipc_ctl_classify(payload, len, &val);
        if (k == AM_IPC_CTL_READY) {
            if (!val) rt->ready_err = -1;
            __atomic_store_n(&rt->thread_ready, 1, __ATOMIC_RELEASE);
            return;
        }
        if (k == AM_IPC_CTL_IDLE && val == 1) {
            __atomic_store_n(&rt->idle_ack, 1, __ATOMIC_RELEASE);
            return;
        }
        if (k == AM_IPC_CTL_PONG) {
            /* 读泵 C 层直回/树中继的 liveness 应答：跨层形态（带 tp）回填
             * pong_seq 供 am_ping_path 配对——单跳 am_ping 的 seq 与跨层
             * seq 同源单调，两 API 都按「pong_seq >= seq」判定，无需第二槽
             * 位。无 tp 的 PONG 不可能是过境帧（tp 只由 am_ping_path 下
             * 发），语义不变。PONG 不进 message_cb。 */
            __atomic_store_n(&rt->pong_seq, (int32_t)corr, __ATOMIC_RELEASE);
            return;
        }
        if (k == AM_IPC_CTL_PFAIL) {
            /* 中间节点转发失败回执（corr = 原始 seq）：am_ping_path 快速
             * 判 -1（路径不存在/无对应子槽位），不白等 timeout。 */
            __atomic_store_n(&rt->ping_fail, (int32_t)corr, __ATOMIC_RELEASE);
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
    am_t *rt = (am_t *)a->data;
    if (__atomic_load_n(&rt->shutting_down, __ATOMIC_ACQUIRE)) return;

    am_msg_t *m;
    while ((m = am_msg_pop(rt)) != NULL) {
        if (!rt->proc) break;
        /* CTL-1：控制命令的信封 target 取自命令 JSON 的 "target" 字段
         * （缺省 1 = 主RT）。ISOLATED 下宿主只有到主RT 的一条通道，
         * target>1 由主RT 按本地槽位表下行（§2.2 逐跳相对寻址）。
         * 写失败（peer 已死/通道异常）不额外终止：读泵侧的 EOF 同样会
         * 收束本循环；线程后端的 post 也只是「入队即返回」。 */
        int32_t target = m->flags
                             ? am_ctl_cmd_target(m->data, m->len)
                             : AM_IPC_MAIN_ID;
        am_proc_post(rt->proc, AM_IPC_HOST_ID, target,
                       m->flags ? IPC_ENV_KIND_CONTROL : IPC_ENV_KIND_MESSAGE,
                       0,
                       (const uint8_t *)m->data, (uint32_t)m->len);
    }
}

/* ── 宿主 loop 线程 ──
 * 只做通道 I/O（JS 在主RT 进程里跑）。与 am_thread_main 的对应关系：
 * thread_ready/ready_err 握手、wait_idle 后 join、destroy 后 join 逐一保持。 */
static void host_thread_main(void *arg)
{
    am_t *rt = (am_t *)arg;
    rt->host_data = rt->config.host_data;

    while (!__atomic_load_n(&rt->shutting_down, __ATOMIC_ACQUIRE)) {
        uv_run(&rt->loop, UV_RUN_ONCE);
        if (__atomic_load_n(&rt->shutting_down, __ATOMIC_ACQUIRE)) break;
        /* 主RT 退出（idle 自退 / 崩溃）→ 通道 EOF → proc DEAD → 宿主收束。 */
        if (rt->proc && rt->proc->state == AM_PROC_DEAD) {
            __atomic_store_n(&rt->shutting_down, 1, __ATOMIC_RELEASE);
            break;
        }
    }

    /* 主RT 若仍活着（destroy 路径）：三级终止（§9.2）——CONTROL{shutdown} →
     * 超时 → SIGKILL + waitpid 收尸。等价于线程后端的 join：destroy 阻塞到
     * 主RT 真正退出（最坏 = 终止超时）。 */
    if (rt->proc) {
        am_proc_terminate(rt->proc, AM_IPC_TERMINATE_TIMEOUT_MS);
        am_proc_free(rt->proc);
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
static void host_stop_thread(am_t *rt)
{
    if (!__atomic_load_n(&rt->shutting_down, __ATOMIC_ACQUIRE)) {
        __atomic_store_n(&rt->shutting_down, 1, __ATOMIC_RELEASE);
        uv_async_send(&rt->wake);
    }
    if (!__atomic_load_n(&rt->thread_joined, __ATOMIC_ACQUIRE))
        uv_thread_join(&rt->thread);
}

int am_host_start(am_t *rt)
{
    /* loop 必须先 init：am_proc_spawn 用 parent->loop 做 uv_pipe_init。 */
    if (uv_loop_init(&rt->loop) != 0) return -1;

    char *tmp = host_write_script(rt->config.initial_script);
    if (rt->config.initial_script && !tmp) {
        uv_loop_close(&rt->loop);
        return -1;
    }

    char fd_arg[16];
    snprintf(fd_arg, sizeof fd_arg, "%d", AM_IPC_CHANNEL_FD);
    char *argv[14];
    int n = 0;
    argv[n++] = (char *)"amoib-rt";
    argv[n++] = (char *)"--amoib-rt-server";
    argv[n++] = (char *)"--parent-fd";
    argv[n++] = fd_arg;
    argv[n++] = (char *)"--worker-backend";
    argv[n++] = (char *)(rt->config.worker_backend == AM_WORKER_BACKEND_PROCESS
                             ? "process" : "thread");
    if (tmp) {
        argv[n++] = (char *)"--script";
        argv[n++] = tmp;
    }
    /* CTL-2：控制面档位 + 端点路径传给主RT（runtime 在主RT 进程；宿主进程
     * 只有通道桩，不监听端点）。路径以字符串字面量传入，argv 仅在 spawn
     * 期间需要（am_proc_spawn 同步 fork+exec）。 */
    if (rt->config.control_plane == AM_CONTROL_IN_PROC ||
        rt->config.control_plane == AM_CONTROL_LOCAL) {
        argv[n++] = (char *)"--control-plane";
        argv[n++] = (char *)(rt->config.control_plane == AM_CONTROL_LOCAL
                                 ? "local" : "in-proc");
        if (rt->config.control_pipe_path) {
            argv[n++] = (char *)"--control-pipe";
            argv[n++] = (char *)rt->config.control_pipe_path;
        }
    }
    argv[n] = NULL;

    rt->proc = am_proc_new();
    if (!rt->proc) {
        if (tmp) { unlink(tmp); free(tmp); }
        uv_loop_close(&rt->loop);
        return -1;
    }

    /* 伴随二进制 amoib-rt 由 M-P1 的解析链定位（显式路径 → AM_RT_SERVER →
     * /proc/self/exe 同目录 → 编译期 AM_RT_PATH）；找不到 = 显式失败。 */
    int rc = am_proc_spawn(rt, rt->proc, NULL, argv,
                             AM_IPC_ROLE_MAIN, AM_IPC_MAIN_ID, 1);
    if (tmp) { unlink(tmp); free(tmp); }   /* 子已读毕并 unlink；此处幂等兜底 */
    if (rc != 0) {        am_proc_free(rt->proc);
        rt->proc = NULL;
        uv_loop_close(&rt->loop);
        return -1;
    }

    /* 入站：信封 → host_proc_msg_cb（读泵在 loop 线程）。 */
    am_proc_start_read_cb(rt->proc, host_proc_msg_cb, rt);

    int wake_inited = 0;
    rt->wake.data = rt;
    if (uv_async_init(&rt->loop, &rt->wake, host_wake_cb) != 0) goto fail;
    wake_inited = 1;
    if (uv_thread_create(&rt->thread, host_thread_main, rt) != 0) goto fail;

    /* 阻塞到主RT ready（CONTROL{ready}）：与线程后端 thread_ready 同一手语
     * （spin + sched_yield —— PVE 6.17 上 cond 唤醒不可靠）。 */
    while (!__atomic_load_n(&rt->thread_ready, __ATOMIC_ACQUIRE))
        sched_yield();
    if (rt->ready_err) {        host_stop_thread(rt);
        return -1;
    }
    return 0;

fail:
    if (rt->proc) { am_proc_free(rt->proc); rt->proc = NULL; }
    if (wake_inited && !uv_is_closing((uv_handle_t *)&rt->wake))
        uv_close((uv_handle_t *)&rt->wake, NULL);
    uv_run(&rt->loop, UV_RUN_DEFAULT);
    uv_loop_close(&rt->loop);
    return -1;
}

int am_host_post(am_t *rt, const char *json, size_t len)
{
    if (!rt || rt->magic != AM_MAGIC || !json) return -1;
    if (__atomic_load_n(&rt->shutting_down, __ATOMIC_ACQUIRE)) return -1;
    int rc = am_msg_push(rt, json, len, AM_MSG_SRC_HOST, 0);
    if (rc == 0) uv_async_send(&rt->wake);
    return rc;
}

void am_host_wait_idle(am_t *rt)
{
    if (!rt || rt->magic != AM_MAGIC) return;
    __atomic_store_n(&rt->wait_idle, 1, __ATOMIC_RELEASE);
    /* CONTROL{idle} 走同一 FIFO：排在所有已投递宿主消息之后。 */
    if (am_msg_push(rt, AM_IPC_CTL_IDLE_REQ,
                      strlen(AM_IPC_CTL_IDLE_REQ),
                      AM_MSG_SRC_HOST, 1) == 0)
        uv_async_send(&rt->wake);

    /* 阻塞至 ack 或 EOF（§6.1），再 join：主RT 排空后回 ack 并自身退出，
     * 宿主 loop 线程见 EOF 收束 → join 返回（对应线程后端 join RT 线程）。 */
    while (!__atomic_load_n(&rt->idle_ack, __ATOMIC_ACQUIRE) &&
           !__atomic_load_n(&rt->shutting_down, __ATOMIC_ACQUIRE))
        sched_yield();
    uv_thread_join(&rt->thread);
    __atomic_store_n(&rt->thread_joined, 1, __ATOMIC_RELEASE);
}

/* ── Liveness ping（宿主→主RT,检测对端 uv loop 是否阻塞）──
 * 发 CONTROL{"amoib":1,"ping":1}（corr = 单调 seq）→ 阻塞等待 PONG（对端
 * C 层读泵就地直回,不经 JS/msgq）→ 回显 seq 命中 = loop 通畅;deadline 内
 * 未命中 = 对端 loop 阻塞（或死亡——死亡另有 EOF 路径）。单飞行:同一 rt
 * 同时只有一个 ping 在途（宿主线程 API,线程不安全由调用方保证）。 */
int am_ping(am_t *rt, int32_t timeout_ms)
{
    if (!rt || rt->magic != AM_MAGIC) return -1;
    if (!rt->proc || rt->proc->state != AM_PROC_RUN) return -1;
    if (__atomic_load_n(&rt->shutting_down, __ATOMIC_ACQUIRE)) return -1;

    int32_t seq = __atomic_add_fetch(&rt->ping_seq, 1, __ATOMIC_ACQ_REL);
    if (am_proc_post(rt->proc, AM_IPC_HOST_ID, AM_IPC_MAIN_ID,
                       IPC_ENV_KIND_CONTROL, seq,
                       (const uint8_t *)AM_IPC_CTL_PING_MSG,
                       (uint32_t)(sizeof AM_IPC_CTL_PING_MSG - 1)) < 0)
        return -1;

    int64_t deadline = am_now_ms() + timeout_ms;
    while (__atomic_load_n(&rt->pong_seq, __ATOMIC_ACQUIRE) < seq) {
        if (am_now_ms() >= deadline) return 1;   /* 超时 = 对端 loop 阻塞 */
        if (__atomic_load_n(&rt->shutting_down, __ATOMIC_ACQUIRE)) return -1;
        sched_yield();
    }
    return 0;   /* deadline 内 PONG 命中 = 对端 loop 通畅 */
}

void am_host_destroy(am_t *rt)
{
    if (!rt || rt->magic != AM_MAGIC) return;
    host_stop_thread(rt);   /* 终止主RT + 收尸 + 关 loop；loop 已退则 join 立即返回 */
    free((void *)rt->config.initial_script);
    free(rt);
}

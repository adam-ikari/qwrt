/*
 * qwrt IPC Process — parent-side spawn / handshake / terminate (M-P1)
 *
 * Tree-edge duplex channel between a parent runtime and a child process.
 * Parent spawns child via fork+exec of qwrt-rt; child inherits one end of
 * a socketpair via --parent-fd. Both sides exchange a CONTROL handshake
 * envelope (FlatBuffers wire format) before any application messages flow.
 *
 * Frame format (on the wire): 4-byte LE length prefix + envelope bytes.
 *
 * Design: docs/plans/2026-09-04-multi-process-model.md §3, §5, §9.2, M-P1.
 */

#ifndef QWRT_IPC_PROCESS_H
#define QWRT_IPC_PROCESS_H

#include "qwrt/qwrt.h"
#include "ipc_envelope.h"
#include <stdint.h>
#include <sys/types.h>   /* pid_t */

#ifdef QWRT_USE_MOCK_LIBUV
#include "mock_libuv.h"
#else
#include <uv.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ── Opaque handle — declared up front so the framing/terminate prototypes
 * below can use qwrt_proc_t without pulling in uv types (review: the typedef
 * used to sit mid-declaration list). Guarded: qwrt_internal.h forward-declares
 * the same name (it must not include this header under mock libuv), and a
 * repeated typedef is an error under -Wpedantic. */
#ifndef QWRT_PROC_T_DEFINED
#define QWRT_PROC_T_DEFINED
typedef struct qwrt_proc_s qwrt_proc_t;
#endif

/* ── Constants ── */

#define QWRT_IPC_PROTO_VERSION    1
#define QWRT_IPC_ROLE_WORKER      0
#define QWRT_IPC_ROLE_MAIN        1   /* M-P2：主RT 进程（宿主↔主RT 通道） */

/* 主RT 通道的本地标签（§4.3 逐跳相对寻址）：宿主=0，主RT=1。 */
#define QWRT_IPC_HOST_ID          0
#define QWRT_IPC_MAIN_ID          1
#define QWRT_IPC_HANDSHAKE_TIMEOUT_MS  5000
#define QWRT_IPC_TERMINATE_TIMEOUT_MS  2000
#define QWRT_IPC_READ_BUF_SIZE    65536

/* 子进程通道 fd 固定值（spawn 分层化）：调用方拼 argv 时 --parent-fd 一律
 * 写 3；qwrt_proc_spawn 内部 socketpair 后，fork 出的 child 把子端
 * dup2 到这个固定 fd 再 execv。固定值让调用方在运行时 fd 未知时也能拼
 * argv（POSIX 约定 0/1/2 为 stdio，3 是第一个可自由使用的 fd；child 拥有
 * 自己的 fd 表，dup2 无条件覆盖 3）。JS 侧（polyfill/src/worker.js）拼 argv
 * 时硬编码 "--parent-fd" "3"，与本常量同步维护。 */
#define QWRT_IPC_CHANNEL_FD    3


/* ── Framing (length-prefixed, blocking I/O for handshake) ── */

/* Write a 4-byte LE length prefix + data to fd (blocking, full write).
 * Returns 0 on success, -1 on error. */
int qwrt_ipc_write_frame(int fd, const uint8_t *data, size_t len);

/* Read one length-prefixed frame from fd (blocking).
 * deadline_ms: ABSOLUTE monotonic deadline (now_ms() + timeout) — the frame
 * must complete before it. Same convention as the spawn/terminate callers. */
int qwrt_ipc_read_frame(int fd, uint8_t **out_frame, size_t *out_len,
                        int64_t deadline_ms);

/* ── Handshake JSON helpers ── */

/* Build handshake payload: {"v":V,"role":R,"id":I}
 * Returns string length (excl NUL). cap must be >= 40. */
size_t qwrt_ipc_build_handshake(char *out, size_t cap, int role, int id);

/* Build ack payload: {"ok":O,"v":V}
 * Returns string length (excl NUL). cap must be >= 24. */
size_t qwrt_ipc_build_ack(char *out, size_t cap, int ok);

/* ── M-P2 主RT 通道 CONTROL 协议（§6.1）──
 * payload = 带 "qwrt" 标记的 JSON（cJSON 解析，与握手同裁决：不手写）：
 *   {"qwrt":1,"ready":V}      V=1 初始化就绪 / 0 失败（宿主据此决定 qwrt_create）
 *   {"qwrt":1,"idle":V}       V=0 宿主请求 idle / 1 主RT 已排空并判 idle（ack）
 *   {"qwrt":1,"shutdown":1}   优雅关停（与 M-P1 的 {"cmd":"shutdown"} 并存）
 * 无 "qwrt" 标记的 CONTROL payload 归上层（控制面消息/应用），classify 返回 NONE。 */
#define QWRT_IPC_CTL_READY_OK   "{\"qwrt\":1,\"ready\":1}"
#define QWRT_IPC_CTL_READY_ERR  "{\"qwrt\":1,\"ready\":0}"
#define QWRT_IPC_CTL_IDLE_REQ   "{\"qwrt\":1,\"idle\":0}"
#define QWRT_IPC_CTL_IDLE_ACK   "{\"qwrt\":1,\"idle\":1}"
#define QWRT_IPC_CTL_SHUTDOWN_MSG "{\"qwrt\":1,\"shutdown\":1}"
#define QWRT_IPC_CTL_CLOSING      "{\"qwrt\":1,\"closing\":1}"   /* M-P4 §9.3：
                                         * worker 自 close 前通知父（EOF 不再
                                         * 当作崩溃触发 onerror） */
typedef enum {
    QWRT_IPC_CTL_NONE = 0,   /* 非 M-P2 协议 CONTROL payload */
    QWRT_IPC_CTL_READY,
    QWRT_IPC_CTL_IDLE,
    QWRT_IPC_CTL_SHUTDOWN,
} qwrt_ipc_ctl_kind_t;

/* 判定 CONTROL payload 是否为 M-P2 协议消息；命中时 *out_val = 对应键的数值
 * （ready: 1=ok/0=fail；idle: 0=请求/1=ack；shutdown: 1）。非协议返回 NONE
 * （*out_val 不写）。 */
qwrt_ipc_ctl_kind_t qwrt_ipc_ctl_classify(const uint8_t *payload,
                                          uint32_t len, int *out_val);

/* 主RT 进程侧：发一条 M-P2 CONTROL 协议消息（source=1, target=0, kind=CONTROL）。 */
int qwrt_ipc_child_emit_ctl(const char *json);

/* 宿主侧：经主RT 通道发一条 M-P2 CONTROL 协议消息（source=0, target=1）。 */
int qwrt_proc_post_ctl(qwrt_proc_t *proc, const char *json);
/* Channel handle — see typedef above (qwrt_proc_t), defined at the top of
 * this header so declarations can reference it. */

#ifndef QWRT_USE_MOCK_LIBUV
/* Full struct body is private to ipc_process.c (real-libuv pipe APIs).
 * Mock test builds (QWRT_USE_MOCK_LIBUV) only see the opaque typedef. */
/* Outbound spill buffer (lossless backpressure): frames append here when
 * send() hits EAGAIN; a 1ms uv_timer flushes while non-empty. */
typedef struct qwrt_tx_s {
    uint8_t    *buf;
    size_t      len;
    size_t      cap;
    uv_timer_t  timer;
    int         fd;             /* socket fd to send on, -1 until first use */
    int         timer_active;
} qwrt_tx_t;

/* 读泵回调（qwrt_proc_start_read_cb）：cb(user, kind, source, payload, len)。
 * payload == NULL 表示 peer-death/EOF（回调后不再调用，kind/source 无意义）；
 * kind = IPC_ENV_KIND_*、source = 信封源标签，接收方据此分流（如 M-P2 宿主侧
 * 区分 CONTROL 协议消息与 MESSAGE 数据）。 */
typedef void (*qwrt_proc_msg_cb_t)(void *user, int8_t kind, int32_t source,
                                   const uint8_t *payload, uint32_t len);

struct qwrt_proc_s {
    uv_pipe_t pipe;           /* duplex pipe to child (parent end) */
    pid_t     pid;            /* child PID */
    int       id;             /* local slot id (source label / handshake 预期) */
    int       role;           /* QWRT_IPC_ROLE_* */
    int       state;          /* qwrt_proc_state_t */
    void     *parent_rt;      /* parent qwrt_t* (for callbacks) */
    /* CTL-1：>0 时本通道上到达的命令类 CONTROL 信封交 qwrt_control_route
     * 树路由（值为本节点槽位 id）；0 = 不路由（宿主侧读泵交 msg_cb）。 */
    int32_t   ctl_route_id;
    /* Read accumulator: frames = [4-byte LE len][envelope]; async reads via
     * qwrt_proc_start_read / qwrt_proc_start_read_cb. */
    uint8_t  *rbuf;
    size_t    rbuf_cap;
    size_t    rbuf_len;
    uint32_t  frame_len;      /* 0 = need 4-byte header */
    int       pipe_inited;    /* uv_pipe_init done (close via uv_close) */
    qwrt_tx_t tx;             /* outbound spill buffer + flush timer */
    /* JS-managed delivery mode (spawn 分层化, Phase B): 非 NULL 时信封解码后
     * 直接交给 msg_cb（bridge.c 的 pal.processOnMessage），不 push 父 msgq、
     * 也不做 worker-slot reap。EOF/peer-death 时以 payload=NULL 回调一次并
     * 标记 DEAD。 */
    void              *msg_user;
    qwrt_proc_msg_cb_t msg_cb;
    /* libuv-idiomatic multi-handle reclaim: proc 内嵌两个 handle（pipe +
     * tx flush timer），qwrt_proc_free 对两者都 uv_close，proc 内存在
     * 最后一个 close 回调里释放（close_pending 统计未完成的 close 数）。
     * freed 保证 qwrt_proc_free 幂等（防重复 free / 二次 uv_close）。 */
    int       close_pending;
    int       freed;
};
#endif /* !QWRT_USE_MOCK_LIBUV */

/* Parse handshake JSON → version/role/id. Returns 0 ok, -1 fail. */
int qwrt_ipc_parse_handshake(const char *json, int *out_v,
                             int *out_role, int *out_id);

/* Parse ack JSON → ok/version. Returns 0 ok, -1 fail. */
int qwrt_ipc_parse_ack(const char *json, int *out_ok, int *out_v);


typedef enum {
    QWRT_PROC_BUILD = 0,   /* pipe opened, awaiting handshake */
    QWRT_PROC_RUN   = 1,   /* handshake done, application messages flow */
    QWRT_PROC_DEAD  = 2    /* peer EOF or killed */
} qwrt_proc_state_t;
/* ── Parent-side channel handle ── */
/* Spawn a child process over a socketpair channel (spawn 分层化原语).
 * Creates socketpair, fork+exec; the child's channel end is dup2'd to
 * QWRT_IPC_CHANNEL_FD (3), so caller-built argv references --parent-fd 3.
 *
 * exe:  任意可执行文件路径；NULL/空串 → auto-detect qwrt-rt (QWRT_RT_SERVER
 *       env / /proc/self/exe dir + "qwrt-rt" / QWRT_RT_PATH).
 * argv: 调用方拼好的 argv（argv[0] = 程序名），含 --parent-fd 3 等子进程
 *       参数；执行的是 exe 路径（argv[0] 只是显示名）。
 * role/id:  握手预期（child 发 {"v":V,"role":role,"id":id}）；role<0 →
 *           跳过握手。
 * require_handshake: 0 → spawn 后直接 QWRT_PROC_RUN，不做握手（用于任意
 *           可执行文件，child 不 speak qwrt 信封协议）。
 * Returns 0 on success, qwrt_err_t (<0) on failure. */
int qwrt_proc_spawn(qwrt_t *parent, qwrt_proc_t *proc,
                    const char *exe,
                    char *const argv[],
                    int role, int id,
                    int require_handshake);
/* 3-tier termination (§9.2):
 *  1. Send CONTROL{shutdown} envelope
 *  2. Poll for child exit up to timeout_ms (default 2000ms)
 *  3. If still alive: kill(SIGKILL) + waitpid (reap)
 * Returns 0 if child exited (graceful or killed), -1 on error. */
int qwrt_proc_terminate(qwrt_proc_t *proc, int timeout_ms);
/* Install async reads on the channel: inbound envelopes decode → parent's
 * msgq push (source = child slot id) + wake. Called by the process-backend
 * worker create after a successful handshake. EOF → state DEAD. */
void qwrt_proc_start_read(qwrt_proc_t *proc);

/* JS-managed delivery mode (spawn 分层化): 同 qwrt_proc_start_read，但信封
 * 解码后不 push 父 msgq，而是调用 cb(user, payload, len)；payload=NULL 表示
 * peer-death/EOF（回调后不再调用）。proc 须已 RUN。cb 运行在读泵所在线程
 * （父 loop 线程 = JS 线程，可直接 JS_Call）。cb 传 NULL 恢复 msgq 模式。 */
void qwrt_proc_start_read_cb(qwrt_proc_t *proc, qwrt_proc_msg_cb_t cb,
                             void *user);

/* Opaque handle lifecycle — worker.c (compiled in mock test builds too)
 * only sees the pointer; the uv_pipe_t body stays private to ipc_process.c
 * (mock_libuv.h has no uv_pipe_t). */
qwrt_proc_t *qwrt_proc_new(void);
void qwrt_proc_free(qwrt_proc_t *proc);   /* destroy + free struct */

/* ── Child-side emit channel ──
 * The child process (qwrt-rt) registers its inherited --parent-fd here so
 * bridge.c's js_pal_worker_emit can send envelopes upstream without a
 * qwrt_proc_t (parent-side handle). Single channel per process. */
void qwrt_ipc_child_set_channel(int fd);
/* Child-side outbound queue init (spill buffer + flush timer on the child's
 * loop). fd is the inherited --parent-fd. */
void qwrt_ipc_child_tx_init(uv_loop_t *loop, int fd);
/* Child → parent envelope write. Returns 0 ok, -1 error. Never blocks; under
 * backpressure the frame is queued in the spill buffer, not dropped. */
int qwrt_ipc_child_emit(int32_t source, int32_t target, int8_t kind,
                        const uint8_t *payload, uint32_t payload_len);
/* Child channel fd, -1 if unset. */
int qwrt_ipc_child_channel(void);
/* M-P4 同步 storage RPC 用：同步排空出站 spill buffer；查询是否仍有未发帧。 */
void qwrt_ipc_child_tx_flush(void);
int qwrt_ipc_child_tx_pending(void);
/* M-P4 同步 storage RPC 用：阻塞发送整帧（先 FIFO 排空 spill buffer，再
 * poll(POLLOUT)+send 循环直到发完；父死 → -1）。异步 emit 依赖 uv_run 的
 * flush timer，而同步等待期间 loop 不转——大 payload（quota 内可达 ~5MB）
 * 必须走本函数。 */
int qwrt_ipc_child_emit_sync(int32_t source, int32_t target, int8_t kind,
                             const uint8_t *payload, uint32_t payload_len);

/* ── M-P4 同步 storage RPC（§10.2 单所有者代理的传输半边）──
 * worker 进程的 localStorage 代理需要「发请求 → 阻塞等回复」的同步原语。
 * 实现（帧累加 + poll/recv 等待 + STORAGE 回复捕获）在 rt_main.c（它独占
 * 子进程管道读状态 g_rx / g_server_mode），libqwrt 侧经函数指针注册访问——
 * 与 qwrt_ipc_child_set_channel 同构：CLI/宿主进程不注册，pal.storageSync
 * 求值报错（worker 进程之外不可达）。
 *
 * 语义：发一条 kind=STORAGE 信封上行（target=父），阻塞等待匹配回复
 * （单飞行：JS 同步调用期间无并发，无需 request id）。期间到达的其它帧
 * 照常进 msgq（wake 未消费，主循环 uv_run 时统一派发，不丢帧）；父进程
 * 死亡（fd EOF）→ 置 shutting_down 走孤儿自杀路径并返回 -1。 */
typedef int (*qwrt_ipc_storage_sync_fn)(
    qwrt_t *rt, const uint8_t *payload, uint32_t payload_len,
    uint8_t **out_reply, uint32_t *out_reply_len);
void qwrt_ipc_child_set_storage_sync(qwrt_ipc_storage_sync_fn fn);
int qwrt_ipc_child_storage_sync(qwrt_t *rt, const uint8_t *payload,
                                uint32_t payload_len,
                                uint8_t **out_reply, uint32_t *out_reply_len);

/* Post an envelope to child (async; 0 = queued/sent, -1 = failed). */
int qwrt_proc_post(qwrt_proc_t *proc,
                   int32_t source, int32_t target, int8_t kind,
                   const uint8_t *payload, uint32_t payload_len);


/* Close pipe and reap child if still alive. NULL-safe on pipe. */
void qwrt_proc_destroy(qwrt_proc_t *proc);

#ifdef __cplusplus
}
#endif

#endif /* QWRT_IPC_PROCESS_H */

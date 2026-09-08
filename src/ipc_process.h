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
 * used to sit mid-declaration list). */
typedef struct qwrt_proc_s qwrt_proc_t;

/* ── Constants ── */

#define QWRT_IPC_PROTO_VERSION    1
#define QWRT_IPC_ROLE_WORKER      0
#define QWRT_IPC_HANDSHAKE_TIMEOUT_MS  5000
#define QWRT_IPC_TERMINATE_TIMEOUT_MS  2000
#define QWRT_IPC_READ_BUF_SIZE    65536

/* 子进程通道 fd 固定值（spawn 分层化）：调用方拼 argv 时 --parent-fd 一律
 * 写 3；qwrt_proc_spawn 内部 socketpair 后，fork 出的 child 把子端
 * dup2 到这个固定 fd 再 execv。固定值让调用方在运行时 fd 未知时也能拼
 * argv（POSIX 约定 0/1/2 为 stdio，3 是第一个可自由使用的 fd；child 拥有
 * 自己的 fd 表，dup2 无条件覆盖 3）。注意 QWRT_IPC_CHANNEL_FD_STR 必须与
 * 此值同步——调用方 argv 里的 --parent-fd 字符串用它。 */
#define QWRT_IPC_CHANNEL_FD    3
#define QWRT_IPC_CHANNEL_FD_STR "3"


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

struct qwrt_proc_s {
    uv_pipe_t pipe;           /* duplex pipe to child (parent end) */
    pid_t     pid;            /* child PID */
    int       id;             /* local slot id (source label / handshake 预期) */
    int       role;           /* QWRT_IPC_ROLE_* */
    int       state;          /* qwrt_proc_state_t */
    void     *parent_rt;      /* parent qwrt_t* (for callbacks) */
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
    void     *msg_user;
    void     (*msg_cb)(void *user_data, const uint8_t *payload,
                       uint32_t payload_len);
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
void qwrt_proc_start_read_cb(qwrt_proc_t *proc,
                             void (*cb)(void *, const uint8_t *, uint32_t),
                             void *user_data);

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

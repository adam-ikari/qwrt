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

/* ── Constants ── */

#define QWRT_IPC_PROTO_VERSION    1
#define QWRT_IPC_ROLE_WORKER      0
#define QWRT_IPC_ROLE_RT          1
#define QWRT_IPC_HANDSHAKE_TIMEOUT_MS  5000
#define QWRT_IPC_TERMINATE_TIMEOUT_MS  2000
#define QWRT_IPC_READ_BUF_SIZE    65536

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
/* Channel handle — typedef first so headers can forward-declare
 * `typedef struct qwrt_proc_s qwrt_proc_t;` without pulling in uv types. */
typedef struct qwrt_proc_s qwrt_proc_t;

#ifndef QWRT_USE_MOCK_LIBUV
/* Full struct body is private to ipc_process.c (real-libuv pipe APIs).
 * Mock test builds (QWRT_USE_MOCK_LIBUV) only see the opaque typedef —
 * mock_libuv.h has no uv_pipe_t (design §10.1). */
struct qwrt_proc_s {
    uv_pipe_t pipe;           /* duplex pipe to child (parent end) */
    pid_t     pid;            /* child PID */
    int       id;             /* local slot id (source label) */
    int       role;           /* QWRT_IPC_ROLE_* */
    int       state;          /* qwrt_proc_state_t */
    void     *parent_rt;      /* parent qwrt_t* (for callbacks) */
    /* Read accumulator: frames = [4-byte LE len][envelope]; async reads via
     * qwrt_proc_start_read → envelope decode → parent msgq push. */
    uint8_t  *rbuf;
    size_t    rbuf_cap;
    size_t    rbuf_len;
    uint32_t  frame_len;      /* 0 = need 4-byte header */
    int       pipe_inited;    /* uv_pipe_init done (close via uv_close) */
};
#endif /* !QWRT_USE_MOCK_LIBUV */

/* Parse handshake JSON → version/role/id. Returns 0 ok, -1 fail. */
int qwrt_ipc_parse_handshake(const char *json, int *out_v,
                             int *out_role, int *out_id);

/* Parse ack JSON → ok/version. Returns 0 ok, -1 fail. */
int qwrt_ipc_parse_ack(const char *json, int *out_ok, int *out_v);

/* ── Parent-side channel handle ── */

typedef enum {
    QWRT_PROC_BUILD = 0,   /* pipe opened, awaiting handshake */
    QWRT_PROC_RUN   = 1,   /* handshake done, application messages flow */
    QWRT_PROC_DEAD  = 2    /* peer EOF or killed */
} qwrt_proc_state_t;

/* Spawn child process (qwrt-rt binary), pass fd via --parent-fd.
 * Creates socketpair, fork+exec, parent-side handshake (blocking, 5s).
 * binary_path: NULL → auto-detect (QWRT_RT_SERVER env / compile-time /
 *               /proc/self/exe dir + "qwrt-rt").
 * script_path: optional --script PATH argument to child (file path).
 * Returns 0 on success, qwrt_err_t (<0) on failure. */
int qwrt_proc_spawn(qwrt_t *parent, qwrt_proc_t *proc,
                    const char *binary_path,
                    int role, int id,
                    const char *script_path);
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
/* Child → parent envelope write. Returns 0 ok, -1 error. */
int qwrt_ipc_child_emit(int32_t source, int32_t target, int8_t kind,
                        const uint8_t *payload, uint32_t payload_len);
/* Child channel fd, -1 if unset. */
int qwrt_ipc_child_channel(void);

/* Post an envelope to child (synchronous uv_try_write). */
int qwrt_proc_post(qwrt_proc_t *proc,
                   int32_t source, int32_t target, int8_t kind,
                   const uint8_t *payload, uint32_t payload_len);

/* Close pipe and reap child if still alive. NULL-safe on pipe. */
void qwrt_proc_destroy(qwrt_proc_t *proc);

#ifdef __cplusplus
}
#endif

#endif /* QWRT_IPC_PROCESS_H */

#ifndef QZ_H
#define QZ_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct qz_t qz_t;

/* ================================================================
 * qzjs configuration
 * ================================================================ */

typedef struct qz_config_s {
    /* 主 context 启动时在 qzjs 线程上 eval；抛异常 → qz_create 返回 NULL */
    const char *initial_script;
    /* 出站消息回调：跑在 qzjs 线程，必须线程安全。nullptr 表示宿主不接收消息。 */
    void (*message_cb)(qz_t *rt, const char *json, size_t len, void *data);
    int  debug;                      /* 沿用 DAP bit 语义 */
    void *host_data;                 /* per-runtime opaque ptr，可经 qz_get_runtime_data 读取 */
    /* 控制面三档（CTL-0/CTL-2）：OFF（默认，qz_control 恒 -1）/
     * IN_PROC（进程内宿主线程命令）/ LOCAL（IN_PROC + 本机 uv_pipe 端点）。
     * 见 docs/plans/2026-09-04-control-plane-design.md §4.1。 */
    int control_plane;               /* qz_control_plane_t 值 */
    /* LOCAL 档的端点路径（AF_UNIX）。NULL → 缺省 /tmp/qzjs-<pid>-<n>.ctl。
     * 端点文件权限 0600；连接方以 SO_PEERCRED 校验 uid（§2.3 / §4.2）。 */
    const char *control_pipe_path;
    /* Worker 执行后端（M-P1/M-P2 多进程模型 §1.4）。取值语义随编译模型
     * （QZ_PROCESS_MODEL）条件编译——见下方 qz_worker_backend_t 注释。
     * 粒度 per-rt：同一 qz_t 的全部 worker 同后端。M-P1 缺省 THREAD；
     * M-P2 起 ISOLATED 编译缺省 PROCESS（编译模型驱动缺省，§1.4）。 */
    int worker_backend;              /* qz_worker_backend_t 值 */
} qz_config_t;
typedef enum {
    QZ_CONTROL_OFF = 0,     /* 默认：控制面关闭 */
    QZ_CONTROL_IN_PROC = 1, /* 进程内命令（msgq 路径） */
    QZ_CONTROL_LOCAL = 2,   /* IN_PROC + uv_pipe 本地端点（CTL-2） */
} qz_control_plane_t;

/* Worker 执行后端（qz_config_t.worker_backend 取值）。
 *
 * 数值随编译模型条件编译（§2.1「C 枚举字段可以条件编译」），即 memset 清零的
 * 配置在两种编译下各自落到该模型的缺省后端：
 *   ISOLATED 编译：0 = PROCESS（缺省＝独立进程）、1 = THREAD（显式回退）
 *   THREAD  编译：0 = THREAD（现状基线）、1 = PROCESS（未启用 → 求值报错，§1.4）
 *   mock 测试构建（QZ_USE_MOCK_LIBUV，无 ipc 后端）：恒按 THREAD 排列
 * 铁律：只用符号常量，勿硬编码 0/1。消费方必须与库看到同一 QZ_PROCESS_MODEL_*
 * 宏——CMake 构建经 target_compile_definitions(qzjs PUBLIC …) 自动传递。 */
#if defined(QZ_PROCESS_MODEL_ISOLATED) && !defined(QZ_USE_MOCK_LIBUV)
typedef enum {
    QZ_WORKER_BACKEND_PROCESS = 0, /* 编译缺省：独立进程 worker（M-P1 机制） */
    QZ_WORKER_BACKEND_THREAD  = 1, /* 显式回退：真线程 */
} qz_worker_backend_t;
#else
typedef enum {
    QZ_WORKER_BACKEND_THREAD  = 0, /* 缺省：真线程（THREAD 编译 / mock 测试构建） */
    QZ_WORKER_BACKEND_PROCESS = 1, /* 独立进程 worker（THREAD 编译未启用 → 报错） */
} qz_worker_backend_t;
#endif

/* ================================================================
 * Core API
 * ================================================================ */

/* 创建 qzjs：阻塞到内部线程 ready。initial_script 在 qzjs 线程上 eval，
 * 抛异常则返回 NULL。宿主回调 message_cb 跑在 qzjs 线程，必须线程安全。
 * 返回的 rt 由宿主线程调用 qz_destroy 销毁。 */
qz_t *qz_create(const qz_config_t *config);

/* 优雅关停：请求内部线程退出 → join → 释放 runtime → free。NULL-safe。
 * 只允许宿主线程调用（与 qz_create 同一线程）。 */
void qz_destroy(qz_t *rt);

/* 线程安全入站消息（任何线程可调）。json 会被拷贝。返回 0 成功，-1 失败。 */
int qz_post_message(qz_t *rt, const char *json, size_t len);
/* 便捷函数：向 JS 发送 eval 指令（执行一段 JS 代码）。
 *
 * 本质：这是对"JSON 消息驱动"模式的便利封装，不是新的执行机制。它构造
 * {"corr":N,"cmd":"eval","code":...} 指令经 qz_post_message 发给 JS 侧
 * 的 onmessage 处理器；JS 收到后 eval(code)，结果经 postMessage 发回宿主
 * （message_cb 收到）。corr 单调递增，供宿主在 message_cb 里把结果与
 * 请求对应。宿主需在 initial_script 里装 onmessage eval 处理器（或使用
 * 默认注入的处理器）。线程安全，返回 0 成功，-1 失败。 */
int qz_eval(qz_t *rt, const char *code);

/* 便捷函数：向 JS 发送调用指令（调用一个全局函数）。
 *
 * 本质同 qz_eval：构造 {"corr":N,"cmd":"call","fn":...,"args":...} 指令，
 * JS onmessage 收到后调用 globalThis[fn](...args)，结果经 postMessage
 * 发回宿主（message_cb）。fn 是全局函数名，args 是 JSON 数组或 NULL。
 * 线程安全，返回 0 成功，-1 失败。 */
int qz_call(qz_t *rt, const char *fn, const char *args_json);

/* Request auto-exit once there is no pending async work (CLI use), and block
 * until the thread has exited. Thread-safe; mutually exclusive with
 * qz_destroy (call one or the other, never both). After this returns the
 * runtime is torn down and must not be used (no further post_message).
 * Do NOT call qz_destroy after this: destroy forces shutdown and would
 * cancel pending async work (e.g. a live timer). */
void qz_wait_idle(qz_t *rt);

/* Liveness ping（ISOLATED 编译，宿主→主RT 进程）：发 CONTROL ping（corr =
 * 单调 seq）并阻塞等待主RT C 层读泵直回的 PONG（不经 JS/msgq——pong 延迟
 * 只反映主RT 进程 uv loop 的健康度，JS 忙不误报）。返回 0 = loop 通畅
 * （deadline 内 PONG 命中）；1 = 超时 = 对端 loop 阻塞（或对端极度繁忙但
 * 读泵 starvation，见设计文档）；-1 = 参数/状态错误（未 ready、正在关停、
 * 通道已死——进程死亡另有 EOF 路径）。timeout_ms 建议 100–1000。 */
int qz_ping(qz_t *rt, int32_t timeout_ms);

/* Liveness ping 到树中任意 worker（§8.2 path 寻址，仅 ISOLATED）：path =
 * root-relative 槽位链（同命令面 target_path，如 {1001,1002} = 主RT 的
 * worker 1001 的 sub worker 1002）。返回 0 = 目标 loop 通畅；1 = 超时 =
 * 目标 loop 阻塞；-1 = 参数/状态错误/路径不存在/中间通道死（转发失败由
 * 中间节点回执快速判 -1，不白等 timeout）。PING 按链逐跳下投（读泵 C 层
 * 转发，不消费不进 JS），目标读泵直回 PONG（回显路径作过境标记）沿树上
 * 行；中间节点目标级 JS 零参与。timeout_ms 建议 100–1000。 */
int qz_ping_path(qz_t *rt, const int32_t *path, int path_len,
                   int32_t timeout_ms);

/* 控制命令入队（线程安全，任何线程可调）。bytes 为命令 JSON，内部拷贝。
 * control_plane=OFF 时恒返回 -1。返回 0 成功，-1 失败（OFF/OOM/参数非法）。
 * 命令由 qzjs 线程在自己事件循环的安全点自主执行；结果经 message_cb 异步
 * 回传，回执 JSON 顶层带 "ctl":true 标记位，correl 原样透传供配对。
 * 设计：docs/plans/2026-09-04-control-plane-design.md §1-§3。 */
int qz_control(qz_t *rt, const char *bytes, size_t len);

void *qz_get_runtime_data(qz_t *rt);
void  qz_set_runtime_data(qz_t *rt, void *data);

/* 释放 qzjs 分配的 malloc 块（历史兼容）。NULL-safe。 */
void qz_free(void *ptr);

/* ================================================================
 * Extension interface
 * ================================================================ */

typedef struct qz_ext_t qz_ext_t;

struct qz_ext_t {
    const char *name;
    int (*init)(qz_ext_t *ext, qz_t *rt);
    void (*destroy)(qz_ext_t *ext, qz_t *rt);
    int (*suspend)(qz_ext_t *ext, qz_t *rt);
    int (*resume)(qz_ext_t *ext, qz_t *rt);
    void *user_data;
};

/* Forward declaration for JSContext (kept for extension ABI compatibility) */
struct JSContext;

#ifdef __cplusplus
}
#endif

#endif /* QZ_H */

#ifndef QWRT_H
#define QWRT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct qwrt_t qwrt_t;

/* ================================================================
 * qwrt configuration
 * ================================================================ */

typedef struct qwrt_config_s {
    /* 主 context 启动时在 qwrt 线程上 eval；抛异常 → qwrt_create 返回 NULL */
    const char *initial_script;
    /* 出站消息回调：跑在 qwrt 线程，必须线程安全。nullptr 表示宿主不接收消息。 */
    void (*message_cb)(qwrt_t *rt, const char *json, size_t len, void *data);
    int  debug;                      /* 沿用 DAP bit 语义 */
    void *host_data;                 /* per-runtime opaque ptr，可经 qwrt_get_runtime_data 读取 */
} qwrt_config_t;

/* ================================================================
 * Core API
 * ================================================================ */

/* 创建 qwrt：阻塞到内部线程 ready。initial_script 在 qwrt 线程上 eval，
 * 抛异常则返回 NULL。宿主回调 message_cb 跑在 qwrt 线程，必须线程安全。
 * 返回的 rt 由宿主线程调用 qwrt_destroy 销毁。 */
qwrt_t *qwrt_create(const qwrt_config_t *config);

/* 优雅关停：请求内部线程退出 → join → 释放 runtime → free。NULL-safe。
 * 只允许宿主线程调用（与 qwrt_create 同一线程）。 */
void qwrt_destroy(qwrt_t *rt);

/* 线程安全入站消息（任何线程可调）。json 会被拷贝。返回 0 成功，-1 失败。 */
int qwrt_post_message(qwrt_t *rt, const char *json, size_t len);

void *qwrt_get_runtime_data(qwrt_t *rt);
void  qwrt_set_runtime_data(qwrt_t *rt, void *data);

/* 释放 qwrt 分配的 malloc 块（历史兼容）。NULL-safe。 */
void qwrt_free(void *ptr);

/* ================================================================
 * Extension interface
 * ================================================================ */

typedef struct qwrt_ext_t qwrt_ext_t;

struct qwrt_ext_t {
    const char *name;
    int (*init)(qwrt_ext_t *ext, qwrt_t *rt);
    void (*destroy)(qwrt_ext_t *ext, qwrt_t *rt);
    int (*suspend)(qwrt_ext_t *ext, qwrt_t *rt);
    int (*resume)(qwrt_ext_t *ext, qwrt_t *rt);
    void *user_data;
};

/* Forward declaration for JSContext (kept for extension ABI compatibility) */
struct JSContext;

#ifdef __cplusplus
}
#endif

#endif /* QWRT_H */

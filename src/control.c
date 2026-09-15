/*
 * qwrt Control Plane — CTL-0 进程内基线
 *
 * 控制命令消息（入队，flags=CONTROL）→ qwrt 线程 wake 分流点自主执行。
 * 外部只发「求」，运行时自主完成——与 idle safepoint 同哲学。
 *
 * 命令集（CTL-0 四命令）：eval / inspect / metrics / interrupt。
 * interrupt 唯一无安全点：原子标志投递即生效（§3.9）。
 *
 * 回执表（§1.2）：登记先于入队（dispatch 必能命中）；消费与超时回收 qwrt
 * 线程独占，一把表内锁串行化（竞争面 = 命令入队频率）。过期条目两个回收点：
 * 主循环 reap（惰性扫描）与 dispatch 前 claim（fail-closed：到期未执行 →
 * 作废，发 TIMEOUT，绝不迟执行）。
 *
 * 设计：docs/plans/2026-09-04-control-plane-design.md §1-§3、§6。
 */

#include "qwrt_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ── Receipt table ── */

struct qwrt_ctl_recept_s {
    char *correl;           /* strdup'd correl（简单 id：无转义字符） */
    uint64_t deadline_ns;   /* uv_hrtime() + timeout_ms * 1e6 */
    int32_t  reply_dir;     /* CTL-1 回程方向：-1 = 本地 message_cb；
                             * >=0 = 回执信封 target（命令来源地址） */
    struct qwrt_ctl_recept_s *next;
};
/* ── 控制命令字段提取（生产者线程，无 JSContext）──
 *
 * 只在 qwrt_control（生产者线程）用于提取 correl/timeout_ms/op 三个顶层
 * 字段。JSRuntime 归 qwrt 线程所有，生产者线程无 JSContext 可用，且
 * interrupt 命令要求 runtime 暂停/未初始化时也能入队生效——JS_ParseJSON
 * 不可用，必须在 C 层做（裁决：docs/architecture/c-js-layering.md §6.6）。
 * 完整解析在 dispatch（qwrt 线程）用 JS_ParseJSON；此处用 vendored
 * cJSON（用户指令：不手写）。correl 约定为简单 id：无转义/嵌套。 */

#include <cJSON.h>

/* CTL-1 跨进程回执走信封（ipc_process.c 的发送原语）。mock 测试构建无 ipc
 * 后端（QWRT_USE_MOCK_LIBUV），回程恒为本地，转发路径不编入。 */
#ifndef QWRT_USE_MOCK_LIBUV
#include "ipc_process.h"
#endif

/* ── Receipt helpers (qwrt thread) ── */

/* 本节点在父树中的槽位 id（宿主 0 / 主RT 1 / worker --worker-id）。不依赖
 * ipc 后端（mock 构建亦编入），供 CTL-1 路由与转发复用。 */
int32_t qwrt_ctl_local_id(qwrt_t *rt)
{
    if (rt->worker_self)
        return (int32_t)((qwrt_worker_t *)rt->worker_self)->id;
    return 1;                   /* QWRT_IPC_MAIN_ID：主RT 通道上的本地标签 */
}

#ifndef QWRT_USE_MOCK_LIBUV
/* 回程发送：把回执 JSON 交给跨进程通道（CTL-1 §2.2「回执沿树回」）。
 * target = 回程地址（命令来源，逐跳相对寻址）：0/1 → 上行（父通道）；
 * >1 → 下行到本地子槽位。source 填本节点槽位 id（父视角的「来源」）。 */

static void ctl_emit_remote(qwrt_t *rt, int32_t target,
                            const uint8_t *json, size_t len)
{
    int32_t self = qwrt_ctl_local_id(rt);
    if (target == 0 || target == 1) {
        qwrt_ipc_child_emit(self, target, IPC_ENV_KIND_CONTROL, json,
                            (uint32_t)len);
        return;
    }
    for (int i = 0; i < QWRT_MAX_PROC_HANDLES; i++) {
        qwrt_proc_handle_t *h = &rt->proc_handles[i];
        if (h->live && h->proc && h->proc->id == target) {
            qwrt_proc_post(h->proc, self, target, IPC_ENV_KIND_CONTROL, json,
                           (uint32_t)len);
            return;
        }
    }
    /* 无对应通道：回执丢弃（发起方靠自身 timeout 收束）。 */
}
#else
static void ctl_emit_remote(qwrt_t *rt, int32_t target,
                            const uint8_t *json, size_t len)
{
    QWRT_UNUSED(rt); QWRT_UNUSED(target); QWRT_UNUSED(json); QWRT_UNUSED(len);
}
#endif

/* 查表取回程方向并移除条目（qwrt 线程独占；回执消费点）。 */
static int32_t ctl_take_reply_dir(qwrt_t *rt, const char *correl)
{
    if (!correl) return -1;
    int32_t dir = -1;
    uv_mutex_lock(&rt->ctl_lock);
    struct qwrt_ctl_recept_s **pp = &rt->ctl_pending;
    while (*pp) {
        if (strcmp((*pp)->correl, correl) == 0) {
            struct qwrt_ctl_recept_s *r = *pp;
            *pp = r->next;
            dir = r->reply_dir;
            free(r->correl);
            free(r);
            break;
        }
        pp = &(*pp)->next;
    }
    uv_mutex_unlock(&rt->ctl_lock);
    return dir;
}

/* 回执出口：序列化 obj（吞掉），按 reply_dir 分发——>=0 走信封（跨进程
 * 命令），否则走 message_cb（进程内命令，CTL-0 行为不变）。 */
static void ctl_emit_obj(qwrt_t *rt, JSContext *ctx, JSValue obj,
                         int32_t reply_dir)
{
    JSValue str = JS_JSONStringify(ctx, obj, JS_UNDEFINED, JS_UNDEFINED);
    JS_FreeValue(ctx, obj);
    if (JS_IsException(str)) {
        JS_FreeValue(ctx, str);
        return;
    }
    const char *json = JS_ToCString(ctx, str);
    JS_FreeValue(ctx, str);
    if (!json) return;
    if (reply_dir >= 0) {
        ctl_emit_remote(rt, reply_dir, (const uint8_t *)json, strlen(json));
    } else if (rt->config.message_cb) {
        rt->config.message_cb(rt, json, strlen(json), rt->host_data);
    }
    JS_FreeCString(ctx, json);
}

/* 构建 JSValue 回执对象并经回程下发（吞掉 obj）。条目在回执生成时消费
 * （§1.2）：命中跨进程命令 → 信封；否则 message_cb。 */
static void ctl_send_receipt(qwrt_t *rt, JSContext *ctx, JSValue receipt_obj)
{
    int32_t reply_dir = -1;
    JSValue cv = JS_GetPropertyStr(ctx, receipt_obj, "correl");
    const char *correl = JS_ToCString(ctx, cv);
    if (correl) {
        reply_dir = ctl_take_reply_dir(rt, correl);
        JS_FreeCString(ctx, correl);
    }
    JS_FreeValue(ctx, cv);
    ctl_emit_obj(rt, ctx, receipt_obj, reply_dir);
}

/* 标准回执骨架：{ctl:true, correl, ok, error?, code?}。吞掉 correl 字符串。 */
static JSValue ctl_receipt_obj(JSContext *ctx, char *correl, int ok)
{
    JSValue r = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, r, "ctl", JS_TRUE);
    JS_SetPropertyStr(ctx, r, "correl", JS_NewString(ctx, correl ? correl : ""));
    JS_SetPropertyStr(ctx, r, "ok", ok ? JS_TRUE : JS_FALSE);
    free(correl);
    return r;
}

static void ctl_error_receipt(qwrt_t *rt, JSContext *ctx, char *correl,
                              const char *error, const char *code)
{
    JSValue r = ctl_receipt_obj(ctx, correl, 0);
    JS_SetPropertyStr(ctx, r, "error", JS_NewString(ctx, error));
    JS_SetPropertyStr(ctx, r, "code", JS_NewString(ctx, code));
    ctl_send_receipt(rt, ctx, r);
}

/* 超时回执：条目已在 claim/reap 中移除，回程方向随参数带入。 */
static void ctl_timeout_receipt(qwrt_t *rt, JSContext *ctx, const char *correl,
                                int32_t reply_dir)
{
    char *dup = correl ? strdup(correl) : NULL;
    JSValue r = ctl_receipt_obj(ctx, dup, 0);
    JS_SetPropertyStr(ctx, r, "error", JS_NewString(ctx, "timeout"));
    JS_SetPropertyStr(ctx, r, "code", JS_NewString(ctx, "TIMEOUT"));
    ctl_emit_obj(rt, ctx, r, reply_dir);
}

/* ── Receipt table operations ── */

void qwrt_ctl_register(qwrt_t *rt, const char *correl, uint64_t deadline_ns,
                       int32_t reply_dir)
{
    struct qwrt_ctl_recept_s *r =
        (struct qwrt_ctl_recept_s *)calloc(1, sizeof *r);
    if (!r) return;             /* OOM：无条目，发起方靠超时（§6） */
    r->correl = strdup(correl ? correl : "");
    if (!r->correl) { free(r); return; }
    r->deadline_ns = deadline_ns;
    r->reply_dir = reply_dir;
    uv_mutex_lock(&rt->ctl_lock);
    r->next = rt->ctl_pending;
    rt->ctl_pending = r;
    uv_mutex_unlock(&rt->ctl_lock);
}

static void ctl_unregister(qwrt_t *rt, const char *correl)
{
    /* push 失败时回收刚登记的条目（§6：入队失败 → 表无条目）。 */
    if (!correl) return;
    uv_mutex_lock(&rt->ctl_lock);
    struct qwrt_ctl_recept_s **pp = &rt->ctl_pending;
    while (*pp) {
        if (strcmp((*pp)->correl, correl) == 0) {
            struct qwrt_ctl_recept_s *r = *pp;
            *pp = r->next;
            uv_mutex_unlock(&rt->ctl_lock);
            free(r->correl);
            free(r);
            return;
        }
        pp = &(*pp)->next;
    }
    uv_mutex_unlock(&rt->ctl_lock);
}

/* dispatch 前核验（fail-closed，§1.3）：命中且未过期 → 放行（1）；命中但
 * 已过期 → 移除 + TIMEOUT 回执（0，命令作废绝不迟执行）；未命中（已被
 * reap 回收/从未登记）→ 0（TIMEOUT 已由 reap 发出，静默跳过）。 */
static int ctl_claim(qwrt_t *rt, const char *correl)
{
    if (!correl) return 1;      /* 无 correl：无法核验，放行（回执本就不可配对） */
    uint64_t now = uv_hrtime();
    struct qwrt_ctl_recept_s *dead = NULL;
    int live = 0;
    uv_mutex_lock(&rt->ctl_lock);
    struct qwrt_ctl_recept_s **pp = &rt->ctl_pending;
    while (*pp) {
        if (strcmp((*pp)->correl, correl) == 0) {
            if ((*pp)->deadline_ns > now) {
                live = 1;
            } else {
                dead = *pp;
                *pp = dead->next;
            }
            break;
        }
        pp = &(*pp)->next;
    }
    uv_mutex_unlock(&rt->ctl_lock);
    if (dead) {
        JSContext *ctx = qwrt_get_active_jsctx(rt);
        if (ctx) ctl_timeout_receipt(rt, ctx, dead->correl, dead->reply_dir);
        free(dead->correl);
        free(dead);
    }
    return live;
}
/* 命令顶层字段提取（生产者/路由路径，无 JSContext；用 vendored cJSON）。
 * buf 须 NUL 结尾。缺失字段留 NULL/缺省；target_out 非 NULL 时提取
 * "target"（CTL-1 寻址字段，缺省 1 = 接收方自身）。 */
static void ctl_extract(const char *buf, char **op_out, char **correl_out,
                        int *timeout_ms_out, int32_t *target_out)
{
    cJSON *j = cJSON_Parse(buf);
    if (!j) return;
    const cJSON *opv = cJSON_GetObjectItemCaseSensitive(j, "op");
    const cJSON *correlv = cJSON_GetObjectItemCaseSensitive(j, "correl");
    const cJSON *tmv = cJSON_GetObjectItemCaseSensitive(j, "timeout_ms");
    const cJSON *tgv = cJSON_GetObjectItemCaseSensitive(j, "target");
    if (op_out && cJSON_IsString(opv) && opv->valuestring)
        *op_out = strdup(opv->valuestring);
    if (correl_out && cJSON_IsString(correlv) && correlv->valuestring)
        *correl_out = strdup(correlv->valuestring);
    if (timeout_ms_out && cJSON_IsNumber(tmv))
        *timeout_ms_out = tmv->valueint;
    if (target_out && cJSON_IsNumber(tgv))
        *target_out = (int32_t)tgv->valueint;
    cJSON_Delete(j);
}

int32_t qwrt_ctl_cmd_target(const char *json, size_t len)
{
    QWRT_UNUSED(len);
    int32_t target = 1;         /* 缺省：接收方自身（QWRT_IPC_MAIN_ID） */
    if (!json) return target;
    ctl_extract(json, NULL, NULL, NULL, &target);
    return target;
}

int qwrt_control(qwrt_t *rt, const char *bytes, size_t len)
{
    if (!rt || rt->magic != QWRT_MAGIC || !bytes)
        return -1;
    if (rt->config.control_plane == QWRT_CONTROL_OFF)
        return -1;

    /* 提取器按 NUL 结尾扫描，而 API 契约只保证 (bytes, len)——先拷贝补
     * NUL（msgq 内部同样要拷，此处多一份短暂副本）。 */
    char *buf = (char *)malloc(len + 1);
    if (!buf) return -1;
    memcpy(buf, bytes, len);
    buf[len] = '\0';

    int timeout_ms = 5000;
    char *op = NULL, *correl = NULL;
    ctl_extract(buf, &op, &correl, &timeout_ms, NULL);

    /* interrupt：投递即生效——原子标志在生产者线程置位（§1.1 唯一例外）。
     * 命令消息照常入队只为 correl 回执。 */
    if (op && strcmp(op, "interrupt") == 0)
        __atomic_store_n(&rt->ctl_interrupt, 1, __ATOMIC_RELEASE);

    /* 登记先于入队（§1.2）：dispatch 必能命中条目。本条命令在本进程产生
     * 回执（进程内 dispatch 或本进程 message_cb）→ reply_dir = -1。 */
    uint64_t deadline = uv_hrtime() + (uint64_t)timeout_ms * 1000000ULL;
    qwrt_ctl_register(rt, correl, deadline, -1);

    /* ISOLATED 宿主：入队后由 host_wake_cb 装 CONTROL 信封发主RT（target
     * 取自命令 "target" 字段）；THREAD：由 qwrt_wake_cb 就地 dispatch。 */
    int rc = qwrt_msg_push(rt, buf, len, QWRT_MSG_SRC_HOST,
                           QWRT_MSG_FLAG_CONTROL);
    free(buf);
    if (rc != 0) {
        ctl_unregister(rt, correl);   /* §6：入队失败 → 表无条目 */
        free(op);
        free(correl);
        return -1;
    }
    free(op);
    free(correl);
    return 0;
}

/* ── CTL-1：信封 CONTROL 命令树路由（§2.2 / 多进程 §4.3、§7.2）──
 *
 * 逐跳相对寻址：target 由当前持有信封的节点相对解释——等于本地槽位 id 即
 * 命中本地（入 msgq flags=CONTROL）；0/1 为朝根方向（上行）；>1 为本地子
 * 槽位（下行）。source 承载回程方向，转发时保持不变（只有 target 逐跳改写）。 */

qwrt_ctl_route_t qwrt_ctl_route_decide(int32_t local_id, int32_t target)
{
    if (target == local_id) return QWRT_CTL_ROUTE_LOCAL;
    if (target == 0 || target == 1) return QWRT_CTL_ROUTE_UP;
    if (target > 1) return QWRT_CTL_ROUTE_DOWN;
    return QWRT_CTL_ROUTE_DROP;     /* target < 0：无此地址 */
}

int qwrt_control_route(qwrt_t *rt, int32_t local_id, int32_t source,
                       int32_t target, const uint8_t *payload, uint32_t len)
{
    if (!rt || rt->magic != QWRT_MAGIC || !payload) return -1;
    /* OFF：信封 CONTROL 命令类入站即丢弃（§4.1）。系统级 CONTROL（握手/
     * idle/shutdown）由调用方先行分流，不受本档影响。 */
    if (rt->config.control_plane == QWRT_CONTROL_OFF) return -1;

    switch (qwrt_ctl_route_decide(local_id, target)) {
    case QWRT_CTL_ROUTE_LOCAL: {
        char *buf = (char *)malloc((size_t)len + 1);
        if (!buf) return -1;
        memcpy(buf, payload, len);
        buf[len] = '\0';
        int timeout_ms = 5000;
        char *op = NULL, *correl = NULL;
        ctl_extract(buf, &op, &correl, &timeout_ms, NULL);
        if (op && strcmp(op, "interrupt") == 0)
            __atomic_store_n(&rt->ctl_interrupt, 1, __ATOMIC_RELEASE);
        uint64_t deadline = uv_hrtime() + (uint64_t)timeout_ms * 1000000ULL;
        qwrt_ctl_register(rt, correl, deadline, source);
        int rc = qwrt_msg_push(rt, buf, (size_t)len, QWRT_MSG_SRC_HOST,
                               QWRT_MSG_FLAG_CONTROL);
        free(buf);
        if (rc != 0) ctl_unregister(rt, correl);
        free(op);
        free(correl);
        return rc == 0 ? 0 : -1;
    }
    case QWRT_CTL_ROUTE_UP:
#ifndef QWRT_USE_MOCK_LIBUV
        qwrt_ipc_child_emit(source, target, IPC_ENV_KIND_CONTROL, payload, len);
#endif
        return 0;
    case QWRT_CTL_ROUTE_DOWN: {
#ifndef QWRT_USE_MOCK_LIBUV
        for (int i = 0; i < QWRT_MAX_PROC_HANDLES; i++) {
            qwrt_proc_handle_t *h = &rt->proc_handles[i];
            if (h->live && h->proc && h->proc->id == target) {
                qwrt_proc_post(h->proc, source, target,
                               IPC_ENV_KIND_CONTROL, payload, len);
                return 0;
            }
        }
#endif
        return -1;              /* 无对应子槽位 */
    }
    case QWRT_CTL_ROUTE_DROP:
    default:
        return -1;
    }
}
void qwrt_ctl_reap_timeouts(qwrt_t *rt)
{
    uint64_t now = uv_hrtime();
    struct qwrt_ctl_recept_s *dead = NULL;
    struct qwrt_ctl_recept_s **dead_tail = &dead;

    uv_mutex_lock(&rt->ctl_lock);
    struct qwrt_ctl_recept_s **pp = &rt->ctl_pending;
    while (*pp) {
        if ((*pp)->deadline_ns <= now) {
            struct qwrt_ctl_recept_s *r = *pp;
            *pp = r->next;           /* unlink */
            *dead_tail = r;           /* append to dead chain */
            dead_tail = &r->next;
        } else {
            pp = &(*pp)->next;
        }
    }
    uv_mutex_unlock(&rt->ctl_lock);

    if (!dead) return;
    JSContext *ctx = qwrt_get_active_jsctx(rt);
    while (dead) {
        struct qwrt_ctl_recept_s *r = dead;
        dead = r->next;
        if (ctx) ctl_timeout_receipt(rt, ctx, r->correl, r->reply_dir);
        free(r->correl);
        free(r);
    }
}

void qwrt_ctl_teardown(qwrt_t *rt)
{
    /* 释放所有未完成回执条目（无回执发出——发起方靠自身超时）+ 销毁锁。 */
    struct qwrt_ctl_recept_s *r = rt->ctl_pending;
    while (r) {
        struct qwrt_ctl_recept_s *next = r->next;
        free(r->correl);
        free(r);
        r = next;
    }
    rt->ctl_pending = NULL;
    uv_mutex_destroy(&rt->ctl_lock);
}

/* ── Interrupt handler (QuickJS callback) ── */

int qwrt_ctl_interrupt_handler(JSRuntime *jsrt, void *opaque)
{
    (void)jsrt;
    qwrt_t *rt = (qwrt_t *)opaque;
    if (__atomic_load_n(&rt->ctl_interrupt, __ATOMIC_ACQUIRE)) {
        __atomic_store_n(&rt->ctl_interrupt, 0, __ATOMIC_RELEASE);
        return 1;              /* non-zero → JS_ThrowInterrupted（uncatchable） */
    }
    return 0;
}


/* ── Dispatch（qwrt 线程独占，wake safepoint） ── */

/* 目标 ctx：cmd.ctx_id 缺省 → active ctx；提供但不存在 → NULL（NOT_FOUND）。 */
static qwrt_ctx_t *ctl_target_ctx(qwrt_t *rt, JSContext *pctx, JSValue cmd)
{
    JSValue cid = JS_GetPropertyStr(pctx, cmd, "ctx_id");
    int has = !(JS_IsUndefined(cid) || JS_IsNull(cid));
    int id = -1;
    if (has && JS_ToInt32(pctx, &id, cid) < 0) id = -1;
    JS_FreeValue(pctx, cid);
    if (!has || id < 0) return qwrt_get_active_ctx(rt);
    return qwrt_get_ctx_by_id(rt, id);
}

static void ctl_eval(qwrt_t *rt, JSContext *rcpt_ctx, JSValue cmd, char *correl)
{
    qwrt_ctx_t *target = ctl_target_ctx(rt, rcpt_ctx, cmd);
    if (!target || !target->jsctx) {
        ctl_error_receipt(rt, rcpt_ctx, correl, "no such context", "NOT_FOUND");
        return;
    }
    JSContext *ctx = target->jsctx;

    JSValue script = JS_GetPropertyStr(rcpt_ctx, cmd, "script");
    const char *code = JS_ToCString(rcpt_ctx, script);
    JS_FreeValue(rcpt_ctx, script);
    if (!code) {
        ctl_error_receipt(rt, rcpt_ctx, correl, "script must be a string",
                          "INVALID_ARG");
        return;
    }

    JSValue val = JS_Eval(ctx, code, strlen(code), "<control-eval>",
                          JS_EVAL_TYPE_GLOBAL);
    JS_FreeCString(rcpt_ctx, code);

    if (JS_IsException(val)) {
        JSValue exc = JS_GetException(ctx);
        JSValue r = ctl_receipt_obj(ctx, correl, 0);
        if (JS_IsUncatchableError(exc)) {
            /* §3.9：中断异常不可捕获 → 专属回执 */
            JS_SetPropertyStr(ctx, r, "error", JS_NewString(ctx, "interrupted"));
            JS_SetPropertyStr(ctx, r, "code", JS_NewString(ctx, "INTERRUPTED"));
        } else {
            const char *msg = JS_ToCString(ctx, exc);
            JS_SetPropertyStr(ctx, r, "error",
                              JS_NewString(ctx, msg ? msg : "unknown error"));
            JS_SetPropertyStr(ctx, r, "code", JS_NewString(ctx, "JS_EXCEPTION"));
            if (msg) JS_FreeCString(ctx, msg);
        }
        JS_FreeValue(ctx, exc);
        JS_FreeValue(ctx, val);
        ctl_send_receipt(rt, rcpt_ctx, r);
        return;
    }

    JSValue r = ctl_receipt_obj(ctx, correl, 1);
    JS_SetPropertyStr(ctx, r, "result", val);   /* val 移交 */
    ctl_send_receipt(rt, rcpt_ctx, r);
}

static void ctl_inspect(qwrt_t *rt, JSContext *rcpt_ctx, JSValue cmd, char *correl)
{
    qwrt_ctx_t *target = ctl_target_ctx(rt, rcpt_ctx, cmd);
    if (!target || !target->jsctx) {
        ctl_error_receipt(rt, rcpt_ctx, correl, "no such context", "NOT_FOUND");
        return;
    }
    JSContext *ctx = target->jsctx;

    JSValue expr = JS_GetPropertyStr(rcpt_ctx, cmd, "expr");
    const char *code = JS_ToCString(rcpt_ctx, expr);
    JS_FreeValue(rcpt_ctx, expr);
    if (!code) {
        ctl_error_receipt(rt, rcpt_ctx, correl, "expr must be a string",
                          "INVALID_ARG");
        return;
    }

    JSValue val = JS_Eval(ctx, code, strlen(code), "<control-inspect>",
                          JS_EVAL_TYPE_GLOBAL);
    JS_FreeCString(rcpt_ctx, code);

    if (JS_IsException(val)) {
        JSValue exc = JS_GetException(ctx);
        const char *msg = JS_ToCString(ctx, exc);
        ctl_error_receipt(rt, rcpt_ctx, correl,
                          msg ? msg : "unknown error", "JS_EXCEPTION");
        if (msg) JS_FreeCString(ctx, msg);
        JS_FreeValue(ctx, exc);
        JS_FreeValue(ctx, val);
        return;
    }

    /* expr 结果必须 JSON 可序列化（§3）：否则 INVALID_ARG */
    JSValue jsonstr = JS_JSONStringify(ctx, val, JS_UNDEFINED, JS_UNDEFINED);
    JS_FreeValue(ctx, val);
    if (JS_IsException(jsonstr) || JS_IsUndefined(jsonstr)) {
        JS_FreeValue(ctx, jsonstr);
        ctl_error_receipt(rt, rcpt_ctx, correl, "not JSON serializable",
                          "INVALID_ARG");
        return;
    }
    JSValue r = ctl_receipt_obj(ctx, correl, 1);
    JS_SetPropertyStr(ctx, r, "json", jsonstr);
    ctl_send_receipt(rt, rcpt_ctx, r);
}

static void ctl_metrics(qwrt_t *rt, JSContext *ctx, char *correl)
{
    /* 只读原子计数 + 引擎内存统计：即时返回，无安全点依赖（§3）。 */
    JSMemoryUsage mem;
    JS_ComputeMemoryUsage(rt->jsrt, &mem);
    int workers = 0;
    for (int i = 0; i < QWRT_MAX_WORKERS; i++)
        if (rt->workers[i]) workers++;

    JSValue r = ctl_receipt_obj(ctx, correl, 1);
    JS_SetPropertyStr(ctx, r, "heap_bytes", JS_NewInt64(ctx, mem.memory_used_size));
    JS_SetPropertyStr(ctx, r, "handle_count",
                      JS_NewInt32(ctx, rt->contexts[0] ? rt->contexts[0]->handle_count : 0));
    JS_SetPropertyStr(ctx, r, "pending_jobs",
                      JS_NewInt32(ctx, JS_IsJobPending(rt->jsrt) ? 1 : 0));
    JS_SetPropertyStr(ctx, r, "ctx_count", JS_NewInt32(ctx, rt->context_count));
    JS_SetPropertyStr(ctx, r, "worker_count", JS_NewInt32(ctx, workers));
    ctl_send_receipt(rt, ctx, r);
}

void qwrt_control_dispatch(qwrt_t *rt, qwrt_msg_t *m)
{
    /* qwrt 线程独占。m->data 为命令 JSON（msgq 保证 NUL 结尾）。 */
    JSContext *ctx = qwrt_get_active_jsctx(rt);
    if (!ctx) return;

    /* fail-closed（§1.3）：先以登记时的同一提取器核验条目——过期的命令
     * 作废（TIMEOUT），已被 reap 回收的跳过（回执已发）。 */
    char *correl0 = NULL;
    {
        cJSON *j = cJSON_Parse(m->data);
        if (j) {
            const cJSON *cv = cJSON_GetObjectItemCaseSensitive(j, "correl");
            if (cJSON_IsString(cv) && cv->valuestring)
                correl0 = strdup(cv->valuestring);
            cJSON_Delete(j);
        }
    }
    if (!ctl_claim(rt, correl0)) {
        free(correl0);
        return;
    }

    JSValue cmd = JS_ParseJSON(ctx, m->data, m->len, "<control-cmd>");
    if (JS_IsException(cmd)) {
        JS_FreeValue(ctx, cmd);
        ctl_error_receipt(rt, ctx, correl0, "bad json", "BAD_REQUEST");
        return;
    }

    JSValue opv = JS_GetPropertyStr(ctx, cmd, "op");
    const char *op = JS_ToCString(ctx, opv);
    JS_FreeValue(ctx, opv);

    if (op && strcmp(op, "eval") == 0) {
        ctl_eval(rt, ctx, cmd, correl0);
        correl0 = NULL;         /* correl 已被回执吞掉 */
    } else if (op && strcmp(op, "inspect") == 0) {
        ctl_inspect(rt, ctx, cmd, correl0);
        correl0 = NULL;
    } else if (op && strcmp(op, "metrics") == 0) {
        ctl_metrics(rt, ctx, correl0);
        correl0 = NULL;
    } else if (op && strcmp(op, "interrupt") == 0) {
        /* 标志已在 qwrt_control（生产者线程）置位并会在引擎指令边界触发；
         * 此处只回执（§3.9 投递即生效）。 */
        JSValue r = ctl_receipt_obj(ctx, correl0, 1);
        correl0 = NULL;
        JS_SetPropertyStr(ctx, r, "interrupted", JS_TRUE);
        ctl_send_receipt(rt, ctx, r);
    } else {
        /* 未知 op（含 IDLE_SAFEPOINT 类——CTL-0 不执行，§6 范围外） */
        ctl_error_receipt(rt, ctx, correl0, op ? op : "missing op",
                          "UNKNOWN_CMD");
        correl0 = NULL;
    }

    if (op) JS_FreeCString(ctx, op);
    free(correl0);
    JS_FreeValue(ctx, cmd);
}

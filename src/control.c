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
    struct qwrt_ctl_recept_s *next;
};

/* ── Minimal JSON value extractor (producer-thread, no JSRuntime) ──
 *
 * 只在 qwrt_control（生产者线程）用于提取 correl/timeout_ms/op 三个顶层
 * 字段（无 JSContext 可用——JSRuntime 归 qwrt 线程所有）。完整解析在
 * dispatch（qwrt 线程）用 JS_ParseJSON。对扁平 JSON 对象足够；correl
 * 约定为简单 id（无转义/嵌套）。 */

static const char *ctl_json_find_val(const char *json, const char *key)
{
    size_t klen = strlen(key);
    const char *p = json;
    while (*p) {
        if (*p == '"') {
            const char *q = p + 1;
            size_t i;
            for (i = 0; i < klen && q[i] && q[i] != '"'; i++) {
                if (q[i] != key[i]) break;
            }
            if (i == klen && q[i] == '"') {
                p = q + klen + 1;   /* skip past closing quote */
                while (*p == ' ' || *p == '\t' || *p == '\n' || *p == ':')
                    p++;
                return p;
            }
        }
        p++;
    }
    return NULL;
}

static char *ctl_json_get_str(const char *json, const char *key)
{
    const char *p = ctl_json_find_val(json, key);
    if (!p || *p != '"') return NULL;
    p++;                        /* skip opening quote */
    const char *start = p;
    while (*p && *p != '"') p++;
    if (*p != '"') return NULL;
    size_t len = (size_t)(p - start);
    char *out = (char *)malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, start, len);
    out[len] = '\0';
    return out;
}

static int ctl_json_get_int(const char *json, const char *key, int default_val)
{
    const char *p = ctl_json_find_val(json, key);
    if (!p) return default_val;
    int v = 0, sign = 1;
    if (*p == '-') { sign = -1; p++; }
    while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
    return sign * v;
}

/* ── Receipt helpers (qwrt thread) ── */

/* 构建 JSValue 回执对象并经 message_cb 下发。吞掉 obj。 */
static void ctl_send_receipt(qwrt_t *rt, JSContext *ctx, JSValue receipt_obj)
{
    if (!rt->config.message_cb) {
        JS_FreeValue(ctx, receipt_obj);
        return;
    }
    JSValue str = JS_JSONStringify(ctx, receipt_obj, JS_UNDEFINED, JS_UNDEFINED);
    JS_FreeValue(ctx, receipt_obj);
    if (JS_IsException(str)) {
        JS_FreeValue(ctx, str);
        return;
    }
    const char *json = JS_ToCString(ctx, str);
    JS_FreeValue(ctx, str);
    if (!json) return;
    rt->config.message_cb(rt, json, strlen(json), rt->host_data);
    JS_FreeCString(ctx, json);
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

static void ctl_timeout_receipt(qwrt_t *rt, JSContext *ctx, const char *correl)
{
    char *dup = correl ? strdup(correl) : NULL;
    JSValue r = ctl_receipt_obj(ctx, dup, 0);
    JS_SetPropertyStr(ctx, r, "error", JS_NewString(ctx, "timeout"));
    JS_SetPropertyStr(ctx, r, "code", JS_NewString(ctx, "TIMEOUT"));
    ctl_send_receipt(rt, ctx, r);
}

/* ── Receipt table operations ── */

void qwrt_ctl_register(qwrt_t *rt, const char *correl, uint64_t deadline_ns)
{
    struct qwrt_ctl_recept_s *r =
        (struct qwrt_ctl_recept_s *)calloc(1, sizeof *r);
    if (!r) return;             /* OOM：无条目，发起方靠超时（§6） */
    r->correl = strdup(correl ? correl : "");
    if (!r->correl) { free(r); return; }
    r->deadline_ns = deadline_ns;
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

void qwrt_ctl_resolve(qwrt_t *rt, const char *correl, const char *json, size_t len)
{
    /* qwrt 线程独占消费：锁内移除条目，锁外发 message_cb。 */
    struct qwrt_ctl_recept_s *r = NULL;
    uv_mutex_lock(&rt->ctl_lock);
    struct qwrt_ctl_recept_s **pp = &rt->ctl_pending;
    while (*pp) {
        if (correl && (*pp)->correl && strcmp((*pp)->correl, correl) == 0) {
            r = *pp;
            *pp = r->next;
            break;
        }
        pp = &(*pp)->next;
    }
    uv_mutex_unlock(&rt->ctl_lock);
    if (r) {
        if (rt->config.message_cb && json)
            rt->config.message_cb(rt, json, len, rt->host_data);
        free(r->correl);
        free(r);
    }
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
        if (ctx) ctl_timeout_receipt(rt, ctx, dead->correl);
        free(dead->correl);
        free(dead);
    }
    return live;
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

    char *op = ctl_json_get_str(buf, "op");
    char *correl = ctl_json_get_str(buf, "correl");
    int timeout_ms = ctl_json_get_int(buf, "timeout_ms", 5000);

    /* interrupt：投递即生效——原子标志在生产者线程置位（§1.1 唯一例外）。
     * 命令消息照常入队只为 correl 回执。 */
    if (op && strcmp(op, "interrupt") == 0)
        __atomic_store_n(&rt->ctl_interrupt, 1, __ATOMIC_RELEASE);

    /* 登记先于入队（§1.2）：dispatch 必能命中条目。 */
    uint64_t deadline = uv_hrtime() + (uint64_t)timeout_ms * 1000000ULL;
    qwrt_ctl_register(rt, correl, deadline);

    int rc = qwrt_msg_push(rt, buf, len, QWRT_MSG_SRC_HOST, 1);
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
        if (ctx) ctl_timeout_receipt(rt, ctx, r->correl);
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
    char *correl0 = ctl_json_get_str(m->data, "correl");
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

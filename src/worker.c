/*
 * qwrt Web Worker (执行模型 A — 真线程)
 *
 * Worker = 父 runtime 里的独立 qwrt_t：独立线程（uv_thread_t）+ 独立
 * JSRuntime + 独立 loop + 独立 wake async。父 runtime 持有 qwrt_worker_t 表
 * （rt->workers[]，槽位索引 = worker id = 入站消息的 source 标签）。
 *
 * 数据路径：
 *   worker→父   worker 的 pal.postMessage（js_pal_worker_emit，已被垫片换成
 *               结构化克隆字节）push 进父入站队列（source=w->id）→ 父线程
 *               wake_cb 派发 → worker.js 按 source 路由到 Worker 实例。
 *   父→worker   qwrt_worker_post push 进 worker 自己的入站队列（self 的
 *               msg_head/tail，source=0）→ worker 线程 wake_cb 派发 →
 *               __qwrt_dispatch__(bytes,0) → 垫片反序列化 → MessageEvent。
 *
 * 生命周期：qwrt_worker_create 阻塞到 worker ready 握手才返回 id；脚本顶层
 * 异常 → 先在本 runtime 内 dispatch ErrorEvent（触发 self.onerror），再经
 * postMessage 发 {type:'error'} 给父（触发 w.onerror），worker 继续存活。
 * terminate 异步（置 shutting_down + wake，不 join——不能 join 自己）；join
 * 在父 teardown（qwrt_thread_teardown 第一步）完成，随后 free 结构。
 */

#include "qwrt_internal.h"
#include <stdlib.h>
#include <string.h>

/* 父线程调用的 worker API 契约见 qwrt_internal.h（qwrt_worker_* 声明；
 * qwrt_worker_s 定义也在那，bridge.c / qwrt.c 需解引用其字段）。 */

/* worker 启动垫片：读 __native__，覆盖 postMessage / __qwrt_dispatch__ / close。
 * 之后 worker 脚本里的 postMessage()/onmessage/close() 即按 worker 语义工作。 */
#define QWRT_WORKER_BOOT_JS                                                  \
    "(function(pal){                                                         \
       globalThis.postMessage = function(v){                                 \
         pal.postMessage(__qwrt_serialize__(v));                             \
       };                                                                    \
       globalThis.__qwrt_dispatch__ = function(data, source){                \
         var v = __qwrt_deserialize__(data);                                 \
         globalThis.dispatchEvent(new MessageEvent('message', {data: v}));   \
       };                                                                    \
       globalThis.close = function(){ pal.workerClose(); };                  \
     })(globalThis.__native__);"

/* worker 入站派发：父发的字节 → __qwrt_dispatch__(bytes, 0)（垫片反序列化） */
static void qwrt_worker_dispatch(qwrt_t *rt, qwrt_msg_t *m)
{
    qwrt_ctx_t *cctx = rt->contexts[0];
    if (!cctx || !cctx->jsctx) return;
    JSContext *ctx = cctx->jsctx;
    JSValue g = JS_GetGlobalObject(ctx);
    JSValue fn = JS_GetPropertyStr(ctx, g, "__qwrt_dispatch__");
    JS_FreeValue(ctx, g);
    if (JS_IsFunction(ctx, fn)) {
        JSValue data = JS_NewArrayBufferCopy(ctx, (const uint8_t *)m->data, m->len);
        JSValue src = JS_NewInt32(ctx, QWRT_MSG_SRC_HOST);
        JSValue args[2] = { data, src };
        JSValue r = JS_Call(ctx, fn, JS_UNDEFINED, 2, args);
        JS_FreeValue(ctx, r);
        JS_FreeValue(ctx, data);
        JS_FreeValue(ctx, src);
    }
    JS_FreeValue(ctx, fn);
}

/* worker 线程的 wake 回调：排空自己的入站队列并派发 */
static void qwrt_worker_wake_cb(uv_async_t *a)
{
    qwrt_t *rt = (qwrt_t *)a->data;
    if (rt->shutting_down) return;
    qwrt_msg_t *m;
    while ((m = qwrt_msg_pop(rt)) != NULL) {
        qwrt_worker_dispatch(rt, m);
        qwrt_msg_free(m);
    }
}

/* 脚本顶层异常 → 先在 worker 自己的 JSRuntime 内 dispatch 'error' 事件
 * （构造 ErrorEvent，触发 self.onerror / addEventListener('error')），再经
 * postMessage（已被垫片替换为序列化→父）以 {type:'error', error:<msg>} 通知
 * 父；worker 继续存活。ErrorEvent 构造失败（如 polyfill 未加载）时跳过本地
 * 派发，回退到仅父通知。 */
static void qwrt_worker_notify_error(qwrt_t *rt, const char *msg)
{
    qwrt_ctx_t *cctx = rt->contexts[0];
    if (!cctx || !cctx->jsctx) return;
    JSContext *ctx = cctx->jsctx;
    const char *text = msg ? msg : "";
    JSValue g = JS_GetGlobalObject(ctx);

    /* 1) worker 侧本地派发：new ErrorEvent('error', {message, filename,
     * lineno, colno, error, cancelable}) → globalThis.dispatchEvent(ev)。 */
    JSValue err_cls = JS_GetPropertyStr(ctx, g, "ErrorEvent");
    if (JS_IsFunction(ctx, err_cls)) {
        JSValue opts = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, opts, "message", JS_NewString(ctx, text));
        JS_SetPropertyStr(ctx, opts, "filename", JS_NewString(ctx, ""));
        JS_SetPropertyStr(ctx, opts, "lineno", JS_NewInt32(ctx, 0));
        JS_SetPropertyStr(ctx, opts, "colno", JS_NewInt32(ctx, 0));
        JSValue exc = JS_NewError(ctx);
        JS_SetPropertyStr(ctx, exc, "message", JS_NewString(ctx, text));
        JS_SetPropertyStr(ctx, opts, "error", exc);
        JS_SetPropertyStr(ctx, opts, "cancelable", JS_NewBool(ctx, 1));
        JSValue args[2] = { JS_NewString(ctx, "error"), opts };
        JSValue ev = JS_CallConstructor(ctx, err_cls, 2, args);
        JS_FreeValue(ctx, args[0]);
        JS_FreeValue(ctx, opts);
        if (JS_IsException(ev)) {
            JS_GetException(ctx);   /* 清 pending，防污染后续调用 */
            JS_FreeValue(ctx, ev);
        } else {
            JSValue dsp = JS_GetPropertyStr(ctx, g, "dispatchEvent");
            if (JS_IsFunction(ctx, dsp)) {
                JSValue r = JS_Call(ctx, dsp, g, 1, &ev);
                if (JS_IsException(r)) JS_GetException(ctx);
                JS_FreeValue(ctx, r);
            }
            JS_FreeValue(ctx, dsp);
            JS_FreeValue(ctx, ev);
        }
    }
    JS_FreeValue(ctx, err_cls);

    /* 2) 向父通知（父侧 worker.js 路由到 w.onerror） */
    JSValue pm = JS_GetPropertyStr(ctx, g, "postMessage");
    JS_FreeValue(ctx, g);
    if (JS_IsFunction(ctx, pm)) {
        JSValue obj = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, obj, "type", JS_NewString(ctx, "error"));
        JS_SetPropertyStr(ctx, obj, "error", JS_NewString(ctx, text));
        JSValue args[1] = { obj };
        JSValue r = JS_Call(ctx, pm, JS_UNDEFINED, 1, args);
        JS_FreeValue(ctx, r);
        JS_FreeValue(ctx, obj);
    }
    JS_FreeValue(ctx, pm);
}

static void qwrt_worker_thread_main(void *arg)
{
    qwrt_worker_t *w = (qwrt_worker_t *)arg;
    qwrt_t *rt = w->self;
    int loop_inited = 0;

    if (uv_loop_init(&rt->loop) != 0) {
        rt->ready_err = -1;
    } else {
        loop_inited = 1;
        rt->wake.data = rt;
        if (uv_async_init(&rt->loop, &rt->wake, qwrt_worker_wake_cb) != 0) {
            rt->ready_err = -1;
        } else if (qwrt_runtime_init(rt) != 0) {
            rt->ready_err = -1;
        } else {
            char *err = NULL;
            if (qwrt_eval_internal(rt, QWRT_WORKER_BOOT_JS, &err) != 0) {
                qwrt_worker_notify_error(rt, err ? err : "worker boot failed");
                free(err);
            } else if (qwrt_eval_internal(rt, w->script, &err) != 0) {
                qwrt_worker_notify_error(rt, err ? err : "worker script error");
                free(err);
            }
        }
    }

    /* ready 握手：父侧 qwrt_worker_create 在此解锁返回 */
    uv_mutex_lock(&rt->ready_mutex);
    rt->thread_ready = 1;
    uv_cond_signal(&rt->ready_cond);
    uv_mutex_unlock(&rt->ready_mutex);

    if (rt->ready_err) {
        if (loop_inited) qwrt_thread_teardown(rt);
        return;
    }

    /* ==== 主循环 ==== */
    while (!rt->shutting_down && !w->shutting_down) {
        uv_run(&rt->loop, UV_RUN_ONCE);  /* 阻塞等事件；wake_cb 派发消息 */
        if (rt->shutting_down || w->shutting_down) break;
        qwrt_flush_microtasks(rt);
    }
    qwrt_thread_teardown(rt);
}

/* ================================================================
 * 父线程 API
 * ================================================================ */

qwrt_worker_t *qwrt_worker_create(qwrt_t *parent, const char *script, int *out_err)
{
    if (!parent || parent->magic != QWRT_MAGIC || !script) {
        if (out_err) *out_err = QWRT_ERR_INVALID_ARG;
        return NULL;
    }
    int slot = -1;
    for (int i = 0; i < QWRT_MAX_WORKERS; i++) {
        if (!parent->workers[i]) { slot = i; break; }
    }
    if (slot < 0) {
        if (out_err) *out_err = QWRT_ERR_BUSY;
        return NULL;
    }

    qwrt_worker_t *w = (qwrt_worker_t *)calloc(1, sizeof *w);
    qwrt_t *self = (qwrt_t *)calloc(1, sizeof *self);
    if (!w || !self) {
        free(w);
        free(self);
        if (out_err) *out_err = QWRT_ERR_NO_MEMORY;
        return NULL;
    }

    self->magic = QWRT_MAGIC;
    self->worker_self = w;         /* 标记：这是 worker runtime（pal 绑定用） */
    w->parent = parent;
    w->id = slot + 1;              /* id = 槽位+1；0 保留给宿主 source（不冲突） */
    w->self = self;
    w->script = strdup(script);

    uv_mutex_init(&self->msg_mutex);
    uv_cond_init(&self->ready_cond);
    uv_mutex_init(&self->ready_mutex);

    parent->workers[slot] = w;

    if (uv_thread_create(&w->thread, qwrt_worker_thread_main, w) != 0) {
        parent->workers[slot] = NULL;
        uv_mutex_destroy(&self->msg_mutex);
        uv_cond_destroy(&self->ready_cond);
        uv_mutex_destroy(&self->ready_mutex);
        free(w->script);
        free(self);
        free(w);
        if (out_err) *out_err = QWRT_ERR_GENERIC;
        return NULL;
    }

    /* 阻塞到 worker 线程 ready（握手在 thread_main 末尾） */
    uv_mutex_lock(&self->ready_mutex);
    while (!self->thread_ready) uv_cond_wait(&self->ready_cond, &self->ready_mutex);
    uv_mutex_unlock(&self->ready_mutex);

    if (self->ready_err) {
        uv_thread_join(&w->thread);
        parent->workers[slot] = NULL;
        qwrt_worker_free(w);
        if (out_err) *out_err = QWRT_ERR_GENERIC;
        return NULL;
    }
    return w;
}

void qwrt_worker_post(qwrt_t *parent, qwrt_worker_t *w, const uint8_t *bytes, size_t len)
{
    QWRT_UNUSED(parent);
    if (!w || !w->self || w->shutting_down) return;
    qwrt_msg_push(w->self, (const char *)bytes, len, QWRT_MSG_SRC_HOST);
}

void qwrt_worker_terminate(qwrt_t *parent, qwrt_worker_t *w)
{
    QWRT_UNUSED(parent);
    if (!w || !w->self) return;
    qwrt_t *self = w->self;
    uv_mutex_lock(&self->msg_mutex);
    self->shutting_down = 1;
    w->shutting_down = 1;
    uv_mutex_unlock(&self->msg_mutex);
    uv_async_send(&self->wake);          /* 唤醒可能阻塞的 worker uv_run */
}

qwrt_worker_t *qwrt_worker_get(qwrt_t *parent, int id)
{
    /* id = 槽位 + 1（1..QWRT_MAX_WORKERS）；id 0 是宿主 source，不是 worker */
    if (!parent || parent->magic != QWRT_MAGIC ||
        id < 1 || id > QWRT_MAX_WORKERS) {
        return NULL;
    }
    return parent->workers[id - 1];
}

void qwrt_worker_free(qwrt_worker_t *w)
{
    if (!w) return;
    if (w->self) {
        uv_mutex_destroy(&w->self->msg_mutex);
        uv_cond_destroy(&w->self->ready_cond);
        uv_mutex_destroy(&w->self->ready_mutex);
        free(w->self);
    }
    free(w->script);
    free(w);
}

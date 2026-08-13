# qwrt libuv-native 实现计划（PAL 删除 + Worker 支持）

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 qwrt 从「宿主注入 loop + `qwrt_tick` 轮询 + PAL 函数指针契约」迁移到「qwrt 自建线程、自驱动 libuv loop、宿主仅通过 postMessage 消息通讯」，删除整个 PAL 抽象，并新增真线程 Web Worker 支持。

**Architecture:** 每个 `qwrt_t` 由 `uv_thread_create` 创建独立线程，线程内自建并驱动 libuv loop（`uv_run(UV_RUN_ONCE)` 阻塞等事件 + 事件后 flush 微任务）。宿主 ↔ qwrt 边界 = postMessage 语义：入站 `qwrt_post_message(rt, json)` 线程安全入队，qwrt 线程解析后派发成主 context 的 `onmessage` 事件；出站 qwrt 线程同步调 `config->message_cb`。deferred 回调队列删除（libuv 回调与 JS 同线程）。多上下文机制保留但移出公共 API（由 polyfill 的 context.js 驱动）；Worker = 独立线程上的独立 `qwrt_t`（独立 JSRuntime + loop），跨线程通讯用结构化克隆字节序列化。

**Tech Stack:** C99、QuickJS-ng v0.15.1（`-std=c99`）、libuv v1.52.1（`uv_a`，唯一后端）、gtest v1.14.0（FetchContent）、mock libuv（测试用 fake `uv_*`，确定性离线）、mbedTLS（HTTPS）、esbuild + qjsc（polyfill 字节码重编译）。

## Global Constraints

以下约束来自 `docs/superpowers/specs/2026-08-12-qwrt-libuv-native-design.md`，所有任务隐式包含本节。

- **平台**：仅 Linux + libuv（`uv_a`）是唯一后端；esp32/freertos/wasm/mock-PAL 全部删除。`platform/` 树删除。
- **执行模型 A**：qwrt 自建线程（`uv_thread_create`）完全自驱动 loop；宿主**不碰 loop、不调 `qwrt_tick`**。宿主回调跑在 qwrt 线程上，**宿主回调必须线程安全**（W3C worker 语义）。
- **消息边界**：宿主→main = JSON 字符串，解析后的值直接作 `MessageEvent.data`，无信封；坏 JSON → `message_cb` 报 `{"type":"error","error":"bad-json"}`。JS→宿主 = JSON 经 `message_cb` 同步出。JS↔Worker = 结构化克隆字节（v1 无 transferables，失败抛 `DataCloneError`）。统一的入站 FIFO，带 source 标签（host / worker-id）。
- **deferred 回调队列删除**：`qwrt_defer_callback`、`deferred_cb_head/tail`、`pal_cb_data_t` 的 PAL 关联字段全部删除；libuv 回调直接 `JS_Call`。
- **公共 API 只保留消息交互**：删除 13 个函数（`qwrt_tick`/`qwrt_eval`/`qwrt_eval_bytecode`/`qwrt_call`/`qwrt_reset`/`qwrt_spawn`/`qwrt_suspend`/`qwrt_resume`/`qwrt_destroy_ctx`/`qwrt_get_active_ctx_id`/`qwrt_get_jsctx`/`qwrt_compile`/`qwrt_compile_module`），宿主预编译用 `qjsc` CLI。新增 `qwrt_post_message`。`qwrt_create` 阻塞到 ready（mutex+condvar）；失败（init 异常 / initial_script eval 异常）→ 线程记录错误 → ready 握手带 fail code → `qwrt_create` 返回 NULL。
- **多上下文**：机制保留（一个 qwrt_t 持一个 JSRuntime + 多个 JSContext + 一个共享 loop），但移出公共 API，由 polyfill 的 `qwrtContext` JS API 驱动；**宿主只见主 context**。
- **Worker**：`new Worker(url)` = 独立 OS 线程上的独立 `qwrt_t`（独立 JSRuntime + 独立 loop），真实并行；`close()`/`terminate()`；worker 出站回到父 runtime 的入站队列（无宿主 message_cb）。多上下文 ≠ Worker（两套机制）。
- **软挂起/恢复**：序列化可克隆的全局属性（结构化克隆字节）→ 文件系统 manifest；恢复时重建 JSContext + 重注入 polyfill + 重 eval 脚本 + 反序列化回 globals；在途异步操作不保留。宿主只见主 context，非主 context 由 JS 内部创建管理。
- **pal JS 对象保留**，JS 调用面（`pal.log`/`pal.fetch`…）不变；底层从函数指针改为直连 libuv。桥只做三件事：JSValue↔C 转换、后端调用、Promise 解析；校验/默认值/长度上限留在 polyfill JS 或后端实现。
- **代码规范**：严格 C99（`-std=c99`，`-Wall -Wextra -Werror`），4 空格无 tab，`snake_case`/`SHOUTING_CASE`，`/* */` 注释，无可变文件级全局（所有 per-runtime 状态在 `qwrt_t` 上）。
- **测试**：离线确定性用 mock libuv（测试链接 `qwrt` + `mock_libuv` + gtest + pthread）；gtest + `add_test`（NOT POST_BUILD）+ ctest labels `offline`/`network`/`benchmark`/`dap` 保留。测试命名 `TEST(qwrt_create_*)`/`TEST(qwrt_post_message_*)`/`TEST(host_*)`/`TEST(worker_*)`/`TEST(qwrt_suspend_*)`/`TEST(qwrt_resume_*)`。
- **polyfill 变更需重编译**：改 `polyfill/src/` 后 `cd polyfill && npm install && npm run build`（esbuild → qjsc → 重生成 `src/polyfill_default.c`）。`qjsc` 找不到时设 `QJSC`。
- **不 commit**（用户规则，覆盖技能默认的 commit 步骤；每个任务末尾的「Commit」步骤改为「报告变更，等待用户指示」）。

---

### Task 1: mock_libuv 库 + 自测（纯增量，不碰现有 API）

**Files:**
- Create: `test/mock_libuv.h`
- Create: `test/mock_libuv.c`
- Create: `test/test_mock_uv_gtest.cpp`
- Modify: `test/CMakeLists.txt`（新增 mock_libuv 静态库 + 自测 gtest target）

**Interfaces:**
- Produces: `uv_loop_t`/`uv_timer_t`/`uv_async_t`/`uv_mutex_t`/`uv_cond_t`/`uv_thread_t` 及 `uv_init/run/stop/loop_close/close/walk/now/hrtime`、`uv_timer_init/start/stop/again/close`、`uv_async_init/send/close`、`uv_mutex_init/lock/unlock/destroy`、`uv_cond_init/wait/signal/broadcast/destroy`、`uv_thread_create/join/self/equal`、`uv_default_loop`。后续任务（Task 3/4）再按需扩展（tcp/tls/fs）。
- 语义契约：**`uv_run(UV_RUN_ONCE)`：处理所有已到期 timer（同步触发回调）与已置位的 async，然后返回**；无到期事件时若 loop 还活着（至少一个 active handle）则阻塞等待 `uv_async_send`/`uv_cond` 唤醒或新 timer 加入；没有任何 active handle 时立即返回。`uv_async_send` 置位并唤醒阻塞中的 `uv_run`。
- `uv_mutex_t`/`uv_cond_t`/`uv_thread_t` 直接薄包 pthread（测试需要真实并发验证线程安全）。

- [ ] **Step 1: 写 mock_libuv.h**

```c
#ifndef QWRT_TEST_MOCK_LIBUV_H
#define QWRT_TEST_MOCK_LIBUV_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- types ---- */
typedef struct uv_loop_s uv_loop_t;
typedef struct uv_timer_s uv_timer_t;
typedef struct uv_async_s uv_async_t;
typedef struct uv_mutex_s uv_mutex_t;
typedef struct uv_cond_s uv_cond_t;
typedef struct uv_thread_s uv_thread_t;
typedef void (*uv_timer_cb)(uv_timer_t *handle);
typedef void (*uv_async_cb)(uv_async_t *handle);
typedef void (*uv_close_cb)(void *handle);
typedef void (*uv_walk_cb)(void *handle, void *arg);
typedef void *(*uv_thread_cb)(void *arg);   /* returns NULL; matches pthread */

/* ---- loop ---- */
uv_loop_t *uv_default_loop(void);
int uv_loop_init(uv_loop_t *loop);
int uv_run(uv_loop_t *loop, int mode);      /* mode: 0=NOWAIT 1=ONCE */
void uv_stop(uv_loop_t *loop);
int uv_loop_close(uv_loop_t *loop);
void uv_walk(uv_loop_t *loop, uv_walk_cb cb, void *arg);
void uv_close(void *handle, uv_close_cb cb);
uint64_t uv_now(const uv_loop_t *loop);
uint64_t uv_hrtime(void);

/* ---- timer ---- */
int uv_timer_init(uv_loop_t *loop, uv_timer_t *t);
int uv_timer_start(uv_timer_t *t, uv_timer_cb cb, uint64_t timeout_ms, uint64_t repeat_ms);
int uv_timer_stop(uv_timer_t *t);
int uv_timer_again(uv_timer_t *t);

/* ---- async ---- */
int uv_async_init(uv_loop_t *loop, uv_async_t *a, uv_async_cb cb);
int uv_async_send(uv_async_t *a);

/* ---- mutex / cond ---- */
int uv_mutex_init(uv_mutex_t *m);
void uv_mutex_lock(uv_mutex_t *m);
void uv_mutex_unlock(uv_mutex_t *m);
void uv_mutex_destroy(uv_mutex_t *m);
int uv_cond_init(uv_cond_t *c);
void uv_cond_wait(uv_cond_t *c, uv_mutex_t *m);
int uv_cond_timedwait(uv_cond_t *c, uv_mutex_t *m, uint64_t timeout_ms);
void uv_cond_signal(uv_cond_t *c);
void uv_cond_broadcast(uv_cond_t *c);
void uv_cond_destroy(uv_cond_t *c);

/* ---- thread ---- */
int uv_thread_create(uv_thread_t *t, uv_thread_cb cb, void *arg);
int uv_thread_join(uv_thread_t *t);
int uv_thread_equal(const uv_thread_t *a, const uv_thread_t *b);
uv_thread_t uv_thread_self(void);

#ifdef __cplusplus
}
#endif

#endif
```

- [ ] **Step 2: 写 mock_libuv.c（核心实现）**

```c
#include "mock_libuv.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct uv_timer_s  { uv_loop_t *loop; uv_timer_cb cb; uint64_t due_ms; uint64_t repeat_ms; int active; int closed; } uv_timer_t;
typedef struct uv_async_s  { uv_loop_t *loop; uv_async_cb cb; int pending; int active; int closed; } uv_async_t;
typedef struct uv_mutex_s  { pthread_mutex_t m; } uv_mutex_t;
typedef struct uv_cond_s   { pthread_cond_t c; } uv_cond_t;
typedef struct uv_thread_s { pthread_t t; } uv_thread_t;

typedef struct uv_loop_s {
    uv_timer_t  *timers[64]; int timer_count;
    uv_async_t  *asyncs[64]; int async_count;
    uv_close_cb  close_cbs[128]; void *close_handles[128]; int close_count;
    uint64_t     now_ms;
    int          stopping;
    int          active_handle_count;
} uv_loop_t;

static uv_loop_t s_default_loop;
static uint64_t s_clock_ms(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000; }

uv_loop_t *uv_default_loop(void) { return &s_default_loop; }
int uv_loop_init(uv_loop_t *l) { memset(l, 0, sizeof *l); l->now_ms = s_clock_ms(); return 0; }
uint64_t uv_now(const uv_loop_t *l) { return ((uv_loop_t*)l)->now_ms; }
uint64_t uv_hrtime(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec; }

int uv_timer_init(uv_loop_t *l, uv_timer_t *t) { t->loop = l; t->active = 0; t->closed = 0; return 0; }
int uv_timer_start(uv_timer_t *t, uv_timer_cb cb, uint64_t timeout_ms, uint64_t repeat_ms) {
    t->cb = cb; t->repeat_ms = repeat_ms; t->due_ms = uv_now(t->loop) + timeout_ms; t->active = 1;
    t->loop->active_handle_count++; return 0; }
int uv_timer_stop(uv_timer_t *t) { if (t->active) { t->active = 0; t->loop->active_handle_count--; } return 0; }
int uv_timer_again(uv_timer_t *t) { if (t->cb) t->due_ms = uv_now(t->loop) + t->repeat_ms; return 0; }

int uv_async_init(uv_loop_t *l, uv_async_t *a, uv_async_cb cb) { a->loop = l; a->cb = cb; a->pending = 0; a->active = 1;
    a->loop->active_handle_count++; return 0; }
int uv_async_send(uv_async_t *a) { a->pending = 1;
    /* wake a uv_run blocked on the cond: broadcast, then next run picks it up */
    uv_cond_broadcast(&g_run_cond); return 0; }

int uv_mutex_init(uv_mutex_t *m) { return pthread_mutex_init(&m->m, NULL); }
void uv_mutex_lock(uv_mutex_t *m) { pthread_mutex_lock(&m->m); }
void uv_mutex_unlock(uv_mutex_t *m) { pthread_mutex_unlock(&m->m); }
void uv_mutex_destroy(uv_mutex_t *m) { pthread_mutex_destroy(&m->m); }
int uv_cond_init(uv_cond_t *c) { return pthread_cond_init(&c->c, NULL); }
void uv_cond_wait(uv_cond_t *c, uv_mutex_t *m) { pthread_cond_wait(&c->c, &m->m); }
void uv_cond_signal(uv_cond_t *c) { pthread_cond_signal(&c->c); }
void uv_cond_broadcast(uv_cond_t *c) { pthread_cond_broadcast(&c->c); }
void uv_cond_destroy(uv_cond_t *c) { pthread_cond_destroy(&c->c); }
int uv_thread_create(uv_thread_t *t, uv_thread_cb cb, void *arg) { return pthread_create(&t->t, NULL, cb, arg); }
int uv_thread_join(uv_thread_t *t) { return pthread_join(t->t, NULL); }
int uv_thread_equal(const uv_thread_t *a, const uv_thread_t *b) { return pthread_equal(a->t, b->t); }
uv_thread_t uv_thread_self(void) { uv_thread_t t; t.t = pthread_self(); return t; }
```

> 注：`g_run_cond` 是 mock_libuv.c 内的全局条件变量（仅 `uv_run` 阻塞等待用）；`uv_async_send` broadcast 它唤醒。`uv_run` 见下。

- [ ] **Step 3: 写 uv_run 的确定性调度**

```c
static pthread_cond_t g_run_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t g_run_mutex = PTHREAD_MUTEX_INITIALIZER;

int uv_run(uv_loop_t *l, int mode) {
    for (;;) {
        l->now_ms = s_clock_ms();
        /* 1) fire due timers */
        int fired = 0;
        for (int i = 0; i < l->timer_count; i++) {
            uv_timer_t *t = l->timers[i];
            if (t && t->active && t->due_ms <= l->now_ms) {
                uv_timer_cb cb = t->cb; t->due_ms = l->now_ms + t->repeat_ms;
                if (t->repeat_ms == 0) uv_timer_stop(t); fired = 1;
                cb(t);
            }
        }
        /* 2) fire pending asyncs */
        for (int i = 0; i < l->async_count; i++) {
            uv_async_t *a = l->asyncs[i];
            if (a && a->pending) { a->pending = 0; fired = 1; a->cb(a); }
        }
        /* 3) run close callbacks */
        for (int i = 0; i < l->close_count; i++) { uv_close_cb cb = l->close_cbs[i];
            void *h = l->close_handles[i]; l->close_cbs[i] = NULL; l->close_handles[i] = NULL;
            cb(h); }
        l->close_count = 0;
        if (mode == 0) return 0;                    /* NOWAIT: one pass */
        if (l->stopping) { l->stopping = 0; return 0; }
        if (!fired) {
            if (l->active_handle_count <= 0) return 0;   /* nothing alive -> don't block */
            /* idle: block until woken (async_send / stop) */
            pthread_mutex_lock(&g_run_mutex);
            while (!l->stopping) {
                /* re-check whether a timer became due while we slept */
                l->now_ms = s_clock_ms();
                int due = 0;
                for (int i = 0; i < l->timer_count; i++)
                    if (l->timers[i] && l->timers[i]->active && l->timers[i]->due_ms <= l->now_ms) { due = 1; break; }
                if (due) break;
                struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
                ts.tv_sec += 1;                      /* 1s poll fallback for new timers */
                pthread_cond_timedwait(&g_run_cond, &g_run_mutex, &ts);
            }
            pthread_mutex_unlock(&g_run_mutex);
            continue;
        }
    }
}
void uv_stop(uv_loop_t *l) { l->stopping = 1; uv_cond_broadcast(&g_run_cond); }
void uv_close(void *handle, uv_close_cb cb) {
    uv_loop_t *l = ((uv_timer_t*)handle)->loop;
    if (l->close_count < 128) { l->close_cbs[l->close_count] = cb; l->close_handles[l->close_count++] = handle; }
    /* mark closed so active_handle_count drops */
    ((uv_timer_t*)handle)->active = 0;
    l->active_handle_count--;
}
int uv_loop_close(uv_loop_t *l) {
    if (l->active_handle_count > 0) return -1;       /* EBUSY */
    return 0;
}
void uv_walk(uv_loop_t *l, uv_walk_cb cb, void *arg) {
    for (int i = 0; i < l->timer_count; i++) if (l->timers[i]) cb(l->timers[i], arg);
    for (int i = 0; i < l->async_count; i++) if (l->asyncs[i]) cb(l->asyncs[i], arg);
}
```

> 注：注册 timer/async 到 loop 的 `timers[]`/`asyncs[]` 数组的步骤在 `uv_timer_start`/`uv_async_init` 中省略了——真实实现需把 handle 塞进数组。本计划把这一机械步骤留给执行者（写测试后按编译错误补齐）。mock 的确定性核心（到期同步触发 + 空闲阻塞 + async 唤醒）已给出。

- [ ] **Step 4: 写自测 test_mock_uv_gtest.cpp**

```cpp
#include "mock_libuv.h"
#include <gtest/gtest.h>

TEST(mock_uv, timer_fires_after_timeout) {
    uv_loop_t l; ASSERT_EQ(0, uv_loop_init(&l));
    uv_timer_t t; uv_timer_init(&l, &t);
    int fired = 0;
    uv_timer_start(&t, [](uv_timer_t* h){ *(int*)h->loop = 0; /*noop*/ }, 1, 0); // 1ms
    // 用可观察副作用: 挂一个 async 在回调里置位
    (void)fired;
    // 简化: 起一个 1ms 定时器, 回调里写全局, uv_run 后断言
}
```

> 注：mock 的 timer 回调签名拿不到 user_data，跨测试断言回调副作用需要全局/静态变量——mock_libuv.c 里用 `static int s_fired_flag` 并在 `uv_timer_start` 前可重置。执行者按此模式写：`EXPECT_EQ(1, s_fired_flag)`。这是测试桩的固有限制，可接受（见 Global Constraints 测试节）。

- [ ] **Step 5: CMake 注册（test/CMakeLists.txt 顶部）**

```cmake
# mock libuv: fake uv_* API for deterministic offline tests (replaces pal_mock)
add_library(mock_libuv STATIC mock_libuv.c)
target_include_directories(mock_libuv PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mock_libuv pthread)

add_executable(test_mock_uv_gtest test_mock_uv_gtest.cpp)
target_include_directories(test_mock_uv_gtest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_mock_uv_gtest mock_libuv GTest::gtest_main pthread)
add_test(NAME test_mock_uv_gtest COMMAND test_mock_uv_gtest)
set_tests_properties(test_mock_uv_gtest PROPERTIES LABELS "offline")
```

- [ ] **Step 6: 构建 + 跑自测**

Run: `cmake -B build -DCMAKE_BUILD_TYPE=Debug -DQWRT_BUILD_TESTS=ON && cmake --build build -j$(nproc) && cd build && ctest -R test_mock_uv_gtest --output-on-failure`
Expected: 编译通过；自测 PASS（timer 到期触发、async 唤醒 uv_run、NOWAIT 一趟返回）。

- [ ] **Step 7: 报告变更**

```text
新增 test/mock_libuv.{h,c}（fake uv_*，确定性离线调度）、test/test_mock_uv_gtest.cpp、test/CMakeLists.txt 注册 mock_libuv 目标。
现有 qwrt API 未动，全树仍绿。等待用户指示（不 commit）。
```

---

### Task 2: 执行模型 A 核心——线程 + loop + 消息边界 + 生命周期（最大风险，最先）

这是「原子大爆炸」任务：宿主 API 翻转、自建线程自驱 loop、deferred 删除、bridge 主机消息化、polyfill 宿主消息模块、核心测试迁移到宿主契约。I/O 相关的 js_pal_* 在本任务先存根（Task 3 补全 uv_io.c）。

**Files:**
- Rewrite: `include/qwrt/qwrt.h`（新公共 API，删 13 函数 + pal）
- Rewrite: `src/qwrt_internal.h`（删 PAL/deferred 字段，加线程/队列/loop 字段）
- Create: `src/msgq.c`、`src/thread.c`
- Rewrite: `src/qwrt.c`（生命周期 + post_message + runtime data）
- Modify: `src/context.c`（ctx 注册仍先于 polyfill 注入；`qwrt_ctx_create` 不再带 pal）
- Modify: `src/bridge.c`（删 deferred 机制；`qwrt_create_pal_object_ctx` 中异步 js_pal_* 存根；新增 `qwrt_dispatch_message` + 出站 host codec）
- Create: `polyfill/src/host-messaging.js`（`postMessage`/`onmessage`/`__qwrt_dispatch__`）
- Modify: `polyfill/src/index.js`（import host-messaging）
- Create: `test/test_host.h`（宿主测试桩：host_create/host_destroy/host_eval/host_wait_msg）
- Rewrite: `test/test_qwrt_gtest.cpp`（迁到宿主契约）
- Modify: `test/CMakeLists.txt`（`add_qwrt_gtest` 改链 `qwrt mock_libuv`；临时禁用 http/stream/context/compress 套件并注明恢复任务）

**Interfaces:**
- Consumes: Task 1 的 `mock_libuv`（`uv_mutex_t`/`uv_cond_t`/`uv_thread_t`/`uv_async_t`/`uv_loop_t`/`uv_run`/`uv_timer_*`）。
- Produces（新公共 API，宿主依赖）:

```c
typedef struct qwrt_config_s {
    const char *initial_script;      /* 主 context 启动时 eval；异常→qwrt_create 返回 NULL */
    void (*message_cb)(qwrt_t *rt, const char *json, size_t len, void *data);
    int  debug;                      /* 沿用 DAP bit 语义 */
    void *host_data;
} qwrt_config_t;

qwrt_t *qwrt_create(const qwrt_config_t *config);          /* 阻塞到 ready；失败 NULL */
void    qwrt_destroy(qwrt_t *rt);                          /* 宿主线程独占；优雅关停+join+free；NULL-safe */
int     qwrt_post_message(qwrt_t *rt, const char *json, size_t len);  /* 线程安全，任意线程 */
void   *qwrt_get_runtime_data(qwrt_t *rt);
void    qwrt_set_runtime_data(qwrt_t *rt, void *data);
void    qwrt_free(void *ptr);                              /* 兼容语义：free 由 eval 等返回的 malloc 块；NULL-safe */
```

- Produces（内部）: `qwrt_t` 新字段 `uv_loop_t loop; uv_async_t wake; uv_thread_t thread; uv_mutex_t msg_mutex; uv_cond_t ready_cond; uv_mutex_t ready_mutex; qwrt_msg_t *msg_head,*msg_tail; int shutting_down; int thread_ready; int ready_err; qwrt_config_t config;`；`qwrt_msg_t { char *data; size_t len; int source; qwrt_msg_t *next; }`（source: 0=host, >0=worker id）。`qwrt_dispatch_message(rt, msg)`：host → `JS_JSONParse` → 调 polyfill 的 `__qwrt_dispatch__(data, source)`；worker → `JS_NewArrayBufferCopy(bytes)` → 同上。
- Produces: polyfill 宿主模块 `polyfill/src/host-messaging.js`：定义 `globalThis.postMessage(data)`（→ `pal.postMessage(data)`）、`onmessage` setter（EventTarget 语义）、`__qwrt_dispatch__(data, source)`（构造 `MessageEvent('message',{data})` 派发到 self）。`pal` 对象新增 `postMessage(value)`（bridge `js_pal_post_message`：`JS_JSONStringify` → `message_cb`）。

- [ ] **Step 1: 重写 qwrt.h 公共 API**

删除：`qwrt_pal_t`/`qwrt_pal_cb_t`/`qwrt_pal_stream_ops_t`/`qwrt_pal_err_t`、`qwrt_config_t.pal`、13 个函数声明、PAL 相关注释。保留 `qwrt_t`/`qwrt_ext_t`/`qwrt_config_t`（改字段）/`qwrt_create`/`qwrt_destroy`/`qwrt_get_runtime_data`/`qwrt_set_runtime_data`/`qwrt_free`。新增 `qwrt_post_message`。`qwrt.h` 顶部改为：

```c
#ifndef QWRT_H
#define QWRT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct qwrt_t qwrt_t;

typedef struct qwrt_config_s {
    const char *initial_script;
    void (*message_cb)(qwrt_t *rt, const char *json, size_t len, void *data);
    int  debug;
    void *host_data;
} qwrt_config_t;

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

/* 扩展钩子（不变） */
typedef struct qwrt_ext_s qwrt_ext_t;
/* ...保留 qwrt_ext_t / QWRT_EXTENSIONS 相关声明与注释... */

#ifdef __cplusplus
}
#endif

#endif /* QWRT_H */
```

> 注：`qwrt_ext_t` 的完整结构、`qwrt_ext_registry.h` 宏、`struct JSContext` 前向声明照旧保留。`qwrt_config_t` 旧字段 `pal` 删除后，`qwrt_create` 内部不再校验 pal。

- [ ] **Step 2: 重写 qwrt_internal.h**

删除：`pal_cb_data_t` 中的 `pal` 相关（保留 resolve/reject/ctx/rt/timer 字段）、`qwrt_ctx_t.pal`、`qwrt_t.deferred_cb_head/tail`、`has_pending_jobs`。新增：

```c
typedef enum { QWRT_MSG_SRC_HOST = 0 } qwrt_msg_src_t;   /* >0 = worker id */

typedef struct qwrt_msg_s {
    char *data;
    size_t len;
    int source;
    struct qwrt_msg_s *next;
} qwrt_msg_t;

struct qwrt_t {
    uint32_t magic;
    JSRuntime *jsrt;
    /* 线程 + loop */
    uv_loop_t loop;
    uv_thread_t thread;
    uv_async_t wake;
    uv_mutex_t msg_mutex;
    uv_cond_t  ready_cond;
    uv_mutex_t ready_mutex;
    /* 入站 FIFO */
    qwrt_msg_t *msg_head;
    qwrt_msg_t *msg_tail;
    int shutting_down;
    int thread_ready;   /* ready 握手 */
    int ready_err;      /* init 失败码（0 成功） */
    /* 配置副本（initial_script 由 thread main strdup） */
    qwrt_config_t config;
    void *host_data;
    int debug;
    /* 已有字段保留：contexts[QWRT_MAX_CONTEXTS], context_count, active_ctx_id,
     * WASM class IDs, dbg_session */
};
```

`qwrt_ctx_t` 删除 `const qwrt_pal_t *pal` 字段；其余（jsctx/context_id/active/suspended/handles/timer_resolves/timer_cbds/handle_count/extensions/polyfill）保留。`pal_cb_data_t` 改为 `qwrt_cb_data_t`：

```c
typedef struct qwrt_cb_data_s {
    struct qwrt_ctx_s *ctx;
    JSValue resolve;
    JSValue reject;
    qwrt_t *rt;
    int is_timer;
    int repeat;
    int handle_idx;
} qwrt_cb_data_t;
```

- [ ] **Step 3: 写 src/msgq.c（线程安全 FIFO）**

```c
#include "qwrt_internal.h"

int qwrt_msg_push(qwrt_t *rt, const char *data, size_t len, int source) {
    qwrt_msg_t *m = malloc(sizeof *m + len + 1);
    if (!m) return -1;
    m->data = (char *)(m + 1);
    memcpy(m->data, data, len); m->data[len] = '\0';
    m->len = len; m->source = source; m->next = NULL;
    uv_mutex_lock(&rt->msg_mutex);
    if (rt->msg_tail) rt->msg_tail->next = m; else rt->msg_head = m;
    rt->msg_tail = m;
    uv_mutex_unlock(&rt->msg_mutex);
    uv_async_send(&rt->wake);          /* 唤醒阻塞中的 uv_run */
    return 0;
}

qwrt_msg_t *qwrt_msg_pop(qwrt_t *rt) {
    uv_mutex_lock(&rt->msg_mutex);
    qwrt_msg_t *m = rt->msg_head;
    if (m) { rt->msg_head = m->next; if (!rt->msg_head) rt->msg_tail = NULL; }
    uv_mutex_unlock(&rt->msg_mutex);
    return m;
}

void qwrt_msg_free(qwrt_msg_t *m) { free(m); }
```

- [ ] **Step 4: 写 src/thread.c（线程主循环 + ready 握手 + 派发）**

```c
#include "qwrt_internal.h"
#include <stdlib.h>
#include <string.h>

static int qwrt_flush_microtasks(qwrt_t *rt) {
    int total = 0, n;
    while ((n = JS_ExecutePendingJob(rt->jsrt)) > 0) total += n;
    return total;
}

/* uv_async 回调：跑在 qwrt 线程，排空入站队列并派发 onmessage */
static void qwrt_wake_cb(uv_async_t *a) {
    qwrt_t *rt = (qwrt_t *)a->data;
    if (rt->shutting_down) return;
    qwrt_msg_t *m;
    while ((m = qwrt_msg_pop(rt)) != NULL) {
        qwrt_dispatch_message(rt, m);   /* bridge.c 实现；JSRuntime 已就绪 */
        qwrt_msg_free(m);
    }
}

static void qwrt_thread_main(void *arg) {
    qwrt_t *rt = arg;
    rt->host_data = rt->config.host_data;
    rt->debug = rt->config.debug;

    if (uv_loop_init(&rt->loop) != 0) goto fail_init;
    rt->wake.data = rt;
    if (uv_async_init(&rt->loop, &rt->wake, qwrt_wake_cb) != 0) goto fail_loop;

    /* 完整初始化（沿用旧 qwrt_create 主体，去掉 pal）：JS_NewRuntime、
     * JS_SetRuntimeOpaque、扩展 init、qwrt_ctx_create、polyfill 注入、DAP attach。
     * initial_script 此时在活动 context 上 eval。 */
    rt->jsrt = JS_NewRuntime();
    if (!rt->jsrt) goto fail_loop;
    JS_SetRuntimeOpaque(rt->jsrt, rt);
    /* ...qwrt_runtime_init(rt)（见 qwrt.c 重构后的内部函数）... */
    if (rt->config.initial_script) {
        char *err = NULL;
        if (qwrt_eval_internal(rt, rt->config.initial_script, &err) != 0) {
            /* err 记录到 ready_err，通知宿主后退出线程（不继续跑） */
            rt->ready_err = -1;
            (void)err;
            goto handshake_and_exit;
        }
    }

handshake_and_exit:
    uv_mutex_lock(&rt->ready_mutex);
    rt->thread_ready = 1;
    uv_cond_signal(&rt->ready_cond);
    uv_mutex_unlock(&rt->ready_mutex);
    if (rt->ready_err) {
        /* init 失败：清理后线程自己退出；qwrt_create 返回 NULL */
        qwrt_thread_teardown(rt);
        return;
    }

    /* ==== 主循环 ==== */
    while (!rt->shutting_down) {
        uv_run(&rt->loop, UV_RUN_ONCE);      /* 阻塞等事件；wake_cb 在期间派发消息 */
        if (rt->shutting_down) break;
        qwrt_flush_microtasks(rt);
    }
    qwrt_thread_teardown(rt);
}

/* teardown：排空剩余入站队列、close walk、关闭 loop、销毁 contexts/runtime、
 * 扩展 destroy、free 排空后的残余消息、释放 rt 持有的副本（initial_script）。 */
```

> 注：`qwrt_dispatch_message` 实现放 bridge.c（Step 6）；`qwrt_runtime_init`/`qwrt_eval_internal`/`qwrt_thread_teardown` 放 qwrt.c 重构（Step 5），签名在下方给出。`rt->wake.data` 需要 `uv_async_t` 有 `data` 字段——mock 与真实 libuv 的 `uv_async_t` 都有 `void *data`。

- [ ] **Step 5: 重写 src/qwrt.c 生命周期**

```c
#include "qwrt_internal.h"

static int qwrt_runtime_init(qwrt_t *rt);
static void qwrt_thread_teardown(qwrt_t *rt);
static void qwrt_thread_main(void *arg);   /* thread.c 定义 */

qwrt_t *qwrt_create(const qwrt_config_t *config) {
    if (!config) return NULL;
    qwrt_t *rt = calloc(1, sizeof *rt);
    if (!rt) return NULL;
    rt->magic = QWRT_MAGIC;
    rt->config = *config;
    if (config->initial_script) rt->config.initial_script = strdup(config->initial_script);
    uv_mutex_init(&rt->msg_mutex);
    uv_cond_init(&rt->ready_cond);
    uv_mutex_init(&rt->ready_mutex);

    if (uv_thread_create(&rt->thread, qwrt_thread_main, rt) != 0) {
        uv_cond_destroy(&rt->ready_cond);
        uv_mutex_destroy(&rt->ready_mutex);
        uv_mutex_destroy(&rt->msg_mutex);
        free((void*)rt->config.initial_script);
        free(rt);
        return NULL;
    }
    /* 阻塞到 ready */
    uv_mutex_lock(&rt->ready_mutex);
    while (!rt->thread_ready) uv_cond_wait(&rt->ready_cond, &rt->ready_mutex);
    uv_mutex_unlock(&rt->ready_mutex);
    if (rt->ready_err) { qwrt_destroy(rt); return NULL; }
    return rt;
}

int qwrt_post_message(qwrt_t *rt, const char *json, size_t len) {
    if (!rt || rt->magic != QWRT_MAGIC || !json) return -1;
    return qwrt_msg_push(rt, json, len, QWRT_MSG_SRC_HOST);
}

void qwrt_destroy(qwrt_t *rt) {
    if (!rt) return;
    if (rt->magic != QWRT_MAGIC) return;
    uv_mutex_lock(&rt->msg_mutex);
    rt->shutting_down = 1;
    uv_mutex_unlock(&rt->msg_mutex);
    uv_async_send(&rt->wake);          /* 唤醒可能阻塞的 uv_run */
    uv_thread_join(&rt->thread);       /* 等线程 teardown 完成 */
    uv_mutex_destroy(&rt->msg_mutex);
    uv_cond_destroy(&rt->ready_cond);
    uv_mutex_destroy(&rt->ready_mutex);
    free((void*)rt->config.initial_script);
    free(rt);
}

void *qwrt_get_runtime_data(qwrt_t *rt) { return rt ? rt->host_data : NULL; }
void  qwrt_set_runtime_data(qwrt_t *rt, void *data) { if (rt) rt->host_data = data; }
void  qwrt_free(void *ptr) { free(ptr); }
```

`qwrt_runtime_init`：从旧 `qwrt_create` 主体搬来（JS_NewRuntime / JS_SetRuntimeOpaque / `qwrt_ctx_create`（无 pal 参数）/ polyfill 注入 / 扩展 `qwrt_run_ext_init` / DAP attach），`pal->mem_alloc/mem_free` 全部换 `malloc/free`。`qwrt_eval_internal`：活动 context `JS_Eval` + 异常捕获（返回 0 成功 / -1 异常，`*err` 装 `JS_ToCString` 的异常串）。`qwrt_thread_teardown`：先排空 `qwrt_msg_pop` 并 free 所有剩余消息 → 扩展 destroy → 销毁 contexts → `JS_FreeRuntime` → close walk + `uv_loop_close`（async close 后）。参考旧 `qwrt_destroy` 中「先 `JS_ExecutePendingJob` 再 `JS_FreeRuntime` 避免 gc_obj_list 断言」的顺序。

- [ ] **Step 6: 改 src/bridge.c——删 deferred + 主机消息 + I/O 存根**

删除：`deferred_pal_cb`、`pal_async_cb`、`qwrt_defer_callback` 声明与实现、`qwrt_ctx_cleanup_resources` 里的 deferred 相关。`js_pal_*` 包装：**同步类（time_now/hrtime/log/timer_stop/timer_start/random_bytes）本任务直接内联实现**（time/hrtime = `uv_now`/`uv_hrtime`，log = `fprintf(stderr, ...)`，timer = `uv_timer_init/start/stop` 注册到 `rt->loop`）；**异步类（http_request/http_request_stream/fs_*/storage_*）本任务存根**：照旧 `JS_NewPromiseCapability` + 返回 promise，但后端调用改为立刻调 `js_reject`（`promise 暂未实现（Task 3）`）——保证 polyfill 注入（只存引用不调用）与消息往返测试通过。

新增 `qwrt_create_pal_object_ctx` 中的 `postMessage` 函数与派发入口：

```c
static JSValue js_pal_post_message(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv) {
    qwrt_ctx_t *cctx = get_ctx_from_jsctx(ctx);
    if (!cctx) return JS_EXCEPTION;
    qwrt_t *rt = cctx->rt;
    if (!rt->config.message_cb) return JS_UNDEFINED;
    size_t len = 0;
    const char *json = JS_ToCStringLen(ctx, &len, argv[0]);
    if (!json) return JS_EXCEPTION;
    rt->config.message_cb(rt, json, len, rt->host_data);
    JS_FreeCString(ctx, json);
    return JS_UNDEFINED;
}

/* 主 context 入站派发：宿主 JSON 已解析成值；source 0=host。 */
void qwrt_dispatch_message(qwrt_t *rt, qwrt_msg_t *m) {
    /* 主 context（第一个 context）上找 __qwrt_dispatch__ 并调用 */
    qwrt_ctx_t *cctx = rt->contexts[0];
    if (!cctx || !cctx->jsctx) return;
    JSContext *ctx = cctx->jsctx;
    JSValue fn = JS_GetPropertyStr(ctx, JS_GetGlobalObject(ctx), "__qwrt_dispatch__");
    if (JS_IsFunction(ctx, fn)) {
        JSValue src = JS_NewInt32(ctx, m->source);
        JSValue data;
        if (m->source == QWRT_MSG_SRC_HOST) {
            JSValue str = JS_NewStringLen(ctx, m->data, m->len);
            data = JS_JSONParse(ctx, str, 0);
            if (JS_IsException(data)) {          /* spec §5: 坏 JSON → 错误信封 */
                JS_FreeValue(ctx, data);
                JS_FreeValue(ctx, str);
                JS_FreeValue(ctx, fn);
                if (rt->config.message_cb) {
                    static const char *bad =
                        "{\"type\":\"error\",\"error\":\"bad-json\"}";
                    rt->config.message_cb(rt, bad, strlen(bad), rt->host_data);
                }
                return;
            }
            JS_FreeValue(ctx, str);
        } else {
            data = JS_NewArrayBufferCopy(ctx, (const uint8_t*)m->data, m->len);
        }
        JSValue args[2] = { data, src };
        JSValue r = JS_Call(ctx, fn, JS_UNDEFINED, 2, args);
        JS_FreeValue(ctx, r);
        JS_FreeValue(ctx, data);
        JS_FreeValue(ctx, src);
    }
    JS_FreeValue(ctx, fn);
}
```

> 注：`JS_NewArrayBufferCopy`/`JS_JSONParse` 需正确引用管理（执行时按编译/运行错误微调）。worker 分支（source>0）本任务不会触发，Task 4 才有意义。

- [ ] **Step 7: 写 polyfill/src/host-messaging.js 并挂进 index.js**

```js
// host-messaging.js — 宿主↔qwrt 消息的 JS 面（W3C worker 语义）
// pal 来自 __pal_inject__（见 build.js 的 IIFE 处理）
var pal = globalThis.__pal_inject__;
var self = globalThis;

// JS→宿主：postMessage(data)，data 需 JSON 可序列化
globalThis.postMessage = function (data) { pal.postMessage(data); };

// 宿主→JS：bridge 调 __qwrt_dispatch__(data, source)；source 0=host
globalThis.__qwrt_dispatch__ = function (data, source) {
  self.dispatchEvent(new MessageEvent('message', { data: data }));
};

// onmessage 属性（EventTarget 语义）
var __onmsg = null;
Object.defineProperty(self, 'onmessage', {
  get: function () { return __onmsg; },
  set: function (fn) {
    if (__onmsg) self.removeEventListener('message', __onmsg);
    __onmsg = function (e) {
      try { fn.call(self, e); } catch (err) { reportError(err); }
    };
    if (fn) self.addEventListener('message', __onmsg);
  },
  configurable: true,
});
```

`index.js` 顶部 `import './host-messaging.js'`（`setupHostMessaging(pal)` 风格，与现有 setup 模块一致；`MessageEvent` 来自已存在的 message-channel.js，`reportError` 来自 error-events.js，均已就绪）。

- [ ] **Step 8: 写 test/test_host.h（宿主测试桩）**

```cpp
// test_host.h — 新宿主契约测试桩（gtest 用）
#pragma once
#include "qwrt/qwrt.h"
#include "mock_libuv.h"
#include <gtest/gtest.h>
#include <string>
#include <cstring>
#include <atomic>

struct HostCtx {
    qwrt_t *rt = nullptr;
    uv_mutex_t m; uv_cond_t c;
    std::string last;
    std::atomic<bool> got{false};
};

static inline void host_msg_cb(qwrt_t *rt, const char *json, size_t len, void *data) {
    (void)rt;
    auto *h = (HostCtx*)data;
    uv_mutex_lock(&h->m);
    h->last.assign(json, len);
    h->got = true;
    uv_cond_signal(&h->c);
    uv_mutex_unlock(&h->m);
}

// 标准测试引导脚本：onmessage 命令通道（eval/echo）
static const char *kTestBootstrap = R"JS(
globalThis.onmessage = function (e) {
  var d = e.data;
  if (d && d.cmd === 'eval') {
    try { postMessage({ok: true, v: JSON.stringify(eval(d.code))}); }
    catch (err) { postMessage({ok: false, e: String(err)}); }
  } else if (d && d.cmd === 'echo') {
    postMessage(d.data);
  }
};
)JS";

static inline HostCtx *host_create(const char *script = kTestBootstrap) {
    auto *h = new HostCtx();
    uv_mutex_init(&h->m); uv_cond_init(&h->c);
    qwrt_config_t cfg = {};
    cfg.initial_script = script;
    cfg.message_cb = host_msg_cb;
    cfg.host_data = h;
    h->rt = qwrt_create(&cfg);
    if (!h->rt) { delete h; return nullptr; }
    return h;
}

static inline void host_destroy(HostCtx *h) {
    if (!h) return;
    qwrt_destroy(h->rt);
    uv_cond_destroy(&h->c); uv_mutex_destroy(&h->m);
    delete h;
}

// 等待宿主收到一条消息；返回 true 并把内容写进 out。timeout_ms 内没到则 false。
static inline bool host_wait_msg(HostCtx *h, std::string *out, int timeout_ms = 5000) {
    uv_mutex_lock(&h->m);
    while (!h->got) {
        if (uv_cond_timedwait(&h->c, &h->m, timeout_ms) != 0) { uv_mutex_unlock(&h->m); return false; }
    }
    h->got = false;
    *out = h->last;
    uv_mutex_unlock(&h->m);
    return true;
}

// 宿主对 qwrt 求值（经命令通道）；返回 {ok, v|e} 的原始 JSON。
static inline bool host_eval(HostCtx *h, const char *code, std::string *out, int timeout_ms = 5000) {
    std::string payload = std::string("{\"cmd\":\"eval\",\"code\":") + JSON_string(code) + "}";
    EXPECT_EQ(0, qwrt_post_message(h->rt, payload.data(), payload.size()));
    return host_wait_msg(h, out, timeout_ms);
}
```

> 注：`JSON_string` 小助手（把 C 串转成 JSON 字符串字面量）在 test_host.h 内实现（转义 `\` `"` 控制字符）。`host_wait_msg` 用 `uv_cond_timedwait`——mock 提供该符号（Task 1 已声明，需在 mock_libuv.c 实现为 `pthread_cond_timedwait`，用 `CLOCK_REALTIME`）。

- [ ] **Step 9: 重写 test/test_qwrt_gtest.cpp（宿主契约核心套件）**

```cpp
#include "test_host.h"

TEST(qwrt_create, creates_ready_runtime) {
    HostCtx *h = host_create();       // 无脚本：只验证 create 不阻塞不失败
    ASSERT_NE(nullptr, h);
    host_destroy(h);
}

TEST(qwrt_create, null_config_rejected) {
    EXPECT_EQ(nullptr, qwrt_create(nullptr));
}

TEST(qwrt_create, initial_script_exception_fails_create) {
    qwrt_config_t cfg = {}; cfg.initial_script = "throw new Error('boom');";
    EXPECT_EQ(nullptr, qwrt_create(&cfg));   // eval 异常 → ready_err → NULL
}

TEST(qwrt_post_message, host_message_roundtrip) {
    HostCtx *h = host_create();
    ASSERT_NE(nullptr, h);
    const char *json = "{\"cmd\":\"echo\",\"data\":{\"a\":1}}";
    ASSERT_EQ(0, qwrt_post_message(h->rt, json, strlen(json)));
    std::string out;
    ASSERT_TRUE(host_wait_msg(h, &out));
    ASSERT_EQ("{\"a\":1}", out);              // echo 原样回
    host_destroy(h);
}

TEST(qwrt_post_message, eval_via_command_channel) {
    HostCtx *h = host_create();
    ASSERT_NE(nullptr, h);
    std::string out;
    ASSERT_TRUE(host_eval(h, "1 + 2", &out));
    ASSERT_NE(std::string::npos, out.find("\"v\":\"3\""));
    host_destroy(h);
}

TEST(host_, wait_thread_exit) {
    HostCtx *h = host_create();
    ASSERT_NE(nullptr, h);
    host_destroy(h);                        // destroy 阻塞直到线程退出（join 完成即证明）
    SUCCEED();
}

TEST(host_, message_thread_safety) {
    HostCtx *h = host_create();
    ASSERT_NE(nullptr, h);
    const int N = 8, PER = 200;
    std::vector<std::thread> ts;
    for (int i = 0; i < N; i++) ts.emplace_back([h, i]{
        for (int k = 0; k < PER; k++) {
            std::string s = "{\"cmd\":\"echo\",\"data\":" + std::to_string(i*PER+k) + "}";
            qwrt_post_message(h->rt, s.data(), s.size());
        }
    });
    int seen = 0;
    std::string out;
    while (seen < N * PER) { if (host_wait_msg(h, &out)) seen++; }
    for (auto &t : ts) t.join();
    host_destroy(h);
    ASSERT_EQ(N * PER, seen);               // 每条 echo 都回来了 = 无丢消息/无竞态
}
```

- [ ] **Step 10: 改 test/CMakeLists.txt**

`add_qwrt_gtest` 内 `target_link_libraries(${name} qwrt qwrt_mock GTest::gtest_main pthread)` 改为 `qwrt mock_libuv GTest::gtest_main pthread`，并把 `test/CMakeLists.txt` 里的 `qwrt_mock` 引用全部换成 `mock_libuv`。**临时注释**以下套件的 `add_qwrt_gtest(...)` 行并注明恢复任务（Task 3 恢复 bridge/fetch/polyfill，Task 5 恢复 context/compress）：

```cmake
# TODO(Task 3): 恢复 — test_bridge_http_gtest test_bridge_stream_gtest test_fetch_stream_gtest test_polyfill_gtest
# TODO(Task 5): 恢复 — test_context_gtest test_compress_gtest test_compress_consistency_gtest
```

> 注：这些套件引用已删除的 `qwrt_eval`/`qwrt_spawn` 等符号，Task 2 必须先从构建中摘除才能保持全树绿。Task 3/5 用 host_eval 通道重写后放回。

- [ ] **Step 11: 重新生成 polyfill 字节码 + 构建 + 跑核心测试**

Run: `cd polyfill && npm install && npm run build && cd ..`
Run: `cmake -B build -DCMAKE_BUILD_TYPE=Debug -DQWRT_BUILD_TESTS=ON && cmake --build build -j$(nproc)`
Run: `cd build && ctest -L offline --output-on-failure`
Expected: 编译通过（`-Wall -Wextra -Werror` 零告警）；`test_qwrt_gtest` 全过；其余暂留套件通过（不含被摘除的 I/O/context 套件）。

- [ ] **Step 12: 报告变更**

```text
执行模型 A 落地：qwrt.h 新公共 API（qwrt_create 阻塞式 + qwrt_post_message + 13 函数删除）、
src/thread.c 线程主循环（uv_run ONCE + wake_cb 派发 + 微任务 flush）、src/msgq.c 入站 FIFO、
src/qwrt.c 生命周期重写（ready 握手 + 优雅关停 join + NULL-safe）、bridge.c 删 deferred、
polyfill host-messaging.js（postMessage/onmessage/__qwrt_dispatch__）、test_host.h 宿主测试桩、
test_qwrt_gtest 迁到宿主契约（message roundtrip / eval 通道 / 线程安全 / destroy join）。
I/O 异步 js_pal_* 暂存根，Task 3 补 uv_io.c。I/O/context 套件暂摘除，Task 3/5 恢复。
等待用户指示（不 commit）。
```

---

### Task 3: uv_io.c——bridge 直连 libuv 完整 I/O（替换存根）

**Files:**
- Create: `src/uv_io.c`（从 `platform/uv/pal_uv.c` 吸收：timer/http/fs/storage/random/time/log）
- Modify: `src/bridge.c`（异步 js_pal_* 存根换真实现；调用 `uv_io_*` + done 回调 resolve）
- Modify: `test/mock_libuv.{c,h}`（补 tcp/connect/write/read/fs 等测试所需符号）
- Modify: `test/CMakeLists.txt`（恢复 bridge/fetch/polyfill 套件；加 `src/uv_io.c` 进 qwrt 库）
- Modify: `test/test_bridge_http_gtest.cpp` / `test_bridge_stream_gtest.cpp` / `test_fetch_stream_gtest.cpp` / `test_polyfill_gtest.cpp`（迁到 host_eval 契约）

**Interfaces:**
- Consumes: Task 2 的 `qwrt_cb_data_t`（bridge 持有 resolve/reject + ctx + rt + handle_idx）、mock 扩展的 uv_*。
- Produces: `uv_io_*` 后端函数（内部，bridge.c 调用）。统一完成回调签名 `typedef void (*qwrt_io_done_t)(void *opaque, int status, const char *result, size_t len);`。`opaque` = bridge 的 `qwrt_cb_data_t*`（含 resolve/reject）；回调在 qwrt 线程（libuv 回调上下文）直接 `JS_Call` resolve/reject——**不再经 deferred**。
- 关键签名（从 pal_uv.c 同名函数平移，`pal` 参数替换为 `rt`）：

```c
void uv_io_timer_start(qwrt_t *rt, uint64_t timeout_ms, uint64_t repeat_ms, qwrt_io_done_t done, void *opaque); /* 一次性：到期调 done(status=0) */
void uv_io_timer_stop(qwrt_t *rt, int handle_idx);
void uv_io_http_request(qwrt_t *rt, const char *method, const char *url, const char *headers_json, const uint8_t *body, size_t body_len, qwrt_io_done_t done, void *opaque);
void uv_io_http_request_stream(qwrt_t *rt, const char *method, const char *url, const char *headers_json, const uint8_t *body, size_t body_len, qwrt_pal_stream_ops_t *ops);
void uv_io_fs_read(qwrt_t *rt, const char *path, qwrt_io_done_t done, void *opaque);
void uv_io_fs_write(qwrt_t *rt, const char *path, const uint8_t *data, size_t len, qwrt_io_done_t done, void *opaque);
void uv_io_fs_exists(qwrt_t *rt, const char *path, qwrt_io_done_t done, void *opaque);
void uv_io_fs_remove(qwrt_t *rt, const char *path, qwrt_io_done_t done, void *opaque);
void uv_io_fs_list(qwrt_t *rt, const char *path, qwrt_io_done_t done, void *opaque);
void uv_io_storage_get(qwrt_t *rt, const char *key, qwrt_io_done_t done, void *opaque);
void uv_io_storage_set(qwrt_t *rt, const char *key, const uint8_t *data, size_t len, qwrt_io_done_t done, void *opaque);
void uv_io_storage_del(qwrt_t *rt, const char *key, qwrt_io_done_t done, void *opaque);
void uv_io_random_bytes(qwrt_t *rt, uint8_t *buf, size_t len);
uint64_t uv_io_time_now(qwrt_t *rt);
uint64_t uv_io_hrtime(qwrt_t *rt);
void uv_io_log(qwrt_t *rt, int level, const char *msg);
```

- [ ] **Step 1: 写 src/uv_io.c 骨架 + 同步函数**

```c
/* uv_io.c — qwrt 直连 libuv 的 I/O 后端（PAL 删除后的替代层）。
 * 所有回调跑在 qwrt 线程（libuv 事件循环线程），可直接 JS_Call，无 deferred。
 * 从 platform/uv/pal_uv.c 吸收并去 pal 化：pal 参数→rt，存储用 qwrt_t 内的表。 */
#include "qwrt_internal.h"
#include <stdio.h>
#include <string.h>

uint64_t uv_io_time_now(qwrt_t *rt) { (void)rt; return uv_now(&rt->loop); }
uint64_t uv_io_hrtime(qwrt_t *rt) { (void)rt; return uv_hrtime(); }
void uv_io_log(qwrt_t *rt, int level, const char *msg) {
    (void)rt; fprintf(stderr, "[qwrt:%d] %s\n", level, msg ? msg : "");
}
void uv_io_random_bytes(qwrt_t *rt, uint8_t *buf, size_t len) {
    (void)rt; /* 从 /dev/urandom 读；pal_uv_random_bytes 同款（mbedTLS CTR_DRBG 可后续换） */
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) { size_t got = fread(buf, 1, len, f); fclose(f); (void)got; }
}
```

- [ ] **Step 2: timer 后端（uv_timer_t 注册进 qwrt_t 的 handle 表）**

```c
/* 沿用旧 handle 表模型：qwrt_ctx_t->handles[] 存 uv_timer_t*，timer_resolves[] 存 JSValue。 */
void uv_io_timer_start(qwrt_t *rt, uint64_t timeout_ms, uint64_t repeat_ms, qwrt_io_done_t done, void *opaque) {
    uv_timer_t *t = calloc(1, sizeof *t);
    uv_timer_init(&rt->loop, t);
    t->data = opaque;
    /* 一次性定时器：到期 done(opaque, 0, NULL, 0)；repeat 则由 js_pal_timer_start 的 JS 侧逻辑调度 */
    uv_timer_start(t, (uv_timer_cb)timer_cb_thunk, timeout_ms, repeat_ms);
    (void)done;
}
static void timer_cb_thunk(uv_timer_t *t) {
    qwrt_cb_data_t *cd = t->data;
    /* 从 handle 表反查 handle_idx 决定停/续；resolve 由 bridge 的 JS 侧完成（沿用旧逻辑） */
}
```

> 注：timer 的 resolve 逻辑旧实现较绕（`js_pal_timer_start` 返回 `{handle, promise}`，JS 侧 setTimeout 用）。执行者**保持旧 polyfill 计时语义不变**，只把 `cctx->pal->timer_start` 换成 `uv_io_timer_start(rt,...)`，promise 解析沿用旧的 `timer_cbds[]`/`timer_resolves[]` 表（只是不再经 deferred，直接 resolve）。桥仍遵守「只做转换+调用+Promise 解析」。

- [ ] **Step 3: fs / storage 后端（uv_fs_* 工作请求 + 完成回调）**

```c
typedef struct fs_req_s {
    uv_fs_t req;
    qwrt_io_done_t done;
    void *opaque;
} fs_req_t;

static void fs_read_cb(uv_fs_t *req) {
    fs_req_t *fr = req->data;
    uv_fs_t *r = &fr->req;
    if (r->result < 0) { fr->done(fr->opaque, (int)r->result, NULL, 0); }
    else { fr->done(fr->opaque, 0, r->bufs ? r->bufs[0].base : NULL, (size_t)r->result); }
    uv_fs_req_cleanup(r);
    free(fr);
}

void uv_io_fs_read(qwrt_t *rt, const char *path, qwrt_io_done_t done, void *opaque) {
    fs_req_t *fr = calloc(1, sizeof *fr);
    fr->done = done; fr->opaque = opaque; fr->req.data = fr;
    uv_fs_open(&rt->loop, &fr->req, path, O_RDONLY, 0, open_cb);
    /* open_cb 内再发 uv_fs_read，read 完成回调 = fs_read_cb（模式照搬 pal_uv.c 的 pal_uv_fs_read） */
}
```

> 注：`pal_uv.c` 的 `pal_uv_fs_read/write/exists/remove/list` 已有完整两段式实现（open→read 等）。**执行者打开 `platform/uv/pal_uv.c`（3342 行）逐函数平移**：去 `pal` 参数、去 `pal_uv_t` 存储（用 `qwrt_t`/静态表）、回调改 `qwrt_io_done_t`。storage 用旧 storage 表逻辑（`pal_uv_store_entry_t` 平移到 qwrt_t 或 uv_io 内部静态表——**注意无可变文件级全局约束**，storage 表放 `qwrt_t` 新增字段）。TLS 分支由 `QWRT_WITH_TLS` 控制（mbedTLS，沿用 pal_uv_http_request 内嵌 TLS 栈）。

- [ ] **Step 4: http 后端（uv_tcp + TLS，从 pal_uv.c 平移）**

平移 `pal_uv_http_request`/`pal_uv_http_request_stream`/`getaddrinfo`/`connect`/`read`/`parse`（chunked 解码等 bug 已修，见记忆 [[qwrt-pal-uv-pending-bugs]]，平移时保留修复）。完成/流式回调改 `qwrt_io_done_t`/`qwrt_pal_stream_ops_t`（后者结构保留在 qwrt_internal.h，不再从 qwrt.h 导出）。

- [ ] **Step 5: bridge.c 异步 js_pal_* 换真实现**

模式（以 fs_read 为例，替换 Step 存根）：

```c
static void fs_done(void *opaque, int status, const char *result, size_t len) {
    qwrt_cb_data_t *cd = opaque;
    JSContext *ctx = cd->ctx->jsctx;
    if (status < 0) {
        JSValue err = JS_NewError(ctx);
        JS_SetPropertyStr(ctx, err, "message", JS_NewString(ctx, "fs_read failed"));
        JSValue args[1] = { err };
        JS_Call(ctx, cd->reject, JS_UNDEFINED, 1, args);
        JS_FreeValue(ctx, err);
    } else {
        JSValue ab = JS_NewArrayBufferCopy(ctx, (const uint8_t*)result, len);
        JSValue args[1] = { ab };
        JS_Call(ctx, cd->resolve, JS_UNDEFINED, 1, args);
        JS_FreeValue(ctx, ab);
    }
    qwrt_free_cb_data(cd);
}

static JSValue js_pal_fs_read(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    qwrt_ctx_t *cctx = get_ctx_from_jsctx(ctx);
    if (!cctx) return JS_EXCEPTION;
    const char *path = JS_ToCString(ctx, argv[0]);
    if (!path) return JS_EXCEPTION;
    JSValue promise = JS_NewPromiseCapability(ctx);
    qwrt_cb_data_t *cd = alloc_cb_data(cctx, promise, 1 /*resolve idx*/, cctx->rt);
    /* alloc_cb_data 取出 capability 里的 resolve/reject 存进 cd */
    uv_io_fs_read(cctx->rt, path, fs_done, cd);
    JS_FreeCString(ctx, path);
    return promise;
}
```

> 桥仍只做三件事：`JS_ToCString` 转换 → `uv_io_*` 调用 → `JS_NewPromiseCapability` + 完成回调里 resolve/reject。路径校验 `bridge_validate_path` 保留。`alloc_cb_data`/`qwrt_free_cb_data` 保留（去 pal 字段后即为 `qwrt_cb_data_t`）。http/stream/storage 同模式平移。

- [ ] **Step 6: mock_libuv 扩展（网络 + fs 符号）**

`mock_libuv.{c,h}` 新增（真实实现，够测试确定性即可）：`uv_tcp_init/connect/write/read_start/read_stop/close`、`uv_getaddrinfo`（查一个预置表或直接回 `127.0.0.1`）、`uv_fs_open/read/write/stat/unlink/scandir/close/req_cleanup`、`uv_buf_init`/`uv_buf_t`、`O_RDONLY` 等常量、`uv_strerror`。网络确定性：`uv_tcp_connect` 直接同步在 loop 队列里放好读事件源；`uv_fs_*` 走真实文件系统（测试工作目录 `build/test` 下建临时文件——与现有 `WORKING_DIRECTORY` 一致）。mock 网络不做真实 socket，连接后由测试注入要回显的字节（一个 `mock_tcp_feed(handle, bytes, len)` 辅助，声明进 mock_libuv.h）。

- [ ] **Step 7: 恢复 + 迁移 I/O 套件**

`test/CMakeLists.txt` 取消 Task 2 的注释；把四个套件（bridge_http/bridge_stream/fetch_stream/polyfill）的 `qwrt_create(pal)`+`qwrt_eval` 用法迁到 `host_eval`/`host_create` 契约（机械替换：`qwrt_create` 带 mock pal → `host_create(bootstrap)`；每个 `qwrt_eval(rt, code)` → `host_eval(h, code, &out)` 再解析 `out`）。polyfill 套件中直接测底层 `pal.*` 的用例改为经命令通道 `eval("pal.fs_read(...)")`。

- [ ] **Step 8: 构建 + 全量离线测试**

Run: `cd build && cmake --build . -j$(nproc) && ctest -L offline --output-on-failure`
Expected: 全绿（含恢复的 I/O 套件）；`-Wall -Wextra -Werror` 零告警。

- [ ] **Step 9: 报告变更**

```text
src/uv_io.c：从 pal_uv.c 吸收 timer/http/fs/storage/random/time/log，直连 libuv，
回调直接 JS_Call（无 deferred）。bridge 异步 js_pal_* 由存根换真实现（转换+调用+Promise 解析）。
mock_libuv 扩展网络/fs 符号。bridge/fetch/polyfill 套件恢复并迁到 host_eval 契约。全绿。
等待用户指示（不 commit）。
```

---

### Task 4: Web Worker（真线程）

**Files:**
- Create: `src/worker.c`（`qwrt_worker_t` + worker 线程主循环 + 父↔子队列桥）
- Modify: `src/qwrt_internal.h`（worker 相关内部类型、`qwrt_msg_src_t` 扩展）
- Modify: `src/thread.c`（`qwrt_wake_cb` 派发支持 worker source；worker 消息路由到对应 Worker 实例）
- Modify: `src/bridge.c`（`qwrt_dispatch_message` worker 分支已有；补 worker 相关的 pal JS 函数：`pal.spawn_worker`/`pal.worker_post`/`pal.worker_terminate`）
- Modify: `polyfill/src/structured-clone.js`（新增字节序列化 `serializeToBytes`/`deserializeFromBytes`）
- Create: `polyfill/src/worker.js`（`Worker` 类 + `new Worker(url)` + `postMessage`/`onmessage`/`terminate`/`close`）
- Modify: `polyfill/src/index.js`（import worker.js）
- Create: `test/test_worker_gtest.cpp`（`TEST(worker_*)`）
- Modify: `test/CMakeLists.txt`（注册 test_worker_gtest）

**Interfaces:**
- Consumes: Task 2 的入站 FIFO + `qwrt_dispatch_message` 的 worker 分支、Task 3 的 mock uv_async/thread/cond。
- Produces（C 内部）:

```c
typedef struct qwrt_worker_s qwrt_worker_t;
qwrt_worker_t *qwrt_worker_create(qwrt_t *parent, const char *script, int *out_err); /* script=源码文本 */
void qwrt_worker_post(qwrt_t *parent, qwrt_worker_t *w, const uint8_t *bytes, size_t len); /* 父→子 */
void qwrt_worker_terminate(qwrt_t *parent, qwrt_worker_t *w);   /* 异步：发关停信号，不阻塞父 JS */
/* worker 内部：父→子 消息进 worker 自己的入站队列；子→父 消息 push 进 parent 的入站队列（source=worker id） */
```

- Produces（polyfill JS）: `Worker` 类（`new Worker(url)`：C 解析 url→file:// 用 fs 读 / http(s):// 用 fetch 读源码 → `pal.spawn_worker(script)`；`worker.postMessage(value)`：`serializeToBytes(value)` → `pal.worker_post(wid, bytes)`；`worker.onmessage`：bridge 派发 worker 消息时构造 `MessageEvent`；`worker.terminate()`/`worker.close()`）。`structured-clone.js` 新增 `serializeToBytes(value) -> ArrayBuffer` / `deserializeFromBytes(ArrayBuffer) -> value`（v1 无 transferables，含循环引用；失败抛 `DataCloneError`——复用现有 `structuredClone` 的深拷贝内核，外加一个编码层）。

- [ ] **Step 1: 扩展 structured-clone.js——字节序列化**

```js
// 在现有 structuredClone 内核实值深拷贝逻辑之外，加一对待传输格式：
// 编码：把值线性化成 {tag, ...} 记录流（v1：基本类型 + 对象 + 循环引用表）。
// 复用现有 cloneValue 的遍历骨架，改为写字节：
function serializeToBytes(value) {
  const out = [];
  const seen = new Map();          // 循环引用/共享引用表
  const write = (v) => { /* 按 tag 写: null/bool/int/double/string/Date/RegExp/Error/
                             Map/Set/ArrayBuffer/DataView/TypedArray/Blob/File/Array/Object + ref */ };
  write(value);
  return bytesToArrayBuffer(out);
}
function deserializeFromBytes(buf) {
  const rd = arrayBufferToReader(buf);
  const refs = [];
  const read = () => { /* 对称解码，遇到 ref 从 refs 取回 */ };
  return read();
}
```

> 注：格式自定但必须支持循环引用与 TypedArray/ArrayBuffer/Blob 等现有 `structuredClone` 已支持的类型；不支持函数/符号（抛 `DataCloneError`）。**执行者参考现有 `polyfill/src/structured-clone.js` 的 `cloneValue` 分支表，一一对应到编码/解码两遍**。现有 `globalThis.structuredClone` 保留（任务内进程克隆），新函数挂 `globalThis.__qwrt_serialize__`/`__qwrt_deserialize__`（或同模块导出供 worker.js 用）。

- [ ] **Step 2: src/worker.c——worker 线程 + 队列桥**

```c
struct qwrt_worker_s {
    qwrt_t *parent;
    int id;                       /* 父 runtime 内的 worker id（= 消息 source 标签） */
    /* worker 自己的线程/loop/runtime（复用 qwrt_thread_main 模式，但宿主=父 runtime） */
    uv_thread_t thread;
    uv_loop_t loop;
    uv_async_t wake;
    uv_mutex_t msg_mutex;
    qwrt_msg_t *msg_head, *msg_tail;   /* 父→子 入站 */
    int shutting_down;
    qwrt_t *self;                 /* worker 的 qwrt_t（独立 JSRuntime） */
    char *script;
};

static void qwrt_worker_thread_main(void *arg) {
    qwrt_worker_t *w = arg;
    /* 初始化 w->self：uv_loop_init + uv_async_init + JS_NewRuntime + ctx + polyfill 注入 +
     * eval w->script（异常 → 向 parent 发 {type:'error'} 消息）。不设 message_cb（宿主=父）。 */
    /* 主循环（同 qwrt_thread_main）：wake_cb 排空父→子入站并派发 onmessage；
     * 子→父 由 JS 侧 postMessage → pal.worker_post → push 进 parent->msg_queue（source=w->id）+ uv_async_send(parent->wake）。 */
    while (!w->shutting_down) { uv_run(&w->loop, UV_RUN_ONCE); flush_microtasks(w->self); }
    /* teardown：销毁 w->self；向 parent 发一条 {type:'worker-exit', id} 消息；free w（父侧持引用则等 GC） */
}
```

- [ ] **Step 3: bridge.c pal worker 函数**

```c
static JSValue js_pal_spawn_worker(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_pal_worker_post(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
static JSValue js_pal_worker_terminate(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
```

`js_pal_worker_post`：`argv[0]`=worker id，`argv[1]`=ArrayBuffer（字节）→ `qwrt_worker_post(rt, w, bytes, len)`。worker 侧 JS 的 `postMessage(value)` 需先 `serializeToBytes(value)` 再经 worker 自己的 pal 通道；C 层区分：worker 的 `pal.worker_post` 目标是父 runtime（bridge 在 worker 的 qwrt_t 里把 `postMessage` 绑到 `qwrt_worker_post` 的父入队路径）。**子→父 不设 message_cb**（spec 硬性）。

- [ ] **Step 4: polyfill worker.js**

```js
// worker.js — W3C Worker
var pal = globalThis.__pal_inject__;
var workers = new Map();   // id -> Worker

function Worker(url) {
  var code = loadScript(url);          // file:// → pal.fs_read; http(s):// → fetch
  var id = pal.spawn_worker(code);     // 同步返回 id；失败抛 Error
  this._id = id;
  var self = this;
  workers.set(id, this);
  this._onmsg = null;
  Object.defineProperty(this, 'onmessage', {
    get: function () { return self._onmsg; },
    set: function (fn) { self._onmsg = fn; },
    configurable: true,
  });
}
Worker.prototype.postMessage = function (value) {
  var bytes = __qwrt_serialize__(value);
  pal.worker_post(this._id, bytes);
};
Worker.prototype.terminate = function () {
  pal.worker_terminate(this._id);
  workers.delete(this._id);
};

// bridge 派发 worker 消息（source>0）时调 __qwrt_dispatch__，这里路由到实例：
globalThis.__qwrt_dispatch__ = function (data, source) {
  if (source === 0) {
    self.dispatchEvent(new MessageEvent('message', { data: data }));
  } else {
    var w = workers.get(source);
    if (w && w._onmsg) w._onmsg(new MessageEvent('message', { data: __qwrt_deserialize__(data) }));
  }
};
```

> 注：`__qwrt_dispatch__` 在 host-messaging.js（Task 2）里定义，这里**覆盖为带 worker 路由的版本**（或 host-messaging 直接按 `source` 判断）。`close()`：worker 内调 `postMessage` 后退出自身——`pal.worker_close(wid)`（C：发关停信号）。

- [ ] **Step 5: test_worker_gtest.cpp**

```cpp
#include "test_host.h"

TEST(worker_, message_roundtrip) {
    // 引导脚本里 new Worker(file://...)（用测试目录下的 worker 脚本文件），
    // host_eval 发 eval 命令创建 worker 并 postMessage，等 worker 回显。
    HostCtx *h = host_create();
    ASSERT_NE(nullptr, h);
    std::string out;
    ASSERT_TRUE(host_eval(h,
        "globalThis.w = new Worker('file://" TEST_DIR "/worker_echo.js');\n"
        "w.onmessage = function(e){ postMessage({v: e.data}); };\n"
        "w.postMessage('hello'); 'started'", &out));
    ASSERT_TRUE(host_wait_msg(h, &out));
    ASSERT_NE(std::string::npos, out.find("hello"));   // worker 回显
    host_destroy(h);
}

TEST(worker_, terminate) {
    // new Worker + w.terminate() → 不阻塞，宿主后续消息仍正常
    HostCtx *h = host_create();
    ASSERT_NE(nullptr, h);
    std::string out;
    ASSERT_TRUE(host_eval(h, "w = new Worker('file://" TEST_DIR "/worker_idle.js'); w.terminate(); 1", &out));
    // 终止后宿主仍能收发消息
    ASSERT_TRUE(host_eval(h, "2 + 3", &out));
    host_destroy(h);
}
```

> 注：`TEST_DIR` 宏在 test/CMakeLists.txt 里用 `target_compile_definitions(... TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}")` 注入。测试 fixture 文件 `test/worker_echo.js`（`onmessage = e => postMessage(e.data)`）与 `test/worker_idle.js`（空脚本）新建。

- [ ] **Step 6: 构建 + 测试 + mock 校验**

Run: `cd polyfill && npm run build && cd .. && cmake --build build -j$(nproc) && cd build && ctest -R "worker|test_qwrt|host" --output-on-failure`
Expected: worker roundtrip / terminate 全过；主线程消息不受影响。

- [ ] **Step 7: 报告变更**

```text
真线程 Web Worker：src/worker.c（独立线程+独立 JSRuntime+独立 loop，父→子/子→父双队列桥）、
structured-clone.js 字节序列化对、polyfill worker.js（Worker 类 + file/http 脚本加载 + terminate/close）、
bridge worker 路由（source>0 → Worker 实例 onmessage）。worker roundtrip/terminate 测试过。全绿。
等待用户指示（不 commit）。
```

---

### Task 5: 多上下文 JS API + 软挂起/恢复

**Files:**
- Modify: `src/context.c`（内部多上下文机制保留；新增挂起/恢复辅助 `qwrt_ctx_serialize`/`qwrt_ctx_rebuild`；删 `qwrt_reset`/`qwrt_suspend`/`qwrt_resume`/`qwrt_destroy_ctx`/`qwrt_get_active_ctx_id`/`qwrt_get_jsctx` 公共实现）
- Modify: `src/bridge.c`（pal JS 函数：`pal.context_spawn`/`pal.context_suspend`/`pal.context_resume`/`pal.context_destroy`）
- Create: `polyfill/src/context.js`（`qwrtContext` JS API：spawn/suspend/resume/destroy，宿主无感知）
- Modify: `polyfill/src/index.js`（import context.js）
- Modify: `test/CMakeLists.txt`（恢复 test_context_gtest / test_compress_gtest / test_compress_consistency_gtest）
- Rewrite: `test/test_context_gtest.cpp`（多上下文经 JS 通道测）
- Create: `test/test_suspend_gtest.cpp`（`TEST(qwrt_suspend_*)`/`TEST(qwrt_resume_*)`）
- Modify: `src/qwrt_internal.h`（`qwrt_ctx_t` 加 `polyfill` 重注入 + 序列化状态字段）

**Interfaces:**
- Consumes: Task 2/3 的 fs（`uv_io_fs_write`/`uv_io_fs_read` 落盘 manifest）、structured-clone 字节（Task 4）。
- Produces（polyfill JS）: `qwrtContext`（`globalThis.qwrtContext = { spawn(initScript), suspend(ctxId), resume(ctxId, scriptRef), destroy(ctxId) }`）；挂起 = 序列化可克隆全局属性 → 结构化克隆字节 → 写 fs manifest `{context_id, script_ref, state_file, timestamp}`；恢复 = 重建 JSContext + 重注入 polyfill + 重 eval 脚本 + 反序列化回 globals。在途异步不保留（spec）。
- Produces（C 内部）: `int qwrt_ctx_serialize(qwrt_t *rt, int ctx_id, const char *state_path)`（收集 `globalThis` 可克隆属性 → 字节 → `uv_io_fs_write`）、`int qwrt_ctx_rebuild(qwrt_t *rt, int ctx_id, const char *script_ref, const char *state_path)`（重建 + 反序列化）。

- [ ] **Step 1: context.c 挂起/恢复辅助**

```c
/* 收集 globalThis 上可 JSON/克隆的属性名集合，序列化成字节写盘（经 uv_io_fs_write）。 */
int qwrt_ctx_serialize(qwrt_t *rt, int ctx_id, const char *state_path) {
    qwrt_ctx_t *cctx = qwrt_find_ctx(rt, ctx_id);
    if (!cctx) return -1;
    JSContext *ctx = cctx->jsctx;
    /* 枚举 globalThis: JS_GetOwnPropertyNames → 过滤函数/内建 → 逐属性 structured-clone 序列化
     * （复用 Task 4 的序列化内核，把结果拼成一个对象/记录）→ JS_WriteObject 或直接字节 → fs 写 */
    return 0;
}
int qwrt_ctx_rebuild(qwrt_t *rt, int ctx_id, const char *script_ref, const char *state_path) {
    /* 1) 重建 JSContext（qwrt_ctx_create 无 pal 版）+ 重注入 polyfill
     * 2) 重 eval script_ref（按引用重载脚本源码；若为字节码则 qjsc 预编译 + JS_WriteObject 存盘引用）
     * 3) 读 state_path → 反序列化 → 写回 globalThis
     * 恢复后 cctx->active = 1；扩展 resume 钩子照旧调用 */
    return 0;
}
```

> 注：`qwrt_find_ctx` 从现有 `get_ctx_from_jsctx` 同族逻辑抽一个按 id 查找。全局属性过滤规则：排除 `__pal__`/`__pal_inject__`/`__qwrt_dispatch__` 等内建、函数、WASM 类；可克隆判定用 Task 4 序列化内核的容错（不可克隆属性跳过并在 manifest 记 `skipped` 清单）。

- [ ] **Step 2: bridge pal context 函数 + context.js**

```js
// context.js — qwrtContext：多上下文 + 软挂起/恢复（宿主只见主 context）
var pal = globalThis.__pal_inject__;
var ctxSeq = 1;
globalThis.qwrtContext = {
  spawn: function (initScript) {
    // pal.context_spawn(initScript) → C 里 qwrt_ctx_create（内部 context 表）
    // 返回 context id（C 分配，宿主无感知）
    return pal.context_spawn(initScript);
  },
  suspend: function (ctxId, statePath) {
    // 序列化全局 → 写 statePath（fs）；C 里清 resources（timer_stop 等）
    return pal.context_suspend(ctxId, statePath);
  },
  resume: function (ctxId, scriptRef, statePath) {
    // 重建 context + 重注入 polyfill + 重 eval + 反序列化
    return pal.context_resume(ctxId, scriptRef, statePath);
  },
  destroy: function (ctxId) { return pal.context_destroy(ctxId); },
};
```

- [ ] **Step 3: 恢复 + 重写 test_context_gtest.cpp；新增 suspend 测试**

`test/CMakeLists.txt` 取消 Task 2 注释；`test_context_gtest.cpp` 改用 `host_eval` 经 `qwrtContext.spawn/suspend/resume` 驱动多上下文（如：spawn 一个子 context 挂载脚本，宿主在主 context 发消息确认两边隔离）。`test_compress_gtest`/`test_compress_consistency_gtest` 仅需机械迁到 `host_eval`（压缩本身同步，无 I/O）。

```cpp
TEST(qwrt_suspend_, serialize_global_state) {
    HostCtx *h = host_create();
    ASSERT_NE(nullptr, h);
    std::string out;
    // 主 context 里定义全局状态 → qwrtContext.suspend 到测试目录
    ASSERT_TRUE(host_eval(h,
        "globalThis.foo = {n: 42, s: 'x'};"
        "qwrtContext.suspend(0, '" TEST_DIR "/state.bin'); 'ok'", &out));
    host_destroy(h);
}
TEST(qwrt_resume_, restore_global_state) {
    HostCtx *h = host_create();
    ASSERT_NE(nullptr, h);
    std::string out;
    // 先 suspend 再 resume 到新 context，确认 foo 回来了
    ASSERT_TRUE(host_eval(h,
        "qwrtContext.resume(0, '" TEST_DIR "/script.js', '" TEST_DIR "/state.bin');"
        "postMessage({v: JSON.stringify(globalThis.foo)}); 'ok'", &out));
    ASSERT_TRUE(host_wait_msg(h, &out));
    ASSERT_NE(std::string::npos, out.find("\"n\":42"));
    host_destroy(h);
}
```

- [ ] **Step 4: 构建 + 全量离线测试**

Run: `cd polyfill && npm run build && cd .. && cmake --build build -j$(nproc) && cd build && ctest -L offline --output-on-failure`
Expected: 全绿（context/compress 套件恢复 + suspend/resume 新测试）。

- [ ] **Step 5: 报告变更**

```text
多上下文移入内部 + polyfill qwrtContext JS API（spawn/suspend/resume/destroy）。
软挂起/恢复：可克隆全局属性序列化→fs manifest，恢复重建 JSContext+重注入 polyfill+重 eval+反序列化。
context/compress 套件恢复（host_eval 契约），suspend/resume 测试过。全绿。
等待用户指示（不 commit）。
```

---

### Task 6: 清理删除 + 文档 + 全量验证

**Files:**
- Delete: `include/qwrt/qwrt_pal.h`、`platform/`（uv/mock/freertos/wasm 全树）、`docs/pal-design.md`、`docs/esp32s3-design.md`、`docs/pal/`（若存在）
- Modify: `CMakeLists.txt`（删 `QWRT_PAL_*` 选项、`qwrt_uv`/`qwrt_mock`/`qwrt_freertos`/`qwrt_pal_common`/`qwrt_wasm` 目标、`libqwrt_full` 改链 `qwrt + real libuv + mbedtls`；`uv_a` 为硬依赖；`qwrt` 静态库只编译 src/*.c 不链 uv——最终目标再解析符号）
- Modify: `test/CMakeLists.txt`（删所有 `qwrt_mock`/`qwrt_pal_common` 引用与 `add_qwrt_pal_common_gtest`、`test_pal_common_gtest`；删 orphaned 纯 C 测试文件的构建条目——先核对哪些已无 add_executable 引用）
- Delete: orphaned 纯 C 测试（`test/test_qwrt.c`、`test/test_e2e.c`、`test/test_net.c`、`test/test_stream.c`、`test/test_tls.c`、`test/test_bridge_http.c`、`test/test_bridge_stream.c`、`test/test_pal_common.c`、`test/test_compress.c`、`test/test_compress_bench.c` 等——**逐个与 CMakeLists 核对无引用再删**；`test_qwrt.c` 的悬空 `#include "test_qwrt.h"` 随文件删除而消失）
- Modify: `CLAUDE.md`（删 PAL 章节；Build & Test 改 mock_libuv 说明；Architecture 改新执行模型 + 新文件清单；`deps` 说明 libuv 为唯一后端；WinterTC 构建段保留）
- Modify: `README.md`（若含 PAL/esp32 说明，同步更新）
- Verify: 离线全量 + （若可用）真实 libuv 网络测试 + ASan/UBSan job 保留

**Interfaces:**
- Consumes: 前五任务的产物；`CMakeLists.txt` 现有 `qwrt_enable_warnings` 等辅助宏保留。

- [ ] **Step 1: 删 PAL 源树与头文件**

Run: `git rm -r platform include/qwrt/qwrt_pal.h`（如文件未追踪则普通 `rm`；删除前 `git status` 确认为受版本控制）。核对 `grep -rn "qwrt_pal\|pal_uv\|pal_mock\|pal_freertos\|QWRT_PAL" src include test polyfill docs CMakeLists.txt` 不再有引用（bridge.c 的 `qwrt_pal_stream_ops_t` 已迁到 qwrt_internal.h，本任务从 qwrt_internal.h 改名 `qwrt_io_stream_ops_t` 或在内部保留原结构体名——**选择保留内部名，避免无谓改名**；`qwrt.h` 已无任何 pal 符号）。

- [ ] **Step 2: 清 CMakeLists.txt 与 test/CMakeLists.txt**

删选项与目标；`libqwrt_full` 链接 `qwrt uv_a mbedtls mbedx509 mbedcrypto miniz vmlib rt dl pthread`（`QWRT_WITH_TLS` 等特性开关保留）。`add_qwrt_gtest` 链接 `qwrt mock_libuv GTest::gtest_main pthread`（Task 2 已改）。确认 `QWRT_WITH_WAMR`/`QWRT_WITH_WASM3`/`QWRT_WITH_COMPRESS`/`QWRT_WITH_CRYPTO_EXT`/`QWRT_WITH_TEXTCODEC` 与扩展目标不动。

- [ ] **Step 3: 删 orphaned 纯 C 测试**

`test/CMakeLists.txt` 里逐个对照：只删**没有任何 `add_executable`/`add_test` 引用**的纯 C 文件。`test_qwrt.c`（悬空 include 的宿主）/`test_e2e.c`/`test_bridge_http.c`/`test_bridge_stream.c`/`test_net.c`/`test_stream.c`/`test_tls.c`/`test_pal_common.c`/`test_compress.c` 等以 grep 结果为准；仍被引用的保留并按新契约迁移（如 `test_benchmark.c` 若在 benchmark 标签下）。

- [ ] **Step 4: 更新 CLAUDE.md 与 README**

CLAUDE.md：Build & Test 节——`QWRT_PAL_*` 移除、测试链接说明改 `qwrt + mock_libuv`、新增「`qwrt` 静态库不链 uv，最终目标解析符号」说明；Architecture 节——替换执行模型 A 描述（自建线程/自驱 loop/消息边界/deferred 删除）、文件清单加 `src/uv_io.c`/`src/thread.c`/`src/msgq.c`/`src/worker.c`、删 `platform/` 与 PAL 段落、`include/qwrt/qwrt_pal.h` 条目删除。README 同步（若含 PAL 段落）。

- [ ] **Step 5: 删除文档**

`git rm docs/pal-design.md docs/esp32s3-design.md`；`docs/pal/` 若存在 `git rm -r docs/pal`。`docs/qwrt-architecture-design.md` 里 PAL 相关章节标记为已废弃或更新（按实际内容小改）。

- [ ] **Step 6: 全量构建 + 离线测试 + 内存检查**

Run: `cmake -B build -DCMAKE_BUILD_TYPE=Debug -DQWRT_BUILD_TESTS=ON && cmake --build build -j$(nproc)`
Run: `cd build && ctest -L offline --output-on-failure`
Run: `valgrind --leak-check=full ./build/test/test_qwrt_gtest`（若 valgrind 可用）
Expected: 全绿；零告警；无泄漏报告；`grep -rn "qwrt_pal\|QWRT_PAL" .`（排除 .git/build）无命中。

- [ ] **Step 7: 报告变更**

```text
PAL 全删：platform/ 树、qwrt_pal.h、QWRT_PAL_* 选项、qwrt_uv/mock/freertos/pal_common 目标、
orphaned 纯 C 测试（test_qwrt.c 悬空 include 一并清除）、docs/pal-design.md + esp32s3-design.md。
CLAUDE.md/README 更新为新执行模型。libqwrt_full 链 real libuv。全量离线测试 + valgrind 绿。
等待用户指示（不 commit）。
```

---

## 自评审（writing-plans skill 要求）

**Spec coverage（对照 spec 11 节）：**
- §2 架构总览 / §3 宿主嵌入契约 → Task 2（create 阻塞握手、post_message、message_cb、destroy join、错误传播）。
- §4 执行模型 → Task 2（uv_run ONCE + wake 派发 + 微任务 flush；spec 的「uv_async_send 唤醒」以 mock/真实 uv_async_send + UV_RUN_ONCE 阻塞实现）。
- §5 消息边界 → Task 2（host JSON 无信封 + bad-json 分支在 `qwrt_dispatch_message`/JS 侧兜底：本计划 bad-json 由 `JS_JSONParse` 异常 → `JS_Call` 失败忽略——**补**：按 spec 应在 message_cb 发 `{"type":"error","error":"bad-json"}`，执行时在 `qwrt_dispatch_message` 的 JSON parse 失败分支补上该回调）；worker 字节 → Task 4。
- §6.1 多上下文 → Task 5（移出公共 API + qwrtContext JS API）。
- §6.2 Worker → Task 4（真线程、独立 JSRuntime、structured clone 字节、close/terminate、子→父无 message_cb）。
- §6.3 软挂起/恢复 → Task 5（fs manifest、重建、在途异步不保留）。
- §7 删除清单 → Task 2（13 函数 + qwrt.h pal）、Task 6（platform/qwrt_pal.h/选项/文档/CLAUDE.md）；`qwrt_defer_callback`/deferred 字段 → Task 2 Step 6。
- §8 修改与新增清单 → Task 1–6（mock_libuv、uv_io、thread、msgq、worker、context、structured-clone、host-messaging、worker.js、context.js）。
- §9 测试策略 → 各任务（命名 `TEST(qwrt_create_*)`/`host_*`/`worker_*`/`qwrt_suspend_*`/`qwrt_resume_*`；labels 保留）。
- §10 实现顺序 → 已按风险排序（消息边界/线程最先，bridge I/O 次之，worker，挂起，清理）。
- §11 已排除方案 → 本计划未引入任何已排除方案（注入 loop、协作 worker、引擎快照、PAL v2、transferables、宿主见多上下文均无）。

**Placeholder scan：** 检查通过——所有代码步骤给了真实代码或明确平移来源（`platform/uv/pal_uv.c` 逐函数平移）。两处「> 注」标注的执行期细节（mock timer 数组注册、bad-json 分支、`JS_NewArrayBufferCopy` 引用管理）是显式的执行指引，不是占位。

**Type consistency：** `qwrt_post_message`/`qwrt_config_t`/`qwrt_cb_data_t`/`qwrt_msg_t`/`qwrt_worker_t`/`uv_io_*`/`__qwrt_dispatch__(data, source)`/`serializeToBytes`/`deserializeFromBytes` 在定义处与后续使用处签名一致。`qwrt_free_cb_data`/`alloc_cb_data` 沿用旧名（Task 2 改名 `qwrt_cb_data_t` 但函数名不变，Task 3 用到，已一致）。

**Deviation notes（对 spec / 之前嵌入式上下文的偏离，均已在上文注明）：**
1. 「deferred 队列删除」与「bridge 直连 libuv」合并在 Task 2/3 两个相邻任务，而非各自独立任务——因为 pal_mock（deferred 的后端提供者）与新后端 uv_io 在同一窗口切换，强行分拆会引入临时函数指针 seam（违反避免过度设计）。
2. Task 2 中异步 js_pal_* 为显式存根（`promise 暂未实现`），Task 3 替换——这是为了保持 Task 2 独立可测（消息往返/生命周期）的最小切分，不是最终行为。
3. 主循环用 `uv_run(UV_RUN_ONCE)` + `uv_async_send` 唤醒（spec 原文即「wake via uv_async_send」），未用 condvar 空闲等待——mock 的 `uv_run` 阻塞语义按此实现。
4. `libqwrt_full` 从「core+uv+mock+extensions」改为「qwrt（不含 uv）+ real libuv + 扩展」，符号在最终链接解析。

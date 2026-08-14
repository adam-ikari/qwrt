# 标准合规补齐 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 补齐 qwrt 在 W3C Worker（错误事件流、importScripts）、W3C WASM（Streaming API）、WinterTC/JS 全局对象（测试覆盖）三个标准领域的合规差距。

**Architecture:** 功能缺口集中在 `polyfill/src/`（JS 侧 Worker/全局对象）和 `src/`（C 侧 worker.c / ext_wamr.c）两层。补齐原则：优先在 JS polyfill 层实现（成本低、可被 WPT 验证），C 层只动必要的注入/桥接。所有新功能必须有 gtest 或 WPT 测试。

**Tech Stack:** C99 + QuickJS-ng + libuv（C 层）；ES module polyfill 经 esbuild 打包（JS 层）；WPT testharness 用于 WinterTC 验证。

---

## 背景：四路审计结论（2026-08-14）

| 领域 | 现状 | 缺口 |
|------|------|------|
| WinterTC | 24 个 polyfill 模块，大部分完整 | **零测试**：crypto.subtle、URLPattern、error-events、host-messaging、FormData；**无 WPT**：encoding(atob/btoa)、storage |
| W3C Worker | 真线程实现（worker.c），postMessage/terminate/close 完整 | **错误事件流缺失**：worker 顶层异常走 `{type:'error'}` 普通消息到父 `onmessage`，标准应为 `self.onerror`/`w.onerror` + `ErrorEvent`；**importScripts 缺失** |
| W3C WASM | WAMR 默认引擎，Module/Instance/Memory/Table/Global/validate/compile/instantiate + 三个 Error 类完整 | **Streaming API 缺失**：`compileStreaming`/`instantiateStreaming` 未注册 |
| JS 全局对象 | globalThis 覆盖 60+ 对象 | 无 DOM 缺口不明显；主要缺测试 |

---

## 文件结构

- `polyfill/src/worker.js` — 父侧 Worker 类（改：error 路由、加 importScripts 垫片）
- `polyfill/src/navigator.js` — globalThis.onerror 定义（改：worker 侧 dispatch error 事件）
- `polyfill/src/message-channel.js` — MessageEvent（参考：ErrorEvent 构造）
- `polyfill/src/error-events.js` — ErrorEvent/PromiseRejectionEvent（参考）
- `src/worker.c` — worker 线程 boot 脚本注入（改：`QWRT_WORKER_BOOT_JS` 内 dispatch error；加 importScripts 原生实现可选）
- `src/ext_wamr.c` — WebAssembly 对象（改：注册 compileStreaming/instantiateStreaming）
- `polyfill/src/index.js` — setup 模块顺序（参考）
- `test/test_worker_gtest.cpp` — Worker 测试（增：error 事件用例）
- `test/wpt/` 各目录 — 新增 WPT 测试文件
- `test/CMakeLists.txt` — 新增 gtest 注册（如需）

---

### Task 1: Worker 错误事件流（W3C Worker 标准）

**目标：** worker 脚本顶层异常 → worker 侧触发 `self.onerror`，父侧触发 `w.onerror`，均收到 `ErrorEvent`（含 `message`/`filename`/`lineno`/`colno`），worker 继续存活。

**现状（根因）：**
- `src/worker.c:78` `qwrt_worker_notify_error` 构造 `{type:'error', error:<msg>}` 经 worker 的 `postMessage`（已被垫片换成结构化克隆）发给父 → 落到父 `w.onmessage`（worker.js:65-85 无 error 特判）。
- worker 侧 `globalThis.onerror` 在 navigator.js:75 定义为 EventTarget 事件处理器，但运行时从不 dispatch `'error'` 事件，是死属性。

**改动：**

1. `src/worker.c` — 顶层异常时不再走 `postMessage({type:'error'})`，改为在 worker 自己的 JSRuntime 内 dispatch 一个 `'error'` Event（若 worker 侧有 `onerror`/`addEventListener('error')` 则触发），同时保持向父发通知。最小改动：保留现有 `qwrt_worker_notify_error` 的父通知，但在 `qwrt_worker_notify_error` 内先尝试 `JS_GetPropertyStr(ctx,g,"ErrorEvent")` 构造 ErrorEvent 并 `dispatchEvent`，若构造失败回退到普通 `postMessage({type:'error'})`。

```c
/* src/worker.c — qwrt_worker_notify_error 增加本地 error 事件派发 */
static void qwrt_worker_notify_error(qwrt_t *rt, const char *msg)
{
    qwrt_ctx_t *cctx = rt->contexts[0];
    if (!cctx || !cctx->jsctx) return;
    JSContext *ctx = cctx->jsctx;
    JSValue g = JS_GetGlobalObject(ctx);

    /* 1) 本地 dispatch ErrorEvent → 触发 worker 侧 self.onerror */
    JSValue err_cls = JS_GetPropertyStr(ctx, g, "ErrorEvent");
    int dispatched = 0;
    if (JS_IsConstructor(ctx, err_cls)) {
        JSValue err_ev = JS_CallConstructor(ctx, err_cls, 0, NULL);
        if (!JS_IsException(err_ev)) {
            JS_SetPropertyStr(ctx, err_ev, "message", JS_NewString(ctx, msg ? msg : ""));
            JS_SetPropertyStr(ctx, err_ev, "filename", JS_NewString(ctx, rt->config.initial_script ? rt->config.initial_script : ""));
            JSValue args[1] = { err_ev };
            JSValue disp = JS_GetPropertyStr(ctx, g, "dispatchEvent");
            if (JS_IsFunction(ctx, disp)) {
                JSValue r = JS_Call(ctx, disp, g, 1, args);
                dispatched = !JS_IsException(r);
                JS_FreeValue(ctx, r);
            }
            JS_FreeValue(ctx, disp);
            JS_FreeValue(ctx, err_ev);
        }
    }
    JS_FreeValue(ctx, err_cls);

    /* 2) 父通知：dispatch 未消费时回退 postMessage({type:'error'})；
     *    已消费则仍发一条（父侧 onerror 与 onmessage 都应看到） */
    JSValue pm = JS_GetPropertyStr(ctx, g, "postMessage");
    JS_FreeValue(ctx, g);
    if (JS_IsFunction(ctx, pm)) {
        JSValue obj = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, obj, "type", JS_NewString(ctx, "error"));
        JS_SetPropertyStr(ctx, obj, "error", JS_NewString(ctx, msg ? msg : ""));
        JSValue args[1] = { obj };
        JSValue r = JS_Call(ctx, pm, JS_UNDEFINED, 1, args);
        JS_FreeValue(ctx, r);
        JS_FreeValue(ctx, obj);
    }
    JS_FreeValue(ctx, pm);
}
```

2. `polyfill/src/worker.js` — 父侧 `__qwrt_dispatch__`（worker id > 0 分支）识别 `{type:'error'}`：反序列化后若 `data.type === 'error'`，构造 `ErrorEvent`（或普通 Event）调 `w._onerror`（若设置），否则走 `w._onmsg`。

```js
// polyfill/src/worker.js — 父侧 inbound 路由加 error 特判
globalThis.__qwrt_dispatch__ = function (data, source) {
  if (source === 0) { hostDispatch(data, source); return; }
  var w = workers.get(source);
  if (!w) return;
  var d;
  try { d = __qwrt_deserialize__(data); }
  catch (err) { reportError(err); return; }
  var handler = (d && d.type === 'error') ? w._onerror : w._onmsg;
  if (!handler) return;
  var e;
  try {
    e = new MessageEvent('message', { data: d });
  } catch (err) { reportError(err); return; }
  try { handler.call(self, e); }
  catch (err) { reportError(err); }
};
```

并在 Worker 构造函数里补 `_onerror` 存取器（与 `_onmsg` 同款）。

3. `polyfill/src/worker.js` — Worker 类加 `onerror` getter/setter（与 `onmessage` 一致）。

```js
Object.defineProperty(this, 'onerror', {
  get: function () { return w._onerror; },
  set: function (fn) { w._onerror = fn; },
  configurable: true,
});
```

**测试：** `test/test_worker_gtest.cpp` 新增用例：worker 脚本顶层 `throw new Error('boom')`；父 `w.onerror = e => postMessage({err: e.data})`；断言宿主收到 `err` 含 `boom`，且 worker 存活（随后父仍能 `w.postMessage` 往返）。

**验收命令：**
```bash
cmake --build build --target test_worker_gtest -j$(nproc)
./build/test/test_worker_gtest
```

**提交信息：** `feat(worker): dispatch ErrorEvent on worker top-level exception (self.onerror + w.onerror)`

---

### Task 2: Worker importScripts（W3C Worker 标准）

**目标：** worker 脚本内 `importScripts('file://.../x.js')` 同步加载并执行额外脚本（file:// v1 范围内，与 `new Worker` 一致）。

**现状：** `QWRT_WORKER_BOOT_JS`（src/worker.c）只定义 `__qwrt_dispatch__` 与 `close`，无 importScripts。

**改动：**

1. `src/worker.c` — 在 `QWRT_WORKER_BOOT_JS` 宏内追加 `globalThis.importScripts` 实现。脚本字符串在当前 worker 上下文 `JS_Eval`（沿用 `qwrt_eval_internal`），文件读取走 `pal.fsReadSync`（与 Worker 构造器读取一致，含路径穿越防护）。

```c
/* src/worker.c — QWRT_WORKER_BOOT_JS 宏内追加（文件开头附近定义） */
"globalThis.importScripts = function () {                    \
   for (var i = 0; i < arguments.length; i++) {              \
     var url = String(arguments[i]);                         \
     if (url.indexOf('file://') !== 0)                       \
       throw new Error('importScripts: only file:// URLs');  \
     var code = pal.fsReadSync(url.slice(7));                \
     (0, eval)(code);                                        \
   }                                                         \
 };                                                          \
"
```

> 注意：需先确认 worker 的 `pal` 对象在 boot 阶段可用（`pal.fsReadSync` 是否存在，参考 bridge.c pal 注册表；若 worker 侧 pal 无 fs，则改为经 `qwrt_eval_internal` 由 C 侧读文件）。实现时以实际 pal 能力为准，二选一：JS `pal.fsReadSync` 或 C 侧新增 `pal.importScripts`。

2. 若 worker 侧 pal 无 fs：在 bridge.c 的 worker pal 注册处补 `fsReadSync`（复用主 context 的实现）。

**测试：** `test/test_worker_gtest.cpp` 新增：worker.js 用 `importScripts('file://<TEST_DIR>/worker_extra.js')` 定义全局 `EXTRA = 42`，主脚本 `postMessage(EXTRA)`；父断言收到 `42`。新增 fixture `test/worker_extra.js`。

**验收命令：**
```bash
cmake --build build --target test_worker_gtest -j$(nproc)
./build/test/test_worker_gtest
```

**提交信息：** `feat(worker): add importScripts for file:// scripts`

---

### Task 3: WebAssembly Streaming API（W3C WASM 标准）

**目标：** `WebAssembly.compileStreaming(source)` / `WebAssembly.instantiateStreaming(source, imports)` 可用，其中 `source` 为 Promise<Response> 或含 `arrayBuffer()` 方法的对象。

**现状：** `src/ext_wamr.c` 的 WebAssembly 对象只注册 `validate`/`compile`/`instantiate` + 构造器（约 885-945 行）。`compileStreaming`/`instantiateStreaming` 未注册。

**改动：** `src/ext_wamr.c` 新增两个 C 函数，注册到 wasm_obj：

```c
/* 取 source（Promise<Response> 或 {arrayBuffer()}）的字节 → compile/instantiate */
static JSValue wamr_wasm_compile_streaming(JSContext *ctx, JSValueConst this_val,
                                           int argc, JSValueConst *argv)
{
    JSValue src = (argc > 0) ? argv[0] : JS_UNDEFINED;
    return js_wasm_streaming_impl(ctx, src, 0); /* 0 = compile */
}
static JSValue wamr_wasm_instantiate_streaming(JSContext *ctx, JSValueConst this_val,
                                               int argc, JSValueConst *argv)
{
    JSValue src = (argc > 0) ? argv[0] : JS_UNDEFINED;
    JSValue imports = (argc > 1) ? argv[1] : JS_UNDEFINED;
    return js_wasm_streaming_impl(ctx, src, imports);
}
```

公共实现 `js_wasm_streaming_impl`：`await` source（Promise）→ 取 `.arrayBuffer()` → 返回 `wamr_wasm_compile` / `wamr_wasm_instantiate` 的结果（两者已接受 ArrayBuffer）。若 source 无 `arrayBuffer()` 方法则 reject TypeError。

注册：
```c
JS_SetPropertyStr(ctx, wasm_obj, "compileStreaming",
    JS_NewCFunction(ctx, wamr_wasm_compile_streaming, "compileStreaming", 1));
JS_SetPropertyStr(ctx, wasm_obj, "instantiateStreaming",
    JS_NewCFunction(ctx, wamr_wasm_instantiate_streaming, "instantiateStreaming", 2));
```

> 实现说明：QuickJS 无原生 async 函数构造器时，可用 `JS_NewPromiseCapability` + `JS_Call` 处理 Promise 链，或复用 polyfill 侧已存在的 `Response`（fetch.js）做 `source.arrayBuffer()`。streaming 语义可简化为"取完整字节后交给 compile/instantiate"（不要求逐块流式编译）。

**测试：** 新增 `test/test_wasm_streaming_gtest.cpp`（复用现有 WAMR 模块生成方式，参考 test 里既有 WAMR 用法或 bench_wamr_vs_node）；或在现有 gtest 内新增用例：构造 `{ arrayBuffer: () => Promise.resolve(wasmBytes) }`，`await WebAssembly.compileStreaming(src)`，断言 `instance.exports.add(1,2)===3`。在 `test/CMakeLists.txt` 用 `add_qwrt_gtest` 注册。

**验收命令：**
```bash
cmake --build build --target test_wasm_streaming_gtest -j$(nproc)
./build/test/test_wasm_streaming_gtest
```

**提交信息：** `feat(wasm): add WebAssembly.compileStreaming / instantiateStreaming`

---

### Task 4: WinterTC 测试覆盖补齐（crypto.subtle / URLPattern / error-events / host-messaging / FormData）

**目标：** 给已实现但零测试的模块补 WPT 测试，暴露并修复真实 bug。

**现状（审计）：** crypto.subtle（crypto-subtle.js，407 行，digest/HMAC/PBKDF2/importKey/generateKey/exportKey）、URLPattern（url-pattern.js，257 行）、error-events.js（54 行）、host-messaging.js、FormData（blob-file-formdata.js）均无任何测试。

**改动：** 在 `test/wpt/` 新增以下目录与文件（沿用现有 `// META: global=shell` + testharness 风格，参考 `test/wpt/timers/setTimeout-basic.any.js`）：

- `test/wpt/crypto/subtle-digest.any.js` — SHA-256 digest 已知向量；`importKey('raw')` + HMAC sign/verify；PBKDF2 deriveBits 向量。
- `test/wpt/url-pattern/urlpattern-basic.any.js` — `new URLPattern('/books/:id')` exec/match、`{search}` 组、`test()`。
- `test/wpt/error-events/errorevent-basic.any.js` — `new ErrorEvent('error',{message:'x'}).message === 'x'`；PromiseRejectionEvent 构造。
- `test/wpt/host-messaging/` — postMessage→message_cb 往返（经 gtest 更合适，故本任务用 gtest：`test/test_host_messaging_gtest.cpp` 新增 roundtrip + 结构化克隆覆盖）。
- `test/wpt/blob-formdata/formdata-basic.any.js` — `new FormData()` append/get/entries；Blob 构造/文本。

若测试暴露实现 bug（如 URLPattern 组语法、crypto.subtle 向量不符），修复对应 polyfill 源文件。

**测试运行：** WPT 由 `wpt_runner` 扫描 `test/wpt`（docs/dev/testing.md 的验证方式）。

**验收命令：**
```bash
cmake --build build --target wpt_runner -j$(nproc)
./build/test/wpt_runner test/wpt
```

**提交信息：** `test(wintertc): add WPT coverage for crypto.subtle / URLPattern / error-events / FormData`

---

### Task 5: 汇总验证 + 文档

**目标：** 全部离线测试 + WPT 通过，CHANGELOG/README 更新。

**改动：**
- `CHANGELOG.md` Unreleased 段追加三项功能条目（worker error 事件、importScripts、wasm streaming）。
- `docs/dev/testing.md` 若有结果表格则更新 WPT 计数。
- 全量回归：offline ctest + wpt_runner + test262_runner。

**验收命令：**
```bash
cmake --build build -j$(nproc)
cd build && ctest -L offline --output-on-failure
./build/test/wpt_runner test/wpt
./build/test/test262_runner test/test262/test
```

**提交信息：** `docs: update CHANGELOG for worker/wasm compliance work`

---

## 任务依赖

```
Task 1 (worker error) ──┐
Task 2 (importScripts) ──┤──> Task 5 (汇总)
Task 3 (wasm streaming) ─┤
Task 4 (wintertc tests) ─┘
```
Task 1-4 相互独立，可并行或按序执行；Task 5 依赖前四个全部完成。

## 不在范围

- Worker transferable objects（MessagePort/ArrayBuffer transfer 列表）——v1 结构化克隆已够用，后续迭代。
- `SharedWorker`、`ServiceWorker`——超出单文件 Worker 目标。
- WASM 真·流式编译（逐块编译）——语义等价即可。
- DOM 相关全局对象（document/window 等）——项目明确无 DOM。

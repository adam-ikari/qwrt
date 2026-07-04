# qwrt — 可嵌入 QuickJS + WinterCG 运行时 设计文档

**版本:** 2.0.0
**日期:** 2026-05-29
**状态:** 设计阶段
**替代:** 2026-05-29 PAL 设计文档 v1.0.0

---

## 1. 设计原则

| 原则               | 定义                                | 实现方式                                     |
| ------------------ | ----------------------------------- | -------------------------------------------- |
| **最小 C 层**      | C 只做 PAL 原语注册，不实现业务 API | WinterCG API 用 JS polyfill 实现             |
| **PAL 回调式异步** | PAL async 操作通过回调通知完成      | 桥接层将回调转换为 JS Promise                |
| **驱动无关**       | PAL 不涉及事件循环                  | 驱动由 PAL 实现层负责（libuv/wasi/自定义）   |
| **对标 WASI**      | PAL 类似 WASI 的 syscall 级接口     | 宿主提供实现，运行时只消费接口               |
| **可嵌入**         | 宿主完全控制运行时生命周期          | qwrt_create/tick/eval/destroy API            |
| **Web 标准**       | 用户 JS 看到 WinterCG 标准 API      | polyfill bundle 基于 `pal.*` 原语封装        |
| **可复用**         | 独立于任何应用，不绑定特定品牌     | qwrt 是通用运行时，上层应用在其上构建 |

---

## 2. 架构总览

```
┌─────────────────────────────────────────────────────────────┐
│                    用户 JS 程序                              │
│   fetch() / setTimeout() / console.log() / fs.readFile()    │
└────────────────────────┬────────────────────────────────────┘
                         │ 调用 WinterCG 标准 API
                         ▼
┌─────────────────────────────────────────────────────────────┐
│              WinterCG Polyfill Bundle (JS)                   │
│                                                             │
│   fetch()          → pal.httpRequest()                       │
│   setTimeout()     → pal.timerStart() / pal.timerStop()      │
│   console.log()    → pal.log()                               │
│   performance.now()→ pal.timeNow()                           │
│   fs.readFile()    → pal.fsRead()                            │
│   TextEncoder      → pal.textEncode() / pal.textDecode()     │
│   URL              → 纯 JS 实现                              │
│   EventTarget      → 纯 JS 实现                              │
│   AbortController  → 纯 JS 实现                              │
│   atob / btoa      → 纯 JS 实现                              │
│   crypto           → pal.randomBytes()                       │
│   storage.get()    → pal.storageGet()                        │
└────────────────────────┬────────────────────────────────────┘
                         │ 调用内部 PAL 原语
                         ▼
┌─────────────────────────────────────────────────────────────┐
│              qwrt C 层 (薄桥接 ~500行)                       │
│                                                             │
│   qwrt_pal_t 函数指针 → pal 内部 JS 对象（不挂 globalThis）   │
│   async PAL 回调     → JS Promise resolve/reject            │
│   JS_ExecutePendingJob ← qwrt_tick() 触发                   │
└────────────────────────┬────────────────────────────────────┘
                         │ 调用
                         ▼
┌─────────────────────────────────────────────────────────────┐
│              qwrt_pal_t (PAL C 接口)                          │
│   http_request / fs_* / storage_* / timer / time / log / mem│
└────────────────────────┬────────────────────────────────────┘
                         │ 实现
                         ▼
┌─────────────────────────────────────────────────────────────┐
│              PAL 实现 (宿主提供)                              │
│   libuv │ wasi+事件循环 │ curl+epoll │ mock                │
└─────────────────────────────────────────────────────────────┘
```

**与上层应用的关系：**

```
qwrt (可嵌入 C 库)         ← 通用运行时，任何项目可复用
  │
  ├── QuickJS-NG            ← JS 引擎
  ├── PAL (qwrt_pal_t)       ← 平台抽象层
  ├── C 桥接层              ← PAL → pal 内部对象（闭包注入）
  └── WinterCG polyfill     ← pal.* → 标准 Web API
      │
      ▼
上层应用 (JS 层)            ← 用 qwrt 提供的 Web 标准 API 开发的应用层
  │
  ├── LLMClient             ← 基于 fetch()
  ├── Memory                ← 基于 storage API
  ├── Tool / Skill / Agent  ← 业务逻辑
  └── ...
```

qwrt 是独立的运行时，不知道上层应用的存在。

---

## 3. 三层 JS 接口

### 3.1 第一层：PAL 原语（qwrt C 层注册，对用户 JS 完全隐藏）

C 桥接层将 qwrt_pal_t 函数注册到一个内部 `pal` 对象，通过闭包注入给 polyfill bundle。
PAL 原语**不挂载到 `globalThis`**，用户 JS 无法访问或绕过。

```javascript
// pal 对象仅在 polyfill 闭包内可见
// 用户 JS 执行时，globalThis 上没有任何 PAL 原语引用
pal.httpRequest(url, method, headers_json, body) → Promise<string>
  // 返回 JSON: {"status":200,"headers":{...},"body":"..."}

pal.fsRead(path) → Promise<string>
pal.fsWrite(path, data) → Promise<void>
pal.fsExists(path) → Promise<boolean>
pal.fsRemove(path) → Promise<void>
pal.fsList(path) → Promise<string>  // JSON array

pal.storageGet(key) → Promise<string|null>
pal.storageSet(key, value) → Promise<void>
pal.storageDel(key) → Promise<void>

pal.timerStart(delay_ms, repeat) → number  // handle
pal.timerStop(handle) → void

pal.timeNow() → number  // ms timestamp
pal.log(level, msg) → void
```

### 3.2 第二层：WinterCG 标准 API（polyfill bundle 实现）

基于 `pal.*` 原语封装的标准 Web API。用户 JS 通过这些 API 编程。

| WinterCG API             | 底层原语              | 实现方式    |
| ------------------------ | --------------------- | ----------- |
| `fetch(url, init)`       | `pal.httpRequest`     | JS polyfill |
| `setTimeout(fn, ms)`     | `pal.timerStart/Stop` | JS polyfill |
| `clearTimeout(id)`       | `pal.timerStop`       | JS polyfill |
| `setInterval(fn, ms)`    | `pal.timerStart`      | JS polyfill |
| `clearInterval(id)`      | `pal.timerStop`       | JS polyfill |
| `console.log/warn/error` | `pal.log`             | JS polyfill |
| `performance.now()`      | `pal.timeNow`         | JS polyfill |
| `TextEncoder/Decoder`    | `pal.textEncode/Decode` | JS polyfill |
| `URL / URLSearchParams`  | 无                    | 纯 JS       |
| `Event / EventTarget`    | 无                    | 纯 JS       |
| `AbortController/Signal` | 无                    | 纯 JS       |
| `ReadableStream`         | 无                    | 纯 JS       |
| `atob / btoa`            | 无                    | 纯 JS       |
| `crypto.getRandomValues` | `pal.randomBytes`     | JS polyfill |

### 3.3 第三层：扩展 API（非标准但实用）

WinterCG 没有定义 fs 和 storage 的标准。提供接近现有生态的 API：

| 扩展 API                   | 底层原语           | 风格参考      |
| -------------------------- | ------------------ | ------------- |
| `fs.readFile(path)`        | `pal.fsRead`       | node:fs async |
| `fs.writeFile(path, data)` | `pal.fsWrite`      | node:fs async |
| `fs.exists(path)`          | `pal.fsExists`     | node:fs async |
| `fs.readdir(path)`         | `pal.fsList`       | node:fs async |
| `fs.unlink(path)`          | `pal.fsRemove`     | node:fs async |
| `storage.get(key)`         | `pal.storageGet`   | 简洁异步 KV   |
| `storage.set(key, value)`  | `pal.storageSet`   | 简洁异步 KV   |
| `storage.delete(key)`      | `pal.storageDel`   | 简洁异步 KV   |

---

## 4. qwrt_pal_t C 接口

### 4.1 回调类型

```c
typedef void (*qwrt_pal_cb_t)(void *user_data, int status,
                              const char *data, size_t data_len);
```

- `status=0` 成功，`data` 为结果
- `status<0` 错误，`data` 为错误信息
- HTTP 回调 `data` 为 `{"status":200,"headers":{...},"body":"..."}`

### 4.2 完整接口

```c
typedef struct qwrt_pal_t {
    void *user_data;

    /* 异步操作 */
    void (*http_request)(struct qwrt_pal_t *pal,
                         const char *url, const char *method,
                         const char *headers,
                         const char *body, size_t body_len,
                         qwrt_pal_cb_t cb, void *cb_data);

    void (*fs_read)(struct qwrt_pal_t *pal, const char *path,
                    qwrt_pal_cb_t cb, void *cb_data);
    void (*fs_write)(struct qwrt_pal_t *pal, const char *path,
                     const char *data, size_t data_len,
                     qwrt_pal_cb_t cb, void *cb_data);
    void (*fs_exists)(struct qwrt_pal_t *pal, const char *path,
                      qwrt_pal_cb_t cb, void *cb_data);
    void (*fs_remove)(struct qwrt_pal_t *pal, const char *path,
                      qwrt_pal_cb_t cb, void *cb_data);
    void (*fs_list)(struct qwrt_pal_t *pal, const char *path,
                    qwrt_pal_cb_t cb, void *cb_data);

    void (*storage_get)(struct qwrt_pal_t *pal, const char *key,
                        qwrt_pal_cb_t cb, void *cb_data);
    void (*storage_set)(struct qwrt_pal_t *pal, const char *key,
                        const char *val, size_t val_len,
                        qwrt_pal_cb_t cb, void *cb_data);
    void (*storage_del)(struct qwrt_pal_t *pal, const char *key,
                        qwrt_pal_cb_t cb, void *cb_data);

    void* (*timer_start)(struct qwrt_pal_t *pal, uint64_t delay_ms,
                         int repeat, qwrt_pal_cb_t cb, void *cb_data);
    void (*timer_stop)(struct qwrt_pal_t *pal, void *handle);

    /* 同步操作 */
    uint64_t (*time_now)(struct qwrt_pal_t *pal);
    void (*log)(struct qwrt_pal_t *pal, int level, const char *msg);
    void* (*mem_alloc)(struct qwrt_pal_t *pal, size_t size);
    void (*mem_free)(struct qwrt_pal_t *pal, void *ptr);
} qwrt_pal_t;
```

### 4.3 错误码

| 错误码 | 含义          |
| ------ | ------------- |
| 0      | 成功          |
| -1     | 通用错误      |
| -2     | 未找到/不存在 |
| -3     | 权限不足      |
| -4     | 超时          |
| -5     | 网络错误      |
| -6     | 参数无效      |

### 4.4 PAL 能力分类

| 类别   | 函数                | 必须真实实现 |      可模拟      |
| ------ | ------------------- | :----------: | :--------------: |
| HTTP   | http_request        |      Y       |        -         |
| 时间   | time_now            |      Y       |        -         |
| 定时器 | timer_start/stop    |      Y       |        -         |
| 日志   | log                 |      Y       |        -         |
| 存储   | storage_get/set/del |      -       | Y (内存 HashMap) |
| 文件   | fs\_\*              |      -       |  Y (内存/no-op)  |
| 内存   | mem_alloc/free      |      -       | Y (malloc/free)  |

缺失的平台能力由 PAL 实现层模拟。PAL 函数指针全部必须非 NULL。

---

## 5. qwrt 公共 API

### 5.1 配置结构体

```c
typedef struct qwrt_config_t {
    const qwrt_pal_t *pal;       /* PAL 实现（必须） */
    const uint8_t *polyfill;    /* WinterCG polyfill bundle（可选，内含默认） */
    size_t polyfill_len;
    int debug;                  /* 0=正常, 1=打印JS错误栈 */
} qwrt_config_t;
```

### 5.2 生命周期 API

```c
/**
 * 创建 qwrt 运行时
 *
 * 1. 创建 QuickJS 运行时
 * 2. 创建内部 pal JS 对象（不挂 globalThis）
 * 3. 加载 WinterCG polyfill bundle
 *
 * @return 运行时实例, NULL=失败
 */
qwrt_t *qwrt_create(const qwrt_config_t *config);

/** 销毁运行时，释放所有资源 */
void qwrt_destroy(qwrt_t *rt);

/**
 * 处理待执行任务（Promise 回调、定时器回调等）
 *
 * 宿主在事件循环中调用。PAL async 回调触发后，
 * 必须调用 qwrt_tick 处理对应的 Promise resolve。
 *
 * @return 待处理任务数, 0=空闲
 */
int qwrt_tick(qwrt_t *rt);

/**
 * 执行 JS 代码
 *
 * @param code     JS 代码
 * @param result   输出结果（需 qwrt_free）
 * @return 0=成功, <0=错误
 */
int qwrt_eval(qwrt_t *rt, const char *code, char **result);

/**
 * 调用 JS 函数
 *
 * @param func       函数路径（如 "myApp.session.chat"）
 * @param args_json  参数 JSON
 * @param result     输出结果（需 qwrt_free）
 * @return 0=成功, <0=错误
 */
int qwrt_call(qwrt_t *rt, const char *func,
              const char *args_json, char **result);

/** 释放 qwrt_eval/qwrt_call 返回的内存 */
void qwrt_free(void *ptr);
```

### 5.3 异步协作模式

`qwrt_eval` / `qwrt_call` 中如果 JS 代码发起了异步操作（如 `fetch()`），
结果不会立即可用。宿主需要驱动事件循环：

```c
// 发起异步调用
char *result = NULL;
qwrt_call(rt, "myAsyncFunc", "{}", &result);

// result == NULL 表示异步进行中
while (result == NULL) {
    // 驱动 PAL 实现（如 uv_run）
    my_event_loop_step();
    // 处理 JS Promise 回调
    qwrt_tick(rt);
    // qwrt_call 会在 Promise resolve 后填充 result
}

printf("Result: %s\n", result);
qwrt_free(result);
```

---

## 6. C 桥接层实现

C 桥接层负责：1）将 qwrt_pal_t 函数注册到一个内部 `pal` JS 对象（**不挂载到 `globalThis`**）；
2）通过闭包注入给 polyfill bundle；3）管理 PAL 回调 → JS Promise 的转换。

PAL 原语对用户 JS **完全隐藏**——用户无法绕过 WinterCG API 直接调原语。

### 6.1 创建内部 pal 对象

PAL 函数注册到一个内部 JS 对象，不挂载到 `globalThis`：

```c
// 创建内部 pal 对象（不挂到 globalThis）
static JSValue qwrt_create_pal_object(qwrt_t *rt) {
    JSContext *ctx = rt->ctx;
    JSValue pal = JS_NewObject(ctx);

    // 同步原语
    JS_SetPropertyStr(ctx, pal, "timeNow",
        JS_NewCFunction(ctx, js_pal_time_now, "timeNow", 0));
    JS_SetPropertyStr(ctx, pal, "log",
        JS_NewCFunction(ctx, js_pal_log, "log", 2));
    JS_SetPropertyStr(ctx, pal, "timerStop",
        JS_NewCFunction(ctx, js_pal_timer_stop, "timerStop", 1));

    // 异步原语（返回 Promise）
    JS_SetPropertyStr(ctx, pal, "httpRequest",
        JS_NewCFunction(ctx, js_pal_http_request, "httpRequest", 4));
    JS_SetPropertyStr(ctx, pal, "fsRead",
        JS_NewCFunction(ctx, js_pal_fs_read, "fsRead", 1));
    JS_SetPropertyStr(ctx, pal, "fsWrite",
        JS_NewCFunction(ctx, js_pal_fs_write, "fsWrite", 2));
    // ... 其余 pal 属性注册

    return pal;  // 返回给 qwrt_create，不挂到 globalThis
}
```

### 6.2 闭包注入给 polyfill

`qwrt_create` 将 `pal` 对象作为闭包参数注入 polyfill bundle：

```c
qwrt_t *qwrt_create(const qwrt_config_t *config) {
    qwrt_t *rt = calloc(1, sizeof(*rt));

    // 1. 创建 QuickJS 运行时
    rt->rt = JS_NewRuntime();
    rt->ctx = JS_NewContext(rt->rt);
    JS_SetContextOpaque(rt->ctx, rt);

    // 2. 创建内部 pal 对象
    JSValue pal = qwrt_create_pal_object(rt);

    // 3. 执行 polyfill bundle — pal 对象作为闭包参数注入
    //    polyfill.js 格式: (function(pal){ ... })(__PAL_INJECT__);
    //    C 层将 __PAL_INJECT__ 替换为 pal 对象
    char *polyfill_code = inject_pal_into_polyfill(
        config->polyfill, config->polyfill_len, pal);

    JSValue result = JS_Eval(rt->ctx, polyfill_code,
                              strlen(polyfill_code),
                              "<qwrt-polyfill>", JS_EVAL_TYPE_GLOBAL);
    // polyfill 执行完毕后，pal 只存在于闭包中

    JS_FreeValue(rt->ctx, pal);
    JS_FreeValue(rt->ctx, result);
    free(polyfill_code);

    // 4. 用户 JS 执行时，globalThis 上没有 PAL 原语引用
    return rt;
}
```

### 6.3 同步原语实现

```c
// pal.timeNow() → number
static JSValue js_pal_time_now(JSContext *ctx, JSValue this_val,
                                int argc, JSValue *argv) {
    qwrt_t *rt = JS_GetContextOpaque(ctx);
    uint64_t now = rt->pal->time_now(rt->pal);
    return JS_NewFloat64(ctx, (double)now);
}

// pal.log(level, msg) → undefined
static JSValue js_pal_log(JSContext *ctx, JSValue this_val,
                           int argc, JSValue *argv) {
    qwrt_t *rt = JS_GetContextOpaque(ctx);
    int level = JS_ToInt32(ctx, argv[0]);
    const char *msg = JS_ToCString(ctx, argv[1]);
    rt->pal->log(rt->pal, level, msg);
    JS_FreeCString(ctx, msg);
    return JS_UNDEFINED;
}
```

### 6.4 异步原语实现

异步 PAL 函数创建 Promise，回调时 resolve：

```c
// pal.httpRequest(url, method, headers, body) → Promise<string>
static JSValue js_pal_http_request(JSContext *ctx, JSValue this_val,
                                    int argc, JSValue *argv) {
    qwrt_t *rt = JS_GetContextOpaque(ctx);

    // 创建 Promise
    JSValue resolving_funcs[2];
    JSValue promise = JS_NewPromiseCapability(ctx, resolving_funcs);

    // 构造回调上下文
    pal_cb_data_t *cbd = calloc(1, sizeof(*cbd));
    cbd->ctx = ctx;
    cbd->resolve = resolving_funcs[0];
    cbd->reject = resolving_funcs[1];
    cbd->rt = rt;

    // 提取参数
    const char *url = JS_ToCString(ctx, argv[0]);
    const char *method = JS_ToCString(ctx, argv[1]);
    const char *headers = JS_ToCString(ctx, argv[2]);
    const char *body = argc > 3 ? JS_ToCString(ctx, argv[3]) : NULL;
    size_t body_len = body ? strlen(body) : 0;

    // 调用 PAL
    rt->pal->http_request(rt->pal, url, method, headers,
                           body, body_len, pal_async_cb, cbd);

    JS_FreeCString(ctx, url);
    JS_FreeCString(ctx, method);
    JS_FreeCString(ctx, headers);
    if (body) JS_FreeCString(ctx, body);
    JS_FreeValue(ctx, resolving_funcs[0]);
    JS_FreeValue(ctx, resolving_funcs[1]);

    return promise;
}
```

### 6.5 PAL 统一回调

```c
static void pal_async_cb(void *user_data, int status,
                          const char *data, size_t data_len) {
    pal_cb_data_t *cbd = user_data;
    JSContext *ctx = cbd->ctx;

    JSValue arg;
    if (status == 0) {
        arg = data ? JS_NewStringLen(ctx, data, data_len) : JS_UNDEFINED;
        JS_Call(ctx, cbd->resolve, JS_UNDEFINED, 1, &arg);
    } else {
        arg = JS_NewStringLen(ctx, data ? data : "unknown error",
                              data_len ? data_len : strlen(data ? data : "unknown error"));
        JS_Call(ctx, cbd->reject, JS_UNDEFINED, 1, &arg);
    }
    JS_FreeValue(ctx, arg);

    cbd->rt->has_pending_jobs = 1;
    free(cbd);
}
```
```

---

## 7. WinterCG Polyfill Bundle

### 7.1 结构

polyfill 是一个 JS 文件（`qwrt-polyfill.js`），由 qwrt*create 加载时执行。
它基于 `\_\_pal*\*` 原语实现 WinterCG 标准 API。

### 7.2 示例：fetch()

```javascript
// 基于 pal.httpRequest 实现 fetch()
globalThis.fetch = async function (resource, options = {}) {
  const url = typeof resource === "string" ? resource : resource.url;
  const method = options.method || "GET";
  const headers = {};
  if (options.headers) {
    if (options.headers instanceof Headers) {
      options.headers.forEach((v, k) => (headers[k] = v));
    } else {
      Object.assign(headers, options.headers);
    }
  }
  if (options.body && !headers["Content-Type"]) {
    headers["Content-Type"] = "application/json";
  }

  const response_json = await pal.httpRequest(
    url,
    method,
    JSON.stringify(headers),
    typeof options.body === "string"
      ? options.body
      : JSON.stringify(options.body),
  );
  const resp = JSON.parse(response_json);

  return new Response(resp.body, {
    status: resp.status,
    headers: new Headers(resp.headers),
  });
};
```

### 7.3 示例：setTimeout()

```javascript
// 基于 pal.timerStart/Stop 实现 setTimeout/clearTimeout
const _timer_callbacks = new Map();
let _timer_next_id = 1;

globalThis.setTimeout = function (fn, delay, ...args) {
  const id = _timer_next_id++;
  _timer_callbacks.set(id, { fn, args, repeat: false });
  pal.timerStart(delay, 0); // 0 = 不重复
  return id;
};

globalThis.setInterval = function (fn, delay, ...args) {
  const id = _timer_next_id++;
  _timer_callbacks.set(id, { fn, args, repeat: true });
  pal.timerStart(delay, 1); // 1 = 重复
  return id;
};

globalThis.clearTimeout = function (id) {
  _timer_callbacks.delete(id);
  pal.timerStop(id);
};

globalThis.clearInterval = globalThis.clearTimeout;

// 定时器回调由 C 层在 pal.timerStart 回调中触发
// C 层需要调用 js 回调函数（通过 qwrt 内部注册的 pal.onTimer(handle)）
```

### 7.4 示例：console

```javascript
globalThis.console = {
  log: (...args) => pal.log(1, args.map(String).join(" ")),
  warn: (...args) => pal.log(2, args.map(String).join(" ")),
  error: (...args) => pal.log(3, args.map(String).join(" ")),
  debug: (...args) => pal.log(0, args.map(String).join(" ")),
};
```

### 7.5 示例：fs

```javascript
// node:fs 风格异步 API
globalThis.fs = {
  readFile: (path) => pal.fsRead(path),
  writeFile: (path, data) => pal.fsWrite(path, data),
  exists: (path) => pal.fsExists(path),
  readdir: async (path) => JSON.parse(await pal.fsList(path)),
  unlink: (path) => pal.fsRemove(path),
};
```

### 7.6 示例：storage

```javascript
// 简洁异步 KV 存储
globalThis.storage = {
  get: (key) => pal.storageGet(key),
  set: (key, value) => pal.storageSet(key, value),
  delete: (key) => pal.storageDel(key),
};
```

### 7.7 纯 JS 实现（无需 PAL）

以下 API 纯 JS 实现，不依赖 PAL 原语：

- `URL` / `URLSearchParams` — 参考现有 polyfill
- `Event` / `EventTarget` — 标准 DOM 事件模型
- `AbortController` / `AbortSignal` — 取消机制
- `ReadableStream` / `WritableStream` — 流 API
- `atob` / `btoa` — Base64 编解码
- `Headers` / `Request` / `Response` — Fetch API 类型

### 7.8 参考移植

从 txiki.js 参考移植的模块：

| txiki.js 模块        | 移植方式                        | 目标                  |
| -------------------- | ------------------------------- | --------------------- |
| `js/url.js`          | 直接复用                        | URL / URLSearchParams |
| `js/event-target.js` | 参考                            | EventTarget           |
| `js/abort-signal.js` | 参考                            | AbortController       |
| `js/fetch.js`        | 参考，改用 `pal.httpRequest`  | fetch()               |
| `mod_fs.c`           | 参考 C 逻辑，改写为 JS polyfill | fs.\*                 |
| `timers.c`           | 参考 C 逻辑，改写为 JS polyfill | setTimeout            |

---

## 8. PAL 实现示例

### 8.1 libuv PAL（REPL 使用）

```c
typedef struct {
    uv_loop_t *loop;
    qwrt_hashmap_t *store;  // storage 模拟
} pal_uv_t;

static void pal_uv_http_request(qwrt_pal_t *pal, const char *url,
                                 const char *method, const char *headers,
                                 const char *body, size_t body_len,
                                 qwrt_pal_cb_t cb, void *cb_data) {
    http_work_t *w = calloc(1, sizeof(*w));
    w->url = strdup(url);
    w->method = strdup(method);
    w->headers = strdup(headers);
    w->body = body ? strndup(body, body_len) : NULL;
    w->body_len = body_len;
    w->cb = cb;
    w->cb_data = cb_data;
    w->pal = pal;
    uv_queue_work(((pal_uv_t*)pal->user_data)->loop,
                   &w->req, http_work_cb, http_done_cb);
}

static uint64_t pal_uv_time_now(qwrt_pal_t *pal) {
    return (uint64_t)(uv_hrtime() / 1000000);
}

static void* pal_uv_timer_start(qwrt_pal_t *pal, uint64_t delay_ms,
                                 int repeat, qwrt_pal_cb_t cb, void *cb_data) {
    uv_timer_t *t = malloc(sizeof(*t));
    timer_data_t *d = malloc(sizeof(*d));
    d->cb = cb;
    d->cb_data = cb_data;
    t->data = d;
    uv_timer_init(((pal_uv_t*)pal->user_data)->loop, t);
    uv_timer_start(t, timer_cb, delay_ms, repeat ? delay_ms : 0);
    return t;
}

// ... 其他 PAL 实现
```

### 8.2 Mock PAL（测试）

```c
static void pal_mock_http_request(qwrt_pal_t *pal, const char *url,
                                   const char *method, const char *headers,
                                   const char *body, size_t body_len,
                                   qwrt_pal_cb_t cb, void *cb_data) {
    // 立即回调预设响应
    const char *resp = "{\"status\":200,\"headers\":{},\"body\":\"mock\"}";
    cb(cb_data, 0, resp, strlen(resp));
}

static void pal_mock_storage_set(qwrt_pal_t *pal, const char *key,
                                  const char *val, size_t val_len,
                                  qwrt_pal_cb_t cb, void *cb_data) {
    hashmap_set(mock_store, key, val, val_len);
    cb(cb_data, 0, NULL, 0);
}
```

---

## 9. 宿主集成示例

### 9.1 ACE REPL

```c
#include "qwrt.h"

int main(int argc, char *argv[]) {
    uv_loop_t *loop = uv_default_loop();

    // 创建 PAL
    pal_uv_t pal_data = { .loop = loop, .store = NULL };
    qwrt_pal_t pal = {
        .user_data   = &pal_data,
        .http_request = pal_uv_http_request,
        .fs_read      = pal_uv_fs_read,
        .fs_write     = pal_uv_fs_write,
        .fs_exists    = pal_uv_fs_exists,
        .fs_remove    = pal_uv_fs_remove,
        .fs_list      = pal_uv_fs_list,
        .storage_get  = pal_uv_storage_get,
        .storage_set  = pal_uv_storage_set,
        .storage_del  = pal_uv_storage_del,
        .timer_start  = pal_uv_timer_start,
        .timer_stop   = pal_uv_timer_stop,
        .time_now     = pal_uv_time_now,
        .log          = pal_uv_log,
        .mem_alloc    = NULL,
        .mem_free     = NULL,
    };

    // 创建 qwrt 运行时
    qwrt_t *rt = qwrt_create(&(qwrt_config_t){
        .pal = &pal,
        .debug = 0,
    });

    // 加载应用 JS（在 qwrt 提供的 WinterCG API 上运行）
    extern const uint8_t app_js[];
    extern const size_t app_js_len;
    qwrt_eval(rt, (const char *)app_js, NULL);

    // 初始化应用
    char *config = read_file("app.config.json");
    char *sid = NULL;
    qwrt_call(rt, "ACE.createSession", config, &sid);
    free(config);

    // REPL 主循环
    char input[4096];
    while (fgets(input, sizeof(input), stdin)) {
        char *result = NULL;
        qwrt_call(rt, "ACE.chat", input, &result);

        while (result == NULL) {
            uv_run(loop, UV_RUN_ONCE);
            qwrt_tick(rt);
        }

        printf("%s\n", result);
        qwrt_free(result);
    }

    qwrt_free(sid);
    qwrt_destroy(rt);
    return 0;
}
```

---

## 10. 文件结构

```
qwrt/
├── include/
│   └── qwrt.h              # 公共 API 头文件
├── src/
│   ├── qwrt.c              # qwrt_create/destroy/tick/eval/call
│   ├── pal.h               # qwrt_pal_t 定义
│   ├── bridge.c            # PAL → pal 内部对象 + 回调→Promise
│   └── pal_uv.c            # libuv PAL 实现（可选模块）
├── polyfill/
│   ├── qwrt-polyfill.js    # 主入口，加载所有 polyfill
│   ├── fetch.js            # fetch() 实现
│   ├── timers.js           # setTimeout/setInterval
│   ├── console.js          # console.*
│   ├── performance.js      # performance.now()
│   ├── url.js              # URL / URLSearchParams
│   ├── event-target.js     # Event / EventTarget
│   ├── abort.js            # AbortController / AbortSignal
│   ├── streams.js          # ReadableStream / WritableStream
│   ├── text-encoding.js    # TextEncoder / TextDecoder
│   ├── crypto.js           # crypto.getRandomValues
│   ├── encoding.js         # atob / btoa
│   ├── fs.js               # fs.readFile/writeFile/...
│   └── storage.js          # storage.get/set/delete
├── CMakeLists.txt
└── README.md
```

---

## 11. 与旧设计（v1.0）的差异

| 方面           | v1.0 (PAL 暴露)           | v2.0 (qwrt)                      |
| -------------- | ------------------------- | -------------------------------- |
| 运行时名称     | 未命名                    | qwrt                             |
| PAL 暴露方式   | app.pal.* 直接暴露给 JS  | pal 内部对象，闭包注入 polyfill |
| 用户 JS 看到的 | app.fetch       | fetch() — WinterCG 标准         |
| LLMClient      | 调用 app.fetch  | 调用 fetch()                    |
| Web API 实现   | C 层实现                  | JS polyfill（基于 pal.*）       |
| C 代码量       | 大（每个 Web API 都写 C） | 小（只注册原语 ~500行）          |
| 可复用性       | 绑定特定品牌             | 独立通用运行时                   |
| 参考来源       | 无                        | txiki.js 模块移植                |
| fs API         | app.fsRead            | fs.readFile（node:fs 风格）     |
| storage API    | app.storageGet        | storage.get（简洁 KV）          |
| AceNative      | 存在                      | 删除，由 WinterCG API 替代       |

---

## 12. 实施计划

### 阶段 1：C 层框架

- [ ] 定义 qwrt_pal_t 结构体 (`pal.h`)
- [ ] 实现 C 桥接层 (`bridge.c`) — PAL → pal 内部对象注册
- [ ] 实现 qwrt 公共 API (`qwrt.c`)
- [ ] 实现 libuv PAL 适配器 (`pal_uv.c`)

### 阶段 2：Polyfill Bundle

- [ ] 实现 console / performance / atob-btoa
- [ ] 实现 setTimeout / setInterval
- [ ] 实现 fetch()（基于 \_\_pal_httpRequest）
- [ ] 实现 URL / URLSearchParams
- [ ] 实现 EventTarget / AbortController
- [ ] 实现 TextEncoder / TextDecoder
- [ ] 实现 fs._（基于 \_\_pal_fs_）
- [ ] 实现 storage._（基于 \_\_pal_storage_）
- [ ] 打包为 qwrt-polyfill.js

### 阶段 3：上层应用迁移

- [ ] 删除 AceNative 接口
- [ ] LLMClient 改用 fetch()
- [ ] Memory 改用 storage API
- [ ] Tool 改用 fs API
- [ ] 更新应用 JS bundle

### 阶段 4：REPL 重写

- [ ] 重写 main.c 使用 qwrt API
- [ ] 替换所有 native\_\* 函数为 qwrt + PAL

### 阶段 5：测试与验证

- [ ] Mock PAL + qwrt 单元测试
- [ ] Polyfill 兼容性测试
- [ ] REPL 端到端测试

# qwrt 编译时组合设计：PAL × WASM引擎 × Worker

## 问题

宿主需要在编译时灵活选择 PAL 后端、WASM 引擎和 Worker 实现，不需要新的运行时 API。

当前状态：
- PAL 有 4 个后端（uv/mock/freertos/wasm），已通过 `QWRT_PAL_*` 选择
- WASM 引擎有 2 个（WAMR/wasm3），已通过 `QWRT_WITH_WAMR/QWRT_WITH_WASM3` 互斥选择
- Worker 不存在，PAL 注入风格刚完成
- 没有"浏览器原生 WASM 引擎"扩展

## 设计

### 原则

1. **qwrt 永远是单线程的。** 一个 qwrt 实例 = 一个线程 = 一个 JSRuntime。qwrt 不创建线程/进程。
2. **并行由宿主提供。** 宿主创建多个 qwrt 实例，各自独立运行。宿主控制并行方式。
3. **编译时决定，不是运行时。** 宿主通过 CMake 选项选择组合。不需要新的 C API。
4. **选项互相独立。** PAL、WASM 引擎、Worker 三个维度正交。
5. **零开销。** 未选择的选项不编译任何代码。
6. **扩展模式。** WASM 引擎作为 `qwrt_ext_t` 实现。Worker 是宿主模式，不是 qwrt 代码。

### 核心模型

```
宿主 = 并行管理者
qwrt = 单线程 JS 运行时

宿主创建 N 个 qwrt 实例：
  实例1 (线程A): qwrt_t + WAMR + pal_uv
  实例2 (线程B): qwrt_t + web_wasm + pal_wasm
  实例3 (进程C): qwrt_t + wasm3 + pal_uv

宿主决定：
  - 每个实例在哪个线程/进程/browser Worker 中运行
  - 每个实例用哪个 WASM 引擎
  - 实例之间如何通信（MessagePort）
```

### 三个维度

#### 维度 1: PAL 后端（已有）

```
QWRT_PAL_UV       → platform/uv/pal_uv.c        (libuv, Linux/macOS)
QWRT_PAL_MOCK     → platform/mock/pal_mock.c    (测试，确定性)
QWRT_PAL_FREERTOS → platform/freertos/pal_freertos.c (ESP32-S3)
QWRT_PAL_WASM     → platform/wasm/pal_wasm.c    (Emscripten，浏览器)
```

PAL 实现是参考代码，宿主直接 `#include` 到自己的构建中。qwrt 构建系统不将它们编译为独立库。

#### 维度 2: WASM 引擎（扩展）

```
QWRT_WITH_WAMR    → src/ext_wamr.c    (WAMR Fast JIT，默认)
QWRT_WITH_WASM3   → src/ext_wasm3.c   (wasm3 解释器，备选)
QWRT_WITH_WEB_WASM → src/ext_web_wasm.c (浏览器原生 WebAssembly)
```

三者互斥（都注册 `WebAssembly` 全局对象）。宿主为每个 qwrt 实例选择一个。

**`ext_web_wasm.c`**（新增）：
- 仅在 `__EMSCRIPTEN__` 下编译
- 不实现 WASM——直接桥接到浏览器的 `WebAssembly.*`
- 零代码体积（浏览器提供所有实现）

#### 维度 3: Worker（polyfill 层 JS API，不是底层实现）

Worker 是 qwrt polyfill 的概念——在 JS 层面提供 `new Worker()` 编程模型来简化并行编程。底层不预设线程/进程——宿主决定如何并行执行：

```
JS 层（polyfill）:
  new Worker('script.js')     → Worker 对象
  worker.postMessage(data)    → 发送消息
  worker.onmessage = fn       → 接收消息

宿主层（C）:
  宿主决定如何执行 Worker:
    - 同一线程内的新 qwrt 实例（模拟，无真正并行）
    - 新线程中的 qwrt 实例（pthread）
    - 新进程中的 qwrt 实例（fork）
    - 浏览器 Worker 中的 qwrt WASM 实例

MessagePort 传输:
  JS 层的 postMessage/onmessage 通过宿主提供的传输后端:
    - 单线程: 直接函数调用
    - 多线程: thread-safe queue
    - 多进程: pipe/fifo
    - 浏览器: 原生 postMessage
```

qwrt 不实现线程/进程——只提供 Worker JS API 和 MessagePort 接口。宿主实现消息传输。

### 三个维度

#### 维度 1: PAL 后端（已有）

```
QWRT_PAL_UV       → platform/uv/pal_uv.c        (libuv, Linux/macOS)
QWRT_PAL_MOCK     → platform/mock/pal_mock.c    (测试，确定性)
QWRT_PAL_FREERTOS → platform/freertos/pal_freertos.c (ESP32-S3)
QWRT_PAL_WASM     → platform/wasm/pal_wasm.c    (Emscripten，浏览器)
```

PAL 实现作为头文件+源文件提供。宿主直接 `#include` 到自己的构建中。qwrt 构建系统不再将它们编译为独立库。

#### 维度 2: WASM 引擎（扩展）

```
QWRT_WITH_WAMR    → src/ext_wamr.c    (WAMR Fast JIT，默认)
QWRT_WITH_WASM3   → src/ext_wasm3.c   (wasm3 解释器，备选)
QWRT_WITH_WEB_WASM → src/ext_web_wasm.c (浏览器原生 WebAssembly)
```

三者互斥（都注册 `WebAssembly` 全局对象）。

**`ext_web_wasm.c`**（新增）：
- 仅在 `__EMSCRIPTEN__` 环境下编译（Emscripten WASM 目标）
- 不实现 WASM 字节码解析——直接桥接到浏览器的 `WebAssembly.*` API
- 通过 `EM_ASM` 调用 `WebAssembly.validate`、`WebAssembly.compile`、`WebAssembly.instantiate`
- Module/Instance/Memory/Table/Global 构造函数桥接到浏览器原生的
- 零代码体积（只是个桥接层，浏览器提供所有实现）

#### 维度 3: Worker 实现（新增）

```
QWRT_WORKER_NONE    → 无 Worker 支持（默认）
QWRT_WORKER_THREAD  → src/worker_thread.c  (pthread)
QWRT_WORKER_PROCESS → src/worker_process.c (fork+pipe)
QWRT_WORKER_BROWSER → src/worker_browser.c (浏览器 Worker)
```

每个实现提供相同的 JS API 表面（`new Worker(script)` → Worker 对象，`postMessage`/`onmessage`），但底层并行机制不同。

**`src/worker_thread.c`**：
- 每个 Worker = 一个 pthread
- 每个 Worker 内部有独立的 `qwrt_t`（独立 JSRuntime）
- 主线程 ↔ Worker 通过 thread-safe queue + MessagePort 通信
- 消息传递使用 structuredClone 序列化

**`src/worker_process.c`**：
- 每个 Worker = 一个 fork 子进程
- 通过 pipe/fifo 传递 MessagePort 消息
- qwrt 在子进程中独立初始化（独立 JSRuntime + 独立 PAL）

**`src/worker_browser.c`**：
- 每个 Worker = 一个浏览器 `new Worker(url)`
- qwrt WASM 模块在 Worker 中独立初始化
- 通过浏览器的 `postMessage`/`onmessage` 通信
- 仅当 `QWRT_PAL_WASM=ON` 时可用

### 组合示例

```bash
# 嵌入式 MCU: FreeRTOS + WAMR + 单线程
cmake -DQWRT_PAL_FREERTOS=ON -DQWRT_WITH_WAMR=ON -DQWRT_WORKER_NONE=ON

# 服务器: libuv + WAMR + 多线程 Worker
cmake -DQWRT_PAL_UV=ON -DQWRT_WITH_WAMR=ON -DQWRT_WORKER_THREAD=ON

# 浏览器 WASM: WASM PAL + 原生 WebAssembly + 浏览器 Worker
cmake -DQWRT_PAL_WASM=ON -DQWRT_WITH_WEB_WASM=ON -DQWRT_WORKER_BROWSER=ON

# 沙箱: libuv + wasm3 + 多进程 Worker
cmake -DQWRT_PAL_UV=ON -DQWRT_WITH_WASM3=ON -DQWRT_WORKER_PROCESS=ON

# 测试: mock + WAMR + 无 Worker (当前默认)
cmake -DQWRT_PAL_MOCK=ON -DQWRT_WITH_WAMR=ON -DQWRT_WORKER_NONE=ON
```

### JS API

无论底层 Worker 实现如何，JS API 保持一致：

```js
// 主线程
const worker = new Worker('worker.js');
worker.onmessage = (e) => console.log('from worker:', e.data);
worker.postMessage({ cmd: 'compute', data: [1,2,3] });

// worker.js (在 Worker 内执行)
onmessage = (e) => {
  const result = e.data.data.reduce((a,b) => a+b);
  postMessage({ result });
};
```

### 文件结构

```
src/
  ext_wamr.c          # WAMR WASM 引擎
  ext_wasm3.c         # wasm3 WASM 引擎
  ext_web_wasm.c      # 浏览器原生 WASM 引擎 (新增)
  worker_thread.c     # pthread Worker (新增)
  worker_process.c    # fork Worker (新增)
  worker_browser.c    # 浏览器 Worker (新增)

platform/
  uv/pal_uv.c         # libuv PAL
  mock/pal_mock.c     # 模拟 PAL
  freertos/pal_freertos.c  # FreeRTOS PAL
  wasm/pal_wasm.c     # 浏览器 WASM PAL

CMakeLists.txt        # 选项: QWRT_PAL_*, QWRT_WITH_*, QWRT_WORKER_*
```


### 宿主并行模式（参考实现）

宿主创建多个 qwrt 实例的示例代码：

**多线程模式：**
```c
// 每个线程一个 qwrt 实例
void *worker_thread(void *arg) {
    qwrt_t *rt = qwrt_create(&config);
    while (running) {
        qwrt_tick(rt, 100);
        // 通过 thread-safe queue 收发消息
    }
    qwrt_destroy(rt);
}
// 主线程
pthread_create(&t1, NULL, worker_thread, config1);
pthread_create(&t2, NULL, worker_thread, config2);
```

**多进程模式：**
```c
pid_t pid = fork();
if (pid == 0) {
    // 子进程: 创建独立的 qwrt 实例
    qwrt_t *rt = qwrt_create(&config);
    while (running) qwrt_tick(rt, 100);
    qwrt_destroy(rt);
} else {
    // 父进程: 另一个 qwrt 实例
    qwrt_t *rt = qwrt_create(&config);
    // 通过 pipe 与子进程通信
}
```

**浏览器 Worker 模式：**
```js
// 宿主在浏览器中创建 Worker，每个 Worker 加载 qwrt WASM
const worker = new Worker('qwrt-worker.js');
worker.postMessage({ code: '1+1' });
worker.onmessage = (e) => console.log(e.data);
```

### 实现优先级

1. `ext_web_wasm.c` — 浏览器原生 WASM 桥接（编译时扩展）
2. 宿主并行模式文档 — pthread/fork/browser Worker 示例
3. MessagePort 跨实例通信 — JS API 已有，补齐不同传输后端

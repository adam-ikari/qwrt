# qwrt 编译时组合设计：PAL × WASM引擎 × Worker

## 问题

宿主需要在编译时灵活选择 PAL 后端、WASM 引擎和 Worker 实现，不需要新的运行时 API。

当前状态：
- PAL 有 4 个后端（uv/mock/freertos/wasm），已通过 `QWRT_PAL_*` 选择
- WASM 引擎有 2 个（WAMR/wasm3），已通过 `QWRT_WITH_WAMR/QWRT_WITH_WASM3` 互斥选择
- Worker 不存在，PAL 注入风格刚完成
- 没有"浏览器原生 WASM 引擎"扩展

## 设计

## PAL Consumer 概念

qwrt 项目中所有消费平台能力的组件都是 PAL Consumer——它们通过 `qwrt_pal_t` 接口获取平台能力，不直接访问系统调用。

```
                     ┌─────────────────────────────┐
                     │        qwrt_pal_t           │
                     │  (PAL 接口 — 平台能力契约)    │
                     └─────────────────────────────┘
                              ▲
          ┌───────────────────┼───────────────────┐
          │                   │                   │
   ┌──────┴──────┐   ┌───────┴───────┐   ┌───────┴───────┐
   │ QuickJS-ng  │   │  WASM 引擎    │   │   Worker      │
   │ + polyfill  │   │ WAMR/wasm3/   │   │   polyfill    │
   │             │   │ web_wasm      │   │               │
   │ fetch/crypto│   │               │   │ new Worker()  │
   │ timers/fs   │   │ SharedMemory  │   │ postMessage   │
   │ storage/log │   │ Mutex/Cond    │   │               │
   └─────────────┘   └───────────────┘   └───────────────┘
          │                   │                   │
          └───────────────────┼───────────────────┘
                              │
                    所有都是 PAL Consumer
                    不直接访问系统调用
```

**PAL Consumer 列表：**

| Consumer | 消费的 PAL 接口 | 提供的 JS API |
|----------|----------------|---------------|
| QuickJS-ng + polyfill | http_request, timer_*, fs_*, storage_*, random_bytes, log | fetch, setTimeout, fs, localStorage, crypto, console |
| ext_wamr / ext_wasm3 | shm_alloc/free, mutex_*, cond_* | WebAssembly.* |
| ext_web_wasm | 无（浏览器原生） | WebAssembly.* |
| Worker polyfill | spawn, channel_*, mutex_*, cond_* | new Worker(), postMessage |



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

**Worker 由 JS 创建，由 JS 控制。** `new Worker('script.js')` 是 JS 层的操作，PAL 提供底层 spawn 机制。Worker 线程内的 tick 由 PAL 驱动——宿主只需要管理主实例的 tick。

```c
// 宿主代码：只管理主实例
qwrt_t *rt = qwrt_create(&config);
while (running) {
    qwrt_tick(rt, 100);  // 驱动主实例
}
// Worker 实例在主实例的 JS 中创建，tick 由 PAL 在 Worker 线程内驱动
```

```js
// JS 代码：创建和控制 Worker
const worker = new Worker('worker.js');
worker.postMessage({ cmd: 'compute', data: [1,2,3] });
worker.onmessage = (e) => console.log('result:', e.data);
```
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

#### 维度 3: PalWorker — Worker 通过 PAL Spawn API 创建

Worker 的创建委托给 PAL。不同 PAL 有不同实现，但 JS API 一致。

**核心流程：**
```
JS 层:    new Worker('script.js')
            ↓
Worker polyfill 调用 pal.spawn(config)
            ↓
PAL 创建新的 qwrt 实例 + MessagePort
            ↓
返回 Worker 对象给 JS
```

**不同 PAL 的 Worker 实现：**

| PAL | Worker 创建方式 | 消息通道 |
|-----|----------------|----------|
| pal_uv | fork() + pipe | pipe fd |
| pal_mock | 同线程新 qwrt_t | 内存 queue |
| pal_wasm | 浏览器 new Worker(url) | 原生 postMessage |
| pal_freertos | xTaskCreate | FreeRTOS queue |

**为什么这是对的：**
- Worker 创建是 PAL 的职责，不是 qwrt 核心的
- PAL 已经抽象了平台差异——Worker 是同一个抽象的延伸
- 宿主选择 PAL → 自动获得对应的 Worker 行为
- 符合依赖注入原则（PAL loop 注入 → PAL Worker 注入）
- 安全：PAL 控制 Worker 的创建方式和权限边界

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



### PAL 同步原语（Worker 和 WASM Threads 的基础设施）

Worker 间通信和 WASM 多线程需要共享内存和同步。这些作为 PAL 的可选接口：

```c
/* 共享内存（同一进程内多个 qwrt 实例可访问） */
void *(*shm_alloc)(qwrt_pal_t *pal, size_t size);
void  (*shm_free)(qwrt_pal_t *pal, void *ptr, size_t size);

/* 互斥锁（跨 qwrt 实例） */
void *(*mutex_create)(qwrt_pal_t *pal);
void  (*mutex_lock)(qwrt_pal_t *pal, void *m);
void  (*mutex_unlock)(qwrt_pal_t *pal, void *m);
void  (*mutex_destroy)(qwrt_pal_t *pal, void *m);

/* 条件变量（跨 qwrt 实例） */
void *(*cond_create)(qwrt_pal_t *pal);
void  (*cond_wait)(qwrt_pal_t *pal, void *c, void *m);
void  (*cond_signal)(qwrt_pal_t *pal, void *c);
void  (*cond_destroy)(qwrt_pal_t *pal, void *c);
```

全部可选（置 NULL）——不需要时零开销。

| PAL | 共享内存 | 互斥锁 | 条件变量 | 场景 |
|-----|---------|--------|---------|------|
| pal_uv | mmap | pthread_mutex | pthread_cond | WASM threads, Worker |
| pal_mock | malloc | spinlock | 无（单线程） | 测试 |
| pal_freertos | heap_caps_malloc | xSemaphoreMutex | xTaskNotify | ESP32 |
| pal_wasm | SharedArrayBuffer | Atomics | 不需要 | 浏览器原生 |

这些同步原语的用途：
- **WASM threads**: WAMR 的 WASM 模块内部使用 SharedArrayBuffer + Atomics
- **Worker lock-free queue**: 同一进程内的 Worker 用共享内存 + 原子操作做高效通信
- **宿主并行**: 宿主可以在 qwrt 实例之间安全共享数据

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

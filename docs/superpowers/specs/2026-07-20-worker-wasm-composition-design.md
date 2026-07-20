# qwrt 编译时组合设计：PAL × WASM引擎 × Worker

## 问题

宿主需要在编译时灵活选择 PAL 后端、WASM 引擎和 Worker 实现，不需要新的运行时 API。

## PAL 作为运行时通用平台接口

PAL 是 qwrt 与外部世界之间的**唯一接口**。所有资源通过 PAL 分配——qwrt 不直接调用任何系统 API。WASM 引擎的内存也通过 PAL 分配，不绕过 PAL。

### 核心原则

1. **qwrt 永远是单线程的。** 一个 qwrt 实例 = 一个线程 = 一个 JSRuntime
2. **并行由宿主提供。** 宿主创建多个 qwrt 实例，各自独立运行
3. **所有资源通过 PAL 分配。** WASM 引擎不绕过 PAL 调用系统 API
4. **PAL 是运行时通用接口。** `qwrt_pal_t` 是唯一的 ABI 契约
5. **qwrt 不拥有任何 PAL 实现。** 参考实现（uv/mock/freertos/wasm）由宿主在自己的构建中编译
6. **扩展模式。** WASM 引擎作为 `qwrt_ext_t` 实现

### PAL Consumer

所有 qwrt 组件都是 PAL Consumer——通过 `qwrt_pal_t` 接口获取平台能力：

| Consumer | 消费的 PAL 接口 | 提供的 JS API |
|----------|----------------|---------------|
| QuickJS-ng + polyfill | http_request, timer_*, fs_*, storage_*, random_bytes, log | fetch, setTimeout, fs, localStorage, crypto, console |
| ext_wamr / ext_wasm3 | mem_alloc, mem_free, mem_alloc_shared | WebAssembly.* |
| ext_web_wasm | 无（浏览器原生） | WebAssembly.* |
| Worker polyfill | worker_spawn, worker_post, worker_on_message | new Worker(), postMessage |

### PAL 内存接口

WASM 引擎不直接调用 `wasm_runtime_malloc`——通过 PAL 分配：

| 接口 | 用途 | POSIX | FreeRTOS | WASM |
|------|------|--------|----------|------|
| mem_alloc | 普通内存 | malloc | heap_caps_malloc | malloc |
| mem_alloc_shared | WASM SharedArrayBuffer | mmap(MAP_SHARED) | malloc（同地址空间） | SharedArrayBuffer |
| mem_free | 释放 | free | free | free |

### Worker

Worker 由 JS 创建（`new Worker('script.js')`），PAL 提供底层 spawn 机制。

```c
/* PAL Worker 接口 */
void *(*worker_spawn)(qwrt_pal_t *pal, const char *script);           /* 创建 Worker */
void  (*worker_post)(qwrt_pal_t *pal, void *w, const char *msg, ...); /* 发送消息 */
void  (*worker_on_message)(qwrt_pal_t *pal, void *w, qwrt_pal_cb_t, ...); /* 接收消息 */
void  (*worker_terminate)(qwrt_pal_t *pal, void *w);                   /* 终止 Worker */
```

不同 PAL 的 Worker 实现不同，但 JS API 一致：

| PAL | 创建方式 | 通信方式 |
|-----|---------|---------|
| pal_uv | fork + pipe | pipe fd |
| pal_mock | 同线程 qwrt_t + queue | 内存 queue |
| pal_wasm | browser new Worker(url) | 原生 postMessage |
| pal_freertos | xTaskCreate + queue | FreeRTOS queue |

### 三个维度

#### 维度 1: PAL 后端（参考实现，宿主编译）

所有 PAL 实现都是参考代码，由宿主在自己的构建中编译：

```
platform/uv/pal_uv.c        (libuv, Linux/macOS)
platform/mock/pal_mock.c    (测试，确定性)
platform/freertos/pal_freertos.c (FreeRTOS, ESP32-S3)
platform/wasm/pal_wasm.c    (Emscripten，浏览器)
```

#### 维度 2: WASM 引擎（扩展）

```
QWRT_WITH_WAMR    → src/ext_wamr.c    (WAMR Fast JIT，默认)
QWRT_WITH_WASM3   → src/ext_wasm3.c   (wasm3 解释器，备选)
QWRT_WITH_WEB_WASM → src/ext_web_wasm.c (浏览器原生 WebAssembly)
```

#### 维度 3: Worker polyfill

JS 层提供 `new Worker()` API。PAL 提供底层 spawn + 通信机制。

### 组合示例

```bash
# 嵌入式: FreeRTOS + WAMR + 单任务 Worker
cmake -DQWRT_PAL_FREERTOS=ON -DQWRT_WITH_WAMR=ON

# 服务器: libuv + WAMR + fork Worker
cmake -DQWRT_PAL_UV=ON -DQWRT_WITH_WAMR=ON

# 浏览器: WASM PAL + 浏览器原生 WASM + browser Worker
cmake -DQWRT_PAL_WASM=ON -DQWRT_WITH_WEB_WASM=ON

# 沙箱: libuv + wasm3
cmake -DQWRT_PAL_UV=ON -DQWRT_WITH_WASM3=ON
```

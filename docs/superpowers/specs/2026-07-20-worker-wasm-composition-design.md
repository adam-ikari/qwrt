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

1. **编译时决定，不是运行时。** 宿主通过 CMake 选项选择组合。不需要新的 C API。
2. **选项互相独立。** PAL、WASM 引擎、Worker 三个维度正交。
3. **零开销。** 未选择的选项不编译任何代码。
4. **扩展模式。** WASM 引擎作为 `qwrt_ext_t` 实现。Worker 作为 `src/worker_*.c` 编译时选择。

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

### 实现优先级

1. `ext_web_wasm.c` — 浏览器原生 WASM 桥接（最小工作量，最大价值）
2. `QWRT_WORKER_*` CMake 选项骨架 — 编译时选择框架
3. `worker_thread.c` — pthread 实现（Linux/macOS 可用，pthread 库已有）
4. `worker_browser.c` — 浏览器 Worker（配合 PAL_WASM）
5. `worker_process.c` — fork 实现（适配已有 spawn/join PAL 接口）
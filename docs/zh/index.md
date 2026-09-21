---
layout: home

hero:
  name: "Qwrt.js"
  text: "可嵌入 QuickJS 运行时"
  tagline: C99 · 宿主自有线程 + libuv 循环 · JSON 消息边界 · 零系统依赖
  actions:
    - theme: brand
      text: 快速开始
      link: /zh/guide/
    - theme: alt
      text: JS API
      link: /zh/js-api/

features:
  - icon: 🔌
    title: 基于消息的宿主边界
    details: 宿主 ⇄ 运行时通过 `qwrt_post_message` / `message_cb` 说 JSON。入站线程安全，出站在运行时线程触发。无 `eval`、无 `tick`——边界干净。
  - icon: 🧵
    title: 自有线程 + 事件循环
    details: qwrt 启动自己的内部线程，内嵌 libuv 循环。宿主从不泵动事件循环，也不会因 JS 阻塞。
  - icon: 📦
    title: 零系统依赖
    details: QuickJS-ng、mbedTLS、miniz、libuv、WAMR — 全部通过 CMake 从源码构建。无需系统包。最小配置约 2.45 MiB（strip 后）。
  - icon: ⚡
    title: 严格 C99 + 可嵌入
    details: 任何 C99 代码库、任何宿主编译器。冷启动约 7.6 ms（Ryzen 级），峰值 RSS 约 3.3 MB——同样 eval 负载下比 node 轻 23 倍。
  - icon: 🌐
    title: WinterTC 兼容
    details: WinterTC 兼容的 API 表面——fetch、crypto.subtle、streams、WebSocket、BroadcastChannel、EventSource、定时器、fs、serve() 等。预编译为字节码，作为全局可用。
  - icon: 🔒
    title: 无全局状态
    details: 零可变文件作用域状态。通过不透明的 `qwrt_t` 实现每运行时隔离——可安全地在同一进程中运行多个独立实例。
---

## 快速开始

```bash
# Clone with all submodules
git clone --recursive https://github.com/adam-ikari/qwrt.git
cd qwrt

# Configure and build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

```c
#include <qwrt/qwrt.h>
#include <stdio.h>

static void on_message(qwrt_t *rt, const char *json, size_t len, void *data) {
    (void)rt; (void)data;
    printf("received: %.*s\n", (int)len, json);
}

int main(void) {
    qwrt_config_t cfg = {0};
    cfg.initial_script = "postMessage({hello: 'world'});";
    cfg.message_cb = on_message;
    qwrt_t *rt = qwrt_create(&cfg);
    if (!rt) return 1;
    qwrt_post_message(rt, "{\"cmd\":\"echo\",\"data\":\"hi\"}", 26);
    qwrt_destroy(rt);
    return 0;
}
```

## 架构

```mermaid
flowchart TB
    subgraph QWRT["Qwrt.js"]
        direction TB
        Core["qwrt.c (core API)"]
        Thread["thread.c — internal thread + libuv loop"]
        Msgq["msgq.c — message queue"]
        Worker["worker.c — dispatch (onmessage/postMessage)"]
        UvIO["uv_io.c — libuv I/O"]
        Core --> Thread
        Thread --> Msgq
        Msgq --> Worker
        Thread --> UvIO
        JS["WinterTC modules: fetch · console · crypto · streams · timers · …"]
        ExtList["Extensions: compress · crypto · textcodec · wamr"]
        Worker -.injects.-> JS
    end
    HOST["Host"] -->|"qwrt_post_message: JSON in"| Msgq
    Worker -->|"message_cb: JSON out"| HOST
    UvIO --> LIBUV["libuv"]
```

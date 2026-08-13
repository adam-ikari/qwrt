---
layout: home

hero:
  name: "Qwrt.js"
  text: "可嵌入 QuickJS 运行时"
  tagline: C99 · WinterTC 兼容 · 内部线程 + libuv 循环 · 零系统依赖
  actions:
    - theme: brand
      text: 快速开始
      link: /zh/guide/
    - theme: alt
      text: JS API
      link: /zh/js-api/

features:
  - icon: ⚡
    title: 严格 C99
    details: 可嵌入任何 C99 代码库。无需 C99 之外的宿主编译器特性。
  - icon: 📦
    title: 零系统依赖
    details: QuickJS-ng、mbedTLS、miniz、libuv、WAMR — 全部通过 CMake 从源码构建。无需系统包。
  - icon: 🌐
    title: WinterTC 兼容
    details: WinterTC 兼容的 JavaScript 运行时 — 嵌入者期望的标准 Web API，预编译为字节码。
  - icon: 🔌
    title: 基于消息的宿主边界
    details: 宿主 ⇄ 运行时通过 qwrt_post_message / message_cb 说 JSON。入站线程安全，出站回调干净。
  - icon: 🧵
    title: 自有线程 + 事件循环
    details: qwrt 运行自己的内部线程，内嵌 libuv 循环。宿主从不泵动事件循环。
  - icon: 🔒
    title: 无全局状态
    details: 零可变文件作用域状态。通过不透明的 qwrt_t 实现每运行时隔离 — 可安全地在同一进程中运行多个独立实例。
---



## 快速开始

```bash
# 克隆仓库及所有子模块
git clone --recursive https://github.com/adam-ikari/qwrt.git
cd qwrt

# 配置并构建
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
        Core["qwrt.c (核心 API)"]
        Thread["thread.c — 内部线程 + libuv 循环"]
        Msgq["msgq.c — 消息队列"]
        Worker["worker.c — 分发 (onmessage/postMessage)"]
        UvIO["uv_io.c — libuv I/O"]
        Core --> Thread
        Thread --> Msgq
        Msgq --> Worker
        Thread --> UvIO
        JS["WinterTC 模块: fetch · console · crypto · streams · timers · …"]
        ExtList["扩展: compress · crypto · textcodec · wamr"]
        Worker -.注入.-> JS
    end
    HOST["宿主"] -->|"qwrt_post_message: JSON 入"| Msgq
    Worker -->|"message_cb: JSON 出"| HOST
    UvIO --> LIBUV["libuv"]
```
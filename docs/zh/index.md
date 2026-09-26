---
layout: home

hero:
  name: "Qz.js"
  text: "可嵌入 WinterTC 运行时"
  tagline: 严格 C99 · 内部线程 + libuv 循环 · JSON 宿主边界
  image:
    light: /logo.svg
    dark: /logo-dark.svg
    alt: Qz.js
  actions:
    - theme: brand
      text: 快速开始
      link: /zh/guide/quickstart
    - theme: alt
      text: JS API
      link: /zh/js-api/

features:
  - icon: 🔌
    title: 基于消息的宿主边界
    details: 宿主与运行时通过 `qz_post_message` / `message_cb` 交换 JSON。入站线程安全，出站在运行时线程触发。没有 eval，也没有 tick。
  - icon: 🧵
    title: 自有线程 + libuv 循环
    details: qzjs 运行自己的内部线程，内嵌 libuv 循环。宿主不泵动事件循环。
  - icon: 📦
    title: 零系统依赖
    details: 运行时及其全部依赖均通过 CMake 从源码构建。最小配置 strip 后约 2.45 MiB。
  - icon: ⚡
    title: 严格 C99
    details: 与依赖一起按 C99 编译。Release 下 `qzjs -e 'console.log(1)'` 启动不到 5 ms，峰值 RSS 约 3 MB。
  - icon: 🌐
    title: WinterTC 兼容运行时
    details: 30 个注册模块——fetch、crypto.subtle、streams、WebSocket、BroadcastChannel、EventSource、timers、fs、serve() 等。预编译为字节码，作为全局可用。
  - icon: 🔒
    title: 无全局状态
    details: 通过不透明的 `qz_t` 实现每运行时隔离。同一进程可运行多个独立实例。
---

## 快速开始

```bash
# Clone with all submodules
git clone --recursive https://github.com/adam-ikari/qzjs.git
cd qzjs

# Configure and build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

```c
#include <qzjs/qzjs.h>
#include <stdio.h>

static void on_message(qz_t *rt, const char *json, size_t len, void *data) {
    (void)rt; (void)data;
    printf("received: %.*s\n", (int)len, json);
}

int main(void) {
    qz_config_t cfg = {0};
    cfg.initial_script = "postMessage({hello: 'world'});";
    cfg.message_cb = on_message;
    qz_t *rt = qz_create(&cfg);
    if (!rt) return 1;
    qz_post_message(rt, "{\"cmd\":\"echo\",\"data\":\"hi\"}", 26);
    qz_destroy(rt);
    return 0;
}
```

完整步骤见[快速开始](/zh/guide/quickstart)。

## 架构

```mermaid
flowchart TB
    subgraph AM["qzjs"]
        direction TB
        Core["qzjs.c (core API)"]
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
    HOST["Host"] -->|"qz_post_message: JSON in"| Msgq
    Worker -->|"message_cb: JSON out"| HOST
    UvIO --> LIBUV["libuv"]
```

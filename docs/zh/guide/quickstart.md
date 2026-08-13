---
title: 快速开始
description: 在 5 分钟内让 Qwrt.js 跑起来 — 克隆、构建并运行你的第一个基于嵌入式 QuickJS-ng 运行时的 JavaScript 程序。
---

# 快速开始

在 5 分钟内让 qwrt 跑起来。

## 前置条件

- **C 编译器** — GCC 8+、Clang 10+ 或 MSVC 2019+
- **CMake** 3.16+
- **Git**（用于子模块）

## 克隆与构建

```bash
# 克隆仓库及所有子模块
git clone --recursive https://github.com/adam-ikari/qwrt.git
cd qwrt

# 配置并构建（Release 模式）
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

构建产物 `libqwrt.a`（静态核心）和 `libqwrt_full.a`（聚合库，链接 libuv、mbedTLS 等）位于 `build/lib/` 目录中。

## 你的第一个程序

创建 `hello.c`：

```c
#include <qwrt/qwrt.h>
#include <stdio.h>

static void on_message(qwrt_t *rt, const char *json, size_t len, void *data) {
    (void)rt; (void)data;
    printf("received: %.*s\n", (int)len, json);
}

int main(void) {
    // 创建运行时 — qwrt 启动自己的内部线程和循环
    qwrt_config_t cfg = {0};
    cfg.initial_script = "console.log('Hello from QuickJS!'); postMessage(1 + 1);";
    cfg.message_cb = on_message;
    qwrt_t *rt = qwrt_create(&cfg);
    if (!rt) {
        fprintf(stderr, "Failed to create runtime\n");
        return 1;
    }

    // 通过发送 JSON 消息驱动运行时
    qwrt_post_message(rt, "{\"cmd\":\"echo\",\"data\":\"hi\"}", 26);

    // 清理 — 优雅关闭
    qwrt_destroy(rt);
    return 0;
}
```

编译并链接 `libqwrt_full`（聚合了 qwrt + libuv + 依赖）：

```bash
cc -std=c99 -I include -o hello hello.c \
   -L build/lib -lqwrt_full -lm
```

## 带测试构建

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DQWRT_BUILD_TESTS=ON
cmake --build build -j$(nproc)
cd build && ctest --output-on-failure
```

测试带有标签，方便定向运行：

```bash
ctest -L offline     # 本地确定性测试（CI 默认）
ctest -L network     # 出站 HTTP/HTTPS 测试
ctest -L benchmark   # 性能基准测试（非通过/失败）
```

## 下一步

- [构建](/zh/guide/building) — 所有 CMake 选项详解
- [运行时生命周期](/zh/guide/lifecycle) — 创建、使用、销毁
- [嵌入模式](/zh/guide/embedding) — 基于消息的宿主模式
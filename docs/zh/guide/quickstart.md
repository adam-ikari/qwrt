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

构建产物 `libqwrt.a` 和所有 PAL 后端位于 `build/lib/` 目录中。

## 你的第一个程序

创建 `hello.c`：

```c
#include <qwrt/qwrt.h>
#include <pal_uv.h>
#include <stdio.h>

int main(void) {
    // 创建平台抽象层（libuv）
    qwrt_pal_t *pal = pal_uv_create(uv_default_loop());

    // 创建运行时
    qwrt_t *rt = qwrt_create(&(qwrt_config_t){ .pal = pal });
    if (!rt) {
        fprintf(stderr, "Failed to create runtime\n");
        return 1;
    }

    // 执行 JavaScript
    char *result = NULL;
    if (qwrt_eval(rt, "console.log('Hello from QuickJS!'); 1 + 1", &result) == 0) {
        printf("Result: %s\n", result);  // "2"
        qwrt_free(result);
    }

    // 驱动事件循环（即使是同步 eval 也需要，用于排空微任务）
    while (pal->run_cycle(pal, 100) > 0) {
        qwrt_tick(rt);
    }

    // 清理
    qwrt_destroy(rt);
    return 0;
}
```

编译并链接：

```bash
cc -std=c99 -I include -o hello hello.c \
   -L build/lib -lqwrt -lqwrt_uv -lm
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
- [运行时生命周期](/zh/guide/lifecycle) — 创建、重置、销毁
- [PAL 概述](/zh/pal/) — 了解平台抽象层
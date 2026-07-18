---
title: 实现 PAL
description: 实现自定义 Qwrt.js PAL 后端的逐步指南 — 必需函数、可选钩子、异步模式和测试。
---

# 实现 PAL

实现自定义平台抽象层后端的逐步指南。

## 概述

PAL 实现是一个 C 文件，填充所有必需的 `qwrt_pal_t` 函数指针并提供构造函数/析构函数。本指南引导你创建一个最小 PAL，然后添加异步 I/O、流式传输和生命周期钩子。

## 第 1 步：骨架

创建 `platform/mypal/pal_mypal.c` 和 `pal_mypal.h`：

```c
// pal_mypal.h
#ifndef QWRT_PAL_MYPAL_H
#define QWRT_PAL_MYPAL_H

#include "qwrt/qwrt.h"

typedef struct pal_mypal_t pal_mypal_t;

pal_mypal_t *pal_mypal_create(void);
void pal_mypal_destroy(pal_mypal_t *mp);
qwrt_pal_t *pal_mypal_get_pal(pal_mypal_t *mp);

#endif /* QWRT_PAL_MYPAL_H */
```

```c
// pal_mypal.c
#include "pal_mypal.h"
#include <stdlib.h>
#include <time.h>

struct pal_mypal_t {
    qwrt_pal_t pal;  // 必须是第一个或接近第一个的成员
    // 你的私有状态放在这里
};

// 前向声明
static uint64_t mypal_time_now(qwrt_pal_t *pal);
static uint64_t mypal_hrtime(qwrt_pal_t *pal);
// ... 所有其他必需方法

pal_mypal_t *pal_mypal_create(void) {
    pal_mypal_t *mp = calloc(1, sizeof(*mp));
    if (!mp) return NULL;

    qwrt_pal_t *pal = &mp->pal;

    // 标识
    pal->user_data = mp;
    pal->version   = 1;
    pal->name      = "mypal";

    // 必需方法（填充这些）
    pal->http_request = mypal_http_request;
    pal->fs_read      = mypal_fs_read;
    // ... 所有必需方法

    // 可选方法（如果未实现则设为 NULL）
    pal->http_request_stream = NULL;
    pal->http_abort          = NULL;
    pal->run_cycle           = NULL;
    pal->spawn               = NULL;
    // ... 所有可选方法

    // 生命周期（NULL = 不需要 init/destroy）
    pal->init    = NULL;
    pal->destroy = NULL;

    // 保留字段（必须为 NULL）
    memset(pal->reserved, 0, sizeof(pal->reserved));

    return mp;
}

void pal_mypal_destroy(pal_mypal_t *mp) {
    if (!mp) return;
    free(mp);
}

qwrt_pal_t *pal_mypal_get_pal(pal_mypal_t *mp) {
    return &mp->pal;
}
```

## 第 2 步：必需方法

每个 PAL 至少必须实现以下同步方法：

| 方法 | 实现 |
|--------|---------------|
| `time_now` | `return (uint64_t)time(NULL) * 1000;` |
| `hrtime` | 平台特定的单调时钟 |
| `log` | `fprintf(stderr, ...)` 或 NULL |
| `random_bytes` | `/dev/urandom` 或硬件 RNG |

以及异步方法的至少存根实现（如果操作不可用，则以 `QWRT_ERR_NOT_SUPPORTED` 调用回调）：

| 方法 | 最小存根 |
|--------|-------------|
| `http_request` | `cb(cb_data, QWRT_ERR_NOT_SUPPORTED, NULL, 0);` |
| `fs_read` | `cb(cb_data, QWRT_ERR_NOT_SUPPORTED, NULL, 0);` |
| `fs_write` | `cb(cb_data, QWRT_ERR_NOT_SUPPORTED, NULL, 0);` |
| `storage_get` | `cb(cb_data, QWRT_ERR_NOT_SUPPORTED, NULL, 0);` |
| `timer_start` | 返回 NULL |
| ... | ... |

## 第 3 步：异步 HTTP

完整模式参见[异步操作](/pal/async)。

## 第 4 步：生命周期钩子

使用 `init` 和 `destroy` 管理资源：

```c
static int mypal_init(qwrt_pal_t *pal) {
    pal_mypal_t *mp = (pal_mypal_t *)pal->user_data;

    // 初始化网络栈、打开文件句柄等
    mp->socket_pool = create_socket_pool(16);
    if (!mp->socket_pool) return QWRT_ERR_NO_MEMORY;

    return QWRT_OK;
}

static void mypal_destroy(qwrt_pal_t *pal) {
    pal_mypal_t *mp = (pal_mypal_t *)pal->user_data;

    // 取消所有未完成的操作
    cancel_all_requests(mp);
    destroy_socket_pool(mp->socket_pool);
}
```

## 第 5 步：在 CMake 中注册

添加到 `CMakeLists.txt`：

```cmake
option(QWRT_PAL_MYPAL "My custom PAL backend" OFF)

if(QWRT_PAL_MYPAL)
    add_library(qwrt_mypal STATIC
        platform/mypal/pal_mypal.c)
    target_link_libraries(qwrt_mypal PRIVATE qwrt_pal_common)
    target_include_directories(qwrt_mypal PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/platform)
    install(TARGETS qwrt_mypal ARCHIVE DESTINATION lib)
endif()
```

## 第 6 步：测试

使用模拟 PAL 作为测试参考。编写单元测试，覆盖：

1. 创建你的 PAL
2. 用有效和无效输入调用每个方法
3. 验证回调状态码
4. 检查边界情况（NULL 指针、空字符串、大型载荷）
5. 使用 Valgrind 检查泄漏

```c
void test_mypal_http_ok(void) {
    pal_mypal_t *mp = pal_mypal_create();
    qwrt_pal_t *pal = pal_mypal_get_pal(mp);

    // 测试有效请求
    // ...

    pal_mypal_destroy(mp);
}
```

## 参考实现

研究三个内置 PAL 了解实际模式：

- **[pal_mock](https://github.com/adam-ikari/qwrt/blob/master/platform/mock/pal_mock.c)** — 最简单，纯 C，无外部依赖。最佳起点。
- **[pal_uv](https://github.com/adam-ikari/qwrt/blob/master/platform/uv/pal_uv.c)** — 全功能，libuv 事件循环，mbedTLS 用于 HTTPS。
- **[pal_freertos](https://github.com/adam-ikari/qwrt/blob/master/platform/freertos/pal_freertos.c)** — 嵌入式，FreeRTOS 原语，lwIP 套接字。

## 检查清单

- [ ] 所有必需函数指针为非 NULL
- [ ] 可选函数指针为 NULL（而非未初始化）
- [ ] `version` = 1，`name` 已设置，`reserved[4]` 已清零
- [ ] `user_data` 指向你的私有状态
- [ ] 所有异步方法恰好调用其回调一次
- [ ] 回调在事件循环线程上触发（或在返回前同步执行）
- [ ] 不在 PAL 回调中调用 `qwrt_eval`/`qwrt_call`（使用 `qwrt_defer_callback`）
- [ ] 错误码来自 `qwrt_pal_err_t` 枚举，而非裸整数
- [ ] 构造函数在分配失败时返回 NULL
- [ ] 析构函数可安全地以 NULL 调用
- [ ] Valgrind 干净（无泄漏，无 use-after-free）

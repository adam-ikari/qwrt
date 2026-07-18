---
title: pal_mock（测试）
description: 用于 Qwrt.js 确定性测试的模拟 PAL 后端 — 无网络、无系统调用、完全可控的响应。
---

# pal_mock — 模拟后端

模拟 PAL 是一个纯 C、无依赖的实现，用于测试和作为 PAL 作者的参考。所有操作都是同步且基于内存的。

## 概述

```c
#include <qwrt/qwrt.h>
#include <pal_mock.h>

int main(void) {
    qwrt_pal_t *pal = pal_mock_create();

    qwrt_t *rt = qwrt_create(&(qwrt_config_t){ .pal = pal });

    // ... 使用运行时 ...

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
    return 0;
}
```

## 能力

| 功能 | 支持 | 备注 |
|---------|---------|-------|
| HTTP | ✅ | 基于内存的 URL 路由，无网络 |
| HTTPS | ❌ | 测试不需要 |
| 流式 HTTP | ❌ | — |
| 中止 | ❌ | — |
| 文件系统 | ✅ | 基于内存的虚拟文件系统 |
| 存储 | ✅ | 基于内存的键值存储 |
| 定时器 | ✅ | 模拟，同步触发 |
| 子进程 | ❌ | — |
| 事件循环 | ❌ | run_cycle 为 NULL |

## HTTP 模拟

模拟 PAL 支持预注册的 HTTP 路由：

```c
pal_mock_t *mp = pal_mock_create();

// 注册一个路由
pal_mock_register_route(mp, "/api/data", "GET", 200,
    "{\"Content-Type\":\"application/json\"}",
    "{\"ok\":true}", 9);

// 注册自定义响应
pal_mock_register_route(mp, "/api/error", "POST", 500,
    "{}", "Internal Error", 14);
```

路由同时匹配 URL 路径和 HTTP 方法。未匹配的请求返回 404。

## 文件系统

模拟 PAL 提供基于内存的虚拟文件系统：

```c
pal_mock_t *mp = pal_mock_create();

// 在创建运行时之前设置文件系统内容
pal_mock_set_fs(mp, "/app/main.js", "console.log('hello');");
pal_mock_set_fs(mp, "/data/config.json", "{\"version\":1}");

// fs_write 写入内存，fs_read 从中读取
// fs_exists 检查路径是否存在
// fs_list 返回目录条目
```

## 存储

基于内存的键值存储，在 PAL 生命周期内持久存在：

```c
pal_mock_set_storage(mp, "token", "abc123");
// storage_get("token") 返回 "abc123"
// storage_set / storage_remove 更新映射
```

## 定时器

定时器在调用 `pal_mock_advance_time` 时同步触发：

```c
// 启动一个 1000ms 定时器
pal->timer_start(pal, 1000, 0, on_timeout, &data);

// 推进时间 1000ms — 触发回调
pal_mock_advance_time(mp, 1000);
```

这使得定时器测试具有确定性且速度快。

## 在测试中使用

模拟 PAL 在 qwrt 的测试套件中广泛使用：

```c
void test_my_feature(void) {
    pal_mock_t *mp = pal_mock_create();
    qwrt_pal_t *pal = pal_mock_get_pal(mp);

    // 预设文件
    pal_mock_set_fs(mp, "/test.js", "export default 42;");

    qwrt_t *rt = qwrt_create(&(qwrt_config_t){ .pal = pal });

    // 运行测试逻辑...

    qwrt_destroy(rt);
    pal_mock_destroy(mp);
}
```

## 参考实现

模拟 PAL 是**编写自己 PAL 的最佳起点**。它不到 500 行纯 C 代码，无外部依赖。在 `platform/mock/pal_mock.c` 查看源码。

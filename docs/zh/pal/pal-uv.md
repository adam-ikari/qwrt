---
title: pal_uv（libuv）
description: Qwrt.js 在 Linux 和 macOS 上基于 libuv 的 PAL 后端 — 事件循环集成、HTTP/HTTPS、定时器和文件系统。
---

# pal_uv — libuv 后端

Linux 和 macOS 的生产环境 PAL。使用 libuv 处理事件循环和 TCP，mbedTLS 处理 HTTPS，POSIX API 处理文件系统操作。

## 概述

```c
#include <qwrt/qwrt.h>
#include <pal_uv.h>

int main(void) {
    // 使用默认 libuv 循环创建
    qwrt_pal_t *pal = pal_uv_create(uv_default_loop());

    // 或者传入一个已有的 uv_loop_t*
    // uv_loop_t *loop = uv_default_loop();
    // qwrt_pal_t *pal = pal_uv_create(loop);

    qwrt_t *rt = qwrt_create(&(qwrt_config_t){ .pal = pal });

    // ... 使用运行时 ...

    // 驱动事件循环
    while (pal->run_cycle(pal, 100) > 0) qwrt_tick(rt);

    qwrt_destroy(rt);
    pal_uv_destroy(pal);  // 释放 uv_loop_t
    return 0;
}
```

## 能力

| 功能 | 支持 | 备注 |
|---------|---------|-------|
| HTTP | ✅ | libuv TCP + 自定义 HTTP/1.1 实现 |
| HTTPS | ✅ | mbedTLS，证书验证 |
| 流式 HTTP | ✅ | 分块传输解码 |
| 中止 | ✅ | 关闭 TCP 连接 |
| 文件系统 | ✅ | POSIX open/read/write/stat/readdir/unlink |
| 存储 | ✅ | 内存键值存储 |
| 定时器 | ✅ | libuv `uv_timer_t` |
| 子进程 | ✅ | fork/exec 配合管道 IPC |
| 事件循环 | ✅ | `uv_run(UV_RUN_NOWAIT)` |

## 事件循环

`pal_uv` 封装 libuv 的事件循环。`run_cycle` 调用 `uv_run(UV_RUN_NOWAIT)`，以非阻塞方式处理就绪的 I/O 和定时器。典型循环：

```c
while (pal->run_cycle(pal, 100) > 0) {
    qwrt_tick(rt);
}
```

- `timeout_ms` 传递给 `uv_run` — 0 表示非阻塞，>0 则最多阻塞该时长
- 返回已处理的事件数（超时返回 0，错误返回 <0）

## HTTP/HTTPS

HTTP 基于 libuv TCP 实现，配合自定义 HTTP/1.1 解析。HTTPS 使用 mbedTLS 处理 TLS 层：

- 证书验证默认启用
- 支持 SNI（Server Name Indication）
- 流式响应的分块传输编码
- 连接复用（Keep-Alive）

## 文件系统

使用 POSIX API：
- `fs_read` — `open()` + `read()` + `close()`
- `fs_write` — `open(O_CREAT|O_WRONLY|O_TRUNC)` + `write()` + `close()`
- `fs_exists` — `stat()`（返回 `QWRT_OK` 或 `QWRT_ERR_NOT_FOUND`）
- `fs_remove` — `unlink()`
- `fs_list` — `opendir()` + `readdir()` + `closedir()`

所有文件系统操作都是**同步**的——回调在函数返回前触发。

## 构建要求

- libuv（git 子模块位于 `deps/libuv/`）
- mbedTLS（git 子模块位于 `deps/mbedtls/`，当 `QWRT_WITH_TLS=ON` 时）
- CMake 选项：`QWRT_PAL_UV=ON`（默认）

```bash
cmake -B build -DQWRT_PAL_UV=ON
```

## 线程安全

所有 libuv 回调在调用 `uv_run` 的线程（即调用 `run_cycle` 的线程）上触发。pal_uv 实现通过 `qwrt_defer_callback` 将这些回调延迟到 JS 线程。

---
title: pal_freertos（ESP32）
description: Qwrt.js 在 ESP32-S3 上的 FreeRTOS PAL 后端 — WiFi、TCP/TLS、文件系统和低功耗运行。
---

# pal_freertos — FreeRTOS 后端

FreeRTOS PAL 面向 ESP32-S3 和类似的微控制器。使用 lwIP 套接字处理网络，FreeRTOS 原语处理定时器和同步。

## 概述

```c
#include <qwrt/qwrt.h>
#include <pal_freertos.h>

void app_main(void) {
    qwrt_pal_t *pal = pal_freertos_create();

    qwrt_t *rt = qwrt_create(&(qwrt_config_t){ .pal = pal });

    // ... 使用运行时 ...

    // 驱动事件循环
    while (pal->run_cycle(pal, 100) > 0) qwrt_tick(rt);

    qwrt_destroy(rt);
    pal_freertos_destroy(pal);
}
```

## 能力

| 功能 | 支持 | 备注 |
|---------|---------|-------|
| HTTP | ✅ | lwIP 套接字，自定义 HTTP/1.1 |
| HTTPS | ❌ | mbedTLS 集成待完成 |
| 流式 HTTP | ✅ | 分块传输解码 |
| 中止 | ✅ | 关闭套接字 |
| 文件系统 | ✅ | 通过类 POSIX API 的 SPIFFS/LittleFS |
| 存储 | ❌ | 改用文件系统 |
| 定时器 | ✅ | FreeRTOS 软件定时器 |
| 子进程 | ❌ | MCU 上不可用 |
| 事件循环 | ✅ | lwIP 套接字 select + 定时器轮询 |

## 事件循环

FreeRTOS PAL 使用 lwIP 套接字轮询和 FreeRTOS 定时器检查实现 `run_cycle`：

```c
while (pal->run_cycle(pal, 100) > 0) {
    qwrt_tick(rt);
}
```

- `timeout_ms` 是等待套接字活动的最大时间
- 返回已处理的事件数
- `timeout_ms` 为 0 时非阻塞

## HTTP

HTTP 使用 lwIP 的套接字 API（`lwip_socket`、`lwip_connect`、`lwip_send`、`lwip_recv`）。实现包括：

- 自定义 HTTP/1.1 请求构建和响应解析
- 流式传输的分块传输编码
- 连接超时处理
- 通过 lwIP 的 `netconn_gethostbyname` 进行 DNS 解析

## 文件系统

使用 ESP-IDF 文件系统层，支持 SPIFFS 和 LittleFS：

```
/mnt/app/main.js
/mnt/data/config.json
```

- `fs_read` — 从闪存读取文件
- `fs_write` — 写入文件到闪存
- `fs_exists` — 通过 `stat()` 检查文件是否存在
- `fs_remove` — 通过 `unlink()` 删除文件
- `fs_list` — 列出目录内容

所有文件系统操作都是同步的。频繁写入需注意闪存磨损——对易变数据可考虑使用内存存储。

## 定时器

使用 FreeRTOS 软件定时器（`xTimerCreate`）：

```c
// 定时器在定时器任务上运行，回调延迟到 JS 线程
void *handle = pal->timer_start(pal, 1000, 1, on_tick, &data);
// handle 是转换为 void* 的 TimerHandle_t
pal->timer_stop(pal, handle);
```

## 内存约束

FreeRTOS PAL 为内存受限环境设计：

- 栈大小经过仔细调校（参见 `platform/freertos/pal_freertos.c`）
- HTTP 缓冲区尽可能静态分配
- 通过固定大小内存池避免频繁 malloc/free 导致的堆碎片
- JSON 解析使用流式方法，而非构建完整 DOM

## 构建要求

此 PAL 仅在 ESP-IDF 环境中构建。标准 CMake 构建不包含它：

```cmake
# 在你的 ESP-IDF 项目的 CMakeLists.txt 中：
set(QWRT_PAL_FREERTOS ON)
add_subdirectory(qwrt)
```

要求：
- ESP-IDF v5.x 或更高版本
- 启用套接字 API 的 lwIP
- 支持软件定时器的 FreeRTOS
- WiFi 已配置并连接

## 平台特定说明

- **WiFi**：创建运行时之前必须先连接。PAL 不管理 WiFi — 使用 ESP-IDF 的 WiFi API。
- **OTA**：不由 PAL 处理。使用 ESP-IDF 的 OTA API 与 qwrt 配合使用。
- **功耗**：PAL 不管理休眠模式。在 `run_cycle` 调用之间使用 `esp_light_sleep_start()`。
- **SPIFFS**：必须在 `pal_freertos_create()` 之前初始化并挂载。挂载点通常为 `/mnt`。

---
title: PAL 接口参考
description: qwrt_pal_t 接口的完整参考 — 函数指针、回调类型、流式操作和生命周期要求。
---

# PAL 接口参考

`qwrt_pal_t` 中每个字段的完整参考。

## 标识与生命周期

### `user_data`

```c
void *user_data;
```

PAL 实现私有状态的不透明指针。由构造函数设置，qwrt 核心绝不触碰。

### `version`

```c
uint32_t version;
```

PAL 接口版本。当前 ABI 必须设置为 `1`。旧 PAL 如果保持此字段为 0（零初始化），将被视为"预版本化"状态，仍然可用。

### `name`

```c
const char *name;
```

用于诊断的可读名称：`"libuv"`、`"mock"`、`"freertos"`。必须是静态字符串或生命周期不短于本结构体的字符串。可以为 NULL。

### `init`

```c
int (*init)(qwrt_pal_t *pal);
```

由 `qwrt_create()` 在任何其他方法之前调用一次。用于分配资源、打开连接等。成功时返回 `QWRT_OK`，失败时返回 `QWRT_ERR_*`。如果此调用失败，`qwrt_create()` 返回 NULL。**可选** — NULL 表示无需初始化。

### `destroy`

```c
void (*destroy)(qwrt_pal_t *pal);
```

由 `qwrt_destroy()` 在所有上下文释放后调用一次。释放所有资源，取消未完成的异步操作。此调用返回后，不再调用任何 PAL 方法。**可选** — NULL 表示嵌入者手动释放 PAL。

## HTTP

### `http_request`

```c
void (*http_request)(qwrt_pal_t *pal,
                     const char *url, const char *method,
                     const char *headers, const char *body,
                     size_t body_len,
                     qwrt_pal_cb_t cb, void *cb_data);
```

执行 HTTP 请求并返回完整的响应主体。回调在成功时接收 `QWRT_OK` + JSON 字符串，失败时接收 `QWRT_ERR_*`。

响应 JSON 格式：
```json
{"status": 200, "headers": {"Content-Type": "application/json"}, "body": "..."}
```

### `http_request_stream`

```c
void (*http_request_stream)(qwrt_pal_t *pal,
                            const char *url, const char *method,
                            const char *headers, const char *body,
                            size_t body_len,
                            qwrt_pal_stream_ops_t *ops);
```

流式 HTTP 请求。响应通过 `qwrt_pal_stream_ops_t` 回调传递，而非单个最终回调。用于大型/长生命周期响应（SSE、LLM 流式输出）。**可选** — NULL 表示 PAL 不支持流式传输。

### `http_abort`

```c
void (*http_abort)(qwrt_pal_t *pal);
```

中止当前活跃的流式 HTTP 请求。向流的 `on_end` 回调传递 `QWRT_ERR_CANCELLED`。在没有活跃流时调用安全（无操作）。**可选** — NULL 表示不支持流式传输。

## 文件系统

### `fs_read`

```c
void (*fs_read)(qwrt_pal_t *pal, const char *path,
                qwrt_pal_cb_t cb, void *cb_data);
```

读取文件的完整内容。回调接收 `QWRT_OK` + 文件内容，或 `QWRT_ERR_*`。

### `fs_write`

```c
void (*fs_write)(qwrt_pal_t *pal, const char *path,
                 const char *data, size_t data_len,
                 qwrt_pal_cb_t cb, void *cb_data);
```

将数据写入文件（创建或覆盖）。回调接收 `QWRT_OK` 或 `QWRT_ERR_*`。

### `fs_exists`

```c
void (*fs_exists)(qwrt_pal_t *pal, const char *path,
                  qwrt_pal_cb_t cb, void *cb_data);
```

检查文件是否存在。回调接收 `QWRT_OK`（存在）或 `QWRT_ERR_NOT_FOUND`（不存在）。

### `fs_remove`

```c
void (*fs_remove)(qwrt_pal_t *pal, const char *path,
                  qwrt_pal_cb_t cb, void *cb_data);
```

删除文件。回调接收 `QWRT_OK` 或 `QWRT_ERR_*`。

### `fs_list`

```c
void (*fs_list)(qwrt_pal_t *pal, const char *path,
                qwrt_pal_cb_t cb, void *cb_data);
```

列出目录中的条目。回调接收 `QWRT_OK` + 条目名称的 JSON 数组。

## 存储

### `storage_get`

```c
void (*storage_get)(qwrt_pal_t *pal, const char *key,
                    qwrt_pal_cb_t cb, void *cb_data);
```

通过键获取值。回调接收 `QWRT_OK` + 值，或 `QWRT_ERR_NOT_FOUND`。

### `storage_set`

```c
void (*storage_set)(qwrt_pal_t *pal, const char *key,
                    const char *value, size_t value_len,
                    qwrt_pal_cb_t cb, void *cb_data);
```

设置键值对（创建或覆盖）。回调接收 `QWRT_OK` 或 `QWRT_ERR_*`。

### `storage_del`

```c
void (*storage_del)(qwrt_pal_t *pal, const char *key,
                    qwrt_pal_cb_t cb, void *cb_data);
```

删除一个键。回调接收 `QWRT_OK` 或 `QWRT_ERR_*`（如果键不存在则返回 `QWRT_ERR_NOT_FOUND`）。

## 定时器

### `timer_start`

```c
void *(*timer_start)(qwrt_pal_t *pal, uint64_t delay_ms, int repeat,
                      qwrt_pal_cb_t cb, void *cb_data);
```

启动一个定时器。`delay_ms` 是首次（或唯一一次）触发前的延迟时间。如果 `repeat` 非零，则定时器重复触发。每次触发时回调接收 `QWRT_OK`。返回用于 `timer_stop()` 的不透明句柄，或在 `QWRT_ERR_NO_MEMORY` 时返回 NULL。

### `timer_stop`

```c
void (*timer_stop)(qwrt_pal_t *pal, void *handle);
```

停止并释放一个定时器。传入 NULL 安全（无操作）。

## 时间

### `time_now`

```c
uint64_t (*time_now)(qwrt_pal_t *pal);
```

自 Unix 纪元以来的当前挂钟时间，以毫秒为单位。

### `hrtime`

```c
uint64_t (*hrtime)(qwrt_pal_t *pal);
```

高精度单调时间，以纳秒为单位，从一个任意（但固定）的纪元开始计时。供 `performance.now()` 使用。

## 工具

### `log`

```c
void (*log)(qwrt_pal_t *pal, int level, const char *msg);
```

输出日志消息。级别语义遵循 syslog：0=EMERG，7=DEBUG。**可选** — NULL 表示日志被静默丢弃。

### `mem_alloc`

```c
void *(*mem_alloc)(qwrt_pal_t *pal, size_t size);
```

分配内存。如果为 NULL，核心使用 `malloc()`。

### `mem_free`

```c
void (*mem_free)(qwrt_pal_t *pal, void *ptr);
```

释放由 `mem_alloc` 分配的内存。如果为 NULL，使用 `free()`。

### `random_bytes`

```c
void (*random_bytes)(qwrt_pal_t *pal, uint8_t *buf, size_t len);
```

用密码学安全的随机字节填充 `buf[0..len-1]`。设计为同步——硬件 RNG 和 `/dev/urandom` 足够快。

## 事件循环

### `run_cycle`

```c
int (*run_cycle)(qwrt_pal_t *pal, int timeout_ms);
```

驱动 PAL 事件循环的一次迭代。返回已处理的事件数，超时返回 0，如果循环应停止则返回 `< 0`。**可选** — NULL 表示没有事件循环需要驱动。

## 进程管理

### `spawn`

```c
void *(*spawn)(qwrt_pal_t *pal,
               const char *cmd,
               const char *const *args,
               const char *const *env);
```

派生一个子进程。`args` 以 NULL 结尾（约定 args[0] = cmd）。`env` 是以 NULL 结尾的 "KEY=VALUE" 对，或 NULL 表示继承环境。返回不透明句柄，失败时返回 NULL。**可选**。

### `channel_*`

```c
void *(*spawn_get_stdin)(qwrt_pal_t *pal, void *proc);
void *(*spawn_get_stdout)(qwrt_pal_t *pal, void *proc);
int   (*channel_send)(qwrt_pal_t *pal, void *ch, const char *data, size_t len);
void  (*channel_recv)(qwrt_pal_t *pal, void *ch, qwrt_pal_cb_t cb, void *cb_data);
void  (*channel_close)(qwrt_pal_t *pal, void *ch);
```

用于子进程 stdin/stdout 的 IPC 通道。全部**可选** — 如果平台不支持子进程则为 NULL。

### `join` / `terminate`

```c
int  (*join)(qwrt_pal_t *pal, void *proc, int timeout_ms);
void (*terminate)(qwrt_pal_t *pal, void *proc);
```

等待子进程退出（`join`）或强制终止（`terminate`）。**可选**。

## 保留

```c
void *reserved[4];
```

必须初始化为 NULL。未来版本的 qwrt 将为这些槽位分配含义。

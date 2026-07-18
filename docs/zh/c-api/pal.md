# PAL 接口

平台抽象层（PAL）是每个后端必须实现的虚函数表。本页文档定义了该契约的 C 类型。

## `qwrt_pal_t`

```c
typedef struct qwrt_pal_t qwrt_pal_t;

struct qwrt_pal_t {
    void *user_data;
    uint32_t version;
    const char *name;

    /* 核心 I/O */
    void (*http_request)(qwrt_pal_t *pal, ...);
    void (*http_request_stream)(qwrt_pal_t *pal, ...);
    void (*http_abort)(qwrt_pal_t *pal);

    /* 文件系统 */
    void (*fs_read)(qwrt_pal_t *pal, ...);
    void (*fs_write)(qwrt_pal_t *pal, ...);
    void (*fs_exists)(qwrt_pal_t *pal, ...);
    void (*fs_remove)(qwrt_pal_t *pal, ...);
    void (*fs_list)(qwrt_pal_t *pal, ...);

    /* 键值存储 */
    void (*storage_get)(qwrt_pal_t *pal, ...);
    void (*storage_set)(qwrt_pal_t *pal, ...);
    void (*storage_del)(qwrt_pal_t *pal, ...);

    /* 定时器 */
    void *(*timer_start)(qwrt_pal_t *pal, ...);
    void (*timer_stop)(qwrt_pal_t *pal, void *handle);

    /* 时间与熵 */
    uint64_t (*time_now)(qwrt_pal_t *pal);
    uint64_t (*hrtime)(qwrt_pal_t *pal);
    void (*random_bytes)(qwrt_pal_t *pal, uint8_t *buf, size_t len);

    /* 日志与内存 */
    void (*log)(qwrt_pal_t *pal, int level, const char *msg);
    void *(*mem_alloc)(qwrt_pal_t *pal, size_t size);
    void (*mem_free)(qwrt_pal_t *pal, void *ptr);

    /* 事件循环 */
    int (*run_cycle)(qwrt_pal_t *pal, int timeout_ms);

    /* 进程管理 */
    void *(*spawn)(qwrt_pal_t *pal, ...);
    void *(*spawn_get_stdin)(qwrt_pal_t *pal, void *proc);
    void *(*spawn_get_stdout)(qwrt_pal_t *pal, void *proc);
    int (*channel_send)(qwrt_pal_t *pal, ...);
    void (*channel_recv)(qwrt_pal_t *pal, ...);
    void (*channel_close)(qwrt_pal_t *pal, void *ch);
    int (*join)(qwrt_pal_t *pal, void *proc, int timeout_ms);
    void (*terminate)(qwrt_pal_t *pal, void *proc);

    /* 生命周期钩子 */
    int (*init)(qwrt_pal_t *pal);
    void (*destroy)(qwrt_pal_t *pal);

    /* 保留 */
    void *reserved[4];
};
```

## 字段分组

### 标识与生命周期

| 字段 | 必需 | 描述 |
|-------|----------|-------------|
| `user_data` | — | PAL 私有状态的不透明指针 |
| `version` | — | 接口版本（必须为 1） |
| `name` | 否 | 用于诊断的人类可读 PAL 名称 |
| `init` | 否 | 由 `qwrt_create()` 调用一次；返回 `QWRT_OK` 或错误 |
| `destroy` | 否 | 由 `qwrt_destroy()` 调用一次；释放所有资源 |

### 核心 I/O

| 字段 | 必需 | 描述 |
|-------|----------|-------------|
| `http_request` | 是 | 执行 HTTP 请求，将完整响应体传递给回调 |
| `http_request_stream` | 否 | 流式 HTTP（SSE、LLM 流式） |
| `http_abort` | 否 | 取消正在进行的流式请求 |

### 文件系统

| 字段 | 必需 | 描述 |
|-------|----------|-------------|
| `fs_read` | 否 | 读取文件内容 |
| `fs_write` | 否 | 写入/覆盖文件 |
| `fs_exists` | 否 | 检查文件是否存在 |
| `fs_remove` | 否 | 删除文件 |
| `fs_list` | 否 | 列出目录条目 |

### 事件循环

| 字段 | 必需 | 描述 |
|-------|----------|-------------|
| `run_cycle` | 否 | 驱动一次 PAL 事件循环迭代。NULL 表示没有循环需要驱动。 |

## `qwrt_pal_cb_t`

```c
typedef void (*qwrt_pal_cb_t)(void *user_data, int status,
                               const char *data, size_t data_len);
```

异步 PAL 操作的标准回调。

| 参数 | 描述 |
|-----------|-------------|
| `user_data` | 从调用点透传的不透明指针 |
| `status` | `qwrt_pal_err_t` 值（0 = 成功，<0 = 错误） |
| `data` | 结果载荷（JSON 字符串、文件内容等） |
| `data_len` | `data` 的字节长度（如果 data 为 NULL 则为 0） |

## `qwrt_pal_stream_ops_t`

```c
typedef struct qwrt_pal_stream_ops {
    void (*on_headers)(void *user_data, int status, const char *headers_json);
    void (*on_data)(void *user_data, const char *data, size_t len);
    void (*on_end)(void *user_data, int error_status);
    void *user_data;
} qwrt_pal_stream_ops_t;
```

HTTP 响应的流式回调。顺序为：
1. `on_headers` — HTTP 状态码 + 响应头（一次，可能是错误时的唯一调用）
2. `on_data` — 响应体数据块（零次或多次）
3. `on_end` — 流完成或中止（恰好一次）

## 参见

- [PAL 错误码](/c-api/errors) — 标准错误返回值
- [PAL 文档](/pal/) — 实现自己的后端
- [PAL 回调类型](/pal/callbacks) — 详细的回调文档
# 运行时生命周期

每个 qzjs 程序都遵循相同的生命周期：**创建 → 使用 → 销毁**。

## `qz_create`

```c
qz_t *qz_create(const qz_config_t *config);
```

创建一个新的 qzjs 运行时。qzjs 启动自己的内部线程和嵌入式 libuv 循环；`qz_create` 会阻塞，直到线程就绪且 `initial_script` 已求值。注册的扩展集在编译期通过 `QZ_EXTENSIONS` 宏固定；没有运行时扩展列表。

失败时返回 `NULL`（包括 `initial_script` 抛出异常）。

**参数：**

| 字段 | 类型 | 描述 |
|-------|------|-------------|
| `config.initial_script` | `const char *` | 在创建时于 qzjs 内部线程上求值的 JS；抛出异常会使 `qz_create` 返回 `NULL` |
| `config.message_cb` | `void (*)(qz_t *, const char *, size_t, void *)` | 出站消息回调；在 qzjs 线程上触发，必须线程安全 |
| `config.debug` | `int` | 位掩码。`0x2` 启用 DAP 调试器（也可设环境变量 `QZ_DEBUG=1`） |
| `config.control_plane` | `int` | `qz_control_plane_t`：`OFF`（0，缺省——`qz_control` 恒 -1）/ `IN_PROC`（1）/ `LOCAL`（2，额外开 AF_UNIX 端点） |
| `config.control_pipe_path` | `const char *` | `LOCAL` 档端点路径；`NULL` → `/tmp/qzjs-<pid>-<n>.ctl`（0600，SO_PEERCRED） |
| `config.worker_backend` | `int` | `qz_worker_backend_t`；缺省随编译模型（ISOLATED → `PROCESS`，THREAD → `THREAD`） |
| `config.host_data` | `void *` | 每个运行时的不透明指针，扩展可读取；作为 `data` 参数传给 `message_cb` |
| `config.initial_script_path` | `const char *` | 从文件读取并求值的 JS，替代 `initial_script`；两者都设时优先 |
| `config.initial_bytecode` | `const uint8_t *` | 预编译字节码（来自 `qz_compile`），在初始脚本之后求值 |
| `config.initial_bytecode_len` | `size_t` | `initial_bytecode` 的字节长度 |

**`qz_create` 内部做了什么：**

1. 启动 qzjs 的内部线程并初始化嵌入式 libuv 循环
2. 创建 `JSRuntime` 和初始上下文
3. 注册编译期扩展集（`QZ_EXTENSIONS` 表）
4. 将 WinterTC 兼容运行时注入初始上下文
5. 在内部线程上求值 `initial_script`
6. 初始脚本之后，若设置了 `initial_bytecode` 则求值预编译字节码

**线程模型：** 所有 JS 在 qzjs 的内部线程上运行；宿主发送消息（`qz_post_message`，线程安全）并通过 `message_cb` 接收。

## `qz_destroy`

```c
void qz_destroy(qz_t *rt);
```

优雅关闭运行时：请求内部线程退出并 join 它，然后销毁所有上下文并释放所有资源（句柄、定时器、polyfill 状态、libuv 循环）。传入 `NULL` 是安全的。仅限宿主线程 — 从调用 `qz_create` 的线程调用。

```c
qz_destroy(rt);
```

## `qz_compile`

```c
int qz_compile(const char *source, size_t len, const char *filename,
               uint8_t **out, size_t *out_len, char **err);
```

把 JS 源码编译为字节码 blob。独立函数——无需运行时实例。
成功返回 0（`*out` 为 malloc 缓冲，用 `free()` 释放；`*out_len` 为长度）；
失败返回 -1（`*err` 为 malloc 错误串，`free()` 释放）。`filename` 仅用于
错误/栈帧命名，可为 `NULL`。

字节码在启动时经 `qz_config_t.initial_bytecode` /
`initial_bytecode_len` 运行，或 CLI `qzjs --bytecode file.bc`。

**兼容性不保证：** 字节码与 qzjs 的具体构建绑定（引擎版本、序列化格式、
编译选项）。不同构建产出的字节码会让 `qz_create` 以
`SyntaxError: invalid version` 失败。请分发源码并在部署环境按目标构建编译。
见[字节码编译](/zh/guide/bytecode)。

## 宿主数据

每个运行时的数据在初始化期间可供扩展使用：

```c
void *qz_get_runtime_data(qz_t *rt);
void qz_set_runtime_data(qz_t *rt, void *data);
```

`qz_create` 将 `config->host_data` 复制到运行时上，因此扩展的 init 钩子可以在宿主获得 `rt` 指针之前读取它——解决了初始化时的排序死锁：

```c
qz_config_t cfg = { .initial_script = "postMessage('ready');",
                      .message_cb = on_message,
                      .host_data = my_state };
qz_t *rt = qz_create(&cfg);
// my_state 现在可通过 qz_get_runtime_data(rt) 在扩展 init 内部访问
// 并作为 message_cb 的 `data` 参数到达
```

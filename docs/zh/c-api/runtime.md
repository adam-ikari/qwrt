# 运行时生命周期

每个 qwrt 程序都遵循相同的生命周期：**创建 → 使用 → 销毁**。

## `qwrt_create`

```c
qwrt_t *qwrt_create(const qwrt_config_t *config);
```

创建一个新的 qwrt 运行时。qwrt 启动自己的内部线程和嵌入式 libuv 循环；`qwrt_create` 会阻塞，直到线程就绪且 `initial_script` 已求值。注册的扩展集在编译期通过 `QWRT_EXTENSIONS` 宏固定；没有运行时扩展列表。

失败时返回 `NULL`（包括 `initial_script` 抛出异常）。

**参数：**

| 字段 | 类型 | 描述 |
|-------|------|-------------|
| `config.initial_script` | `const char *` | 在创建时于 qwrt 内部线程上求值的 JS；抛出异常会使 `qwrt_create` 返回 `NULL` |
| `config.message_cb` | `void (*)(qwrt_t *, const char *, size_t, void *)` | 出站消息回调；在 qwrt 线程上触发，必须线程安全 |
| `config.debug` | `int` | 启用调试输出（0 或 1） |
| `config.host_data` | `void *` | 每个运行时的不透明指针，扩展可读取；作为 `data` 参数传给 `message_cb` |

**`qwrt_create` 内部做了什么：**

1. 启动 qwrt 的内部线程并初始化嵌入式 libuv 循环
2. 创建 `JSRuntime` 和初始上下文
3. 注册编译期扩展集（`QWRT_EXTENSIONS` 表）
4. 将 WinterTC 兼容运行时注入初始上下文
5. 在内部线程上求值 `initial_script`

**线程模型：** 所有 JS 在 qwrt 的内部线程上运行；宿主发送消息（`qwrt_post_message`，线程安全）并通过 `message_cb` 接收。

## `qwrt_destroy`

```c
void qwrt_destroy(qwrt_t *rt);
```

优雅关闭运行时：请求内部线程退出并 join 它，然后销毁所有上下文并释放所有资源（句柄、定时器、polyfill 状态、libuv 循环）。传入 `NULL` 是安全的。仅限宿主线程 — 从调用 `qwrt_create` 的线程调用。

```c
qwrt_destroy(rt);
```

## 宿主数据

每个运行时的数据在初始化期间可供扩展使用：

```c
void *qwrt_get_runtime_data(qwrt_t *rt);
void qwrt_set_runtime_data(qwrt_t *rt, void *data);
```

`qwrt_create` 将 `config->host_data` 复制到运行时上，因此扩展的 init 钩子可以在宿主获得 `rt` 指针之前读取它——解决了初始化时的排序死锁：

```c
qwrt_config_t cfg = { .initial_script = "postMessage('ready');",
                      .message_cb = on_message,
                      .host_data = my_state };
qwrt_t *rt = qwrt_create(&cfg);
// my_state 现在可通过 qwrt_get_runtime_data(rt) 在扩展 init 内部访问
// 并作为 message_cb 的 `data` 参数到达
```

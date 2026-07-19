# 运行时生命周期

每个 qwrt 程序遵循相同的生命周期：**创建 → 使用 → 销毁**。

## `qwrt_create`

```c
qwrt_t *qwrt_create(const qwrt_config_t *config);
```

创建一个新的 qwrt 运行时。`config` 中的 PAL 是**借用的**（非拥有）——它必须比运行时存活更久。注册的扩展集在编译期通过 `QWRT_EXTENSIONS` 宏固定；没有运行时扩展列表。

失败时返回 `NULL`。

**参数：**

| 字段 | 类型 | 描述 |
|-------|------|-------------|
| `config.pal` | `const qwrt_pal_t *` | 平台抽象层（必需） |
| `config.debug` | `int` | 启用调试输出（0 或 1） |
| `config.host_data` | `void *` | 每个运行时的不透明指针，扩展可读取 |

**`qwrt_create` 内部做了什么：**

1. 如果提供了钩子，调用 `pal->init(pal)`
2. 创建 `JSRuntime` 和初始上下文
3. 注册编译期扩展集（`QWRT_EXTENSIONS` 表）
4. 将 WinterCG 兼容运行时注入初始上下文

**线程绑定：** 所有后续的 `qwrt_*` 调用必须来自创建线程。

## `qwrt_destroy`

```c
void qwrt_destroy(qwrt_t *rt);
```

销毁运行时并释放所有资源：上下文、句柄、定时器、polyfill 状态。PAL **不会**被释放——调用者拥有它，必须单独销毁。传入 `NULL` 是安全的。

```c
qwrt_destroy(rt);
pal_mock_destroy(pal);  // 调用者拥有 PAL
```

## `qwrt_tick`

```c
int qwrt_tick(qwrt_t *rt);
```

排空待处理的 JS 微任务和延迟的 PAL 回调。成功返回 0，错误返回 -1。

宿主通过在 `pal->run_cycle` 之后调用此函数来驱动事件循环：

```c
while (pal->run_cycle(pal, 100) > 0) {
    qwrt_tick(rt, 100);
}
```

## `qwrt_reset`

```c
int qwrt_reset(qwrt_t *rt, const qwrt_config_t *config);
```

销毁所有上下文并根据 `config` 创建全新的初始上下文。PAL 被重用。成功返回 0，错误返回 -1。

## 宿主数据

每个运行时的数据在初始化期间可供扩展使用：

```c
void *qwrt_get_runtime_data(qwrt_t *rt);
void qwrt_set_runtime_data(qwrt_t *rt, void *data);
```

`qwrt_create` 将 `config->host_data` 复制到运行时上，因此扩展的 init 钩子可以在宿主获得 `rt` 指针之前读取它——解决了初始化时的排序死锁：

```c
qwrt_config_t cfg = { .pal = pal, .host_data = my_state };
qwrt_t *rt = qwrt_create(&cfg);
// my_state 现在可通过 qwrt_get_runtime_data(rt) 在扩展 init 内部访问
```
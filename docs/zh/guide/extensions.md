---
title: 扩展
description: Qwrt.js 构建时原生 C 扩展 — qwrt_ext_t 接口、QWRT_EXTENSIONS 宏、生命周期钩子和每运行时数据。
---

# 扩展

扩展是为 JS 上下文添加全局对象和函数的原生 C 模块。它们实现了带有生命周期钩子的 `qwrt_ext_t` 接口。

## 内置扩展

| 扩展 | 选项 | JS API |
|-----------|--------|-------|
| `ext_compress` | `QWRT_WITH_COMPRESS` | gzip/zlib/deflate 压缩 |
| `ext_crypto` | `QWRT_WITH_CRYPTO_EXT` | SHA、HMAC、PBKDF2、AES-GCM |
| `ext_textcodec` | `QWRT_WITH_TEXTCODEC` | UTF-8、Base64 编解码 |
| `ext_wamr` | `QWRT_WITH_WAMR` | 通过 WAMR 的 WebAssembly（默认） |
| `ext_wasm3` | `QWRT_WITH_WASM3` | 通过 wasm3 的 WebAssembly（可选） |

**注意：** `ext_wamr` 和 `ext_wasm3` 互斥 — 两者都注册 `WebAssembly` 全局对象，因此每次构建只能启用其中一个。

内置扩展会在每个新上下文上自动注册。

## 扩展接口

```c
typedef struct qwrt_ext_t {
    const char *name;          // 人类可读的名称，用于诊断
    int (*init)(qwrt_ext_t *ext, qwrt_t *rt);      // 上下文创建时调用
    void (*destroy)(qwrt_ext_t *ext, qwrt_t *rt);   // 上下文销毁时调用
    int (*suspend)(qwrt_ext_t *ext, qwrt_t *rt);    // 上下文挂起时调用
    int (*resume)(qwrt_ext_t *ext, qwrt_t *rt);     // 上下文恢复时调用
    void *user_data;           // 不透明的扩展状态
} qwrt_ext_t;
```

## 编写自定义扩展

```c
#include <qwrt/qwrt.h>
#include <quickjs.h>

static int my_ext_init(qwrt_ext_t *ext, qwrt_t *rt) {
    JSContext *ctx = qwrt_get_jsctx(rt);
    if (!ctx) return -1;

    // 添加全局函数
    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "hello",
        JS_NewCFunction(ctx, my_hello_fn, "hello", 0));
    JS_FreeValue(ctx, global);

    return 0;  // 成功
}

static void my_ext_destroy(qwrt_ext_t *ext, qwrt_t *rt) {
    // 清理扩展资源
    // JSContext 的清理由 qwrt 处理
}

static int my_ext_suspend(qwrt_ext_t *ext, qwrt_t *rt) {
    // 保存状态、关闭连接等
    return 0;
}

static int my_ext_resume(qwrt_ext_t *ext, qwrt_t *rt) {
    // 恢复状态、重新打开连接等
    return 0;
}

qwrt_ext_t my_extension = {
    .name = "my_extension",
    .init = my_ext_init,
    .destroy = my_ext_destroy,
    .suspend = my_ext_suspend,
    .resume = my_ext_resume,
    .user_data = NULL,
};
```

## 注册扩展

扩展在**构建时**通过 `QWRT_EXTENSIONS` 宏（定义在
`include/qwrt/qwrt_ext_registry.h` 中）注册。没有运行时注册 API —
扩展集在编译 qwrt 库时固定。

### 内置扩展

内置扩展（compress/crypto/textcodec/wamr）在其 `QWRT_WITH_*` CMake 选项开启时自动注册。
它们作为条件槽出现在 `QWRT_DEFAULT_EXTENSIONS` 中
（禁用的内置扩展变为 NULL 槽，在初始化时被跳过）。

### 添加自定义扩展（非侵入式）

父项目可以**不修改 qwrt 源码**来添加自己的扩展：将扩展的 `.c` 编译进 qwrt 目标
（使其 `&my_extension` 符号对 `context.c` 可见），并将其追加到 `QWRT_EXTENSIONS`：

```cmake
# 在父项目的 CMakeLists.txt 中，在 add_subdirectory(qwrt) 之前：
set(QWRT_EXTENSIONS "QWRT_DEFAULT_EXTENSIONS, &my_extension")
set(QWRT_EXTRA_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/my_extension.c)
add_subdirectory(deps/qwrt)
```

`QWRT_EXTRA_SOURCES` 将源文件添加到 `qwrt` 目标；`QWRT_EXTENSIONS`
覆盖表格以在默认集之后追加 `&my_extension`。要**裁剪**内置扩展，
只需列出你需要的条目，而不是 `QWRT_DEFAULT_EXTENSIONS`。

## 生命周期钩子

- **`init`** — 在扩展注册到上下文时调用（在 `qwrt_create` / `qwrt_spawn` / `qwrt_reset` 时）。注册 JS 全局对象，分配资源。成功返回 0，失败返回 <0。
- **`destroy`** — 在上下文销毁时调用。释放扩展资源。JSContext 清理是自动的 — 你只需要释放自己的分配。
- **`suspend`** — 在上下文挂起时调用。保存状态、暂停定时器、关闭连接。
- **`resume`** — 在上下文恢复时调用。恢复状态、恢复定时器、重新打开连接。

所有钩子都接收扩展和运行时。通过 `qwrt_get_jsctx(rt)` 获取活跃的 `JSContext*`。

### init 中的每运行时数据

`qwrt_ext_t.user_data` 字段位于**共享的编译时**扩展结构体上 — 它不是每实例的。
要在 `init` 中获取每运行时数据（`init` 在 `qwrt_create` 期间运行，此时宿主尚未获得 `rt`），
在 `qwrt_create` 之前设置 `config.host_data` 并通过
`qwrt_get_runtime_data(rt)` 读取：

```c
/* 宿主端： */
qwrt_config_t cfg = { .pal = pal, .host_data = my_per_rt_state };
qwrt_t *rt = qwrt_create(&cfg);

/* 扩展 init： */
static int my_ext_init(qwrt_ext_t *ext, qwrt_t *rt) {
    my_state_t *st = (my_state_t *)qwrt_get_runtime_data(rt);
    /* st 是宿主通过 config 设置的每实例数据 */
    ...
}
```

这解决了初始化时的排序死锁：`rt` 在 `init` 内部是有效的，
即使宿主尚未收到它。
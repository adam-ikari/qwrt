# 扩展（C API）

扩展是向 JS 上下文添加全局对象和函数的原生 C 模块。它们实现 `qwrt_ext_t` 接口及其生命周期钩子。

## `qwrt_ext_t`

```c
typedef struct qwrt_ext_t {
    const char *name;
    int (*init)(qwrt_ext_t *ext, qwrt_t *rt);
    void (*destroy)(qwrt_ext_t *ext, qwrt_t *rt);
    int (*suspend)(qwrt_ext_t *ext, qwrt_t *rt);
    int (*resume)(qwrt_ext_t *ext, qwrt_t *rt);
    void *user_data;
} qwrt_ext_t;
```

| 字段 | 描述 |
|-------|-------------|
| `name` | 用于诊断的人类可读名称 |
| `init` | 在上下文创建时调用 — 注册 JS 全局对象，分配资源。成功返回 0，失败返回 <0。 |
| `destroy` | 在上下文销毁时调用 — 释放扩展资源。JSContext 清理是自动的。 |
| `suspend` | 在上下文挂起时调用 — 保存状态、暂停定时器、关闭连接。 |
| `resume` | 在上下文恢复时调用 — 恢复状态、恢复定时器、重新打开连接。 |
| `user_data` | 不透明的扩展状态。**注意：** 这在所有运行时之间共享 — 对于每个实例的数据，使用 `config.host_data`。 |

## 注册模型

扩展在**编译期**通过 `QWRT_EXTENSIONS` 宏注册（定义在 `include/qwrt/qwrt_ext_registry.h` 中）。没有运行时注册 API — 扩展集在 qwrt 库编译时固定。

```c
// include/qwrt/qwrt_ext_registry.h
#define QWRT_DEFAULT_EXTENSIONS \
    QWRT_EXT_IF_WITH(COMPRESS,   &qwrt_compress_ext) \
    QWRT_EXT_IF_WITH(CRYPTO_EXT, &qwrt_crypto_ext)   \
    QWRT_EXT_IF_WITH(TEXTCODEC,  &qwrt_textcodec_ext) \
    QWRT_EXT_IF_WITH(WAMR,       &qwrt_wamr_ext)
```

父项目通过在包含 qwrt 子目录之前覆盖 `QWRT_EXTENSIONS` 来添加自定义扩展：

```cmake
set(QWRT_EXTENSIONS "QWRT_DEFAULT_EXTENSIONS, &my_extension")
set(QWRT_EXTRA_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/my_extension.c)
add_subdirectory(deps/qwrt)
```

## 内置扩展

| 扩展 | CMake 选项 | JS API |
|-----------|-------------|-------|
| `ext_compress` | `QWRT_WITH_COMPRESS` | gzip/zlib/deflate |
| `ext_crypto` | `QWRT_WITH_CRYPTO_EXT` | crypto.subtle（SHA、HMAC、PBKDF2、AES-GCM） |
| `ext_textcodec` | `QWRT_WITH_TEXTCODEC` | TextEncoder、TextDecoder |
| `ext_wamr` | `QWRT_WITH_WAMR` | WebAssembly（WAMR，默认） |
| `ext_wasm3` | `QWRT_WITH_WASM3` | WebAssembly（wasm3，可选） |

## 编写扩展

```c
#include <qwrt/qwrt.h>
#include <quickjs.h>

static int my_ext_init(qwrt_ext_t *ext, qwrt_t *rt) {
    JSContext *ctx = qwrt_get_jsctx(rt);
    if (!ctx) return -1;

    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "hello",
        JS_NewCFunction(ctx, my_hello_fn, "hello", 0));
    JS_FreeValue(ctx, global);

    return 0;
}

static void my_ext_destroy(qwrt_ext_t *ext, qwrt_t *rt) {
    // 释放任何扩展特定资源
}

qwrt_ext_t my_extension = {
    .name = "my_extension",
    .init = my_ext_init,
    .destroy = my_ext_destroy,
    .suspend = NULL,
    .resume = NULL,
    .user_data = NULL,
};
```

## 每个运行时的数据

`qwrt_ext_t.user_data` 在所有运行时之间共享。对于每个实例的状态，使用 `config.host_data`：

```c
qwrt_config_t cfg = { .pal = pal, .host_data = my_per_rt_state };
qwrt_t *rt = qwrt_create(&cfg);

// 在扩展 init 内部：
my_state_t *st = (my_state_t *)qwrt_get_runtime_data(rt);
```

## 参见

- [扩展指南](/guide/extensions) — 详细的扩展文档
- [扩展注册头文件](/c-api/runtime) — 运行时生命周期
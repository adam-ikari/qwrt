# C API 参考

Qwrt.js 暴露了一个小巧、专注的 C API 接口。每个函数都操作一个不透明的 `qwrt_t*` 运行时句柄。该 API 是单线程的——所有调用必须来自创建运行时的线程。

## API 分组

| 分组 | 描述 |
|-------|-------------|
| [运行时生命周期](/c-api/runtime) | `qwrt_create`、`qwrt_destroy`、`qwrt_tick` |
| [JS 求值](/c-api/eval) | `qwrt_eval`、`qwrt_eval_bytecode`、`qwrt_call`、`qwrt_compile` |
| [多上下文](/guide/multi-context) | `qwrt_spawn`、`qwrt_suspend`、`qwrt_resume`、`qwrt_destroy_ctx` |
| [PAL 接口](/c-api/pal) | `qwrt_pal_t`、回调类型、流式 |
| [扩展](/c-api/extensions) | `qwrt_ext_t`、生命周期钩子 |
| [错误码](/c-api/errors) | `qwrt_pal_err_t`、标准化的错误返回值 |
| [宿主数据](/c-api/runtime#host-data) | `qwrt_get_runtime_data`、`qwrt_set_runtime_data` |

## 快速示例

```c
#include <qwrt/qwrt.h>
#include <pal_mock.h>
#include <stdio.h>

int main(void) {
    qwrt_pal_t *pal = pal_mock_create();
    qwrt_t *rt = qwrt_create(&(qwrt_config_t){ .pal = pal });
    if (!rt) { fprintf(stderr, "create failed\n"); return 1; }

    char *result = NULL;
    if (qwrt_eval(rt, "1 + 1", &result) == 0) {
        printf("1+1 = %s\n", result);
        qwrt_free(result);
    }

    qwrt_tick(rt);  // 排空微任务
    qwrt_destroy(rt);
    pal_mock_destroy(pal);
    return 0;
}
```

## 构建集成

```cmake
find_package(qwrt REQUIRED)
target_link_libraries(your_app PRIVATE qwrt::qwrt)
```

## 线程模型

Qwrt.js 在设计上就是**单线程**的。`JSContext` 绑定到调用 `qwrt_create` 的线程。所有后续的 `qwrt_*` 调用必须来自同一线程。PAL 回调（来自 libuv 等）在事件循环线程上触发，并通过 `qwrt_tick` 延迟以在有效上下文中重放。
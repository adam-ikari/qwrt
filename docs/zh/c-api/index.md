# C API 参考

qzjs 暴露了一个小巧、专注的 C API 接口。每个函数都操作一个不透明的 `qz_t*` 运行时句柄。该 API 是单线程的——所有调用必须来自创建运行时的线程。

## API 分组

| 分组 | 描述 |
|-------|-------------|
| [运行时生命周期](/zh/c-api/runtime) | `qz_create`、`qz_destroy`、`qz_post_message` |
| [多上下文](/zh/guide/multi-context) | 一个运行时内隔离的 JS 上下文 |
| [扩展](/zh/c-api/extensions) | `qz_ext_t`、生命周期钩子 |
| [宿主数据](/zh/c-api/runtime#宿主数据) | `qz_get_runtime_data`、`qz_set_runtime_data` |

## 快速示例

```c
#include <qzjs/qzjs.h>
#include <stdio.h>

static void on_message(qz_t *rt, const char *json, size_t len, void *data) {
    (void)rt; (void)data;
    printf("received: %.*s\n", (int)len, json);
}

int main(void) {
    qz_config_t cfg = {0};
    cfg.initial_script = "postMessage(1 + 1);";
    cfg.message_cb = on_message;
    qz_t *rt = qz_create(&cfg);
    if (!rt) { fprintf(stderr, "create failed\n"); return 1; }

    qz_post_message(rt, "{\"cmd\":\"echo\",\"data\":\"hi\"}", 26);

    qz_destroy(rt);
    return 0;
}
```

## 构建集成

```cmake
find_package(qzjs REQUIRED)
target_link_libraries(your_app PRIVATE qzjs::qzjs)
```

## 线程模型

qzjs 在设计上就是**单线程**的。所有 JS 在 qzjs 自己的内部线程上运行（该线程同时运行嵌入式 libuv 循环）— 宿主线程从不直接调用 JS。宿主通过 JSON 消息通信：`qz_post_message` 是线程安全的（入站），`message_cb` 在 qzjs 线程上触发（你的回调必须线程安全）。`qz_destroy` 仅限宿主线程。
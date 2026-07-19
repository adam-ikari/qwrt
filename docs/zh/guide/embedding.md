---
title: 嵌入模式
description: 在 C 应用程序中嵌入 Qwrt.js 的模式 — 宿主数据、自定义扩展、PAL 集成以及多实例设置。
---

# 嵌入模式

在 C 应用程序中嵌入 qwrt 的常见模式。

## 基本嵌入

```c
#include <qwrt/qwrt.h>
#include <pal_uv.h>

int main(void) {
    qwrt_pal_t *pal = pal_uv_create(uv_default_loop());
    qwrt_t *rt = qwrt_create(&(qwrt_config_t){ .pal = pal });

    // 你的应用程序逻辑
    char *result = NULL;
    qwrt_eval(rt, "your_js_code_here", &result);
    qwrt_free(result);

    // 事件循环
    pal->run_cycle(pal, 100); qwrt_tick(rt, 100);

    qwrt_destroy(rt);
    pal_uv_destroy(pal);
    return 0;
}
```

## 从 JS 调用 C 函数

将 C 函数注册为 JS 全局对象：

```c
#include <quickjs.h>

static JSValue greet(JSContext *ctx, JSValue this_val,
                     int argc, JSValue *argv) {
    QWRT_UNUSED(this_val);
    const char *name = "World";
    if (argc > 0) name = JS_ToCString(ctx, argv[0]);
    printf("Hello, %s!\n", name);
    if (argc > 0) JS_FreeCString(ctx, name);
    return JS_UNDEFINED;
}

// 在自定义扩展的 init 钩子中注册：
static int my_ext_init(qwrt_ext_t *ext, qwrt_t *rt) {
    QWRT_UNUSED(ext);
    JSContext *ctx = qwrt_get_jsctx(rt);
    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "greet",
        JS_NewCFunction(ctx, greet, "greet", 1));
    JS_FreeValue(ctx, global);
    return 0;
}
```

## 从 C 调用 JS 并传递结构化数据

```c
// 定义一个 JS 函数
qwrt_eval(rt,
    "function process(data) {"
    "  return { doubled: data.value * 2, ok: true };"
    "}"
, NULL);

// 使用 JSON 参数调用它
char *result = NULL;
qwrt_call(rt, "process", "[{\"value\": 21}]", &result);
// result = "{\"doubled\":42,\"ok\":true}"
printf("JS returned: %s\n", result);
qwrt_free(result);
```

## 按请求隔离上下文

为每个传入请求创建全新的上下文，完成后销毁：

```c
void handle_request(qwrt_t *rt, const char *js_code) {
    qwrt_config_t cfg = { .pal = request_pal, .debug = 0 };
    int ctx_id = qwrt_spawn(rt, &cfg);

    qwrt_suspend(rt);
    qwrt_resume(rt, ctx_id);

    char *result = NULL;
    int ret = qwrt_eval(rt, js_code, &result);
    qwrt_free(result);

    qwrt_suspend(rt);
    qwrt_resume(rt, 0);  // 回到主上下文
    qwrt_destroy_ctx(rt, ctx_id);
}
```

## 使用 Mock PAL 进行测试

```c
#include <pal_mock.h>

void test_my_js(void) {
    pal_mock_t *mock = pal_mock_create();

    // 为 fetch 预置响应
    pal_mock_add_response(mock, "https://api.example.com/data",
        "{\"status\":200,\"headers\":{},\"body\":\"{\\\"ok\\\":true}\"}");

    qwrt_pal_t *pal = pal_mock_get_pal(mock);
    qwrt_t *rt = qwrt_create(&(qwrt_config_t){ .pal = pal });

    char *result = NULL;
    qwrt_eval(rt,
        "fetch('https://api.example.com/data')"
        "  .then(r => r.json())"
        "  .then(d => console.log(d.ok))",
        &result);
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_mock_destroy(mock);
}
```

## 多个独立运行时

由于 qwrt 具有零全局状态，你可以运行多个 `qwrt_t` 实例：

```c
qwrt_t *rt1 = qwrt_create(&(qwrt_config_t){ .pal = pal1 });
qwrt_t *rt2 = qwrt_create(&(qwrt_config_t){ .pal = pal2 });

// 每个实例完全独立
qwrt_eval(rt1, "var x = 1;", NULL);
qwrt_eval(rt2, "var x = 2;", NULL);

// 驱动两个事件循环
while (running) {
    pal1->run_cycle(pal1, 10);
    pal2->run_cycle(pal2, 10);
    qwrt_tick(rt1);
    qwrt_tick(rt2);
}

qwrt_destroy(rt1);
qwrt_destroy(rt2);
```

## 错误处理模式

```c
char *result = NULL;
int ret = qwrt_eval(rt, code, &result);

if (ret < 0) {
    // JS 异常 — result 包含错误消息
    fprintf(stderr, "JS error: %s\n", result ? result : "unknown");
    qwrt_free(result);
} else if (result) {
    // 成功且有返回值
    printf("OK: %s\n", result);
    qwrt_free(result);
}
// 否则：成功但无返回值（如 console.log）
```

## 内存管理

- 来自 `qwrt_eval`/`qwrt_call`/`qwrt_compile` 的结果必须使用 `qwrt_free` 释放
- `qwrt_free(NULL)` 是安全的
- PAL 由调用者拥有——在 `qwrt_destroy` 之后释放它
- 每个运行时的宿主数据：在 `qwrt_create` 之前设置 `config.host_data`；扩展的 `init` 钩子在创建期间通过 `qwrt_get_runtime_data(rt)` 读取它（rt 在 init 内部有效，在宿主接收到之前）。注意：`qwrt_ext_t.user_data` 位于共享的编译期扩展结构体上——对于每个实例的数据，请使用 `qwrt_get_runtime_data`/`qwrt_set_runtime_data`，而非 `user_data`（它在运行时之间共享）。
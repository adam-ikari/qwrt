---
title: JS 执行
description: Qwrt.js 如何求值 JavaScript — qwrt_eval、字节码求值、结果处理、错误传播以及 Promise 支持。
---

# JS 执行

qwrt 提供三种运行 JavaScript 的方式：求值源代码、求值字节码以及调用命名函数。

## 求值源代码

```c
char *result = NULL;
int ret = qwrt_eval(rt, "1 + 1", &result);
if (ret == 0) {
    printf("Result: %s\n", result);  // "2"
    qwrt_free(result);
} else {
    // JS 异常 — result 包含错误消息
    fprintf(stderr, "Error: %s\n", result ? result : "unknown");
    qwrt_free(result);
}
```

参数：
- `rt` — 运行时
- `code` — 以 null 结尾的 JavaScript 源字符串
- `result` — 如果非 NULL，接收一个 `malloc` 分配的字符串（使用 `qwrt_free` 释放）

成功返回 0，JS 异常返回 <0。

WinterTC 兼容运行时会在每个上下文的第一次 `qwrt_eval` 之前**自动注入** — 你无需手动设置 `fetch`、`console` 等。

## 求值字节码

对于生产环境，将 JS 预编译为 QuickJS 字节码以获得更快的启动速度和更小的体积：

```c
// 编译步骤（执行一次，分发字节码）
size_t bytecode_len;
uint8_t *bytecode = qwrt_compile(rt, "1 + 1", 5, &bytecode_len);

// 求值步骤（快速 — 无需解析）
char *result = NULL;
qwrt_eval_bytecode(rt, bytecode, bytecode_len, &result);
qwrt_free(result);
qwrt_free(bytecode);
```

详见[字节码编译](/guide/bytecode)。

## 调用 JavaScript 函数

按名称调用全局 JS 函数，使用 JSON 参数：

```c
// 首先，定义函数
qwrt_eval(rt,
    "function add(a, b) { return a + b; }"
, NULL);

// 然后调用它
char *result = NULL;
qwrt_call(rt, "add", "[3, 4]", &result);
printf("add(3, 4) = %s\n", result);  // "7"
qwrt_free(result);
```

- `func` — 全局函数名称（必须在活动上下文中存在）
- `args_json` — 参数的 JSON 数组（如 `"[1, \"hello\", true]"`），无参数时为 NULL
- `result` — 接收 JSON 字符串化的返回值

## 排空微任务

许多 JS API（Promise、async/await）会将微任务入队。调用 `qwrt_tick` 来排空它们：

```c
// 在任何创建 Promise 的求值之后：
qwrt_tick(rt, 100);
```

通常你在循环中与 PAL 事件循环一起驱动它：

```c
while (pal->run_cycle(pal, 100) > 0) {
    qwrt_tick(rt, 100);
}
```

## 访问 JSContext

对于高级用法（直接使用 QuickJS API），获取原始的 `JSContext*`：

```c
JSContext *ctx = qwrt_get_jsctx(rt);
if (ctx) {
    // 直接使用 QuickJS C API
    JSValue val = JS_NewInt32(ctx, 42);
    // ...
}
```

该指针在上下文被销毁或运行时重置之前一直有效。
---
title: JS 求值
description: 发送 JS 求值/调用指令的便捷函数 —— qz_eval 与 qz_call，以及它们的本质。
---

# JS 求值 —— 便捷函数

`qz_eval` / `qz_call` 是**便捷函数**：把"向 JS 发指令"这个常用操作封装成简单函数调用。
它们**不是新的执行机制**——底层是对 JSON 消息边界的语法糖。

## `int qz_eval(qz_t *rt, const char *code)`

发送一条 eval 指令——在运行时里执行一段 JS 代码：

```c
qz_eval(rt, "1 + 1");
qz_eval(rt, "globalThis.add = function(a,b){ return a + b; };");
```

等价于手写 `qz_post_message(rt, "{\"corr\":1,\"cmd\":\"eval\",\"code\":\"...\"}", len)`。

## `int qz_call(qz_t *rt, const char *fn, const char *args_json)`

发送一条调用指令——用 JSON 参数调用一个全局 JS 函数：

```c
qz_call(rt, "add", "[3, 4]");   // 调用 globalThis.add(3, 4)
```

`fn` 是全局函数名；`args_json` 是 JSON 数组，或 `NULL` 表示 `[]`。

两者均线程安全；返回 `0` 成功，`-1` 失败。

## 本质

- **只是发送**。每个函数构造 `{corr, cmd, ...}` 指令 JSON（含字符串转义、单调递增的
  `corr`）交给 `qz_post_message`。不引入新的执行路径。
- **结果经 message_cb 回流**。JS 侧在 `onmessage` 收到指令、执行后，用
  `postMessage({corr, result})` 把结果发回宿主，宿主在 `message_cb` 里收到。`corr`
  单调递增，用于把结果与请求对应。
- **内置默认处理器**。若宿主 `initial_script` 未定义全局 `onmessage`，qzjs 注入内置
  处理器，自动应答 `{cmd:"eval"}` 与 `{cmd:"call"}` 指令——`qz_eval`/`qz_call`
  开箱即用。宿主自定义 `onmessage` 时由宿主处理器接管（默认处理器不覆盖）。
- **异步**。发送即返回，结果稍后经 `message_cb` 到达；要收集结果就在 `message_cb`
  里按 `corr` 匹配。

## 完整示例

```c
#include <qzjs/qzjs.h>

static void on_message(qz_t *rt, const char *json, size_t len, void *data) {
    (void)rt; (void)data;
    printf("JS: %.*s\n", (int)len, json);   // {"corr":1,"result":42}
}

int main(void) {
    qz_config_t cfg = {0};
    cfg.message_cb = on_message;   // 未设 initial_script → 注入默认处理器
    qz_t *rt = qz_create(&cfg);

    qz_eval(rt, "40 + 2");                  // → {"corr":1,"result":42}
    qz_eval(rt, "globalThis.mul = (a,b) => a*b;");
    qz_call(rt, "mul", "[6,7]");            // → {"corr":2,"result":42}

    /* ... */
    qz_destroy(rt);
    return 0;
}
```

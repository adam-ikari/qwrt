# 示例

完整的可运行示例，展示如何在 C 应用程序中使用 Qwrt.js。每个示例在 `cmake -DQWRT_BUILD_EXAMPLES=ON` 时构建。

## Minimal（最小示例）

最简单的 qwrt 程序。创建运行时，执行 JavaScript，输出结果。

```c
// example_minimal.c
#include <qwrt/qwrt.h>
#include <pal_uv.h>
#include <stdio.h>

int main(void) {
    qwrt_pal_t *pal = pal_uv_create(uv_default_loop());
    qwrt_t *rt = qwrt_create(&(qwrt_config_t){ .pal = pal });
    if (!rt) return 1;

    char *result = NULL;
    if (qwrt_eval(rt, "1 + 1", &result) == 0) {
        printf("1 + 1 = %s\n", result);
        qwrt_free(result);
    }

    pal->run_cycle(pal, 100); qwrt_tick(rt, 100);

    qwrt_destroy(rt);
    pal_uv_destroy(pal);
    return 0;
}
```

**关键概念：**
- `pal_uv_create()` — 创建 Linux/macOS 用的 libuv PAL
- `qwrt_create()` — 通过配置创建运行时
- `qwrt_eval()` — 执行 JavaScript，以 JSON 字符串获取结果
- `qwrt_free()` — 释放结果内存
- 事件循环：`pal->run_cycle()` + `qwrt_tick()`
- `qwrt_destroy()` — 清理资源

## Mock（模拟示例）

使用模拟 PAL 进行确定性测试，无网络、无文件系统。适用于单元测试和 CI。

```c
// example_mock.c
#include <qwrt/qwrt.h>
#include <pal_mock.h>
#include <stdio.h>

int main(void) {
    qwrt_pal_t *pal = pal_mock_create();
    qwrt_t *rt = qwrt_create(&(qwrt_config_t){ .pal = pal });
    if (!rt) return 1;

    char *result = NULL;
    qwrt_eval(rt, "console.log('Hello from mock!'); 42", &result);
    printf("Result: %s\n", result);
    qwrt_free(result);

    qwrt_tick(rt, 100);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
    return 0;
}
```

**与 uv 示例的关键区别：**
- 使用 `pal_mock_create()` 而不是 `pal_uv_create()`
- 使用 `pal_mock_destroy()` 而不是 `pal_uv_destroy()`
- 无需事件循环（mock 中 `run_cycle` 为 NULL）

## Fetch（网络请求）

从 JavaScript 中使用 `fetch()` API 发起 HTTP 请求。需要带网络的 libuv PAL。

```c
// example_fetch.c
#include <qwrt/qwrt.h>
#include <pal_uv.h>
#include <stdio.h>

int main(void) {
    qwrt_pal_t *pal = pal_uv_create(uv_default_loop());
    qwrt_t *rt = qwrt_create(&(qwrt_config_t){ .pal = pal });
    if (!rt) return 1;

    char *result = NULL;
    qwrt_eval(rt,
        "(async function() {"
        "  const res = await fetch('https://httpbin.org/json');"
        "  const data = await res.json();"
        "  return JSON.stringify(data);"
        "})()",
        &result);

    printf("Fetch: %s\n", result ? result : "ERROR");
    qwrt_free(result);

    pal->run_cycle(pal, 100); qwrt_tick(rt, 100);

    qwrt_destroy(rt);
    pal_uv_destroy(pal);
    return 0;
}
```

**关键概念：**
- 在 qwrt_eval 中使用 async/await
- Promise 解析由运行时处理
- 事件循环驱动 HTTP 请求/响应

## Timers（定时器）

演示 setTimeout、setInterval 以及定时器清理。

```c
// example_timers.c
#include <qwrt/qwrt.h>
#include <pal_uv.h>
#include <stdio.h>

int main(void) {
    qwrt_pal_t *pal = pal_uv_create(uv_default_loop());
    qwrt_t *rt = qwrt_create(&(qwrt_config_t){ .pal = pal });
    if (!rt) return 1;

    char *result = NULL;
    qwrt_eval(rt,
        "(async function() {"
        "  const start = Date.now();"
        "  await new Promise(r => setTimeout(r, 50));"
        "  return 'Slept ' + (Date.now() - start) + 'ms';"
        "})()",
        &result);

    pal->run_cycle(pal, 100); qwrt_tick(rt, 100);

    printf("Result: %s\n", result ? result : "ERROR");
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_uv_destroy(pal);
    return 0;
}
```

## Storage（存储）

使用键值存储 API 在多次执行之间持久化数据。

```c
// example_storage.c
#include <qwrt/qwrt.h>
#include <pal_uv.h>
#include <stdio.h>

int main(void) {
    qwrt_pal_t *pal = pal_uv_create(uv_default_loop());
    qwrt_t *rt = qwrt_create(&(qwrt_config_t){ .pal = pal });
    if (!rt) return 1;

    qwrt_eval(rt, "localStorage.setItem('key', 'hello')", NULL);
    pal->run_cycle(pal, 100); qwrt_tick(rt, 100);

    char *result = NULL;
    qwrt_eval(rt, "localStorage.getItem('key')", &result);
    printf("storage.key = %s\n", result ? result : "ERROR");
    qwrt_free(result);

    qwrt_destroy(rt);
    pal_uv_destroy(pal);
    return 0;
}
```

## Bytecode（字节码）

将 JavaScript 预编译为 QuickJS 字节码，以实现更快的启动速度和更小的分发体积。

```c
// example_bytecode.c
#include <qwrt/qwrt.h>
#include <pal_mock.h>
#include <stdio.h>

int main(void) {
    qwrt_pal_t *pal = pal_mock_create();
    qwrt_t *rt = qwrt_create(&(qwrt_config_t){ .pal = pal });
    if (!rt) return 1;

    size_t bc_len = 0;
    uint8_t *bc = qwrt_compile(rt, "1 + 1", 5, &bc_len);
    if (!bc) { printf("Compile failed\n"); return 1; }

    char *result = NULL;
    qwrt_eval_bytecode(rt, bc, bc_len, &result);
    printf("Bytecode result: %s\n", result ? result : "ERROR");
    qwrt_free(result);
    qwrt_free(bc);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
    return 0;
}
```

## 错误处理

所有可能失败的 qwrt 函数都会返回错误码。请使用它们：

```c
char *result = NULL;
int rc = qwrt_eval(rt, "throw new Error('oops')", &result);
if (rc != 0) {
    printf("Error: %s\n", result ? result : "unknown");
    // result 中包含 JS 异常消息
}
if (result) qwrt_free(result);
```
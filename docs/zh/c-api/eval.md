# JS 求值

qzjs 提供三种执行 JavaScript 的方式，外加一个字节码编译 API。

## `qz_eval`

```c
int qz_eval(qz_t *rt, const char *code, char **result);
```

在活动上下文上求值 JS 源代码。WinterTC 运行时（fetch、console、定时器等）在第一次求值前自动注入新上下文。

- `code` — 以 null 结尾的 JavaScript 源字符串
- `result` — 如果非 NULL，接收一个 `malloc` 分配的字符串化结果（JSON）。使用 `qz_free()` 释放
- 成功返回 0，JS 异常返回 <0

```c
char *result = NULL;
if (qz_eval(rt, "JSON.stringify({hello: 'world'})", &result) == 0) {
    printf("%s\n", result);  // {"hello":"world"}
    qz_free(result);
}
```

## `qz_eval_bytecode`

```c
int qz_eval_bytecode(qz_t *rt, const uint8_t *bytecode, size_t len,
                       char **result);
```

求值预编译的 字节码。结果/返回语义与 `qz_eval` 相同。使用 `qz_compile` 从源代码生成字节码。

```c
size_t bc_len = 0;
uint8_t *bc = qz_compile(rt, "1 + 1", 5, &bc_len);
char *result = NULL;
qz_eval_bytecode(rt, bc, bc_len, &result);
qz_free(bc);
qz_free(result);
```

## `qz_call`

```c
int qz_call(qz_t *rt, const char *func,
              const char *args_json, char **result);
```

使用 JSON 编码的参数调用全局 JS 函数。结果语义与 `qz_eval` 相同。

```c
// 相当于：myFunc(1, "hello", true)
char *result = NULL;
qz_call(rt, "myFunc", "[1,\"hello\",true]", &result);
```

## `qz_compile`

```c
uint8_t *qz_compile(qz_t *rt, const char *code, size_t code_len,
                      size_t *out_len);
```

将 JS 源代码编译为 字节码。返回分配的缓冲区（使用 `qz_free` 释放），并将长度写入 `*out_len`。错误时返回 `NULL`。

## `qz_compile_module`

```c
uint8_t *qz_compile_module(qz_t *rt, const char *code, size_t code_len,
                             size_t *out_len);
```

与 `qz_compile` 相同，但将源代码视为 ES 模块。

## `qz_free`

```c
void qz_free(void *ptr);
```

释放由 `qz_eval`、`qz_call`、`qz_compile` 或 `qz_compile_module` 返回的内存。NULL 安全。

## 错误处理

所有求值函数成功返回 0，失败返回负值。当发生 JS 异常时，错误消息可通过 `result` 参数获取：

```c
char *result = NULL;
if (qz_eval(rt, "throw new Error('oops')", &result) < 0) {
    printf("JS error: %s\n", result);  // Error: oops
    qz_free(result);
}
```
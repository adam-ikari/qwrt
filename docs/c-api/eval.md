# JS Evaluation

Qwrt.js provides three ways to execute JavaScript, plus a bytecode compilation API.

## `qwrt_eval`

```c
int qwrt_eval(qwrt_t *rt, const char *code, char **result);
```

Evaluates JS source code on the active context. The WinterTC runtime (fetch, console, timers, etc.) is auto-injected into new contexts before first eval.

- `code` — null-terminated JavaScript source string
- `result` — if non-NULL, receives a `malloc`'d stringified result (JSON). Free with `qwrt_free()`
- Returns 0 on success, <0 on JS exception

```c
char *result = NULL;
if (qwrt_eval(rt, "JSON.stringify({hello: 'world'})", &result) == 0) {
    printf("%s\n", result);  // {"hello":"world"}
    qwrt_free(result);
}
```

## `qwrt_eval_bytecode`

```c
int qwrt_eval_bytecode(qwrt_t *rt, const uint8_t *bytecode, size_t len,
                       char **result);
```

Evaluates precompiled QuickJS bytecode. Same result/return semantics as `qwrt_eval`. Use `qwrt_compile` to produce bytecode from source.

```c
size_t bc_len = 0;
uint8_t *bc = qwrt_compile(rt, "1 + 1", 5, &bc_len);
char *result = NULL;
qwrt_eval_bytecode(rt, bc, bc_len, &result);
qwrt_free(bc);
qwrt_free(result);
```

## `qwrt_call`

```c
int qwrt_call(qwrt_t *rt, const char *func,
              const char *args_json, char **result);
```

Calls a global JS function with JSON-encoded arguments. Result semantics match `qwrt_eval`.

```c
// Equivalent to: myFunc(1, "hello", true)
char *result = NULL;
qwrt_call(rt, "myFunc", "[1,\"hello\",true]", &result);
```

## `qwrt_compile`

```c
uint8_t *qwrt_compile(qwrt_t *rt, const char *code, size_t code_len,
                      size_t *out_len);
```

Compiles JS source to QuickJS bytecode. Returns an allocated buffer (free with `qwrt_free`) and writes the length to `*out_len`. Returns `NULL` on error.

## `qwrt_compile_module`

```c
uint8_t *qwrt_compile_module(qwrt_t *rt, const char *code, size_t code_len,
                             size_t *out_len);
```

Same as `qwrt_compile` but treats the source as an ES module.

## `qwrt_free`

```c
void qwrt_free(void *ptr);
```

Frees memory returned by `qwrt_eval`, `qwrt_call`, `qwrt_compile`, or `qwrt_compile_module`. NULL-safe.

## Error Handling

All evaluation functions return 0 on success or a negative value on failure. When a JS exception occurs, the error message is available through the `result` parameter:

```c
char *result = NULL;
if (qwrt_eval(rt, "throw new Error('oops')", &result) < 0) {
    printf("JS error: %s\n", result);  // Error: oops
    qwrt_free(result);
}
```
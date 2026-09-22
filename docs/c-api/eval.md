# JS Evaluation

Qzjs.js provides three ways to execute JavaScript, plus a bytecode compilation API.

## `qz_eval`

```c
int qz_eval(qz_t *rt, const char *code, char **result);
```

Evaluates JS source code on the active context. The WinterTC runtime (fetch, console, timers, etc.) is auto-injected into new contexts before first eval.

- `code` — null-terminated JavaScript source string
- `result` — if non-NULL, receives a `malloc`'d stringified result (JSON). Free with `qz_free()`
- Returns 0 on success, <0 on JS exception

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

Evaluates precompiled QuickJS bytecode. Same result/return semantics as `qz_eval`. Use `qz_compile` to produce bytecode from source.

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

Calls a global JS function with JSON-encoded arguments. Result semantics match `qz_eval`.

```c
// Equivalent to: myFunc(1, "hello", true)
char *result = NULL;
qz_call(rt, "myFunc", "[1,\"hello\",true]", &result);
```

## `qz_compile`

```c
uint8_t *qz_compile(qz_t *rt, const char *code, size_t code_len,
                      size_t *out_len);
```

Compiles JS source to QuickJS bytecode. Returns an allocated buffer (free with `qz_free`) and writes the length to `*out_len`. Returns `NULL` on error.

## `qz_compile_module`

```c
uint8_t *qz_compile_module(qz_t *rt, const char *code, size_t code_len,
                             size_t *out_len);
```

Same as `qz_compile` but treats the source as an ES module.

## `qz_free`

```c
void qz_free(void *ptr);
```

Frees memory returned by `qz_eval`, `qz_call`, `qz_compile`, or `qz_compile_module`. NULL-safe.

## Error Handling

All evaluation functions return 0 on success or a negative value on failure. When a JS exception occurs, the error message is available through the `result` parameter:

```c
char *result = NULL;
if (qz_eval(rt, "throw new Error('oops')", &result) < 0) {
    printf("JS error: %s\n", result);  // Error: oops
    qz_free(result);
}
```
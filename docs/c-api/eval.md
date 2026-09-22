# JS Evaluation

Amoib.js provides three ways to execute JavaScript, plus a bytecode compilation API.

## `am_eval`

```c
int am_eval(am_t *rt, const char *code, char **result);
```

Evaluates JS source code on the active context. The WinterTC runtime (fetch, console, timers, etc.) is auto-injected into new contexts before first eval.

- `code` — null-terminated JavaScript source string
- `result` — if non-NULL, receives a `malloc`'d stringified result (JSON). Free with `am_free()`
- Returns 0 on success, <0 on JS exception

```c
char *result = NULL;
if (am_eval(rt, "JSON.stringify({hello: 'world'})", &result) == 0) {
    printf("%s\n", result);  // {"hello":"world"}
    am_free(result);
}
```

## `am_eval_bytecode`

```c
int am_eval_bytecode(am_t *rt, const uint8_t *bytecode, size_t len,
                       char **result);
```

Evaluates precompiled QuickJS bytecode. Same result/return semantics as `am_eval`. Use `am_compile` to produce bytecode from source.

```c
size_t bc_len = 0;
uint8_t *bc = am_compile(rt, "1 + 1", 5, &bc_len);
char *result = NULL;
am_eval_bytecode(rt, bc, bc_len, &result);
am_free(bc);
am_free(result);
```

## `am_call`

```c
int am_call(am_t *rt, const char *func,
              const char *args_json, char **result);
```

Calls a global JS function with JSON-encoded arguments. Result semantics match `am_eval`.

```c
// Equivalent to: myFunc(1, "hello", true)
char *result = NULL;
am_call(rt, "myFunc", "[1,\"hello\",true]", &result);
```

## `am_compile`

```c
uint8_t *am_compile(am_t *rt, const char *code, size_t code_len,
                      size_t *out_len);
```

Compiles JS source to QuickJS bytecode. Returns an allocated buffer (free with `am_free`) and writes the length to `*out_len`. Returns `NULL` on error.

## `am_compile_module`

```c
uint8_t *am_compile_module(am_t *rt, const char *code, size_t code_len,
                             size_t *out_len);
```

Same as `am_compile` but treats the source as an ES module.

## `am_free`

```c
void am_free(void *ptr);
```

Frees memory returned by `am_eval`, `am_call`, `am_compile`, or `am_compile_module`. NULL-safe.

## Error Handling

All evaluation functions return 0 on success or a negative value on failure. When a JS exception occurs, the error message is available through the `result` parameter:

```c
char *result = NULL;
if (am_eval(rt, "throw new Error('oops')", &result) < 0) {
    printf("JS error: %s\n", result);  // Error: oops
    am_free(result);
}
```
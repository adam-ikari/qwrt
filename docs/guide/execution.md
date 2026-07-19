---
title: JS Execution
description: How Qwrt.js evaluates JavaScript — qwrt_eval, bytecode evaluation, result handling, error propagation, and promise support.
---

# JS Execution

qwrt provides three ways to run JavaScript: evaluate source code, evaluate bytecode, and call named functions.

## Evaluating Source Code

```c
char *result = NULL;
int ret = qwrt_eval(rt, "1 + 1", &result);
if (ret == 0) {
    printf("Result: %s\n", result);  // "2"
    qwrt_free(result);
} else {
    // JS exception — result contains the error message
    fprintf(stderr, "Error: %s\n", result ? result : "unknown");
    qwrt_free(result);
}
```

Parameters:
- `rt` — the runtime
- `code` — null-terminated JavaScript source string
- `result` — if non-NULL, receives a `malloc`'d string (free with `qwrt_free`)

Returns 0 on success, <0 on JS exception.

The WinterCG-compatible runtime is **automatically injected** before the first `qwrt_eval` on each context — you don't need to manually set up `fetch`, `console`, etc.

## Evaluating Bytecode

For production, precompile JS to QuickJS bytecode for faster startup and smaller size:

```c
// Compile step (do once, ship the bytecode)
size_t bytecode_len;
uint8_t *bytecode = qwrt_compile(rt, "1 + 1", 5, &bytecode_len);

// Eval step (fast — no parsing needed)
char *result = NULL;
qwrt_eval_bytecode(rt, bytecode, bytecode_len, &result);
qwrt_free(result);
qwrt_free(bytecode);
```

See [Bytecode Compilation](/guide/bytecode) for details.

## Calling JavaScript Functions

Call a global JS function by name with JSON arguments:

```c
// First, define the function
qwrt_eval(rt,
    "function add(a, b) { return a + b; }"
, NULL);

// Then call it
char *result = NULL;
qwrt_call(rt, "add", "[3, 4]", &result);
printf("add(3, 4) = %s\n", result);  // "7"
qwrt_free(result);
```

- `func` — name of a global function (must exist in the active context)
- `args_json` — JSON array of arguments (e.g., `"[1, \"hello\", true]"`) or NULL for no args
- `result` — receives the JSON-stringified return value

## Draining Microtasks

Many JS APIs (Promises, async/await) enqueue microtasks. Call `qwrt_tick` to drain them:

```c
// After any eval that creates promises:
qwrt_tick(rt, 100);
```

Typically you drive this in a loop with the PAL event loop:

```c
while (pal->run_cycle(pal, 100) > 0) {
    qwrt_tick(rt, 100);
}
```

## Accessing the JSContext

For advanced use (direct QuickJS API), get the raw `JSContext*`:

```c
JSContext *ctx = qwrt_get_jsctx(rt);
if (ctx) {
    // Use QuickJS C API directly
    JSValue val = JS_NewInt32(ctx, 42);
    // ...
}
```

The pointer is valid until the context is destroyed or the runtime is reset.

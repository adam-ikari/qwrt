---
title: JS Evaluation
description: Convenience functions for sending JS evaluation / call instructions — qz_eval and qz_call — and what they really are.
---

# JS Evaluation — Convenience Functions

`qz_eval` / `qz_call` are **convenience functions**: they wrap the common
"send an instruction to JS" operation into a simple call. They are **not a
new execution mechanism** — underneath they are just sugar over the JSON
message boundary.

## `int qz_eval(qz_t *rt, const char *code)`

Send an eval instruction — execute a piece of JS code in the runtime:

```c
qz_eval(rt, "1 + 1");
qz_eval(rt, "globalThis.add = function(a,b){ return a + b; };");
```

Equivalent to hand-writing `qz_post_message(rt, "{\"corr\":1,\"cmd\":\"eval\",\"code\":\"...\"}", len)`.

## `int qz_call(qz_t *rt, const char *fn, const char *args_json)`

Send a call instruction — invoke a global JS function with JSON args:

```c
qz_call(rt, "add", "[3, 4]");   // calls globalThis.add(3, 4)
```

`fn` is a global function name; `args_json` is a JSON array, or `NULL` for `[]`.

Both are thread-safe; return `0` on success, `-1` on failure.

## What they really are

- **They only send.** Each builds the `{corr, cmd, ...}` instruction JSON
  (with proper string escaping and a monotonically increasing `corr`) and
  hands it to `qz_post_message`. No new execution path is introduced.
- **Results come back through `message_cb`.** The JS side receives the
  instruction in `onmessage`, runs it, and replies with `postMessage({corr, result})`.
  The host sees that reply in `message_cb`. `corr` lets you match a reply to
  its request.
- **Built-in default handler.** If your `initial_script` does not define a
  global `onmessage`, qzjs injects a built-in handler that automatically
  answers `{cmd:"eval"}` and `{cmd:"call"}` — so `qz_eval`/`qz_call` work out
  of the box. If you do define `onmessage`, yours takes over (the default is
  not injected).
- **Asynchronous.** Send returns immediately; the result arrives later in
  `message_cb`. To collect results, match `corr` in `message_cb`.

## Complete example

```c
#include <qzjs/qzjs.h>

static void on_message(qz_t *rt, const char *json, size_t len, void *data) {
    (void)rt; (void)data;
    printf("JS: %.*s\n", (int)len, json);   // {"corr":1,"result":42}
}

int main(void) {
    qz_config_t cfg = {0};
    cfg.message_cb = on_message;   // no initial_script → default handler injected
    qz_t *rt = qz_create(&cfg);

    qz_eval(rt, "40 + 2");                  // → {"corr":1,"result":42}
    qz_eval(rt, "globalThis.mul = (a,b) => a*b;");
    qz_call(rt, "mul", "[6,7]");            // → {"corr":2,"result":42}

    /* ... */
    qz_destroy(rt);
    return 0;
}
```

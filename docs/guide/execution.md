---
title: JS Execution
description: How Amoib.js executes JavaScript — initial_script, message-driven evaluation, Web Workers, and extension-injected globals. The host never evaluates JS directly.
---

# JS Execution

All JavaScript runs on amoib's internal thread. The host never evaluates or
calls into JS directly — there is **no `am_eval`**, no `am_call`, and no
`am_tick` in the public API. Code is executed in one of four ways:

1. **`initial_script`** — a script eval'd once when the runtime starts
2. **Message-driven** — JSON messages posted from the host run handlers in JS
3. **Web Workers** — `new Worker(url)` runs a separate script in parallel
4. **Extension globals** — C extensions (built into amoib at compile time) expose
   native functions to JS

## 1. Initial Script

`am_create` eval's `config.initial_script` on the internal thread before it
returns. A throw makes `am_create` return `NULL`:

```c
am_config_t cfg = {
    .initial_script =
        "console.log('hello from amoib');"
        "globalThis.onmessage = function (e) { postMessage('got: ' + e.data); };",
    .message_cb = on_message,
};
am_t *rt = am_create(&cfg);   // NULL if initial_script threw
```

This is where you install message handlers and top-level state before the host
starts driving the runtime.

## 2. Message-Driven Execution

The host drives JS by posting JSON messages; JS replies with `postMessage`:

```
host  ── am_post_message(json) ──▶  JS: globalThis.onmessage(e)
host  ◀── message_cb(json)       ───  JS: postMessage(value)
```

- `am_post_message` is **thread-safe** (the JSON is copied) and may be called
  from any host thread.
- The message arrives as a JS object/string via `onmessage`; `e.data` is the
  parsed payload.
- `message_cb` fires on the amoib thread with the JSON serialized from
  `postMessage`, so the callback must be thread-safe.

This is the only channel for host ↔ JS data. There is no synchronous return
value — results always flow back through `message_cb`.

## 3. Web Workers

`new Worker(url)` runs a script in a separate, isolated execution context —
real parallel work, not a shared-`JSRuntime` context. Workers communicate with
their creator and each other via `postMessage`/`onmessage`:

```js
// main script
const w = new Worker("worker.js");
w.onmessage = (e) => console.log("from worker:", e.data);
w.postMessage("start");
```

Under `-DAM_PROCESS_MODEL=THREAD` a worker runs on a parallel thread; under
the default `ISOLATED` model it runs as a dedicated child process (`amoib-rt`
spawned via fork+exec). See [Multi-Context](/guide/multi-context).

## 4. Extension Globals

Native C functions are exposed to JS by building an extension into amoib (the
compile-time `AM_EXTENSIONS` table), not by calling into JS from the host.
An extension's `init` hook runs when a context is created and may register
globals via the QuickJS API:

```c
#include <amoib/amoib.h>
#include <quickjs.h>
#include "am_internal.h"   // am_get_active_jsctx (internal)

static JSValue js_greet(JSContext *ctx, JSValueConst this_val,
                        int argc, JSValueConst *argv) {
    const char *name = argc > 0 ? JS_ToCString(ctx, argv[0]) : "world";
    JSValue v = JS_NewString(ctx, name);
    if (argc > 0) JS_FreeCString(ctx, name);
    return v;
}

static int my_ext_init(am_ext_t *ext, am_t *rt) {
    JSContext *ctx = am_get_active_jsctx(rt);   // internal helper
    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "greet",
                      JS_NewCFunction(ctx, js_greet, "greet", 1));
    JS_FreeValue(ctx, global);
    return 0;
}
```

Register the extension at compile time (see [Extensions](/guide/extensions)).

## Asynchronous Execution

Promises, `async`/`await`, and timers are driven by the embedded libuv loop on
the internal thread. Microtasks are flushed naturally between loop iterations —
the host does not and cannot pump the queue. A `setTimeout`/`fetch`/stream
continues to make progress until it settles; see [Event Loop](/guide/event-loop).

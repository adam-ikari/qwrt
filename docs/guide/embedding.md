---
title: Embedding Patterns
description: Patterns for embedding Qwrt.js in C applications — host data, custom extensions, message-based communication, and multi-instance setups.
---

# Embedding Patterns

Common patterns for embedding qwrt in C applications.

## Basic Embedding

qwrt owns its own internal thread and libuv event loop. All JS runs on that
thread; the host communicates with the runtime over JSON messages.

```c
#include <qwrt/qwrt.h>
#include <stdio.h>

static void on_message(qwrt_t *rt, const char *json, size_t len, void *data) {
    (void)rt; (void)data;
    printf("received: %.*s\n", (int)len, json);
}

int main(void) {
    qwrt_config_t cfg = {0};
    cfg.initial_script = "postMessage({hello: 'world'});";
    cfg.message_cb = on_message;
    qwrt_t *rt = qwrt_create(&cfg);
    if (!rt) return 1;

    // Your application logic: drive the runtime by posting JSON messages
    qwrt_post_message(rt, "{\"cmd\":\"echo\",\"data\":\"hi\"}", 26);

    qwrt_destroy(rt);
    return 0;
}
```

`qwrt_create` blocks until qwrt's internal thread is ready and
`initial_script` has been eval'd. There is no `qwrt_eval` and no `qwrt_tick` —
the host sends messages via `qwrt_post_message` (thread-safe) and receives
replies through `message_cb`, which fires on the qwrt thread (so your callback
must be thread-safe). `qwrt_destroy` performs a graceful shutdown.

## Calling C Functions from JS

Register C functions as JS globals:

```c
#include <quickjs.h>

static JSValue greet(JSContext *ctx, JSValue this_val,
                     int argc, JSValue *argv) {
    QWRT_UNUSED(this_val);
    const char *name = "World";
    if (argc > 0) name = JS_ToCString(ctx, argv[0]);
    printf("Hello, %s!\n", name);
    if (argc > 0) JS_FreeCString(ctx, name);
    return JS_UNDEFINED;
}

// Register in a custom extension's init hook:
static int my_ext_init(qwrt_ext_t *ext, qwrt_t *rt) {
    QWRT_UNUSED(ext);
    JSContext *ctx = qwrt_get_active_jsctx(rt);
    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "greet",
        JS_NewCFunction(ctx, greet, "greet", 1));
    JS_FreeValue(ctx, global);
    return 0;
}
```

The `init` hook runs on qwrt's internal thread during `qwrt_create`, before
the host receives the runtime — so registering globals here is safe.
`qwrt_get_active_jsctx` is an internal helper (declared in
`src/qwrt_internal.h`), for use from extension hooks.

## Calling JS from C with Structured Data

Calling JS from C means posting a JSON message and letting the JS side reply
via `postMessage`. Install a handler in `initial_script`:

```c
static void on_message(qwrt_t *rt, const char *json, size_t len, void *data) {
    (void)rt; (void)data;
    printf("JS returned: %.*s\n", (int)len, json);
}

// Bootstrap an onmessage handler that processes structured data
qwrt_config_t cfg = {0};
cfg.initial_script =
    "globalThis.onmessage = function (e) {"
    "  var d = e.data;"
    "  if (d.cmd === 'process')"
    "    postMessage({ doubled: d.value * 2, ok: true });"
    "};";
cfg.message_cb = on_message;
qwrt_t *rt = qwrt_create(&cfg);

// Post the input as a JSON message; the reply arrives via message_cb
qwrt_post_message(rt, "{\"cmd\":\"process\",\"value\":21}", 28);
// on_message prints: JS returned: {"doubled":42,"ok":true}
```

The JSON is copied by `qwrt_post_message` (thread-safe, callable from any
thread). There is no synchronous `qwrt_call` — results always flow back as
messages.

## Per-Request Context Isolation

Contexts are managed **inside** the runtime (`src/context.c`); the host does
not manipulate them through the public C API. The host sees one runtime and
communicates over JSON messages (`qwrt_post_message` / `message_cb`). For
request-level isolation, either create a fresh `qwrt_t` per request (each is
fully independent — own thread, loop, and JS state) or route requests into a
running runtime by message, tagging them so the JS side can keep per-request
state.

## Testing with mock_libuv

For deterministic offline tests, replace libuv with `mock_libuv`
(`test/mock_libuv.{c,h}`) — a fake `uv_*` API with no network or system calls.
Gtest suites link `qwrt + mock_libuv` (with `-DQWRT_USE_MOCK_LIBUV`) and drive
the runtime through the `HostCtx` harness in `test/test_host.h`:

- `host_create(script)` / `host_destroy(h)` — start/stop a runtime with a
  bootstrap that installs `globalThis.onmessage` handling `{cmd:'eval', code}`
  and `{cmd:'echo'}`
- `host_eval(h, code, &out)` / `host_value(h, code, &out)` — evaluate JS via
  the command channel
- `host_poll_until_value(h, expr, sub, &out)` — poll until a condition holds
  (used for async results: promises, timers, storage)

See [Testing](/dev/testing) for details.

## Multiple Independent Runtimes

Since qwrt has zero global state, you can run multiple `qwrt_t` instances —
each owns its own internal thread, libuv loop, and JS state:

```c
static void on_message(qwrt_t *rt, const char *json, size_t len, void *data) {
    (void)rt;
    printf("%s: %.*s\n", (const char *)data, (int)len, json);
}

qwrt_config_t cfg1 = { .initial_script = "postMessage('rt1');",
                       .message_cb = on_message, .host_data = "rt1" };
qwrt_config_t cfg2 = { .initial_script = "postMessage('rt2');",
                       .message_cb = on_message, .host_data = "rt2" };

qwrt_t *rt1 = qwrt_create(&cfg1);
qwrt_t *rt2 = qwrt_create(&cfg2);

// Post to each independently; replies arrive on each runtime's message_cb
qwrt_post_message(rt1, "{\"cmd\":\"echo\",\"data\":\"a\"}", 26);
qwrt_post_message(rt2, "{\"cmd\":\"echo\",\"data\":\"b\"}", 26);

qwrt_destroy(rt1);
qwrt_destroy(rt2);
```

No host loop to drive — each runtime runs itself.

## Error Handling Patterns

There is no synchronous eval, so errors surface as messages rather than return
codes:

- If `initial_script` throws, `qwrt_create` returns `NULL`.
- At runtime, JS can report failures explicitly — e.g. an `onmessage` handler
  replies `postMessage({ ok: false, e: String(err) })`, which the host reads in
  `message_cb`:

```c
static void on_message(qwrt_t *rt, const char *json, size_t len, void *data) {
    (void)rt; (void)data;
    printf("%.*s\n", (int)len, json);  // e.g. {"ok":false,"e":"TypeError: ..."}
}
```

An uncaught exception inside a message handler does not take down the
runtime.

## Memory Management

- `qwrt_free` still exists for malloc'd blocks returned by qwrt (`qwrt_free(NULL)` is safe) — there is no longer any `qwrt_eval`/`qwrt_call` result to free
- The runtime owns all its internal resources (thread, libuv loop, contexts) — `qwrt_destroy` frees everything on graceful shutdown
- Per-runtime host data: set `config.host_data` before `qwrt_create`; an
  extension's `init` hook reads it via `qwrt_get_runtime_data(rt)` during
  create (the rt is valid inside init, before the host receives it). Note:
  `qwrt_ext_t.user_data` is on the shared compile-time extension struct — use
  `qwrt_get_runtime_data`/`qwrt_set_runtime_data` for per-instance data, not
  `user_data` (which is shared across runtimes).

# Runtime Lifecycle

Every amoib program follows the same lifecycle: **create → use → destroy**.

## `am_create`

```c
am_t *am_create(const am_config_t *config);
```

Creates a new amoib runtime. amoib starts its own internal thread and embedded
libuv loop; `am_create` blocks until the thread is ready and
`initial_script` has been eval'd. The registered extension set is fixed at
build time via the `AM_EXTENSIONS` macro; there is no runtime extension list.

Returns `NULL` on failure (including a throwing `initial_script`).

**Parameters:**

| Field | Type | Description |
|-------|------|-------------|
| `config.initial_script` | `const char *` | JS eval'd on amoib's internal thread at create; a throw makes `am_create` return `NULL` |
| `config.message_cb` | `void (*)(am_t *, const char *, size_t, void *)` | Outbound message callback; fires on the amoib thread, must be thread-safe |
| `config.debug` | `int` | Enable debug output (0 or 1) |
| `config.host_data` | `void *` | Per-runtime opaque pointer, readable by extensions; passed as the `data` arg to `message_cb` |

**What `am_create` does internally:**

1. Starts amoib's internal thread and initializes the embedded libuv loop
2. Creates a `JSRuntime` and initial context
3. Registers the build-time extension set (the `AM_EXTENSIONS` table)
4. Injects the WinterTC-compatible runtime into the initial context
5. Eval's `initial_script` on the internal thread

**Thread model:** all JS runs on amoib's internal thread; the host posts
messages (`am_post_message`, thread-safe) and receives them via
`message_cb`.

## `am_destroy`

```c
void am_destroy(am_t *rt);
```

Gracefully shuts down the runtime: requests the internal thread to exit, joins
it, then destroys all contexts and frees all resources (handles, timers,
polyfill state, the libuv loop). Safe to call with `NULL`. Host-thread only —
call it from the thread that called `am_create`.

```c
am_destroy(rt);
```

## Host Data

Per-runtime data is available to extensions during initialization:

```c
void *am_get_runtime_data(am_t *rt);
void am_set_runtime_data(am_t *rt, void *data);
```

`am_create` copies `config->host_data` onto the runtime, so extension init
hooks can read it before the host has the `rt` pointer — resolving the
init-time ordering deadlock:

```c
am_config_t cfg = { .initial_script = "postMessage('ready');",
                      .message_cb = on_message,
                      .host_data = my_state };
am_t *rt = am_create(&cfg);
// my_state is now available inside extension init via am_get_runtime_data(rt)
// and arrives as the `data` arg of message_cb
```

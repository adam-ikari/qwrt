# Runtime Lifecycle

Every qzjs program follows the same lifecycle: **create → use → destroy**.

## `qz_create`

```c
qz_t *qz_create(const qz_config_t *config);
```

Creates a new qzjs runtime. qzjs starts its own internal thread and embedded
libuv loop; `qz_create` blocks until the thread is ready and
`initial_script` has been eval'd. The registered extension set is fixed at
build time via the `QZ_EXTENSIONS` macro; there is no runtime extension list.

Returns `NULL` on failure (including a throwing `initial_script`).

**Parameters:**

| Field | Type | Description |
|-------|------|-------------|
| `config.initial_script` | `const char *` | JS eval'd on qzjs's internal thread at create; a throw makes `qz_create` return `NULL` |
| `config.message_cb` | `void (*)(qz_t *, const char *, size_t, void *)` | Outbound message callback; fires on the qzjs thread, must be thread-safe |
| `config.debug` | `int` | Enable debug output (0 or 1) |
| `config.host_data` | `void *` | Per-runtime opaque pointer, readable by extensions; passed as the `data` arg to `message_cb` |
| `config.initial_script_path` | `const char *` | JS file read and eval'd instead of `initial_script`; wins if both are set |
| `config.initial_bytecode` | `const uint8_t *` | Precompiled bytecode (from `qz_compile`), eval'd after the initial script |
| `config.initial_bytecode_len` | `size_t` | Length in bytes of `initial_bytecode` |

**What `qz_create` does internally:**

1. Starts qzjs's internal thread and initializes the embedded libuv loop
2. Creates a `JSRuntime` and initial context
3. Registers the build-time extension set (the `QZ_EXTENSIONS` table)
4. Injects the WinterTC-compatible runtime into the initial context
5. Eval's `initial_script` on the internal thread
6. After the initial script, evals `initial_bytecode` if set

**Thread model:** all JS runs on qzjs's internal thread; the host posts
messages (`qz_post_message`, thread-safe) and receives them via
`message_cb`.

## `qz_destroy`

```c
void qz_destroy(qz_t *rt);
```

Gracefully shuts down the runtime: requests the internal thread to exit, joins
it, then destroys all contexts and frees all resources (handles, timers,
polyfill state, the libuv loop). Safe to call with `NULL`. Host-thread only —
call it from the thread that called `qz_create`.

```c
qz_destroy(rt);
```

## `qz_compile`

```c
int qz_compile(const char *source, size_t len, const char *filename,
               uint8_t **out, size_t *out_len, char **err);
```

Compiles JS source to a bytecode blob. Standalone — no runtime needed.
Returns 0 on success (`*out` malloc'd, free with `qz_free`; `*out_len` set)
or -1 (`*err` malloc'd message, free with `qz_free`). `filename` is for
error/backtrace naming only, may be `NULL`.

Run the blob at startup via `qz_config_t.initial_bytecode` /
`initial_bytecode_len`, or `qzjs --bytecode file.bc` on the CLI.

**Compatibility is not guaranteed:** bytecode is bound to the exact qzjs
build (engine version, serialization format, compile options). A blob from a
different build fails `qz_create` with `SyntaxError: invalid version`.
Distribute source and compile at deploy time on the target build. See
[Bytecode Compilation](/guide/bytecode).

## Host Data

Per-runtime data is available to extensions during initialization:

```c
void *qz_get_runtime_data(qz_t *rt);
void qz_set_runtime_data(qz_t *rt, void *data);
```

`qz_create` copies `config->host_data` onto the runtime, so extension init
hooks can read it before the host has the `rt` pointer — resolving the
init-time ordering deadlock:

```c
qz_config_t cfg = { .initial_script = "postMessage('ready');",
                      .message_cb = on_message,
                      .host_data = my_state };
qz_t *rt = qz_create(&cfg);
// my_state is now available inside extension init via qz_get_runtime_data(rt)
// and arrives as the `data` arg of message_cb
```

# Runtime Lifecycle

Every qwrt program follows the same lifecycle: **create → use → destroy**.

## `qwrt_create`

```c
qwrt_t *qwrt_create(const qwrt_config_t *config);
```

Creates a new qwrt runtime. The PAL in `config` is **borrowed** (not owned) — it must outlive the runtime. The registered extension set is fixed at build time via the `QWRT_EXTENSIONS` macro; there is no runtime extension list.

Returns `NULL` on failure.

**Parameters:**

| Field | Type | Description |
|-------|------|-------------|
| `config.pal` | `const qwrt_pal_t *` | Platform Abstraction Layer (required) |
| `config.debug` | `int` | Enable debug output (0 or 1) |
| `config.host_data` | `void *` | Per-runtime opaque pointer, readable by extensions |

**What `qwrt_create` does internally:**

1. Calls `pal->init(pal)` if the hook is provided
2. Creates a `JSRuntime` and initial context
3. Registers the build-time extension set (the `QWRT_EXTENSIONS` table)
4. Injects the WinterTC-compatible runtime into the initial context

**Thread-bound:** all subsequent `qwrt_*` calls must come from the creating thread.

## `qwrt_destroy`

```c
void qwrt_destroy(qwrt_t *rt);
```

Destroys the runtime and frees all resources: contexts, handles, timers, polyfill state. The PAL is **NOT** freed — the caller owns it and must destroy it separately. Safe to call with `NULL`.

```c
qwrt_destroy(rt);
pal_mock_destroy(pal);  // caller owns the PAL
```

## `qwrt_tick`

```c
int qwrt_tick(qwrt_t *rt);
```

Processes one batch of deferred PAL callbacks and pending JS microtasks. Returns immediately — does NOT loop internally.

- Returns `1` if any work was processed, `0` if idle, `-1` on error

The host controls the event loop by interleaving `qwrt_tick` with its own work:

```c
// One tick per iteration — your code never starved
pal->run_cycle(pal, 100);
qwrt_tick(rt, 100);
my_other_work();  // always runs, never delayed
```

## `qwrt_reset`

```c
int qwrt_reset(qwrt_t *rt, const qwrt_config_t *config);
```

Destroys all contexts and creates a fresh initial context from `config`. The PAL is reused. Returns 0 on success, -1 on error.

## Host Data

Per-runtime data is available to extensions during initialization:

```c
void *qwrt_get_runtime_data(qwrt_t *rt);
void qwrt_set_runtime_data(qwrt_t *rt, void *data);
```

`qwrt_create` copies `config->host_data` onto the runtime, so extension init hooks can read it before the host has the `rt` pointer — resolving the init-time ordering deadlock:

```c
qwrt_config_t cfg = { .pal = pal, .host_data = my_state };
qwrt_t *rt = qwrt_create(&cfg);
// my_state is now available inside extension init via qwrt_get_runtime_data(rt)
```
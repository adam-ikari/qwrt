# qwrt examples

Small, self-contained C programs showing how to embed qwrt. Each example is a
single `.c` file that creates a PAL, spins up a `qwrt_t`, runs some JavaScript,
drains the event loop, and tears down.

## Build

```bash
cmake -B build -DQWRT_BUILD_EXAMPLES=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

The mock-backed examples build on any host toolchain with no network access.
The libuv-backed examples additionally require libuv (built from `deps/libuv`
by default) and, for `example_fetch` HTTPS URLs, mbedTLS (`QWRT_WITH_TLS=ON`).

## Run

```bash
# mock backend — no network
./build/examples/example_mock
./build/examples/example_bytecode

# libuv backend — real event loop
./build/examples/example_minimal
./build/examples/example_timers
./build/examples/example_storage
./build/examples/example_fetch https://example.com
```

## What each example shows

| Example | PAL | Demonstrates |
|---------|-----|--------------|
| `example_minimal` | uv | The minimal create → eval → tick → destroy lifecycle |
| `example_fetch` | uv | Async `fetch()` resolved over the libuv loop |
| `example_timers` | uv | `setTimeout` / `setInterval` self-cancelling pattern |
| `example_storage` | uv | `qwrt.storage` KV store persisting across runtimes |
| `example_mock` | mock | console, crypto, and timers with no event loop |
| `example_bytecode` | mock | `qwrt_compile` → `qwrt_eval_bytecode` AOT compilation |

## The host loop pattern

Every example drives the loop itself — qwrt is single-threaded and has no
hidden background thread. For the libuv backend the contract from
`include/qwrt/qwrt.h` is: drain deferred JS callbacks first (`qwrt_tick`),
then run one PAL iteration. PAL callbacks fire on the loop thread and enqueue
JS work; `qwrt_tick` is what actually performs the `JS_Call`.

```c
uv_loop_t *loop = uv_default_loop();
qwrt_pal_t *pal = pal_uv_create(loop);     /* share the loop */
qwrt_config_t cfg = { .pal = pal, .debug = 0 };
qwrt_t *rt = qwrt_create(&cfg);

qwrt_eval(rt, "...js that starts a promise...", NULL);

/* Pump until your completion flag flips (set it from JS), not until "idle":
 * run_cycle / uv_run return 0 while waiting out a timer or HTTP round-trip,
 * which is normal — not a signal to stop. */
for (;;) {
    qwrt_tick(rt);                 /* drain deferred JS callbacks */
    uv_run(loop, UV_RUN_ONCE);     /* one PAL I/O iteration */

    char *done = NULL;
    qwrt_eval(rt, "globalThis.__done", &done);
    if (done && strcmp(done, "true") == 0) { qwrt_free(done); break; }
    qwrt_free(done);
}

qwrt_destroy(rt);        /* does NOT free the PAL */
pal_uv_destroy(pal);
```

`pal->run_cycle(pal, timeout_ms)` wraps `uv_run` and may be `NULL` on PALs
with no event loop. The mock backend has no loop, so mock examples call
`qwrt_tick(rt)` directly and trigger mock side effects with
`pal_mock_fire_all_timers(pal)`. See `example_fetch.c` and `example_timers.c`
for the full async pattern, and `example_mock.c` for the no-loop case.


See the [JS API reference](https://github.com/adam-ikari/qwrt) for the full
set of globals available inside `qwrt_eval`.

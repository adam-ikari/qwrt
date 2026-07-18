---
title: Debugging
description: Debug Qwrt.js with the DAP debugger — breakpoints, step-through, variable inspection, and VS Code integration.
---

# Debugging qwrt programs with VS Code

qwrt ships a **DAP (Debug Adapter Protocol)** step-debugger built into the
library itself — no separate debugger binary. When enabled, any program that
embeds qwrt can be step-debugged in VS Code (breakpoints, step over/into/out,
call stack, locals, evaluate).

## How it works

The debugger is a **library capability**, not a separate process. It lives in
`src/debugger.c` (debug core) and `src/debugger_dap.c` (DAP protocol layer),
compiled into `libqwrt.a` when `QWRT_BUILD_DEBUGGER=ON`. A small patch to the
QuickJS-ng engine (`deps/quickjs-ng-debugger.patch`) adds the breakpoint/step
introspection primitives the core uses.

Activation is **automatic via config or env** — your host code does not
change. `qwrt_create` checks for debugging and, if enabled, attaches the DAP
layer (which speaks DAP on stdin/stdout) and pauses at entry. VS Code then
attaches.

### Two-layer disable (zero overhead when off)

- `QWRT_BUILD_DEBUGGER=OFF` (default): the engine patch is **not** applied,
  `src/debugger.c`/`src/debugger_dap.c` are **not** compiled, and `qwrt_create`
  has no debug code path. Debugging does not exist; `libqwrt.a` is unchanged.
- `QWRT_BUILD_DEBUGGER=ON`: the patch is applied and the sources compile in,
  but the engine's per-opcode `DEBUGGER_CHECK` is a no-op (one never-taken
  branch) **unless a debugger is attached at runtime**. Non-debugged runs pay
  essentially nothing.

## Build

```bash
cmake -B build -DQWRT_BUILD_DEBUGGER=ON -DQWRT_BUILD_TESTS=ON
cmake --build build -j$(nproc)
```

This applies `deps/quickjs-ng-debugger.patch` to the QuickJS-ng submodule
working tree at configure time (the submodule stays clean in git — the patch
is the source of truth). `cmake -DQWRT_BUILD_DEBUGGER=OFF` restores pristine.

## Enable debugging in your program

**Option A — no code change (env var):** run your program with `QWRT_DEBUG=1`:

```bash
QWRT_DEBUG=1 ./myapp app.js
```

**Option B — config bit:** set bit 1 of `qwrt_config_t.debug` (bit 0 is the
existing verbose-log flag):

```c
qwrt_config_t cfg = { .pal = pal, .debug = 0x2 };  /* bit 1 = debug-enable */
qwrt_t *rt = qwrt_create(&cfg);
qwrt_eval(rt, src, NULL);   /* pauses at entry, then at breakpoints */
```

That's it — `qwrt_create` auto-attaches DAP, sends `initialized`, and blocks
on the DAP configuration phase (initialize / setBreakpoints /
configurationDone) before returning. `stop_on_entry` pauses at the first
statement of your program.

## VS Code setup

Your program is the debug target — VS Code's `runtimeExecutable` points at
**your** binary, not a qwrt-provided one. Create `.vscode/launch.json`:

```json
{
  "version": "0.2.0",
  "configurations": [{
    "type": "qwrt",
    "request": "attach",
    "name": "qwrt: debug",
    "program": "${workspaceFolder}/app.js",
    "runtimeExecutable": "${workspaceFolder}/myapp",
    "runtimeArgs": ["${workspaceFolder}/app.js"],
    "env": { "QWRT_DEBUG": "1" }
  }]
}
```

> **Note:** `type: "qwrt"` requires a VS Code extension that registers the
> `qwrt` debug type. Until a packaged extension ships, you can drive the DAP
> layer directly (the adapter speaks standard DAP over stdio) or use the
> scripted test (`test/test_dap_debugger.c`) as a reference client. The DAP
> layer implements: initialize, attach, setBreakpoints, configurationDone,
> threads, stackTrace, scopes, variables, continue, next, stepIn, stepOut,
> evaluate, disconnect.

Set a breakpoint in your source, press F5, and VS Code attaches to your
program paused at entry. Continue to hit the breakpoint; inspect Locals,
step, evaluate watch expressions.

## What works (MVP)

- Breakpoints by (source file, line) — set from VS Code before launch.
- Pause at entry (`stop_on_entry`).
- Step over / into / out, continue.
- Call stack with file/line/function per frame.
- Locals scope (arguments + local variables) with values.
- `evaluate` (REPL/watch). Globals and pure expressions eval directly; a
  frame's locals are exposed on a `locals` object during evaluate, so
  `locals.x` reads a local variable. (Bare `x` won't bind — true eval-in-frame
  would need engine support QuickJS doesn't expose.)
- **Async-across-pause**: `fetch`/`setTimeout` advance while paused (the DAP
  loop pumps the PAL event loop between stdin polls, single-threaded).

## Limitations (MVP)

- **`evaluate` bare-local binding**: watch expressions referencing locals
  must use the `locals.` prefix (`locals.x`, not `x`). True eval-in-frame
  (binding locals directly) needs engine support QuickJS doesn't expose.
- **No CDP / Chrome DevTools**: DAP only. Chrome DevTools Protocol (CDP over
  WebSocket) is deferred.
- **No source maps**, no conditional/logpoint breakpoints, no exception
  breakpoints, no edit-and-continue, no multi-isolate.
- **`debugger;` keyword** is still a no-op (breakpoints are set from the UI).
- A packaged VS Code extension registering the `qwrt` debug type is a
  follow-up; the DAP layer is complete and tested via the scripted clients.

## Async support

The debugger **does** advance async JS while paused. When stopped at a
breakpoint, the DAP layer's `on_stopped` loop polls stdin with a short timeout
and, between polls, drives one non-blocking iteration of the PAL event loop
(`pal->run_cycle(0)` + `qwrt_tick`). So `fetch` responses, `setTimeout`
callbacks, etc. continue to fire while you inspect the paused state — all on
the single JS thread (qwrt owns no threads). A re-entrancy guard prevents
PAL-driven JS from nesting another stop.

`test/test_dap_async.c` validates this: a uv-backed debuggee schedules a 100ms
`setTimeout`, hits a breakpoint, and the timer fires during the pause (the loop
exits with `n=1` rather than spinning to its cap).

## Test

```bash
cd build && ctest -R test_dap_debugger --output-on-failure
```

`test/test_dap_debugger.c` is an in-process embedding host that forks a child
running a tiny JS program under `QWRT_DEBUG=1`, then acts as the VS Code
client over a pipe: initialize → setBreakpoints → configurationDone → expects
`stopped` at the breakpoint → stackTrace/scopes/variables/evaluate → step →
continue → terminate. It validates the whole stack: engine patch + debug core
+ DAP layer + the auto-attach path in `qwrt_create`.

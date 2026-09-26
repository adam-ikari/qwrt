---
title: Debugging
description: Debug qzjs with the DAP debugger — breakpoints, step-through, variable inspection, and VS Code integration.
---

# Debugging qzjs programs with VS Code

qzjs ships a **DAP (Debug Adapter Protocol)** step-debugger built into the
library itself — no separate debugger binary. When enabled, any program that
embeds qzjs can be step-debugged in VS Code (breakpoints, step over/into/out,
call stack, locals, evaluate).

## How it works

The debugger is a **library capability**, not a separate process. It lives in
`src/debugger.c` (debug core) and `src/debugger_dap.c` (DAP protocol layer),
compiled into `libqzjs.a` when `QZ_BUILD_DEBUGGER=ON`. A small patch to the
QuickJS-ng engine (`deps/quickjs-ng-debugger.patch`) adds the breakpoint/step
introspection primitives the core uses.

Activation is **automatic via config or env** — your host code does not
change. `qz_create` checks for debugging and, if enabled, attaches the DAP
layer (which speaks DAP on stdin/stdout) and pauses at entry. VS Code then
attaches.

### Two-layer disable (zero overhead when off)

- `QZ_BUILD_DEBUGGER=OFF` (default): the engine patch is **not** applied,
  `src/debugger.c`/`src/debugger_dap.c` are **not** compiled, and `qz_create`
  has no debug code path. Debugging does not exist; `libqzjs.a` is unchanged.
- `QZ_BUILD_DEBUGGER=ON`: the patch is applied and the sources compile in,
  but the engine's per-opcode `DEBUGGER_CHECK` is a no-op (one never-taken
  branch) **unless a debugger is attached at runtime**. Non-debugged runs pay
  essentially nothing.

## Build

```bash
cmake -B build -DQZ_BUILD_DEBUGGER=ON -DQZ_BUILD_TESTS=ON
cmake --build build -j$(nproc)
```

This applies `deps/quickjs-ng-debugger.patch` to the QuickJS-ng submodule
working tree at configure time (the submodule stays clean in git — the patch
is the source of truth). `cmake -DQZ_BUILD_DEBUGGER=OFF` restores pristine.

## Enable debugging in your program

**Option A — no code change (env var):** run your program with `QZ_DEBUG=1`:

```bash
QZ_DEBUG=1 ./myapp app.js
```

**Option B — config bit:** set bit 1 of `qz_config_t.debug` (bit 0 is the
existing verbose-log flag):

```c
qz_config_t cfg = {};
cfg.debug = 0x2;            /* bit 1 = debug-enable (or just run with QZ_DEBUG=1) */
cfg.initial_script = src;   /* pauses at entry, then at breakpoints */
qz_t *rt = qz_create(&cfg);
```

That's it — `qz_create` auto-attaches DAP, sends `initialized`, and blocks
on the DAP configuration phase (initialize / setBreakpoints /
configurationDone) before returning. `stop_on_entry` pauses at the first
statement of your program.

## VS Code setup

Your program is the debug target — VS Code's `runtimeExecutable` points at
**your** binary, not a qzjs-provided one. Create `.vscode/launch.json`:

```json
{
  "version": "0.2.0",
  "configurations": [{
    "type": "qzjs",
    "request": "attach",
    "name": "qzjs: debug",
    "program": "${workspaceFolder}/app.js",
    "runtimeExecutable": "${workspaceFolder}/myapp",
    "runtimeArgs": ["${workspaceFolder}/app.js"],
    "env": { "QZ_DEBUG": "1" }
  }]
}
```

> **Note:** `type: "qzjs"` requires a VS Code extension that registers the
> `qzjs` debug type. Until a packaged extension ships, you can drive the DAP
> layer directly (the adapter speaks standard DAP over stdio) or use the
> scripted test (`test/test_dap_gtest.cpp`) as a reference client. The DAP
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
- A packaged VS Code extension registering the `qzjs` debug type is a
  follow-up; the DAP layer is complete and tested via the scripted clients.

## Async support

The debugger **does** advance async JS while paused. When stopped at a
breakpoint, the DAP layer's `on_stopped` loop polls stdin with a short timeout
and, between polls, drives one non-blocking iteration of the PAL event loop
(`pal->run_cycle(0)` + `qz_tick`). So `fetch` responses, `setTimeout`
callbacks, etc. continue to fire while you inspect the paused state — all on
the single JS thread (qzjs owns no threads). A re-entrancy guard prevents
PAL-driven JS from nesting another stop.

`test/test_dap_async.c` validates this: a uv-backed debuggee schedules a 100ms
`setTimeout`, hits a breakpoint, and the timer fires during the pause (the loop
exits with `n=1` rather than spinning to its cap).

## Test

```bash
cmake -B build -DQZ_BUILD_DEBUGGER=ON -DQZ_BUILD_TESTS=ON && cmake --build build -j$(nproc)
ctest --test-dir build -L dap --output-on-failure   # or: make -C <dir> then ctest -L dap
```

`test/test_dap_gtest.cpp` is an in-process embedding host that forks a child
running a tiny JS program under `QZ_DEBUG=1`, then acts as the VS Code
client over a pipe: initialize → setBreakpoints → configurationDone → expects
`stopped` at the breakpoint → stackTrace/scopes/variables/evaluate → step →
continue → terminate. It validates the whole stack: engine patch + debug core
+ DAP layer + the auto-attach path in `qz_create`.

## Troubleshooting

**Breakpoints never hit / no `stopped` event / tests time out at 30 s**

The engine's per-opcode breakpoint check is gated by a compile-time macro.
If the macro the CMake passes to the engine differs from the one in
`deps/quickjs-ng-debugger.patch`, `DEBUGGER_CHECK` compiles to a no-op and
debugging silently does nothing — no error, breakpoints just never fire.

1. Verify the macro matches in both places:

   ```bash
   grep -n DEBUG_SUPPORT deps/quickjs-ng-debugger.patch | head -4
   grep -n "QZ_DEBUG_SUPPORT_DEFINE" CMakeLists.txt
   ```

   Both must use the same name (currently `QZ_DEBUG_SUPPORT`). A project
   rename that misses the patch produces exactly this silent failure.

2. Verify the engine code is actually compiled in (not compiled out):

   ```bash
   grep -c "js_debugger_check" deps/quickjs-ng/quickjs.c
   ```

3. Confirm the DAP layer itself is linked (it lives in `libqzjs` only with
   `QZ_BUILD_DEBUGGER=ON`):

   ```bash
   nm build/libqzjs.a 2>/dev/null | grep -c qz_dap_attach   # or build_dbg/libqzjs.a for the debugger build
   ```

4. Run the end-to-end client — if it passes, the whole stack works and the
   problem is in your client's protocol exchange:

   ```bash
   ctest --test-dir build -L dap --output-on-failure
   ```

**`QZ_DEBUG=1` set but program doesn't pause at entry**

- Embedded host with the THREAD backend: the auto-attach happens on the
  qzjs thread during `qz_create` and blocks on the DAP configuration
  exchange — your client must send `initialize` + `setBreakpoints` +
  `configurationDone` or `qz_create` never returns.
- Worker runtimes never auto-attach (one stdio channel per process; a
  worker would race the parent for stdin). Breakpoints only apply to the
  attached runtime.
- `config.debug = 1` is **not** the debug bit — use `0x2` (bit 1).

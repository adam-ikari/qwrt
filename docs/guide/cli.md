---
title: Standalone CLI
description: Use qwrt as a standalone WinterTC runtime — run JavaScript scripts, one-liners, or an interactive REPL without embedding or Node.js.
---

# Standalone CLI

qwrt ships a standalone runtime executable (built by default with `QWRT_BUILD_CLI=ON`)
that runs the full WinterTC Web API surface directly — no Node.js APIs
(`process`, `require`, `Buffer`) by design.

## Build

The CLI is part of the default build:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)   # produces build/qwrt
```

## Usage

```bash
qwrt script.js [args...]   # run a script file
qwrt -e 'code' [args...]   # evaluate an expression / statement
qwrt                       # interactive REPL (Ctrl-D to exit)
qwrt --help                # usage
qwrt --version             # version string
```

### Script mode

```bash
./build/qwrt hello.js
# hello from qwrt
```

Script args are exposed as `globalThis.arguments` (WinterCG
[proposal-cli-api](https://github.com/wintercg/proposal-cli-api) direction — the
executable name and script path are excluded):

```bash
./build/qwrt -e 'console.log(JSON.stringify(globalThis.arguments))' a b c
# ["a","b","c"]
```

### -e eval mode

```bash
./build/qwrt -e 'const r = await fetch("https://example.com"); console.log(r.status)'
```

### REPL

Run `qwrt` with no arguments for an interactive session:

```text
$ qwrt
qwrt 0.1.0 (WinterTC runtime) — type JS, Ctrl-D to exit
1 + 2
3
```

## Runtime Behaviour

- **Async exit** — after the top-level script finishes, the runtime keeps
  running until all pending async work (timers, fetch, streams) completes, then
  exits. A 50ms `setTimeout` always fires before the process terminates.
- **Console routing** — `console.log`/`info`/`debug` → stdout;
  `console.warn`/`error` → stderr (aligned with web-runtime console semantics).
- **`globalThis.env`** — the process environment as a plain object.
- **Exit codes** — `0` success; `1` script threw (message on stderr) or the file
  was unreadable; `2` unknown flag / bad `-e` usage.

## No Node.js API

The CLI intentionally exposes **no** Node-style globals — there is no `process`,
no `require`, no `Buffer`, no CommonJS. Scripts use the same WinterTC Web APIs
that embedded qwrt offers (fetch, console, crypto.subtle, ReadableStream,
timers, URL, TextEncoder, …).

## Next Steps

- [Event Loop](/guide/event-loop) — how the internal thread and libuv loop work
- [Embedding](/guide/embedding) — the C API for host applications

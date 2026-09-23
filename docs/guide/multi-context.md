---
title: Multi-Context & Web Workers
description: Parallel execution in qzjs — new Worker(url), isolated contexts, and the ISOLATED vs THREAD process models.
---

# Multi-Context & Web Workers

qzjs runs multiple independent JS execution contexts. The **host-facing** way
to get parallelism is the standard `new Worker(url)` Web API. Internally, each
worker gets its own isolated `JSContext` (and, under the default process model,
its own process).

## Web Workers

Create a worker from any script with the standard API:

```js
// main script
const w = new Worker("worker.js");   // file:// script path
w.onmessage = (e) => console.log("from worker:", e.data);
w.postMessage({ cmd: "start" });

// worker.js
globalThis.onmessage = (e) => postMessage("echo: " + e.data.cmd);
```

Workers communicate only via `postMessage`/`onmessage` — they share no globals,
no DOM, and no `JSRuntime` with their creator. This is the only multi-context
surface exposed to JS.

## Process Model

The `-DQZ_PROCESS_MODEL` build option controls how a worker runs:

| Model | Worker execution |
|-------|------------------|
| `THREAD` | a parallel thread in the same process |
| `ISOLATED` (default) | a dedicated child process |

`ISOLATED` (the default) gives each worker a separate
process with its own address space and event loop. `THREAD` is the single-
process fallback. Both present the same `new Worker` API to JS.

## Isolated Contexts (internal)

At the C layer qzjs maintains a set of isolated contexts (`qz_ctx_t`) with a
single active context at a time. Contexts have independent globals, native state, and
extension state, and can be soft-suspended/resumed to disk. This machinery is
**internal** — there is no public host API to spawn/suspend/resume a context.
It exists to back `new Worker` and the extension lifecycle, and is exposed to C
extensions (built into qzjs) through the internal context helpers
(`qz_get_active_ctx`, `qz_get_active_jsctx`, `qz_get_ctx_by_id` in
`src/context.c`).

Extension `init`/`destroy`/`suspend`/`resume` hooks fire at the corresponding
context lifecycle points; see [Extensions](/guide/extensions).

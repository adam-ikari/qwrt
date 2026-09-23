---
title: Bytecode Compilation
description: How qzjs uses bytecode internally (qjsc) to speed startup — and why there is no public host-side bytecode-loading API.
---

# Bytecode Compilation

qzjs precompiles its own JavaScript (the WinterTC polyfill and the worker boot
script) to **bytecode** at build time using the `qjsc` compiler.
Loading bytecode skips parsing entirely, which speeds startup and shrinks the
shipped payload.

## Internal Use

The build pipeline compiles the polyfill sources to bytecode and embeds them in
the binary:

```bash
# qzjs's build does this for the WinterTC polyfill and worker boot script
qjsc -c polyfill.js -o polyfill_bytecode.c
```

At runtime the embedded bytecode is evaluated on the internal thread instead of
parsing source. This is a qzjs-internal optimization — the bytecode is produced
from qzjs's own sources and never exposed to hosts.

## No Public Bytecode API

There is **no public `qz_compile` / `qz_eval_bytecode`** in `qzjs.h`, and
the `qzjs` CLI has no bytecode option. A host cannot hand qzjs a bytecode blob
to execute; JS is provided to the runtime as source via `initial_script`, as
messages, or as `new Worker(url)` scripts (see [JS Execution](/guide/execution)).

The only bytecode-evaluation entry point is internal
(`qz_eval_bytecode_internal` in `src/qz_internal.h`), used by qzjs's own
runtime and by C extensions built into qzjs. If you are writing such an
extension you may use it; ordinary host embedding cannot.

## When Bytecode Still Helps You

If startup latency matters, you do not need bytecode — you ship a small script
in `initial_script` and let qzjs's precompiled polyfill carry the cost. For
larger application scripts, prefer bundling them into a single file (or a
`new Worker` script) over hand-tuning bytecode, since the public surface has no
bytecode path.

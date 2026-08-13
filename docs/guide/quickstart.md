---
title: Quick Start
description: Get Qwrt.js running in under 5 minutes — clone, build, and run your first JavaScript program on the embedded QuickJS-ng runtime.
---

# Quick Start

Get qwrt running in under 5 minutes.

## Prerequisites

- **C compiler** — GCC 8+, Clang 10+, or MSVC 2019+
- **CMake** 3.16+
- **Git** (for submodules)

## Clone & Build

```bash
# Clone with all submodules
git clone --recursive https://github.com/adam-ikari/qwrt.git
cd qwrt

# Configure and build (Release mode)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

The build produces `libqwrt.a` (static core) and `libqwrt_full.a` (aggregator that links libuv, mbedTLS, and friends) in `build/lib/`.

## Your First Program

Create `hello.c`:

```c
#include <qwrt/qwrt.h>
#include <stdio.h>

static void on_message(qwrt_t *rt, const char *json, size_t len, void *data) {
    (void)rt; (void)data;
    printf("received: %.*s\n", (int)len, json);
}

int main(void) {
    // Create the runtime — qwrt starts its own internal thread and loop
    qwrt_config_t cfg = {0};
    cfg.initial_script = "console.log('Hello from QuickJS!'); postMessage(1 + 1);";
    cfg.message_cb = on_message;
    qwrt_t *rt = qwrt_create(&cfg);
    if (!rt) {
        fprintf(stderr, "Failed to create runtime\n");
        return 1;
    }

    // Drive the runtime by posting JSON messages
    qwrt_post_message(rt, "{\"cmd\":\"echo\",\"data\":\"hi\"}", 26);

    // Clean up — graceful shutdown
    qwrt_destroy(rt);
    return 0;
}
```

Compile and link against `libqwrt_full` (aggregates qwrt + libuv + deps):

```bash
cc -std=c99 -I include -o hello hello.c \
   -L build/lib -lqwrt_full -lm
```

## Build with Tests

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DQWRT_BUILD_TESTS=ON
cmake --build build -j$(nproc)
cd build && ctest --output-on-failure
```

Tests are labeled for targeted runs:

```bash
ctest -L offline     # local, deterministic tests (CI default)
ctest -L network     # outbound HTTP/HTTPS tests
ctest -L benchmark   # performance benchmarks (not pass/fail)
```

## Next Steps

- [Building](/guide/building) — all CMake options explained
- [Runtime Lifecycle](/guide/lifecycle) — create, use, destroy
- [Embedding](/guide/embedding) — message-based host patterns

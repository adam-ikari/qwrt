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

The build produces `libqwrt.a` and all PAL backends in `build/lib/`.

## Your First Program

Create `hello.c`:

```c
#include <qwrt/qwrt.h>
#include <pal_uv.h>
#include <stdio.h>

int main(void) {
    // Create the Platform Abstraction Layer (libuv)
    qwrt_pal_t *pal = pal_uv_create(uv_default_loop());

    // Create the runtime
    qwrt_t *rt = qwrt_create(&(qwrt_config_t){ .pal = pal });
    if (!rt) {
        fprintf(stderr, "Failed to create runtime\n");
        return 1;
    }

    // Evaluate some JavaScript
    char *result = NULL;
    if (qwrt_eval(rt, "console.log('Hello from QuickJS!'); 1 + 1", &result) == 0) {
        printf("Result: %s\n", result);  // "2"
        qwrt_free(result);
    }

    // Drive the event loop (needed even for synchronous eval to drain microtasks)
    while (pal->run_cycle(pal, 100) > 0) {
        qwrt_tick(rt);
    }

    // Clean up
    qwrt_destroy(rt);
    return 0;
}
```

Compile and link:

```bash
cc -std=c99 -I include -o hello hello.c \
   -L build/lib -lqwrt -lqwrt_uv -lm
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
- [Runtime Lifecycle](/guide/lifecycle) — create, reset, destroy
- [PAL Overview](/pal/) — understand the Platform Abstraction Layer

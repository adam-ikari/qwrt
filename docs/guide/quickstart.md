---
title: Quick Start
description: Get Amoib.js running in under 5 minutes — clone, build, and run your first JavaScript program on the embedded QuickJS-ng runtime.
---

# Quick Start

Get amoib running in under 5 minutes.

## Prerequisites

- **C compiler** — GCC 8+ or Clang 10+ (POSIX; Windows/MSVC is not yet supported)
- **CMake** 3.10+
- **Git** (for submodules)

## Clone & Build

```bash
# Clone with all submodules
git clone --recursive https://github.com/adam-ikari/amoib.git
cd amoib

# Configure and build (Release mode)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Size-sensitive builds: add `-DAM_PROFILE=minimal` (keeps WinterTC
compatibility, 2.45 MiB stripped). See [Build Options](/guide/build-options).

The build produces `libamoib.a` (static core) and `libam_full.a` (link-interface aggregator for CMake consumers) in `build/`, plus `build/amoib.pc` for pkg-config.

## Your First Program

Create `hello.c`:

```c
#include <amoib/amoib.h>
#include <stdio.h>

static void on_message(am_t *rt, const char *json, size_t len, void *data) {
    (void)rt; (void)data;
    printf("received: %.*s\n", (int)len, json);
}

int main(void) {
    // Create the runtime — amoib starts its own internal thread and loop
    am_config_t cfg = {0};
    cfg.initial_script = "console.log('Hello from QuickJS!'); postMessage(1 + 1);";
    cfg.message_cb = on_message;
    am_t *rt = am_create(&cfg);
    if (!rt) {
        fprintf(stderr, "Failed to create runtime\n");
        return 1;
    }

    // Drive the runtime by posting JSON messages
    am_post_message(rt, "{\"cmd\":\"echo\",\"data\":\"hi\"}", 26);

    // Clean up — graceful shutdown
    am_destroy(rt);
    return 0;
}
```

Compile and link with pkg-config (pulls the full static link line — all vendored archives):

```bash
cc -std=c99 -o hello hello.c $(pkg-config --cflags --libs amoib)
```

For an in-tree build, point pkg-config at the build directory first:

```bash
export PKG_CONFIG_PATH="$PWD/build"
```

## Build with Tests

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DAM_BUILD_TESTS=ON
cmake --build build -j$(nproc)
cd build && ctest --output-on-failure
```

Tests are labeled for targeted runs:

```bash
ctest -L offline   # local, deterministic tests (CI default)
ctest -L dap       # DAP protocol tests
ctest -L test262   # ECMA-262 conformance suite
```

## Next Steps

- [Building](/guide/building) — all CMake options explained
- [Runtime Lifecycle](/guide/lifecycle) — create, use, destroy
- [Embedding](/guide/embedding) — message-based host patterns

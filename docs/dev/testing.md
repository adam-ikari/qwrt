# Testing

Qwrt.js has a comprehensive multi-layer test suite.

## Test Layers

| Layer | Runner | Coverage | Command |
|-------|--------|----------|---------|
| **Offline** | gtest + ctest | Core runtime, extensions, WASM | `ctest -L offline` |
| **WPT** | wpt_runner | WinterTC Web APIs | `./build/test/wpt_runner test/wpt` |
| **test262** | test262_runner | ECMAScript language conformance | `./build/test/test262_runner test/test262/test` |
| **Network** | ctest | HTTP/HTTPS/TLS integration | `ctest -L network` |
| **Benchmark** | ctest | Performance regression | `ctest -L benchmark` |
| **DAP** | ctest | Debugger protocol | `ctest -L dap` |

## Quick Run

```bash
# Configure with tests
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DQWRT_BUILD_TESTS=ON
cmake --build build -j$(nproc)

# All offline tests
cd build && ctest -L offline --output-on-failure

# WPT WinterTC compliance
./build/test/wpt_runner test/wpt

# test262 ECMAScript conformance (first 2000 tests)
./build/test/test262_runner test/test262/test
```

## Test Labels

| Label | Description |
|-------|-------------|
| `offline` | Local, deterministic — CI default |
| `network` | Outbound HTTP/HTTPS (non-blocking) |
| `benchmark` | Performance tests |
| `wpt` | WinterTC Web Platform Tests |
| `test262` | ECMAScript language conformance |
| `dap` | Debugger protocol tests |

## Current Results

| Suite | Tests | Pass | Fail | Skip | Rate |
|-------|-------|------|------|------|------|
| Offline | 15 | 15 | 0 | 0 | 100% |
| WASM compliance | 14 | 14 | 0 | 0 | 100% |
| WPT WinterTC | 32 | 27 | 0 | 5 | 100%¹ |
| test262 (built-ins) | 2,000 | 1,118 | 494 | 388 | 69.4% |

¹ All 5 skipped tests are non-UTF encoding labels (intentionally unsupported).

## Memory Safety

All offline tests pass under AddressSanitizer with leak detection
(`ASAN_OPTIONS=detect_leaks=1`) and UndefinedBehaviorSanitizer (UBSan).
Valgrind confirms zero bytes definitely lost.

## Writing Tests

Tests use GoogleTest (C++), linked against `qwrt` + `mock_libuv` — a
deterministic in-process fake of the libuv API (see `test/mock_libuv.{c,h}`) —
and are built with `-DQWRT_USE_MOCK_LIBUV`. Tests drive the runtime through
the `HostCtx` harness in `test/test_host.h`: `host_create` starts a qwrt
runtime and installs a bootstrap `onmessage` command channel
(`{cmd:'eval'}`, `{cmd:'echo'}`); `host_eval`/`host_value` evaluate JS and
return the result; `host_poll_until_value` polls until an async condition
(timer, promise, storage) is met.

```cpp
#include <qwrt/qwrt.h>
#include "test_host.h"   // HostCtx harness + mock_libuv
#include <gtest/gtest.h>

class MyTest : public ::testing::Test {
protected:
    HostCtx *h = nullptr;

    void SetUp() override {
        h = host_create();       // starts a runtime + test bootstrap
        ASSERT_NE(nullptr, h);
    }

    void TearDown() override { host_destroy(h); }
};

TEST_F(MyTest, EvalExpression) {
    std::string out;
    ASSERT_TRUE(host_value(h, "1 + 1", &out));
    EXPECT_EQ(out, "2");
}

TEST_F(MyTest, AsyncTimer) {
    host_eval(h, "setTimeout(() => { globalThis.flag = 'fired'; }, 100);");
    std::string out;
    EXPECT_TRUE(host_poll_until_value(h, "globalThis.flag", "fired", &out));
}
```

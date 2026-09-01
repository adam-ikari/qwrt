# Testing

Qwrt.js has a comprehensive multi-layer test suite.

## Test Layers

| Layer | Runner | Coverage | Command |
|-------|--------|----------|---------|
| **Offline** | gtest + ctest | Core runtime, extensions, WASM | `ctest -L offline` |
| **WinterTC** | gtest | Web APIs (URL/URLPattern/FormData/Event/Blob/console/...) | `ctest -L offline` (`test_polyfill_gtest` etc.) |
| **test262** | ctest | ECMAScript language conformance | `ctest -R test262` |
| **Network** | ctest | HTTP/HTTPS/TLS integration | `ctest -L network` |
| **Benchmark** | ctest | Performance regression | `ctest -L benchmark` |
| **DAP** | ctest | Debugger protocol | `ctest -L dap` |

> The old `wpt_runner` (vendored WPT `.any.js` files) was removed in the
> libuv-native refactor (mock-PAL gone). WinterTC Web API coverage now lives
> in the offline gtest suites (`test_polyfill_gtest` etc.); the vendored
> `test/wpt/` files remain for reference.

## Quick Run

```bash
# Configure with tests
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DQWRT_BUILD_TESTS=ON
cmake --build build -j$(nproc)

# All offline tests (includes WinterTC Web API gtest suites)
cd build && ctest -L offline --output-on-failure

# test262 ECMAScript conformance
cd build && ctest -R test262
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
| Offline (gtest) | 13 | 13 | 0 | 0 | 100% |
| WASM compliance | 14 | 14 | 0 | 0 | 100% |
| WASM streaming | 3 | 3 | 0 | 0 | 100% |
| CLI end-to-end | 9 | 9 | 0 | 0 | 100% |
| HTTPServer e2e | 8 | 8 | 0 | 0 | 100% |
| test262 (quickjs runner) | 1 | 1 | 0 | 0 | 100% |

¹ WASM streaming 3 用例来自 `test/test_wasm_streaming_gtest.cpp`：compileStreaming/instantiateStreaming 语义等价实现 + 非法 source 拒绝。
² CLI end-to-end 来自 `test/test_cli_gtest.cpp`（fork 真实 qwrt 可执行文件，断言 stdout/stderr/退出码）。
³ HTTPServer e2e 来自 `test/test_httpserver_e2e.py`（真实 libuv 构建 + 纯 JS serve() listener）。

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

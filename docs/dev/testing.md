# Testing

Qwrt.js has a comprehensive multi-layer test suite.

## Test Layers

| Layer | Runner | Coverage | Command |
|-------|--------|----------|---------|
| **Offline** | gtest + ctest | Core runtime, PAL, extensions, WASM | `ctest -L offline` |
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

Tests use GoogleTest (C++), linked against `qwrt` + `qwrt_mock` for
deterministic behavior with no network or system calls.

```cpp
#include <qwrt/qwrt.h>
#include <pal_mock.h>
#include <gtest/gtest.h>

class MyTest : public ::testing::Test {
protected:
    qwrt_t *rt = nullptr;
    qwrt_pal_t *pal = nullptr;

    void SetUp() override {
        pal = pal_mock_create();
        rt = qwrt_create(&(qwrt_config_t){ .pal = pal });
    }

    void TearDown() override {
        if (rt) qwrt_destroy(rt);
        if (pal) pal_mock_destroy(pal);
    }
};

TEST_F(MyTest, EvalExpression) {
    char *result = nullptr;
    ASSERT_EQ(qwrt_eval(rt, "1 + 1", &result), 0);
    EXPECT_STREQ(result, "2");
    qwrt_free(result);
}
```

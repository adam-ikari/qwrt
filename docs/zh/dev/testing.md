# 测试

qzjs 拥有全面的多层测试套件。

## 测试层次

| 层次 | 运行器 | 覆盖范围 | 命令 |
|------|--------|----------|------|
| **离线** | gtest + ctest | 核心运行时、扩展、WASM | `ctest -L offline` |
| **test262** | run-test262 | ECMAScript 语言合规性 | `ctest -L test262` |
| **DAP** | ctest | 调试器协议 | `ctest -L dap` |
| **e2e** | shell / python / mjs | HTTPServer、多进程、控制面、Service Worker、gRPC —— 真实 libuv，非 mock | 见 `.github/workflows/ci.yml` 的 `e2e` job |

> `wpt_runner`（vendored WPT `.any.js` 文件）已在 libuv-native 重构中移除。
> WinterTC Web API 覆盖现在位于离线 gtest 套件（`test_polyfill_gtest` 等）；
> vendored 的 `test/wpt/` 文件仅作参考保留，不参与构建或 CI。

## 快速运行

```bash
# 带测试配置
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DQZ_BUILD_TESTS=ON
cmake --build build -j$(nproc)

# 所有离线测试
cd build && ctest -L offline --output-on-failure

# test262 ECMAScript 合规性
ctest -L test262
```

## 测试标签

| 标签 | 描述 |
|------|------|
| `offline` | 本地、确定性 — CI 默认 |
| `dap` | 调试器协议测试（需 `-DQZ_BUILD_DEBUGGER=ON`） |
| `test262` | ECMAScript 语言合规性（需语料） |

以上三者是**全部**已注册标签——请以当前构建实际注册的为准，不要相信写死的列表：

```bash
ctest --print-labels   # 本构建的标签
ctest -N               # 本构建的测试名
```

> 历史说明：本页早期草稿出现过 `network` / `benchmark` / `wpt` 标签，但从未被
> 注册过。网络与性能覆盖实际位于 CI 的 shell/python/mjs e2e 与基准 job；
> WPT 已移除（见上）。

## 当前结果

跑一遍即可得到——测试数量随构建的 `QZ_*` 开关变化，写死的表格会立刻过期：

```bash
cd build
ctest -L offline --output-on-failure
ctest -N -L offline | tail -1     # 本构建注册了多少个测试
```

## 内存安全

所有离线测试在 AddressSanitizer（`ASAN_OPTIONS=detect_leaks=1`）和 UndefinedBehaviorSanitizer（UBSan）下通过。Valgrind 确认零字节 definite lost。

## 编写测试

测试使用 GoogleTest（C++），链接 `qzjs` + `mock_libuv` —— 一个确定性的进程内 libuv API 假实现（见 `test/mock_libuv.{c,h}`）——并使用 `-DQZ_USE_MOCK_LIBUV` 构建。测试通过 `test/test_host.h` 中的 `HostCtx` 测试桩驱动运行时：`host_create` 启动一个 qzjs 运行时并安装引导 `onmessage` 命令通道（`{cmd:'eval'}`、`{cmd:'echo'}`）；`host_eval`/`host_value` 求值 JS 并返回结果；`host_poll_until_value` 轮询直到异步条件（定时器、promise、存储）满足。

```cpp
#include <qzjs/qzjs.h>
#include "test_host.h"   // HostCtx 测试桩 + mock_libuv
#include <gtest/gtest.h>

class MyTest : public ::testing::Test {
protected:
    HostCtx *h = nullptr;

    void SetUp() override {
        h = host_create();       // 启动运行时 + 测试引导
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
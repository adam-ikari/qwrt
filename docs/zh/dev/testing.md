# 测试

Qwrt.js 拥有全面的多层测试套件。

## 测试层次

| 层次 | 运行器 | 覆盖范围 | 命令 |
|------|--------|----------|------|
| **离线** | gtest + ctest | 核心运行时、PAL、扩展、WASM | `ctest -L offline` |
| **WPT** | wpt_runner | WinterTC Web API | `./build/test/wpt_runner test/wpt` |
| **test262** | run-test262 | ECMAScript 语言合规性 | `ctest -L test262` |
| **网络** | ctest | HTTP/HTTPS/TLS 集成 | `ctest -L network` |
| **基准** | ctest | 性能回归 | `ctest -L benchmark` |
| **DAP** | ctest | 调试器协议 | `ctest -L dap` |

## 快速运行

```bash
# 带测试配置
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DQWRT_BUILD_TESTS=ON
cmake --build build -j$(nproc)

# 所有离线测试
cd build && ctest -L offline --output-on-failure

# WPT WinterTC 合规性
./build/test/wpt_runner test/wpt

# test262 ECMAScript 合规性
ctest -L test262
```

## 测试标签

| 标签 | 描述 |
|------|------|
| `offline` | 本地、确定性 — CI 默认 |
| `network` | 出站 HTTP/HTTPS（非阻塞） |
| `benchmark` | 性能测试 |
| `wpt` | WinterTC Web 平台测试 |
| `test262` | ECMAScript 语言合规性 |
| `dap` | 调试器协议测试 |

## 当前结果

| 套件 | 测试数 | 通过 | 失败 | 跳过 | 通过率 |
|------|--------|------|------|------|--------|
| 离线 | 15 | 15 | 0 | 0 | 100% |
| WASM 合规 | 14 | 14 | 0 | 0 | 100% |
| WPT WinterTC | 32 | 27 | 0 | 5 | 100%¹ |
| test262 | 42,407 | 42,339 | 68 | 10,004² | 99.8% |

¹ 5 个跳过均为非 UTF 编码标签（有意不支持）  
² 4,986 个排除（QuickJS-ng test262.conf 特性跳过）+ 6,018 个跳过（模块/异步模式）

## 内存安全

所有离线测试在 AddressSanitizer（`ASAN_OPTIONS=detect_leaks=1`）和 UndefinedBehaviorSanitizer（UBSan）下通过。Valgrind 确认零字节 definite lost。

## 编写测试

测试使用 GoogleTest（C++），链接 `qwrt` + `qwrt_mock`，实现确定性行为，无网络或系统调用。

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
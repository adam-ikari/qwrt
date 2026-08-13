// test_context_gtest.cpp — Task 5: 多上下文 JS API（qwrtContext）
//
// 宿主只见主 context；子 context 由 qwrtContext.spawn 建立（返回 ctx id），
// destroy 释放槽位，suspend/resume 见 test_suspend_gtest.cpp。
// 全部经 host_eval 命令通道驱动，子 context 不与宿主直接通信（避免 eval
// 进行中子 context 的同步 postMessage 打乱回复匹配）。
#include "test_host.h"

// spawn 返回首个空闲槽（主 context 占槽 0 → 子 context id = 1）；子 context
// 的全局不泄漏到主 context；主 context 不受影响、继续工作。
TEST(context_, spawn_isolation) {
    HostCtx *h = host_create();
    ASSERT_NE(nullptr, h);
    std::string out;

    std::string spawnCode =
        "JSON.stringify(qwrtContext.spawn(" + JSON_string("globalThis.y = 5;") + "))";
    ASSERT_TRUE(host_value(h, spawnCode.c_str(), &out));
    EXPECT_EQ("1", out) << "got: " << out;

    ASSERT_TRUE(host_value(h, "typeof globalThis.y", &out));
    EXPECT_EQ("undefined", out) << "got: " << out;

    ASSERT_TRUE(host_value(h, "1 + 1", &out));
    EXPECT_NE(std::string::npos, out.find("2")) << "got: " << out;
    host_destroy(h);
}

// destroy 释放槽位：destroy(1) 后再 spawn 复用同一 id；主 context 仍正常。
TEST(context_, spawn_destroy_reuse_slot) {
    HostCtx *h = host_create();
    ASSERT_NE(nullptr, h);
    std::string out;

    ASSERT_TRUE(host_value(h,
        "JSON.stringify(qwrtContext.spawn('globalThis.a = 1;'))", &out));
    EXPECT_EQ("1", out) << "got: " << out;

    ASSERT_TRUE(host_value(h,
        "qwrtContext.destroy(1); JSON.stringify(qwrtContext.spawn('globalThis.b = 2;'))", &out));
    EXPECT_EQ("1", out) << "got: " << out;

    ASSERT_TRUE(host_value(h, "1 + 2", &out));
    EXPECT_NE(std::string::npos, out.find("3")) << "got: " << out;
    host_destroy(h);
}

// 挂起正在执行的主 context（active=0）被拒（QWRT_ERR_BUSY）——不能销毁/挂起
// 自己正在运行的 context；错误经 throw 传播给调用方。
TEST(context_, cannot_suspend_active_ctx) {
    HostCtx *h = host_create();
    ASSERT_NE(nullptr, h);
    std::string out;

    ASSERT_TRUE(host_eval(h,
        "var rejected = false;"
        "try { qwrtContext.suspend(0, ''); } catch (e) { rejected = true; }"
        "JSON.stringify(rejected)", &out));
    ASSERT_NE(std::string::npos, out.find("true")) << "got: " << out;

    ASSERT_TRUE(host_value(h, "2 + 3", &out));
    EXPECT_NE(std::string::npos, out.find("5")) << "got: " << out;
    host_destroy(h);
}

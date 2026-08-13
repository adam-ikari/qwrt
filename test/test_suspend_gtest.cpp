// test_suspend_gtest.cpp — Task 5: 软挂起/恢复（qwrtContext.suspend/resume）
//
// 挂起 = 捕获子 context 的可克隆全局属性 → 结构化克隆字节 → 写盘 → 销毁 ctx。
// 恢复 = 在原槽位重建 ctx + 反序列化状态回 globalThis。
//
// 往返一致性：suspend → a.bin；resume（空 init 脚本，状态完全来自 a.bin）→
// suspend → b.bin；字节级比较 a == b。restore 若丢属性，第二次捕获会缺键，
// 字节必然不同——这个测试真实验证了反序列化回写。
#include "test_host.h"
#include <string>
#include <cstdio>

static bool read_file_bytes(const char *path, std::string *out) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return false; }
    long sz = ftell(f);
    rewind(f);
    std::string b;
    if (sz > 0) {
        b.resize((size_t)sz);
        if (fread(&b[0], 1, (size_t)sz, f) != (size_t)sz) { fclose(f); return false; }
    }
    fclose(f);
    *out = b;
    return true;
}

TEST(qwrt_suspend_, child_state_roundtrip) {
    HostCtx *h = host_create();
    ASSERT_NE(nullptr, h);
    std::string out;

    // 子 context：init 脚本定义用户状态（foo / marker）
    const char *childScript =
        "globalThis.foo = {n: 42, s: 'x'}; globalThis.marker = 'state';";
    std::string spawnCode =
        "JSON.stringify(qwrtContext.spawn(" + JSON_string(childScript) + "))";
    ASSERT_TRUE(host_value(h, spawnCode.c_str(), &out));
    ASSERT_EQ("1", out) << "spawn id: " << out;

    // 挂起 → a.bin
    std::string suspendA =
        "qwrtContext.suspend(1, '" TEST_DIR "/state_a.bin'); 'ok'";
    ASSERT_TRUE(host_eval(h, suspendA.c_str(), &out));
    ASSERT_NE(std::string::npos, out.find("ok")) << "got: " << out;

    // 恢复：空 init 脚本，状态完全来自 a.bin（验证 restore 而非重 eval）
    std::string resumeCode =
        "JSON.stringify(qwrtContext.resume(1, '', '" TEST_DIR "/state_a.bin'))";
    ASSERT_TRUE(host_value(h, resumeCode.c_str(), &out));
    ASSERT_EQ("1", out) << "resume id: " << out;

    // 再挂起 → b.bin；两次捕获必须字节级一致
    std::string suspendB =
        "qwrtContext.suspend(1, '" TEST_DIR "/state_b.bin'); 'ok'";
    ASSERT_TRUE(host_eval(h, suspendB.c_str(), &out));
    ASSERT_NE(std::string::npos, out.find("ok")) << "got: " << out;

    std::string a, b;
    ASSERT_TRUE(read_file_bytes(TEST_DIR "/state_a.bin", &a));
    ASSERT_TRUE(read_file_bytes(TEST_DIR "/state_b.bin", &b));
    EXPECT_FALSE(a.empty());
    EXPECT_EQ(a, b) << "suspend→resume→suspend 状态不一致（restore 可能丢属性）";

    // 子 context 的状态始终不泄漏到主 context
    ASSERT_TRUE(host_value(h, "typeof globalThis.foo", &out));
    EXPECT_EQ("undefined", out) << "got: " << out;
    ASSERT_TRUE(host_value(h, "1 + 1", &out));
    EXPECT_NE(std::string::npos, out.find("2")) << "got: " << out;

    host_destroy(h);
    remove(TEST_DIR "/state_a.bin");
    remove(TEST_DIR "/state_b.bin");
}

// 不存在的 context id 挂起 → 报错（NOT_FOUND），主 context 不受影响。
TEST(qwrt_suspend_, unknown_ctx_id) {
    HostCtx *h = host_create();
    ASSERT_NE(nullptr, h);
    std::string out;

    ASSERT_TRUE(host_eval(h,
        "var rejected = false;"
        "try { qwrtContext.suspend(99, ''); } catch (e) { rejected = true; }"
        "JSON.stringify(rejected)", &out));
    ASSERT_NE(std::string::npos, out.find("true")) << "got: " << out;

    ASSERT_TRUE(host_value(h, "2 + 2", &out));
    EXPECT_NE(std::string::npos, out.find("4")) << "got: " << out;
    host_destroy(h);
}

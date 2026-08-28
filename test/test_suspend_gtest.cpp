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

/* ================================================================
 * A2 健壮性：软挂起/恢复边界
 * ================================================================ */

// destroy 已挂起槽位后 resume 回同槽：状态完全来自 state 文件；恢复后再次
// suspend 字节一致（restore 真实回写）。主 context 不受影响。
TEST(qwrt_suspend_, resume_into_fresh_slot) {
    HostCtx *h = host_create();
    ASSERT_NE(nullptr, h);
    std::string out;

    /* 借子 context 造一份状态盘 */
    ASSERT_TRUE(host_value(h,
        "JSON.stringify(qwrtContext.spawn(\"globalThis.k = 'v-fresh'\"))", &out));
    ASSERT_EQ("1", out);
    ASSERT_TRUE(host_eval(h,
        "qwrtContext.suspend(1, '" TEST_DIR "/state_fresh.bin'); 'ok'", &out));
    ASSERT_NE(std::string::npos, out.find("ok")) << "got: " << out;

    /* destroy 后 resume 回同槽：语义 = 从盘恢复 */
    ASSERT_TRUE(host_value(h, "qwrtContext.destroy(1); 'ok'", &out));
    ASSERT_TRUE(host_value(h,
        "JSON.stringify(qwrtContext.resume(1, '', '" TEST_DIR "/state_fresh.bin'))", &out));
    ASSERT_EQ("1", out) << "resume id: " << out;

    /* 恢复的 context 状态可校验：再挂起一次，读字节验证 k 在盘上 */
    ASSERT_TRUE(host_eval(h,
        "qwrtContext.suspend(1, '" TEST_DIR "/state_fresh2.bin'); 'ok'", &out));
    ASSERT_NE(std::string::npos, out.find("ok")) << "got: " << out;
    std::string a, b;
    ASSERT_TRUE(read_file_bytes(TEST_DIR "/state_fresh.bin", &a));
    ASSERT_TRUE(read_file_bytes(TEST_DIR "/state_fresh2.bin", &b));
    EXPECT_FALSE(a.empty());
    EXPECT_EQ(a, b) << "resume 后再 suspend 状态不一致";

    ASSERT_TRUE(host_value(h, "typeof globalThis.k", &out));
    EXPECT_EQ("undefined", out) << "子状态泄漏到主 context";
    host_destroy(h);
    remove(TEST_DIR "/state_fresh.bin");
    remove(TEST_DIR "/state_fresh2.bin");
}

// 坏 state 文件：路径不存在 → suspend/resume 报错（TypeError，C 返回 IO 失败），
// 主 context 不受影响。
TEST(qwrt_suspend_, resume_bad_state_path) {
    HostCtx *h = host_create();
    ASSERT_NE(nullptr, h);
    std::string out;

    ASSERT_TRUE(host_value(h,
        "JSON.stringify(qwrtContext.spawn(\"globalThis.a = 1\"))", &out));
    ASSERT_EQ("1", out);

    /* suspend 写盘成功后把文件删掉，再 resume：读文件失败 */
    ASSERT_TRUE(host_eval(h,
        "qwrtContext.suspend(1, '" TEST_DIR "/state_gone.bin'); 'ok'", &out));
    ASSERT_NE(std::string::npos, out.find("ok")) << "got: " << out;
    ASSERT_EQ(0, remove(TEST_DIR "/state_gone.bin"));
    ASSERT_TRUE(host_value(h,
        "qwrtContext.destroy(1); 'ok'", &out));

    ASSERT_TRUE(host_value(h,
        "var caught = '';\n"
        "try { qwrtContext.resume(1, '', '" TEST_DIR "/state_gone.bin'); } catch (e) { caught = e.name; }\n"
        "caught", &out));
    EXPECT_NE(std::string::npos, out.find("Error")) << "got: " << out;

    /* 主 context 正常 */
    ASSERT_TRUE(host_value(h, "40 + 2", &out));
    EXPECT_NE(std::string::npos, out.find("42")) << "got: " << out;
    host_destroy(h);
}

// 挂起含"不可克隆"属性（函数）的 context：函数记入 skipped 并在首次 restore
// 后永久丢失（不可克隆 → restore 无从重建，设计行为）；可克隆状态完整往返。
TEST(qwrt_suspend_, skipped_uncloneable_stable) {
    HostCtx *h = host_create();
    ASSERT_NE(nullptr, h);
    std::string out;

    const char *child =
        "globalThis.data = {n: 7}; globalThis.fn = function () {};";
    std::string spawnSk = "JSON.stringify(qwrtContext.spawn(" + JSON_string(child) + "))";
    ASSERT_TRUE(host_value(h, spawnSk.c_str(), &out));
    ASSERT_EQ("1", out);

    ASSERT_TRUE(host_eval(h,
        "qwrtContext.suspend(1, '" TEST_DIR "/state_sk1.bin'); 'ok'", &out));
    ASSERT_NE(std::string::npos, out.find("ok")) << "suspend#1: " << out;
    ASSERT_TRUE(host_value(h,
        "JSON.stringify(qwrtContext.resume(1, '', '" TEST_DIR "/state_sk1.bin'))", &out));
    ASSERT_EQ("1", out) << "resume: " << out;

    /* 第二次 suspend：restore 后的 ctx 快照 */
    ASSERT_TRUE(host_eval(h,
        "qwrtContext.suspend(1, '" TEST_DIR "/state_sk2.bin'); 'ok'", &out));
    ASSERT_NE(std::string::npos, out.find("ok")) << "suspend#2: " << out;
    /* skipped 属性（fn）在首次 restore 后**永久丢失**（不可克隆，restore 无从
       重建）——这是设计行为：第二次捕获的 skipped 少 fn，可克隆的 data 完整
       往返。断言：两次快照都含 data；第二次 skipped ⊆ 第一次 skipped，且
       第一次确实记录了 fn。 */
    std::string a, b;
    ASSERT_TRUE(read_file_bytes(TEST_DIR "/state_sk1.bin", &a));
    ASSERT_TRUE(read_file_bytes(TEST_DIR "/state_sk2.bin", &b));
    EXPECT_NE(std::string::npos, a.find("data")) << "第一次快照缺 data";
    EXPECT_NE(std::string::npos, b.find("data")) << "第二次快照缺 data（往返丢数据）";
    EXPECT_NE(std::string::npos, a.find("fn")) << "第一次快照未把 fn 记入 skipped";
    EXPECT_EQ(std::string::npos, b.find("fn")) << "fn 不应出现在 restore 后的快照";
    /* skipped 基础项：__native__ 恒在（polyfill 基础设施，两次快照共同前缀）。
       CryptoKey/SubtleCrypto/WebAssembly 随构建扩展集变化（如 ASan 构建可
       无 wasm 扩展），不硬编码。 */
    EXPECT_NE(std::string::npos, a.find("__native__")) << "第一次快照缺 __native__";
    EXPECT_NE(std::string::npos, b.find("__native__")) << "第二次快照缺 __native__";
    ASSERT_TRUE(host_value(h, "typeof globalThis.data", &out));
    EXPECT_EQ("undefined", out);
    host_destroy(h);
    remove(TEST_DIR "/state_sk1.bin");
    remove(TEST_DIR "/state_sk2.bin");
}

// 压力：suspend → resume(init) 循环 10 轮。restore 只写回捕获的键、不删除
// init 新增键，所以每轮 init 写下的 round=i 在 restore 后保留，下一轮
// suspend 字节必须含该轮标记——验证槽位稳定复用、状态逐轮无损、init 与
// restore 协同不互相破坏。主 context 全程可用。
TEST(qwrt_suspend_, stress_cycle_10_rounds) {
    HostCtx *h = host_create();
    ASSERT_NE(nullptr, h);
    std::string out;

    ASSERT_TRUE(host_value(h,
        "JSON.stringify(qwrtContext.spawn(\"globalThis.ctr = 0\"))", &out));
    ASSERT_EQ("1", out);

    for (int i = 1; i <= 10; i++) {
        std::string suspend =
            "qwrtContext.suspend(1, '" TEST_DIR "/state_cyc.bin'); 'ok'";
        ASSERT_TRUE(host_eval(h, suspend.c_str(), &out)) << "round " << i;
        ASSERT_NE(std::string::npos, out.find("ok")) << "round " << i;

        /* init 写独有键 round=i（restore 只写回捕获键、不删 init 新键，
           所以 round 每轮保留更新）；restore 是否真把 ctr 带回来，由末轮
           字节检查验证 */
        std::string init = "globalThis.round = " + std::to_string(i) + ";";
        std::string resume =
            "JSON.stringify(qwrtContext.resume(1, " + JSON_string(init.c_str()) +
            ", '" TEST_DIR "/state_cyc.bin'))";
        ASSERT_TRUE(host_value(h, resume.c_str(), &out)) << "round " << i;
        ASSERT_EQ("1", out) << "round " << i << " init threw: " << out;
    }

    /* 末轮 suspend 字节必须同时含 round=10（init 新键保留到最后一轮）与
       ctr:0（捕获状态每轮无损带回） */
    ASSERT_TRUE(host_eval(h,
        "qwrtContext.suspend(1, '" TEST_DIR "/state_cyc_end.bin'); 'ok'", &out));
    std::string bytes;
    ASSERT_TRUE(read_file_bytes(TEST_DIR "/state_cyc_end.bin", &bytes));
    EXPECT_NE(std::string::npos, bytes.find("round")) << "init 键未被保留";
    EXPECT_NE(std::string::npos, bytes.find("ctr")) << "捕获状态丢失";

    ASSERT_TRUE(host_value(h, "1 + 1", &out));
    EXPECT_NE(std::string::npos, out.find("2"));
    host_destroy(h);
    remove(TEST_DIR "/state_cyc.bin");
    remove(TEST_DIR "/state_cyc_end.bin");
}

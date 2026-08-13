// test_qwrt_gtest.cpp — 宿主契约核心套件（执行模型 A）
// 通过新 public API（qwrt_create / qwrt_post_message / qwrt_destroy）驱动
// 真实 qwrt 线程 + mock_libuv loop，消息经 message_cb 回传。
#include "test_host.h"
#include <thread>
#include <vector>
#include <set>
#include <cstdlib>

TEST(qwrt_create, creates_ready_runtime) {
    HostCtx *h = host_create();       // 无脚本：只验证 create 不阻塞不失败
    ASSERT_NE(nullptr, h);
    host_destroy(h);
}

TEST(qwrt_create, null_config_rejected) {
    EXPECT_EQ(nullptr, qwrt_create(nullptr));
}

TEST(qwrt_create, initial_script_exception_fails_create) {
    qwrt_config_t cfg = {}; cfg.initial_script = "throw new Error('boom');";
    EXPECT_EQ(nullptr, qwrt_create(&cfg));   // eval 异常 → ready_err → NULL
}

TEST(qwrt_post_message, host_message_roundtrip) {
    HostCtx *h = host_create();
    ASSERT_NE(nullptr, h);
    const char *json = "{\"cmd\":\"echo\",\"data\":{\"a\":1}}";
    ASSERT_EQ(0, qwrt_post_message(h->rt, json, strlen(json)));
    std::string out;
    ASSERT_TRUE(host_wait_msg(h, &out));
    ASSERT_EQ("{\"a\":1}", out);              // echo 原样回
    host_destroy(h);
}

TEST(qwrt_post_message, eval_via_command_channel) {
    HostCtx *h = host_create();
    ASSERT_NE(nullptr, h);
    std::string out;
    ASSERT_TRUE(host_eval(h, "1 + 2", &out));
    ASSERT_NE(std::string::npos, out.find("\"v\":\"3\""));
    host_destroy(h);
}

TEST(host_, wait_thread_exit) {
    HostCtx *h = host_create();
    ASSERT_NE(nullptr, h);
    host_destroy(h);                        // destroy 阻塞直到线程退出（join 完成即证明）
    SUCCEED();
}

TEST(host_, message_thread_safety) {
    HostCtx *h = host_create();
    ASSERT_NE(nullptr, h);
    const int N = 8, PER = 200;                       // 1600 条 echo, data = 0..1599 唯一
    std::vector<std::thread> ts;
    std::atomic<int> push_fail{0};
    for (int i = 0; i < N; i++) ts.emplace_back([h, i, &push_fail]{
        for (int k = 0; k < PER; k++) {
            std::string s = "{\"cmd\":\"echo\",\"data\":" + std::to_string(i*PER+k) + "}";
            if (qwrt_post_message(h->rt, s.data(), s.size()) != 0) push_fail++;
        }
    });
    /* 收集所有 echo 回复。多槽 inbox 保证每条都消费；内容校验 = 每个 0..1599
     * 恰好出现一次（无重复、无缺失、无覆盖），这才是"无丢消息"的严格判据。 */
    std::set<int> got;
    std::string out;
    auto t0 = std::chrono::steady_clock::now();
    while ((int)got.size() < N * PER) {
        if (host_wait_msg(h, &out, 2000)) got.insert(std::atoi(out.c_str()));
        else if (std::chrono::steady_clock::now() - t0 > std::chrono::seconds(20)) break;
    }
    for (auto &t : ts) t.join();
    ASSERT_EQ(0, push_fail.load());                   // 所有消息都 push 成功

    /* 排空迟到消息后验证命令通道仍健康（probe 内容必须确实是 40+2 的结果）。 */
    while (host_wait_msg(h, &out, 200)) got.insert(std::atoi(out.c_str()));
    std::string probe;
    ASSERT_TRUE(host_eval(h, "40 + 2", &probe, 2000));
    ASSERT_NE(std::string::npos, probe.find("\"v\":\"42\""));
    host_destroy(h);

    ASSERT_EQ(N * PER, (int)got.size());              // 1600 条全到，无重复
    for (int i = 0; i < N * PER; i++) ASSERT_TRUE(got.count(i)) << "missing echo " << i;
}

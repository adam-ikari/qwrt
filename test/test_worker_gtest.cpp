// test_worker_gtest.cpp — Task 4: 真线程 Web Worker
#include "test_host.h"

// worker 回显往返：父 → worker 'hello' → worker 回显 → 父 onmessage → 宿主。
// 事件顺序确定：eval ack 先到（父线程派发 eval 命令时 postMessage），worker
// 回显随后（worker 线程异步处理 + 父线程下一次 wake 派发），两者都经父线程
// 顺序写入宿主 inbox。
TEST(worker_, message_roundtrip) {
    HostCtx *h = host_create();
    ASSERT_NE(nullptr, h);

    std::string out;
    ASSERT_TRUE(host_eval(h,
        "globalThis.w = new Worker('file://" TEST_DIR "/worker_echo.js');\n"
        "w.onmessage = function(e){ postMessage({v: e.data}); };\n"
        "w.postMessage('hello');\n"
        "'started'", &out));

    ASSERT_TRUE(host_wait_msg(h, &out));   /* 等 worker 回显 */
    EXPECT_NE(std::string::npos, out.find("hello")) << "got: " << out;
    host_destroy(h);
}

// terminate：worker 退出后父 runtime 仍能正常 eval（worker 线程在父 teardown
// 时 join）。
TEST(worker_, terminate) {
    HostCtx *h = host_create();
    ASSERT_NE(nullptr, h);

    std::string out;
    ASSERT_TRUE(host_eval(h,
        "globalThis.w = new Worker('file://" TEST_DIR "/worker_idle.js');"
        "w.terminate(); 'ok'", &out));
    EXPECT_NE(std::string::npos, out.find("ok")) << "got: " << out;

    ASSERT_TRUE(host_value(h, "2 + 3", &out));
    EXPECT_NE(std::string::npos, out.find("5")) << "got: " << out;
    host_destroy(h);
}

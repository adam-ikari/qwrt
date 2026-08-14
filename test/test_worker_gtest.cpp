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

// Task 1: 脚本顶层异常 → 父侧 w.onerror 收到 {type:'error', error:<msg>}
// （事件 data），且 worker 继续存活（之后父→worker 往返仍通）。
TEST(worker_, error_notifies_parent) {
    HostCtx *h = host_create();
    ASSERT_NE(nullptr, h);

    std::string out;
    ASSERT_TRUE(host_eval(h,
        "globalThis.w = new Worker('file://" TEST_DIR "/worker_throw.js');\n"
        "w.onmessage = function(e){ postMessage({v: e.data}); };\n"
        "w.onerror = function(e){ postMessage({err: e.data}); };\n"
        "'started'", &out));

    ASSERT_TRUE(host_wait_msg(h, &out));   /* w.onerror → {err: e.data} */
    /* 断言 "err" 键：若路由损坏、错误消息落到 w.onmessage，载荷为 {v: {...}}
     * （键 "v" 而非 "err"），仅断言 "error"/"boom" 无法区分两条路径。 */
    EXPECT_NE(std::string::npos, out.find("\"err\"")) << "got: " << out;
    EXPECT_EQ(std::string::npos, out.find("\"v\"")) << "got: " << out;
    EXPECT_NE(std::string::npos, out.find("boom")) << "got: " << out;

    /* worker 存活：父 → worker 往返仍通 */
    ASSERT_TRUE(host_eval(h, "w.postMessage('ping'); 'ok'", &out));
    ASSERT_TRUE(host_wait_msg(h, &out));
    EXPECT_NE(std::string::npos, out.find("ping")) << "got: " << out;
    host_destroy(h);
}

// Task 1: 脚本顶层异常 → worker 侧 self.onerror 收到 ErrorEvent（其 message
// 含异常文本），并可通过 postMessage 回报父。
TEST(worker_, error_fires_self_onerror) {
    HostCtx *h = host_create();
    ASSERT_NE(nullptr, h);

    std::string out;
    ASSERT_TRUE(host_eval(h,
        "globalThis.w = new Worker('file://" TEST_DIR "/worker_self_error.js');\n"
        "w.onmessage = function(e){ postMessage({v: e.data}); };\n"
        "'started'", &out));

    /* worker 的 self.onerror → postMessage({workerErr: e.message}) → 父 onmessage */
    ASSERT_TRUE(host_wait_msg(h, &out));
    EXPECT_NE(std::string::npos, out.find("workerErr")) << "got: " << out;
    EXPECT_NE(std::string::npos, out.find("boom")) << "got: " << out;
    host_destroy(h);
}

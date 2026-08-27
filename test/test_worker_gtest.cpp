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

// Task 2: importScripts 同步加载 file:// 附加脚本（worker 侧通过 onmessage
// 接收路径，同步调用 importScripts，再回传 EXTRA 全局）。
TEST(worker_, import_scripts) {
    HostCtx *h = host_create();
    ASSERT_NE(nullptr, h);

    std::string out;
    ASSERT_TRUE(host_eval(h,
        "globalThis.w = new Worker('file://" TEST_DIR "/worker_import.js');\n"
        "w.onmessage = function(e){ postMessage({v: e.data}); };\n"
        "w.postMessage('file://" TEST_DIR "/worker_extra.js');\n"
        "'started'", &out));
    ASSERT_TRUE(host_wait_msg(h, &out));   /* worker 回传 EXTRA */
    EXPECT_NE(std::string::npos, out.find("42")) << "got: " << out;
    host_destroy(h);
}

// transferable：父 → worker 传 ArrayBuffer + transfer 列表，worker 回显内容，
// 父侧原 buffer 被 detach（byteLength → 0）。
TEST(worker_, transfer_arraybuffer_parent_to_worker) {
    HostCtx *h = host_create();
    ASSERT_NE(nullptr, h);

    std::string out;
    ASSERT_TRUE(host_eval(h,
        "var ab = new ArrayBuffer(4);\n"
        "new Uint8Array(ab).set([10, 20, 30, 40]);\n"
        "globalThis.w = new Worker('file://" TEST_DIR "/worker_echo.js');\n"
        "w.onmessage = function(e){ postMessage({v: Array.prototype.join.call(new Uint8Array(e.data), ','), src: ab.byteLength}); };\n"
        "w.postMessage(ab, [ab]);\n"
        "'started'", &out));

    ASSERT_TRUE(host_wait_msg(h, &out));   /* worker 回显内容 */
    EXPECT_NE(std::string::npos, out.find("\"v\":\"10,20,30,40\"")) << "got: " << out;
    EXPECT_NE(std::string::npos, out.find("\"src\":0")) << "got: " << out;   /* 父侧原 buffer detached */
    host_destroy(h);
}

// transferable：worker → 父 传 ArrayBuffer + transfer 列表，父侧收到内容，
// worker 侧原 buffer 被 detach（byteLength → 0）。
TEST(worker_, transfer_arraybuffer_worker_to_parent) {
    HostCtx *h = host_create();
    ASSERT_NE(nullptr, h);

    std::string out;
    ASSERT_TRUE(host_eval(h,
        "globalThis.w = new Worker('file://" TEST_DIR "/worker_transfer.js');\n"
        "w.onmessage = function(e){ var d = e.data; if (d.buf !== undefined) { postMessage({bufc: Array.prototype.join.call(new Uint8Array(d.buf), ',')}); } else { postMessage({after: d.after, c: d.contents}); } };\n"
        "w.postMessage('go');\n"
        "'started'", &out));

    /* 第一条：父侧收到转移的 ArrayBuffer，内容完整 */
    ASSERT_TRUE(host_wait_msg(h, &out));
    EXPECT_NE(std::string::npos, out.find("\"bufc\":\"7,8,9,10\"")) << "got: " << out;
    /* 第二条：worker 侧 detached 后的 byteLength === 0，且内容一致 */
    ASSERT_TRUE(host_wait_msg(h, &out));
    EXPECT_NE(std::string::npos, out.find("\"after\":0")) << "got: " << out;
    EXPECT_NE(std::string::npos, out.find("\"c\":\"7,8,9,10\"")) << "got: " << out;
    host_destroy(h);
}

// transferable: 父 → worker 转移一个 MessagePort（w.postMessage(..., [port1])），
// worker 侧 event.ports[0] 收到该端口，发 'ready' 经端口回传；父侧 port2.onmessage 收到。
// 之后父侧 port2 → worker 侧 port1 双向 echo。
TEST(worker_, transfer_messageport) {
    HostCtx *h = host_create();
    ASSERT_NE(nullptr, h);

    std::string out;
    ASSERT_TRUE(host_eval(h,
        "var ch = new MessageChannel();\n"
        "globalThis.w = new Worker('file://" TEST_DIR "/worker_port.js');\n"
        "ch.port2.onmessage = function(e){ postMessage({port: e.data}); };\n"
        "w.postMessage('init', [ch.port1]);\n"
        "'started'", &out));

    /* worker 经转移的端口回传 'ready' → 父侧 port2.onmessage → 宿主 */
    ASSERT_TRUE(host_wait_msg(h, &out));
    EXPECT_NE(std::string::npos, out.find("\"port\":\"ready\"")) << "got: " << out;
    host_destroy(h);
}

// transferable: 转移后双向通信——父侧 port2 → worker 侧 port1（worker_port.js
// echo 回 'echo:<msg>'）；同时父侧原 port1 已 detached。
TEST(worker_, transfer_messageport_bidirectional) {
    HostCtx *h = host_create();
    ASSERT_NE(nullptr, h);

    std::string out;
    ASSERT_TRUE(host_eval(h,
        "var ch = new MessageChannel();\n"
        "globalThis.w = new Worker('file://" TEST_DIR "/worker_port.js');\n"
        "ch.port2.onmessage = function(e){ postMessage({port: e.data}); };\n"
        "w.postMessage('init', [ch.port1]);\n"
        "globalThis.p2 = ch.port2;\n"
        "globalThis.p1 = ch.port1;\n"
        "'started'", &out));

    /* 第一条：worker 收到 port 后回传 'ready' → 父侧 port2.onmessage → 宿主 */
    ASSERT_TRUE(host_wait_msg(h, &out));
    EXPECT_NE(std::string::npos, out.find("\"port\":\"ready\"")) << "got: " << out;

    /* 父侧 port2.postMessage('ping') → worker 侧 port echo 'echo:ping' → 父侧 */
    ASSERT_TRUE(host_eval(h, "p2.postMessage('ping'); 'sent'", &out));
    ASSERT_TRUE(host_wait_msg(h, &out));
    EXPECT_NE(std::string::npos, out.find("\"port\":\"echo:ping\"")) << "got: " << out;

    /* 父侧原 port1 已 detached（被转移） */
    ASSERT_TRUE(host_value(h, "p1._detached ? 'detached' : 'active'", &out));
    EXPECT_NE(std::string::npos, out.find("detached")) << "got: " << out;
    host_destroy(h);
}

// transferable: worker → 父 转移一个 MessagePort。worker 侧创建 channel，
// postMessage(data, [port1]) 把 port1 转移给父；父侧 event.ports[0] 收到
// 端口代理，经它发 'ping' → worker 侧对端 port2 echo 'echo:ping' 返回；
// worker 侧原 port1 已 detached。该方向在 v1 曾未支持（父侧无 workerId
// 路由信息——worker 侧硬编码 peerThread 'parent'，父侧代理 port 无法路由
// 回 worker），本用例验证补齐（worker 侧 pal.workerId）后的双向通信。
TEST(worker_, transfer_messageport_worker_to_parent) {
    HostCtx *h = host_create();
    ASSERT_NE(nullptr, h);

    std::string out;
    ASSERT_TRUE(host_eval(h,
        "globalThis.w = new Worker('file://" TEST_DIR "/worker_to_parent.js');\n"
        "w.onmessage = function(e){ if (e.ports && e.ports[0]) globalThis.p = e.ports[0]; postMessage({got: e.data}); };\n"
        "w.postMessage('init');\n"
        "'started'", &out));

    /* 第一条：worker 转移 port 给父 → event.ports[0] 可用，payload {ready:true} */
    ASSERT_TRUE(host_wait_msg(h, &out));
    EXPECT_NE(std::string::npos, out.find("\"got\"")) << "got: " << out;
    EXPECT_NE(std::string::npos, out.find("ready")) << "got: " << out;

    /* 第二条：worker 侧原 port1 已 detached（转移语义） */
    ASSERT_TRUE(host_wait_msg(h, &out));
    EXPECT_NE(std::string::npos, out.find("detached")) << "got: " << out;

    /* 父侧经转移端口 p 发 'ping' → worker 侧 port2 echo 'echo:ping' → p.onmessage */
    ASSERT_TRUE(host_eval(h,
        "globalThis.p.onmessage = function(e){ postMessage({back: e.data}); };\n"
        "p.postMessage('ping'); 'sent'", &out));
    ASSERT_TRUE(host_wait_msg(h, &out));
    EXPECT_NE(std::string::npos, out.find("\"back\":\"echo:ping\"")) << "got: " << out;
    host_destroy(h);
}

// transferable: 多 worker 并发各自创建 channel 转移给父。每个 worker 转移的
// port 在父侧形成独立代理，路由必须回到「正确的」worker（workerId 不混淆）：
// p1 回包必须来自 w1、p2 回包必须来自 w2；且 p1/p2 的 _peerThread 应分别等于
// w1/w2 的 workerId（1/2）。这验证 worker→父 转移的 workerId 路由在并发下的
// 正确性。
TEST(worker_, transfer_messageport_multi_worker) {
    HostCtx *h = host_create();
    ASSERT_NE(nullptr, h);

    std::string out;
    ASSERT_TRUE(host_eval(h,
        "globalThis.w1 = new Worker('file://" TEST_DIR "/worker_to_parent.js');\n"
        "globalThis.w2 = new Worker('file://" TEST_DIR "/worker_to_parent.js');\n"
        "w1.onmessage = function(e){ if (e.ports && e.ports[0]) globalThis.p1 = e.ports[0]; };\n"
        "w2.onmessage = function(e){ if (e.ports && e.ports[0]) globalThis.p2 = e.ports[0]; };\n"
        "w1.postMessage('init'); w2.postMessage('init');\n"
        "'started'", &out));

    /* 轮询直到两个代理 port 都已就位（各自 worker 转移成功） */
    ASSERT_TRUE(host_poll_until_value(h,
        "(globalThis.p1 && globalThis.p2) ? 'both-ports-ready' : 'waiting'",
        "both-ports-ready", &out));

    /* 断言 p1/p2 路由到各自 workerId（w1=1、w2=2），不混淆 */
    ASSERT_TRUE(host_value(h, "p1._peerThread + ':' + p2._peerThread", &out));
    EXPECT_EQ(std::string("1:2"), out) << "got: " << out;

    /* p1/p2 并发发消息，回包带来源标签 */
    ASSERT_TRUE(host_eval(h,
        "globalThis.p1.onmessage = function(e){ postMessage({from:'p1', data:e.data}); };\n"
        "globalThis.p2.onmessage = function(e){ postMessage({from:'p2', data:e.data}); };\n"
        "p1.postMessage('ping-A'); p2.postMessage('ping-B');\n"
        "'sent'", &out));

    /* 等两条回包：p1 回 'echo:ping-A'（来自 w1），p2 回 'echo:ping-B'（来自 w2） */
    bool got1 = false, got2 = false;
    for (int i = 0; i < 2; i++) {
        ASSERT_TRUE(host_wait_msg(h, &out));
        if (out.find("\"from\":\"p1\"") != std::string::npos &&
            out.find("echo:ping-A") != std::string::npos) got1 = true;
        if (out.find("\"from\":\"p2\"") != std::string::npos &&
            out.find("echo:ping-B") != std::string::npos) got2 = true;
    }
    EXPECT_TRUE(got1) << "p1 未收到来自 w1 的回包（路由混淆？）last: " << out;
    EXPECT_TRUE(got2) << "p2 未收到来自 w2 的回包（路由混淆？）last: " << out;
    host_destroy(h);
}

// transferable: worker 转移 port 给父后，terminate worker。父侧代理 port 再
// 发消息不应抛错/崩溃——qwrt_worker_post 检查 worker shutting_down 后静默丢弃，
// 消息不投递（优雅失败）。
TEST(worker_, transfer_messageport_terminate_worker) {
    HostCtx *h = host_create();
    ASSERT_NE(nullptr, h);

    std::string out;
    ASSERT_TRUE(host_eval(h,
        "globalThis.w = new Worker('file://" TEST_DIR "/worker_to_parent.js');\n"
        "w.onmessage = function(e){ if (e.ports && e.ports[0]) globalThis.p = e.ports[0]; };\n"
        "w.postMessage('init');\n"
        "'started'", &out));

    /* 等父侧收到转移的代理 port */
    ASSERT_TRUE(host_poll_until_value(h,
        "globalThis.p ? 'port-ready' : 'waiting'", "port-ready", &out));

    /* terminate worker */
    ASSERT_TRUE(host_value(h, "w.terminate(); 'terminated'", &out));
    EXPECT_NE(std::string::npos, out.find("terminated")) << "got: " << out;

    /* 代理 port 发消息：不抛错（worker 已 shutting_down，静默丢弃） */
    ASSERT_TRUE(host_value(h,
        "try { p.postMessage('after-terminate'); 'no-throw'; } catch (e) { 'throw:' + e.message; }",
        &out));
    EXPECT_NE(std::string::npos, out.find("no-throw")) << "got: " << out;
    host_destroy(h);
}

// transferable: 多跳转移——父→worker 转移 port1（w.postMessage('init',[port1])）；
// worker 收到后把同一 port 再 postMessage 回父（worker_multihop.js，第二跳）；父侧
// event.ports[0] 收到经多跳回来的 port，应与其本地 port2 重建同线程纠缠（_peerThread
// === 'local'），可同线程 echo 通信。v1 只支持单次跨线程转移（父→worker 或 worker→父），
// 多跳（父→worker→父）依赖纠缠关系重建——boot shim 的转移 ref 必须保留原 port 的对端
// 线程信息（而非硬编码 pal.workerId()），接收侧 __qwrt_port_from_ref__ 须重建本地纠缠。
TEST(worker_, transfer_messageport_multihop) {
    HostCtx *h = host_create();
    ASSERT_NE(nullptr, h);

    std::string out;
    ASSERT_TRUE(host_eval(h,
        "var ch = new MessageChannel();\n"
        "globalThis.w = new Worker('file://" TEST_DIR "/worker_multihop.js');\n"
        "globalThis.p2 = ch.port2;\n"
        "p2.onmessage = function(e){ postMessage({p2: e.data}); };\n"
        "w.onmessage = function(e){ if (e.ports && e.ports[0]) { globalThis.p = e.ports[0]; postMessage({fwd: e.data, p1detached: ch.port1._detached}); } };\n"
        "w.postMessage('init', [ch.port1]);\n"
        "'started'", &out));

    /* 第一条：worker 把收到的 port 转回父 → 父侧 event.ports[0] 收到多跳 port；
       父侧原 port1 已 detached（首次转移语义） */
    ASSERT_TRUE(host_wait_msg(h, &out));
    EXPECT_NE(std::string::npos, out.find("\"fwd\"")) << "got: " << out;
    EXPECT_NE(std::string::npos, out.find("forwarding")) << "got: " << out;
    EXPECT_NE(std::string::npos, out.find("\"p1detached\":true")) << "got: " << out;

    /* 多跳 port 与父侧 port2 同线程纠缠：p.postMessage('ping') 同步分发到
       p2.onmessage → 宿主 {p2:'ping'}（先于 eval 响应到达 inbox） */
    ASSERT_TRUE(host_eval(h, "p.postMessage('ping'); 'sent'", &out));
    EXPECT_NE(std::string::npos, out.find("\"p2\":\"ping\"")) << "got: " << out;
    /* 清掉残留的 eval 响应 {ok,v:'"sent"'} */
    ASSERT_TRUE(host_wait_msg(h, &out));

    /* 反向：p2.postMessage('pong') → p.onmessage → 宿主 {back:'pong'} */
    ASSERT_TRUE(host_eval(h,
        "globalThis.p.onmessage = function(e){ postMessage({back: e.data}); };\n"
        "p2.postMessage('pong'); 'sent'", &out));
    EXPECT_NE(std::string::npos, out.find("\"back\":\"pong\"")) << "got: " << out;
    ASSERT_TRUE(host_wait_msg(h, &out));   /* 清掉 eval 响应残留 */

    /* 多跳 port 应与 port2 重建同线程纠缠（_peerThread === 'local'） */
    ASSERT_TRUE(host_value(h,
        "p._peerThread === 'local' ? 'entangled' : String(p._peerThread)", &out));
    EXPECT_NE(std::string::npos, out.find("entangled")) << "got: " << out;
    host_destroy(h);
}

// worker 自关（脚本内 close()）：请求终止自身线程（pal.workerClose →
// qwrt_worker_terminate），父 runtime 不受影响；自关后父侧 w.terminate()
//（对已退出的 worker）静默安全，父 teardown join 不崩溃/不双重释放。
TEST(worker_, self_close) {
    HostCtx *h = host_create();
    ASSERT_NE(nullptr, h);

    std::string out;
    /* worker 先回报一条消息，随后 close() 终止自身 */
    ASSERT_TRUE(host_eval(h,
        "globalThis.w = new Worker('file://" TEST_DIR "/worker_selfclose.js');\n"
        "w.onmessage = function(e){ postMessage({v: e.data}); };\n"
        "'started'", &out));

    ASSERT_TRUE(host_wait_msg(h, &out));
    EXPECT_NE(std::string::npos, out.find("\"v\"")) << "got: " << out;
    EXPECT_NE(std::string::npos, out.find("before-close")) << "got: " << out;

    /* 自关后父侧 terminate 不抛错（worker 已 shutting_down，静默丢弃） */
    ASSERT_TRUE(host_value(h, "w.terminate(); 'ok'", &out));
    EXPECT_NE(std::string::npos, out.find("ok")) << "got: " << out;
    host_destroy(h);
}

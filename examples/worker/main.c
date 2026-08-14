/*
 * Qwrt.js — worker: 真线程 Web Worker 示例
 *
 * 父 runtime 通过 new Worker('file://.../worker.js') 创建独立线程的 worker，
 * 双向 postMessage 通信（结构化克隆）。
 *
 * 构建：
 *   cd build
 *   cmake -DQWRT_BUILD_EXAMPLES=ON ..
 *   cmake --build . --target qwrt_worker
 * 运行：
 *   ./examples/worker/qwrt_worker
 *   （程序内 worker 脚本路径为编译期注入的 file:// 绝对路径）
 */
#include <qwrt/qwrt.h>
#include <stdio.h>
#include <unistd.h>

#ifndef QWRT_WORKER_SCRIPT
#error "QWRT_WORKER_SCRIPT must be defined by CMake (file:// absolute path)"
#endif

static void on_message(qwrt_t *rt, const char *json, size_t len, void *data) {
    (void)rt; (void)data;
    printf("[host] 收到: %.*s\n", (int)len, json);
}

int main(void) {
    qwrt_config_t cfg = {0};
    cfg.message_cb = on_message;
    /* 父脚本：建 worker、发消息、收回显 */
    cfg.initial_script =
        "var w = new Worker('" QWRT_WORKER_SCRIPT "');\n"
        "w.onmessage = function (e) {\n"
        "  console.log('父线程收到 worker 回显: ' + e.data);\n"
        "  postMessage({ from: 'parent', got: e.data });\n"
        "};\n"
        "w.postMessage('ping from parent');\n"
        "console.log('worker 已创建');\n";

    qwrt_t *rt = qwrt_create(&cfg);
    if (!rt) {
        fprintf(stderr, "Failed to create qwrt runtime\n");
        return 1;
    }

    usleep(500 * 1000);   /* 等 worker 往返完成 */
    qwrt_destroy(rt);
    printf("[host] done.\n");
    return 0;
}

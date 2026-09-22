/*
 * qzjs — worker: 真线程 Web Worker 示例
 *
 * 父 runtime 通过 new Worker('file://.../worker.js') 创建独立线程的 worker，
 * 双向 postMessage 通信（结构化克隆）。
 *
 * 构建：
 *   cd build
 *   cmake -DQZ_BUILD_EXAMPLES=ON ..
 *   cmake --build . --target qz_worker
 * 运行：
 *   ./examples/worker/qz_worker
 *   （程序内 worker 脚本路径为编译期注入的 file:// 绝对路径）
 */
#include <qzjs/qzjs.h>
#include <stdio.h>
#include <unistd.h>

#ifndef QZ_WORKER_SCRIPT
#error "QZ_WORKER_SCRIPT must be defined by CMake (file:// absolute path)"
#endif

static void on_message(qz_t *rt, const char *json, size_t len, void *data) {
    (void)rt; (void)data;
    printf("[host] 收到: %.*s\n", (int)len, json);
}

int main(void) {
#ifdef QZ_RT_SERVER_PATH
    setenv("QZ_RT_SERVER", QZ_RT_SERVER_PATH, 0);
#endif
    qz_config_t cfg = {0};
    cfg.message_cb = on_message;
    /* 父脚本：建 worker、发消息、收回显 */
    cfg.initial_script =
        "var w = new Worker('" QZ_WORKER_SCRIPT "');\n"
        "w.onmessage = function (e) {\n"
        "  console.log('父线程收到 worker 回显: ' + e.data);\n"
        "  postMessage({ from: 'parent', got: e.data });\n"
        "};\n"
        "w.postMessage('ping from parent');\n"
        "console.log('worker 已创建');\n";

    qz_t *rt = qz_create(&cfg);
    if (!rt) {
        fprintf(stderr, "Failed to create qzjs runtime\n");
        return 1;
    }

    usleep(500 * 1000);   /* 等 worker 往返完成 */
    qz_destroy(rt);
    printf("[host] done.\n");
    return 0;
}

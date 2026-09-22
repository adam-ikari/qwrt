/*
 * qzjs — hello: 最简示例
 *
 * 流程：创建运行时 → 执行 JS（console.log + postMessage）→ 宿主收消息 →
 * 宿主发消息给 JS → 销毁。
 *
 * 构建：
 *   cd build
 *   cmake -DQZ_BUILD_EXAMPLES=ON ..
 *   cmake --build . --target qz_hello
 * 运行：
 *   ./examples/hello/qz_hello
 */
#include <qzjs/qzjs.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void on_message(qz_t *rt, const char *json, size_t len, void *data) {
    (void)rt; (void)data;
    printf("[host] 收到 JS 消息: %.*s\n", (int)len, json);
}

int main(void) {
#ifdef QZ_RT_SERVER_PATH
    setenv("QZ_RT_SERVER", QZ_RT_SERVER_PATH, 0);
#endif
    qz_config_t cfg = {0};
    cfg.message_cb  = on_message;
    /* JS 侧：console.log 走原生 console；postMessage 发给宿主 */
    cfg.initial_script =
        "console.log('hello from qzjs!');\n"
        "postMessage({ greeting: 'hello from JS', ts: Date.now() });\n"
        "onmessage = function (e) {\n"
        "  console.log('JS 收到宿主消息: ' + JSON.stringify(e.data));\n"
        "  postMessage({ reply: 'pong' });\n"
        "};\n";

    qz_t *rt = qz_create(&cfg);
    if (!rt) {
        fprintf(stderr, "Failed to create qzjs runtime\n");
        return 1;
    }

    /* 等初始脚本跑完（异步线程） */
    usleep(300 * 1000);

    /* 宿主 → JS */
    const char *ping = "{\"cmd\":\"ping\"}";
    printf("[host] 发消息给 JS: %s\n", ping);
    qz_post_message(rt, ping, strlen(ping));

    /* 等 JS 回包 */
    usleep(300 * 1000);

    qz_destroy(rt);
    printf("[host] 已销毁 runtime\n");
    return 0;
}

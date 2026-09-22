/*
 * Amoib.js — hello: 最简示例
 *
 * 流程：创建运行时 → 执行 JS（console.log + postMessage）→ 宿主收消息 →
 * 宿主发消息给 JS → 销毁。
 *
 * 构建：
 *   cd build
 *   cmake -DAM_BUILD_EXAMPLES=ON ..
 *   cmake --build . --target am_hello
 * 运行：
 *   ./examples/hello/am_hello
 */
#include <amoib/amoib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void on_message(am_t *rt, const char *json, size_t len, void *data) {
    (void)rt; (void)data;
    printf("[host] 收到 JS 消息: %.*s\n", (int)len, json);
}

int main(void) {
    am_config_t cfg = {0};
    cfg.message_cb  = on_message;
    /* JS 侧：console.log 走原生 console；postMessage 发给宿主 */
    cfg.initial_script =
        "console.log('hello from amoib!');\n"
        "postMessage({ greeting: 'hello from JS', ts: Date.now() });\n"
        "onmessage = function (e) {\n"
        "  console.log('JS 收到宿主消息: ' + JSON.stringify(e.data));\n"
        "  postMessage({ reply: 'pong' });\n"
        "};\n";

    am_t *rt = am_create(&cfg);
    if (!rt) {
        fprintf(stderr, "Failed to create amoib runtime\n");
        return 1;
    }

    /* 等初始脚本跑完（异步线程） */
    usleep(300 * 1000);

    /* 宿主 → JS */
    const char *ping = "{\"cmd\":\"ping\"}";
    printf("[host] 发消息给 JS: %s\n", ping);
    am_post_message(rt, ping, strlen(ping));

    /* 等 JS 回包 */
    usleep(300 * 1000);

    am_destroy(rt);
    printf("[host] 已销毁 runtime\n");
    return 0;
}

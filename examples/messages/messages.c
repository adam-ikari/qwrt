/*
 * qzjs — messages: host ↔ JS JSON 消息往返
 *
 * 演示 qzjs 的核心架构：宿主与运行时只经 JSON 消息通信。
 *  - 宿主 → JS：qz_post_message(rt, json, len)（线程安全，可任意线程调用）
 *  - JS → 宿主：postMessage(value) 触发 message_cb（在 qzjs 线程上）
 *
 * 本示例跑一轮"请求-响应"状态机：宿主发 echo / add / date 三条命令，
 * JS 的 onmessage 处理器分别回复，message_cb 打印结果。
 *
 * 构建：
 *   cd build
 *   cmake -DQZ_BUILD_EXAMPLES=ON ..
 *   cmake --build . --target qz_messages
 * 运行：
 *   ./examples/messages/qz_messages
 */
#include <qzjs/qzjs.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* 宿主侧回调：JS 的 postMessage 落在这里（qzjs 线程，保持快 + 线程安全） */
static void on_message(qz_t *rt, const char *json, size_t len, void *data) {
    (void)rt; (void)data;
    printf("[host]  收到: %.*s\n", (int)len, json);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);   /* 行缓冲：消息立即可见 */
#ifdef QZ_RT_SERVER_PATH
    setenv("QZ_RT_SERVER", QZ_RT_SERVER_PATH, 0);
#endif
    qz_config_t cfg = {0};
    cfg.message_cb  = on_message;

    /* JS 侧：一个 onmessage 命令分发器，按 cmd 字段分派 */
    cfg.initial_script =
        "globalThis.onmessage = function (e) {\n"
        "  var d = e.data;\n"
        "  if (d.cmd === 'echo') {\n"
        "    postMessage({ ok: true, echo: d.text });\n"
        "  } else if (d.cmd === 'add') {\n"
        "    postMessage({ ok: true, sum: d.a + d.b });\n"
        "  } else if (d.cmd === 'date') {\n"
        "    postMessage({ ok: true, date: new Date().toISOString() });\n"
        "  } else {\n"
        "    postMessage({ ok: false, error: 'unknown cmd: ' + d.cmd });\n"
        "  }\n"
        "};\n"
        "postMessage({ ready: true });\n";

    qz_t *rt = qz_create(&cfg);
    if (!rt) {
        fprintf(stderr, "Failed to create qzjs runtime\n");
        return 1;
    }

    /* 等 ready（异步线程启动） */
    usleep(200 * 1000);

    const char *reqs[] = {
        "{\"cmd\":\"echo\",\"text\":\"hello from host\"}",
        "{\"cmd\":\"add\",\"a\":20,\"b\":22}",
        "{\"cmd\":\"date\"}",
        "{\"cmd\":\"bogus\"}",
    };
    for (size_t i = 0; i < sizeof(reqs) / sizeof(reqs[0]); i++) {
        printf("[host]  发:  %s\n", reqs[i]);
        qz_post_message(rt, reqs[i], strlen(reqs[i]));
        usleep(200 * 1000);   /* 等 JS 处理 + 回复 */
    }

    qz_destroy(rt);
    printf("[host]  已销毁 runtime\n");
    return 0;
}

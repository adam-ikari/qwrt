#include <qwrt/qwrt.h>
#include <stdio.h>
#include <stdlib.h>

static void on_message(qwrt_t *rt, const char *json, size_t len, void *data) {
    (void)rt; (void)data;
    printf("Received: %.*s\n", (int)len, json);
}

int main(void) {
    qwrt_config_t cfg = {0};
    cfg.initial_script = "postMessage({msg: 'hello from qwrt'});";
    cfg.message_cb = on_message;
    qwrt_t *rt = qwrt_create(&cfg);
    if (!rt) {
        fprintf(stderr, "Failed to create qwrt runtime\n");
        return 1;
    }

    // Let the runtime run for a short time to process the initial script
    qwrt_run(rt, 100); // Run for 100 milliseconds

    qwrt_destroy(rt);
    return 0;
}

/*
 * qwrt minimal example.
 *
 * Creates a libuv-backed PAL, spins up a qwrt runtime, evaluates some
 * JavaScript using the WinterCG polyfill (console, fetch, timers, ...),
 * drains the event loop, and tears down.
 *
 * Build: cmake -B build -DQWRT_BUILD_EXAMPLES=ON
 * Run:   ./build/examples/example_minimal
 */
#include <qwrt/qwrt.h>
#include <pal_uv.h>
#include <stdio.h>

int main(void) {
    /* Create a libuv PAL that owns its own event loop. */
    qwrt_pal_t *pal = pal_uv_create(uv_default_loop());
    if (!pal) {
        fprintf(stderr, "failed to create pal_uv\n");
        return 1;
    }

    qwrt_config_t config = { .pal = pal, .debug = 0 };
    qwrt_t *rt = qwrt_create(&config);
    if (!rt) {
        fprintf(stderr, "failed to create qwrt runtime\n");
        pal_uv_destroy(pal);
        return 1;
    }

    /* Synchronous eval — WinterCG globals (console, etc.) are available. */
    char *result = NULL;
    if (qwrt_eval(rt, "1 + 1", &result) == 0 && result) {
        printf("1 + 1 = %s\n", result);
        qwrt_free(result);
    }

    qwrt_eval(rt, "console.log('Hello from QuickJS!');", NULL);

    /* Drive the PAL event loop + JS microtasks until idle. */
    while (pal->run_cycle && pal->run_cycle(pal, 100) > 0) {
        qwrt_tick(rt, 100);
    }

    qwrt_destroy(rt);
    pal_uv_destroy(pal);
    return 0;
}

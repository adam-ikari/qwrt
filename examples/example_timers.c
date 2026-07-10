/*
 * qwrt timers example.
 *
 * Demonstrates setTimeout / setInterval over a real libuv event loop.
 * Schedules a one-shot timeout and a recurring interval that self-clears
 * after N ticks, then drains the loop until everything settles.
 *
 * Build: cmake -B build -DQWRT_BUILD_EXAMPLES=ON
 * Run:   ./build/examples/example_timers
 */
#include <qwrt/qwrt.h>
#include <pal_uv.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    qwrt_pal_t *pal = pal_uv_create(NULL);
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

    /* A recurring interval that prints and self-cancels after 3 ticks,
     * plus a one-shot timeout that fires after it. Timers are part of the
     * WinterCG polyfill — no import needed. */
    const char *js =
        "globalThis.__done = false; "
        "var n = 0; "
        "var iv = setInterval(function() { "
        "  n++; console.log('tick', n); "
        "  if (n >= 3) { clearInterval(iv); console.log('done'); "
        "    setTimeout(function() { console.log('final'); "
        "      globalThis.__done = true; }, 50); } "
        "}, 100);";

    if (qwrt_eval(rt, js, NULL) != 0) {
        fprintf(stderr, "eval failed\n");
        qwrt_destroy(rt);
        pal_uv_destroy(pal);
        return 1;
    }

    /* Drive the loop until the final one-shot timer has fired (the __done
     * flag flips), or we exceed a safety bound. run_cycle returns 0 when
     * idle (e.g. while waiting out the 100ms interval), so we keep pumping
     * instead of treating idle as "finished". */
    int ticks = 0;
    for (;;) {
        if (pal->run_cycle) {
            pal->run_cycle(pal, 100);  /* block up to 100ms for events */
        }
        qwrt_tick(rt);

        char *done = NULL;
        if (qwrt_eval(rt, "globalThis.__done", &done) == 0) {
            int is_done = done && strcmp(done, "true") == 0;
            qwrt_free(done);
            if (is_done) break;
        }
        if (++ticks > 30) {  /* ~3s safety bound */
            fprintf(stderr, "timeout waiting for timers\n");
            break;
        }
    }

    qwrt_destroy(rt);
    pal_uv_destroy(pal);
    return 0;
}

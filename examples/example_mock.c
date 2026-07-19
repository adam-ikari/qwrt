/*
 * qwrt mock example.
 *
 * A dependency-free demo: the mock PAL needs no libuv and no network, so
 * this example builds and runs on any host toolchain (and in CI without
 * network access). It shows console output, setTimeout driven by the mock's
 * synchronous timer model, and a crypto random draw.
 *
 * Build: cmake -B build -DQWRT_BUILD_EXAMPLES=ON
 * Run:   ./build/examples/example_mock
 */
#include <qwrt/qwrt.h>
#include <pal_mock.h>
#include <stdio.h>

int main(void) {
    qwrt_pal_t *pal = pal_mock_create();
    if (!pal) {
        fprintf(stderr, "failed to create pal_mock\n");
        return 1;
    }

    qwrt_config_t config = { .pal = pal, .debug = 0 };
    qwrt_t *rt = qwrt_create(&config);
    if (!rt) {
        fprintf(stderr, "failed to create qwrt runtime\n");
        pal_mock_destroy(pal);
        return 1;
    }

    /* console and crypto are WinterCG globals — no import needed. */
    qwrt_eval(rt,
        "console.log('hello from the mock backend'); "
        "var bytes = crypto.getRandomValues(new Uint8Array(4)); "
        "globalThis.__rand = Array.from(bytes).join('.');",
        NULL);

    /* setTimeout is mocked: fire timers explicitly, then drain JS. */
    qwrt_eval(rt,
        "globalThis.__fired = false; "
        "setTimeout(function() { console.log('timer fired'); "
        "  globalThis.__fired = true; }, 0);",
        NULL);
    pal_mock_fire_all_timers(pal);
    qwrt_tick(rt, 100);

    char *r = NULL;
    if (qwrt_eval(rt, "globalThis.__rand", &r) == 0 && r) {
        printf("random: %s\n", r);
        qwrt_free(r);
    }
    if (qwrt_eval(rt, "globalThis.__fired", &r) == 0 && r) {
        printf("timer fired: %s\n", r);
        qwrt_free(r);
    }

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
    return 0;
}

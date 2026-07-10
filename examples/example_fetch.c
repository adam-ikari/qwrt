/*
 * qwrt fetch example.
 *
 * Demonstrates the async WinterCG fetch() polyfill over a real libuv event
 * loop. The host starts the fetch, then drives the loop (qwrt_tick to drain
 * deferred JS callbacks, uv_run for one I/O iteration) until the promise
 * resolves and a global flips.
 *
 * Build: cmake -B build -DQWRT_BUILD_EXAMPLES=ON
 * Run:   ./build/examples/example_fetch https://example.com
 */
#define _POSIX_C_SOURCE 200809L

#include <qwrt/qwrt.h>
#include <pal_uv.h>
#include <uv.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    const char *url = (argc > 1) ? argv[1] : "https://example.com";
    fprintf(stderr, "fetching %s ...\n", url);

    /* Share the default loop; the PAL does not own it. */
    uv_loop_t *loop = uv_default_loop();
    qwrt_pal_t *pal = pal_uv_create(loop);
    if (!pal) {
        fprintf(stderr, "failed to create pal_uv\n");
        return 1;
    }

    qwrt_config_t config = { .pal = pal, .debug = 0, .extensions = NULL };
    qwrt_t *rt = qwrt_create(&config);
    if (!rt) {
        fprintf(stderr, "failed to create qwrt runtime\n");
        pal_uv_destroy(pal);
        return 1;
    }

    /* Kick off a fetch. The polyfill's fetch() returns a Promise; we stash
     * the resolved body text in a global so we can read it back from C once
     * the loop drains. */
    char js[512];
    snprintf(js, sizeof(js),
        "globalThis.__body = null; "
        "fetch('%s').then(function(r) { return r.text(); })"
        ".then(function(t) { globalThis.__body = t; })"
        ".catch(function(e) { globalThis.__body = 'ERROR: ' + e; })",
        url);

    if (qwrt_eval(rt, js, NULL) != 0) {
        fprintf(stderr, "eval failed\n");
        qwrt_destroy(rt);
        pal_uv_destroy(pal);
        return 1;
    }

    /* Drive the loop: drain deferred JS callbacks first, then run one libuv
     * iteration. Order (tick then run) matches the contract in qwrt.h: PAL
     * callbacks fire on the loop thread and enqueue JS work via qwrt_tick. */
    for (int iter = 0; iter < 5000; ++iter) {
        qwrt_tick(rt);
        uv_run(loop, UV_RUN_ONCE);

        char *done = NULL;
        if (qwrt_eval(rt, "globalThis.__body", &done) == 0) {
            if (done && strcmp(done, "null") != 0) {
                printf("%s\n", done);
                qwrt_free(done);
                break;
            }
            qwrt_free(done);
        }
    }

    qwrt_destroy(rt);
    pal_uv_destroy(pal);
    return 0;
}

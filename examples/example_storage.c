/*
 * qwrt storage example.
 *
 * Demonstrates the platform-extension key-value store (qwrt.storage) backed
 * by the libuv PAL. Writes a value, reads it back across a fresh runtime
 * (storage persists in PAL-level state), then deletes it.
 *
 * Build: cmake -B build -DQWRT_BUILD_EXAMPLES=ON
 * Run:   ./build/examples/example_storage
 */
#include <qwrt/qwrt.h>
#include <pal_uv.h>
#include <stdio.h>
#include <string.h>

/* Run a snippet of JS that resolves a Promise, draining the loop until the
 * global __out holds the resolved value (or an error string). */
static void run_async(qwrt_t *rt, qwrt_pal_t *pal, const char *js) {
    if (qwrt_eval(rt, js, NULL) != 0) return;
    /* Pump until __out is set, or we hit a safety bound. run_cycle returns 0
     * when idle — normal between PAL events — so we keep going rather than
     * treating idle as done. */
    for (int i = 0; i < 50; ++i) {
        if (pal->run_cycle) {
            pal->run_cycle(pal, 50);
        }
        qwrt_tick(rt);
        char *out = NULL;
        if (qwrt_eval(rt, "globalThis.__out", &out) == 0) {
            int ready = out && strcmp(out, "null") != 0;
            qwrt_free(out);
            if (ready) return;
        }
    }
}

static void read_out(qwrt_t *rt, const char *label) {
    char *r = NULL;
    if (qwrt_eval(rt, "globalThis.__out", &r) == 0 && r) {
        printf("%s: %s\n", label, r);
        qwrt_free(r);
    }
}

int main(void) {
    qwrt_pal_t *pal = pal_uv_create(uv_default_loop());
    if (!pal) {
        fprintf(stderr, "failed to create pal_uv\n");
        return 1;
    }

    /* First runtime: set a value. */
    qwrt_config_t config = { .pal = pal, .debug = 0 };
    qwrt_t *rt = qwrt_create(&config);
    if (!rt) {
        fprintf(stderr, "failed to create qwrt runtime\n");
        pal_uv_destroy(pal);
        return 1;
    }
    run_async(rt, pal,
        "globalThis.__out = null; "
        "qwrt.storage.set('greeting', 'hello from qwrt').then(function() { "
        "  globalThis.__out = 'set ok'; });");
    read_out(rt, "set");
    qwrt_destroy(rt);

    /* Second runtime, same PAL: the value persists. */
    rt = qwrt_create(&config);
    if (!rt) {
        fprintf(stderr, "failed to recreate qwrt runtime\n");
        pal_uv_destroy(pal);
        return 1;
    }
    run_async(rt, pal,
        "globalThis.__out = null; "
        "qwrt.storage.get('greeting').then(function(v) { "
        "  globalThis.__out = v; });");
    read_out(rt, "get");
    run_async(rt, pal,
        "globalThis.__out = null; "
        "qwrt.storage.delete('greeting').then(function() { "
        "  globalThis.__out = 'deleted'; });");
    read_out(rt, "delete");
    qwrt_destroy(rt);

    pal_uv_destroy(pal);
    return 0;
}

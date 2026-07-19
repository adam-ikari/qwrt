/*
 * qwrt Native vs JS Benchmark — runs bench_native_vs_js.js inside qwrt
 *
 * Uses pal_uv (libuv PAL) for realistic production-level timing.
 */
#define _POSIX_C_SOURCE 200809L
#include "qwrt/qwrt.h"
#include "pal_uv.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *load_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc((size_t)len + 1);
    size_t n = fread(buf, 1, (size_t)len, f);
    buf[n] = 0;
    fclose(f);
    *out_len = n;
    return buf;
}

int main(void) {
    /* Polyfill is auto-injected by qwrt_create (qwrt_default_polyfill). */
    size_t blen;
    uint8_t *bench = load_file("../test/bench_native_vs_js.js", &blen);
    if (!bench) { printf("SKIP: bench_native_vs_js.js not found\n"); return 0; }

    qwrt_pal_t *pal = pal_uv_create(uv_default_loop());
    if (!pal) { free(bench); return 1; }

    qwrt_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.pal = pal;
    cfg.debug = 0;
    qwrt_t *rt = qwrt_create(&cfg);
    if (!rt) { free(bench); pal_uv_destroy(pal); return 1; }

    int rc = qwrt_eval(rt, (const char *)bench, NULL);
    if (rc < 0) {
        printf("Error evaluating benchmark\n");
    }

    /* Process any pending async operations */
    uv_loop_t *loop = pal_uv_get_loop(pal);
    if (loop) {
        uv_run(loop, UV_RUN_ONCE);
    }

    qwrt_destroy(rt);
    pal_uv_destroy(pal);
    free(bench);
    return 0;
}

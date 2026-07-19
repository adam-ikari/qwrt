/*
 * Test fetch() via polyfill with libuv PAL — the correct way to call HTTP from JS
 */
#define _POSIX_C_SOURCE 200809L

#include <qwrt/qwrt.h>
#include <uv.h>
#include <pal_uv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    uv_loop_t *loop = uv_default_loop();
    qwrt_pal_t *pal = pal_uv_create(loop);
    if (!pal) { fprintf(stderr, "PAL create failed\n"); return 1; }

    qwrt_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.pal = pal;
    qwrt_t *rt = qwrt_create(&cfg);
    if (!rt) { fprintf(stderr, "qwrt create failed\n"); pal_uv_destroy(pal); return 1; }

    /* Test non-streaming fetch: httpbin returns JSON response */
    const char *code =
        "var _done = false; var _result = null;\n"
        "console.log('fetch() test...');\n"
        "fetch('https://httpbin.org/get')\n"
        ".then(function(r) {\n"
        "  console.log('status:', r.status);\n"
        "  return r.text();\n"
        "}).then(function(body) {\n"
        "  console.log('body length:', body.length);\n"
        "  _result = 'ok';\n"
        "  _done = true;\n"
        "}).catch(function(e) {\n"
        "  console.log('ERROR:', e.message || String(e));\n"
        "  _result = 'error';\n"
        "  _done = true;\n"
        "});\n";

    int rc = qwrt_eval(rt, code, NULL);
    if (rc != 0) { fprintf(stderr, "eval failed: %d\n", rc); qwrt_destroy(rt); pal_uv_destroy(pal); return 1; }

    printf("Driving event loop...\n");

    int max_iter = 5000;
    int iter = 0;
    while (iter < max_iter) {
        qwrt_tick(rt, 100);
        uv_run(loop, UV_RUN_ONCE);

        char *result = NULL;
        rc = qwrt_eval(rt, "_done", &result);
        if (rc == 0 && result && strcmp(result, "true") == 0) {
            qwrt_free(result);
            break;
        }
        if (result) qwrt_free(result);
        iter++;
        if (iter % 500 == 0) printf("  tick %d...\n", iter);
    }

    char *final_result = NULL;
    rc = qwrt_eval(rt, "_result", &final_result);
    printf("Result after %d ticks: %s\n", iter,
           (rc == 0 && final_result) ? final_result : "(null)");
    int passed = (final_result && strstr(final_result, "ok") != NULL);
    if (final_result) qwrt_free(final_result);

    qwrt_destroy(rt);
    pal_uv_destroy(pal);
    return passed ? 0 : 1;
}
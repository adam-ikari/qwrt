/*
 * Test non-streaming HTTP from JS polyfill via libuv PAL
 * The polyfill's fetch() uses httpRequestStream when available,
 * so we test with a direct approach: call the polyfill's internal
 * non-streaming path by using a URL that triggers it.
 *
 * Actually, the polyfill ALWAYS uses httpRequestStream when pal
 * has it. So we need to test the streaming path.
 *
 * Let's verify the polyfill is loaded and check what's available.
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

    /* Check what's available */
    char *result = NULL;
    int rc;

    rc = qwrt_eval(rt, "typeof fetch", &result);
    printf("typeof fetch: %s (rc=%d)\n", result ? result : "null", rc);
    if (result) { qwrt_free(result); result = NULL; }

    rc = qwrt_eval(rt, "typeof storage", &result);
    printf("typeof storage: %s (rc=%d)\n", result ? result : "null", rc);
    if (result) { qwrt_free(result); result = NULL; }

    rc = qwrt_eval(rt, "typeof console", &result);
    printf("typeof console: %s (rc=%d)\n", result ? result : "null", rc);
    if (result) { qwrt_free(result); result = NULL; }

    rc = qwrt_eval(rt, "typeof __pal__", &result);
    printf("typeof __pal__: %s (rc=%d)\n", result ? result : "null", rc);
    if (result) { qwrt_free(result); result = NULL; }

    /* Check if __pal__ has httpRequestStream */
    rc = qwrt_eval(rt, "typeof __pal__.httpRequestStream", &result);
    printf("typeof __pal__.httpRequestStream: %s (rc=%d)\n", result ? result : "null", rc);
    if (result) { qwrt_free(result); result = NULL; }

    /* Check if __pal__ has httpRequest */
    rc = qwrt_eval(rt, "typeof __pal__.httpRequest", &result);
    printf("typeof __pal__.httpRequest: %s (rc=%d)\n", result ? result : "null", rc);
    if (result) { qwrt_free(result); result = NULL; }

    /* Now test: call __pal__.httpRequest directly (non-streaming) */
    const char *code =
        "var _done_ns = false; var _result_ns = null;\n"
        "console.log('Testing __pal__.httpRequest (non-streaming)...');\n"
        "__pal__.httpRequest('http://httpbin.org/get', 'GET', '{}', null)\n"
        ".then(function(data) {\n"
        "  console.log('Non-streaming response length:', data.length);\n"
        "  _result_ns = 'ok';\n"
        "  _done_ns = true;\n"
        "}).catch(function(e) {\n"
        "  console.log('Non-streaming error:', e);\n"
        "  _result_ns = 'error';\n"
        "  _done_ns = true;\n"
        "});\n";

    rc = qwrt_eval(rt, code, NULL);
    if (rc != 0) { fprintf(stderr, "eval failed: %d\n", rc); qwrt_destroy(rt); pal_uv_destroy(pal); return 1; }

    printf("Driving event loop for non-streaming test...\n");
    int max_iter = 5000;
    int iter = 0;
    while (iter < max_iter) {
        qwrt_tick(rt, 100);
        uv_run(loop, UV_RUN_ONCE);

        char *r = NULL;
        rc = qwrt_eval(rt, "_done_ns", &r);
        if (rc == 0 && r && strcmp(r, "true") == 0) {
            qwrt_free(r);
            break;
        }
        if (r) qwrt_free(r);
        iter++;
        if (iter % 500 == 0) printf("  tick %d...\n", iter);
    }

    char *final = NULL;
    rc = qwrt_eval(rt, "_result_ns", &final);
    printf("Non-streaming result after %d ticks: %s\n", iter,
           (rc == 0 && final) ? final : "(null)");
    int passed = (final && strcmp(final, "\"ok\"") == 0);
    if (final) qwrt_free(final);

    qwrt_destroy(rt);
    pal_uv_destroy(pal);
    printf("\n%s\n", passed ? "PASSED" : "FAILED");
    return passed ? 0 : 1;
}
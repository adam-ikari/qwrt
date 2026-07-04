/*
 * Minimal test: verify pal.httpRequest is callable from JS with libuv PAL
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

    /* Step 1: verify pal object exists */
    char *result = NULL;
    int rc = qwrt_eval(rt, "typeof pal", &result);
    printf("typeof pal: %s (rc=%d)\n", result ? result : "null", rc);
    if (result) { qwrt_free(result); result = NULL; }

    /* Step 2: verify pal.httpRequest exists */
    rc = qwrt_eval(rt, "typeof pal.httpRequest", &result);
    printf("typeof pal.httpRequest: %s (rc=%d)\n", result ? result : "null", rc);
    if (result) { qwrt_free(result); result = NULL; }

    /* Step 3: verify pal.httpRequestStream exists */
    rc = qwrt_eval(rt, "typeof pal.httpRequestStream", &result);
    printf("typeof pal.httpRequestStream: %s (rc=%d)\n", result ? result : "null", rc);
    if (result) { qwrt_free(result); result = NULL; }

    /* Step 4: try calling pal.httpRequest with a try/catch */
    const char *code =
        "var _step4 = null;\n"
        "try {\n"
        "  var p = pal.httpRequest('http://httpbin.org/get', 'GET', '{}', null);\n"
        "  _step4 = 'promise:' + typeof p + ':' + (p instanceof Promise);\n"
        "} catch(e) {\n"
        "  _step4 = 'error:' + e.message;\n"
        "}\n";

    rc = qwrt_eval(rt, code, NULL);
    printf("Step 4 eval rc: %d\n", rc);

    rc = qwrt_eval(rt, "_step4", &result);
    printf("Step 4 result: %s\n", result ? result : "null");
    if (result) { qwrt_free(result); result = NULL; }

    qwrt_destroy(rt);
    pal_uv_destroy(pal);
    return 0;
}
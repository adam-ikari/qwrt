/*
 * Test JS bridge streaming — calls pal.httpRequestStream() from JS
 * and verifies JS callbacks fire correctly. Uses mock PAL so no network needed.
 */
#define _POSIX_C_SOURCE 200809L

#include <qwrt/qwrt.h>
#include "pal_mock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    qwrt_pal_t *pal = pal_mock_create();
    if (!pal) { fprintf(stderr, "PAL create failed\n"); return 1; }

    /* Configure mock PAL with a streaming response */
    pal_mock_set_http_response(pal,
        "{\"status\":200,\"headers\":{\"Content-Type\":\"application/json\"},"
        "\"body\":\"{\\\"data\\\":\\\"test_value\\\"}\"}");

    qwrt_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.pal = pal;
    qwrt_t *rt = qwrt_create(&cfg);
    if (!rt) { fprintf(stderr, "qwrt create failed\n"); pal_mock_destroy(pal); return 1; }

    /* JS code that calls __pal__.httpRequestStream directly (bypassing fetch) */
    const char *code =
        "var _done = false; var _result = null;\n"
        "var _headerStatus = -1;\n"
        "var _dataChunks = 0;\n"
        "console.log('Starting stream test...');\n"
        "__pal__.httpRequestStream(\n"
        "  'http://api.example.com/data',\n"
        "  'GET',\n"
        "  '{}',\n"
        "  null,\n"
        "  function(status, headersJson) {\n"
        "    console.log('onHeaders:', status);\n"
        "    _headerStatus = status;\n"
        "  },\n"
        "  function(chunk) {\n"
        "    _dataChunks++;\n"
        "  },\n"
        "  function(errorStatus) {\n"
        "    console.log('onEnd:', errorStatus);\n"
        "    _result = errorStatus === 0 ? 'ok' : 'error:' + errorStatus;\n"
        "    _done = true;\n"
        "  }\n"
        ");\n"
        "console.log('Stream request sent');\n";

    int rc = qwrt_eval(rt, code, NULL);
    if (rc != 0) {
        fprintf(stderr, "eval failed: %d\n", rc);
        qwrt_destroy(rt); pal_mock_destroy(pal); return 1;
    }

    printf("JS eval'd, running tick...\n");

    /* Run tick — mock PAL completes synchronously, deferred callbacks fire */
    qwrt_tick(rt);

    char *done_val = NULL;
    rc = qwrt_eval(rt, "_done", &done_val);
    if (rc == 0 && done_val && strcmp(done_val, "true") == 0) {
        printf("_done is true\n");
    }
    if (done_val) qwrt_free(done_val);

    char *final_result = NULL;
    rc = qwrt_eval(rt, "_result", &final_result);
    printf("Result: %s\n", (rc == 0 && final_result) ? final_result : "(null)");

    char *hs = NULL;
    qwrt_eval(rt, "_headerStatus", &hs);
    printf("Header status: %s\n", hs ? hs : "null");
    if (hs) qwrt_free(hs);

    char *dc = NULL;
    qwrt_eval(rt, "_dataChunks", &dc);
    printf("Data chunks: %s\n", dc ? dc : "null");
    if (dc) qwrt_free(dc);

    int passed = (final_result && strcmp(final_result, "\"ok\"") == 0);
    if (final_result) qwrt_free(final_result);

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
    printf("\n%s\n", passed ? "PASSED" : "FAILED");
    return passed ? 0 : 1;
}

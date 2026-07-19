/*
 * Test JS bridge non-streaming HTTP — calls __pal__.httpRequest() from JS
 * and verifies the promise resolves. Uses mock PAL so no network needed.
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

    /* Configure mock PAL with a response for both tests */
    pal_mock_set_http_response(pal,
        "{\"status\":0,\"headers\":{\"Content-Type\":\"application/json\"},"
        "\"body\":\"{\\\"data\\\":\\\"test_value\\\"}\"}");

    qwrt_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.pal = pal;
    qwrt_t *rt = qwrt_create(&cfg);
    if (!rt) { fprintf(stderr, "qwrt create failed\n"); pal_mock_destroy(pal); return 1; }

    /* Test 1: non-streaming httpRequest via JS bridge */
    const char *code =
        "var _done = false; var _result = null;\n"
        "console.log('Test 1: non-streaming httpRequest...');\n"
        "__pal__.httpRequest('http://api.example.com/data', 'GET', '{}', null)\n"
        ".then(function(data) {\n"
        "  console.log('Got response, length:', data.length);\n"
        "  _result = 'ok';\n"
        "  _done = true;\n"
        "}).catch(function(e) {\n"
        "  console.log('Error:', e);\n"
        "  _result = 'error';\n"
        "  _done = true;\n"
        "});\n";

    int rc = qwrt_eval(rt, code, NULL);
    if (rc != 0) { fprintf(stderr, "eval failed: %d\n", rc); qwrt_destroy(rt); pal_mock_destroy(pal); return 1; }

    /* Run tick — mock PAL completes synchronously, deferred callbacks fire */
    qwrt_tick(rt, 100);

    char *done_val = NULL;
    rc = qwrt_eval(rt, "_done", &done_val);
    printf("Test 1 _done: %s\n", (rc == 0 && done_val) ? done_val : "(null)");
    if (done_val) qwrt_free(done_val);

    char *final_result = NULL;
    rc = qwrt_eval(rt, "_result", &final_result);
    printf("Non-streaming result: %s\n", (rc == 0 && final_result) ? final_result : "(null)");

    int passed = (final_result && strcmp(final_result, "\"ok\"") == 0);
    if (final_result) qwrt_free(final_result);

    /* Test 2: streaming httpRequestStream via JS bridge */
    if (passed) {
        const char *code2 =
            "var _done2 = false; var _result2 = null;\n"
            "var _hdrStatus = -1; var _chunks = 0;\n"
            "console.log('Test 2: streaming httpRequestStream...');\n"
            "__pal__.httpRequestStream(\n"
            "  'http://api.example.com/data',\n"
            "  'GET',\n"
            "  '{}',\n"
            "  null,\n"
            "  function(status, hdrs) {\n"
            "    console.log('onHeaders:', status);\n"
            "    _hdrStatus = status;\n"
            "  },\n"
            "  function(chunk) {\n"
            "    _chunks++;\n"
            "  },\n"
            "  function(errStatus) {\n"
            "    console.log('onEnd:', errStatus, 'chunks:', _chunks);\n"
            "    _result2 = errStatus === 0 ? 'ok' : 'error:' + errStatus;\n"
            "    _done2 = true;\n"
            "  }\n"
            ");\n"
            "console.log('Stream request sent');\n";

        rc = qwrt_eval(rt, code2, NULL);
        if (rc != 0) {
            fprintf(stderr, "stream eval failed: %d\n", rc);
        } else {
            qwrt_tick(rt, 100);

            char *final2 = NULL;
            rc = qwrt_eval(rt, "_result2", &final2);
            printf("Streaming result: %s\n", (rc == 0 && final2) ? final2 : "(null)");
            if (final2 && strcmp(final2, "\"ok\"") == 0) passed = 2;
            if (final2) qwrt_free(final2);
        }
    }

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
    printf("\n%s\n", passed == 2 ? "ALL PASSED" : passed == 1 ? "PARTIAL (non-streaming only)" : "FAILED");
    return passed == 2 ? 0 : 1;
}

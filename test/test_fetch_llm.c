/*
 * Minimal fetch-to-LLM test — isolates the HTTP layer from ace-core
 */
#define _POSIX_C_SOURCE 200809L

#include <qwrt/qwrt.h>
#include <uv.h>
#include <pal_uv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, (size_t)len, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

static int eval_file(qwrt_t *rt, const char *path)
{
    char *js = read_file(path);
    if (!js) {
        fprintf(stderr, "Cannot read %s\n", path);
        return -1;
    }
    int rc = qwrt_eval(rt, js, NULL);
    free(js);
    return rc;
}

static char *extract_json_string(const char *json, const char *key)
{
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return NULL;
    p += strlen(pattern);
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ':') p++;
    if (*p != '"') return NULL;
    p++;
    const char *end = p;
    while (*end && *end != '"') {
        if (*end == '\\') end++;
        end++;
    }
    size_t len = (size_t)(end - p);
    char *result = malloc(len + 1);
    if (!result) return NULL;
    memcpy(result, p, len);
    result[len] = '\0';
    return result;
}

int main(int argc, char **argv)
{
    /* Load config */
    char *api_key = getenv("LLM_API_KEY");
    char *api_base = getenv("LLM_API_BASE");
    char *model = getenv("LLM_MODEL");

    const char *home = getenv("HOME");
    if (home) {
        char path[512];
        snprintf(path, sizeof(path), "%s/.alpha/config.json", home);
        char *content = read_file(path);
        if (content) {
            if (!api_base) api_base = extract_json_string(content, "llm_endpoint");
            if (!api_key) api_key = extract_json_string(content, "llm_api_key");
            if (!model) model = extract_json_string(content, "llm_model");
            free(content);
        }
    }
    if (!api_base) api_base = "https://api.openai.com/v1";
    if (!model) model = "gpt-4";

    if (!api_key) {
        fprintf(stderr, "No API key\n");
        return 1;
    }

    printf("Endpoint: %s\nModel: %s\n\n", api_base, model);

    /* Create runtime with real libuv PAL */
    uv_loop_t *loop = uv_default_loop();
    qwrt_pal_t *pal = pal_uv_create(loop);
    if (!pal) { fprintf(stderr, "PAL create failed\n"); return 1; }

    qwrt_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.pal = pal;
    qwrt_t *rt = qwrt_create(&cfg);
    if (!rt) { fprintf(stderr, "qwrt create failed\n"); pal_uv_destroy(pal); return 1; }

    /* Run minimal fetch test */
    char code[4096];
    snprintf(code, sizeof(code),
        "var _done = false; var _result = null;\n"
        "var apiKey = '%s';\n"
        "var apiBase = '%s';\n"
        "console.log('Fetching...');\n"
        "fetch(apiBase + '/chat/completions', {\n"
        "  method: 'POST',\n"
        "  headers: {\n"
        "    'Content-Type': 'application/json',\n"
        "    'Authorization': 'Bearer ' + apiKey\n"
        "  },\n"
        "  body: JSON.stringify({\n"
        "    model: '%s',\n"
        "    messages: [{role: 'user', content: 'Say hi'}],\n"
        "    max_tokens: 10\n"
        "  })\n"
        "}).then(function(r) {\n"
        "  console.log('status:', r.status);\n"
        "  return r.text();\n"
        "}).then(function(body) {\n"
        "  console.log('body length:', body.length);\n"
        "  console.log('body:', body.substring(0, 200));\n"
        "  _result = 'ok';\n"
        "  _done = true;\n"
        "}).catch(function(e) {\n"
        "  console.log('ERROR:', e.message || String(e));\n"
        "  _result = 'error:' + (e.message || String(e));\n"
        "  _done = true;\n"
        "});\n",
        api_key, api_base, model);

    int rc = qwrt_eval(rt, code, NULL);
    if (rc != 0) {
        fprintf(stderr, "eval failed: %d\n", rc);
        qwrt_destroy(rt); pal_uv_destroy(pal);
        return 1;
    }

    /* Drive event loop */
    int max_iter = 5000;
    int iter = 0;
    while (iter < max_iter) {
        qwrt_tick(rt);
        uv_run(loop, UV_RUN_ONCE);

        char *result = NULL;
        rc = qwrt_eval(rt, "_done", &result);
        if (rc == 0 && result && strcmp(result, "true") == 0) {
            qwrt_free(result);
            break;
        }
        if (result) qwrt_free(result);
        iter++;
    }

    /* Get final result */
    char *final_result = NULL;
    rc = qwrt_eval(rt, "_result", &final_result);
    printf("\nResult after %d ticks: %s\n", iter,
           (rc == 0 && final_result) ? final_result : "(null)");
    int passed = (final_result && strstr(final_result, "ok") != NULL);
    if (final_result) qwrt_free(final_result);

    qwrt_destroy(rt);
    pal_uv_destroy(pal);
    return passed ? 0 : 1;
}
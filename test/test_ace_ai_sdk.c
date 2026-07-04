/*
 * ACE AI SDK End-to-End Test (Minimal)
 *
 * Tests ace-ai-sdk bundled in ace-core-qwrt.js directly through qwrt runtime.
 * Uses createOpenAIProvider + generateText with a real LLM API.
 * Loads adapter.js + ace-core-qwrt.js, then runs a minimal SDK test.
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

static int eval_file_search(qwrt_t *rt, const char **paths, int n)
{
    for (int i = 0; i < n; i++) {
        if (eval_file(rt, paths[i]) == 0) return 0;
    }
    return -1;
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

struct llm_config {
    char *api_base;
    char *api_key;
    char *model;
};

static struct llm_config load_alpha_config(void)
{
    struct llm_config cfg = {NULL, NULL, NULL};
    const char *env_base = getenv("LLM_API_BASE");
    const char *env_key = getenv("LLM_API_KEY");
    const char *env_model = getenv("LLM_MODEL");
    if (env_base) cfg.api_base = strdup(env_base);
    if (env_key) cfg.api_key = strdup(env_key);
    if (env_model) cfg.model = strdup(env_model);
    const char *home = getenv("HOME");
    if (home) {
        char path[512];
        snprintf(path, sizeof(path), "%s/.alpha/config.json", home);
        char *content = read_file(path);
        if (content) {
            if (!cfg.api_base) cfg.api_base = extract_json_string(content, "llm_endpoint");
            if (!cfg.api_key) cfg.api_key = extract_json_string(content, "llm_api_key");
            if (!cfg.model) cfg.model = extract_json_string(content, "llm_model");
            free(content);
        }
    }
    if (!cfg.api_base) cfg.api_base = strdup("https://api.openai.com/v1");
    if (!cfg.model) cfg.model = strdup("gpt-4");
    return cfg;
}

static const char *adapter_paths[] = {
    "examples/ace-qwrt/adapter.js",
    "../examples/ace-qwrt/adapter.js",
    "../../examples/ace-qwrt/adapter.js",
    "../../../examples/ace-qwrt/adapter.js",
};

static const char *ace_core_paths[] = {
    "packages/ace-core/dist/ace-core-qwrt.js",
    "../packages/ace-core/dist/ace-core-qwrt.js",
    "../../packages/ace-core/dist/ace-core-qwrt.js",
    "../../../packages/ace-core/dist/ace-core-qwrt.js",
};

int main(int argc, char **argv)
{
    struct llm_config cfg = load_alpha_config();

    if (!cfg.api_key || cfg.api_key[0] == '\0') {
        fprintf(stderr, "Error: No API key found\n");
        free(cfg.api_base); free(cfg.api_key); free(cfg.model);
        return 1;
    }

    printf("ACE AI SDK End-to-End Test\n");
    printf("===========================\n");
    printf("Endpoint: %s\n", cfg.api_base);
    printf("Model:    %s\n\n", cfg.model);

    /* Create libuv PAL */
    uv_loop_t *loop = uv_default_loop();
    qwrt_pal_t *pal = pal_uv_create(loop);
    if (!pal) {
        fprintf(stderr, "Failed to create libuv PAL\n");
        free(cfg.api_base); free(cfg.api_key); free(cfg.model);
        return 1;
    }

    /* Create qwrt runtime */
    qwrt_config_t qwrt_cfg;
    memset(&qwrt_cfg, 0, sizeof(qwrt_cfg));
    qwrt_cfg.pal = pal;
    qwrt_t *rt = qwrt_create(&qwrt_cfg);
    if (!rt) {
        fprintf(stderr, "Failed to create qwrt runtime\n");
        pal_uv_destroy(pal);
        free(cfg.api_base); free(cfg.api_key); free(cfg.model);
        return 1;
    }

    /* Load adapter + ace-core */
    printf("Loading adapter.js...\n");
    if (eval_file_search(rt, adapter_paths, 4) != 0) {
        fprintf(stderr, "Failed to find adapter.js\n");
        qwrt_destroy(rt); pal_uv_destroy(pal);
        free(cfg.api_base); free(cfg.api_key); free(cfg.model);
        return 1;
    }
    printf("Loading ace-core-qwrt.js...\n");
    if (eval_file_search(rt, ace_core_paths, 4) != 0) {
        fprintf(stderr, "Failed to find ace-core-qwrt.js\n");
        qwrt_destroy(rt); pal_uv_destroy(pal);
        free(cfg.api_base); free(cfg.api_key); free(cfg.model);
        return 1;
    }
    printf("Bundle loaded successfully.\n\n");

    /* Verify aceCore exports are available */
    printf("Checking aceCore exports...\n");
    char *check = NULL;
    int rc = qwrt_eval(rt,
        "var _check = {"
        "  hasCreateAceSession: typeof aceCore.createAceSession === 'function',"
        "  hasCreateOpenAIProvider: typeof aceCore.createOpenAIProvider === 'function',"
        "  hasGenerateText: typeof aceCore.generateText === 'function',"
        "  hasStreamText: typeof aceCore.streamText === 'function',"
        "  hasWrapLanguageModel: typeof aceCore.wrapLanguageModel === 'function'"
        "}; _check;", &check);
    if (rc == 0 && check) {
        printf("  %s\n", check);
        qwrt_free(check);
    }

    /*
     * Test ace-ai-sdk: createOpenAIProvider → generateText
     * Uses a simple non-streaming request.
     */
    char code[4096];
    snprintf(code, sizeof(code),
        "var _sdk_done = false; var _sdk_result = null;\n"
        "(async function() {\n"
        "  try {\n"
        "    var provider = aceCore.createOpenAIProvider({\n"
        "      apiKey: '%s',\n"
        "      baseURL: '%s'\n"
        "    });\n"
        "    var model = provider.languageModel('%s');\n"
        "    console.log('Calling generateText...');\n"
        "    var result = await aceCore.generateText({\n"
        "      model: model,\n"
        "      messages: [{ role: 'user', content: 'Say hello in exactly one word' }]\n"
        "    });\n"
        "    console.log('Result:', JSON.stringify({\n"
        "      text: result.text,\n"
        "      finishReason: result.finishReason,\n"
        "      usage: result.usage\n"
        "    }));\n"
        "    _sdk_result = result.text ? 'ok' : 'fail:empty';\n"
        "    _sdk_done = true;\n"
        "  } catch(e) {\n"
        "    console.log('Exception:', e.message || String(e));\n"
        "    _sdk_result = 'error:' + (e.message || String(e));\n"
        "    _sdk_done = true;\n"
        "  }\n"
        "})();\n",
        cfg.api_key, cfg.api_base, cfg.model);

    rc = qwrt_eval(rt, code, NULL);
    if (rc != 0) {
        fprintf(stderr, "Failed to eval SDK test code\n");
        qwrt_destroy(rt); pal_uv_destroy(pal);
        free(cfg.api_base); free(cfg.api_key); free(cfg.model);
        return 1;
    }

    /* Drive event loop */
    int max_iter = 5000;
    int iter = 0;
    while (iter < max_iter) {
        qwrt_tick(rt);
        uv_run(loop, UV_RUN_ONCE);

        char *result = NULL;
        rc = qwrt_eval(rt, "_sdk_done", &result);
        if (rc == 0 && result && strcmp(result, "true") == 0) {
            qwrt_free(result);
            break;
        }
        if (result) qwrt_free(result);
        iter++;
    }

    char *final_result = NULL;
    rc = qwrt_eval(rt, "_sdk_result", &final_result);
    if (rc != 0 || !final_result) {
        fprintf(stderr, "\nFailed to get result (timeout after %d iterations)\n", iter);
        qwrt_destroy(rt); pal_uv_destroy(pal);
        free(cfg.api_base); free(cfg.api_key); free(cfg.model);
        return 1;
    }

    printf("\nResult after %d ticks: %s\n", iter, final_result);
    int passed = strstr(final_result, "ok") != NULL;
    if (passed) {
        printf("TEST 1 PASSED — ace-ai-sdk generateText works via QuickJS\n");
    } else {
        printf("TEST 1 FAILED\n");
    }
    qwrt_free(final_result);

    /* Drain the event loop to ensure full cleanup of first request */
    for (int i = 0; i < 100; i++) {
        qwrt_tick(rt);
        uv_run(loop, UV_RUN_NOWAIT);
    }

    /* ================================================================
     * Test 2: streamText — real LLM API streaming via ReadableStream
     *
     * Uses the real createOpenAIProvider + streamText with the actual
     * LLM API endpoint. Exercises the full streaming pipeline:
     * HTTP fetch → SSE parsing → ReadableStream tee → textStream.
     * ================================================================ */
    printf("\n--- Test 2: streamText (real API) ---\n");

    char stream_js[4096];
    snprintf(stream_js, sizeof(stream_js),
        "var _stream_done = false; var _stream_result = null;\n"
        "(async function() {\n"
        "  try {\n"
        "    var provider = aceCore.createOpenAIProvider({\n"
        "      apiKey: '%s',\n"
        "      baseURL: '%s'\n"
        "    });\n"
        "    var model = provider.languageModel('%s');\n"
        "    var streamResult = await aceCore.streamText({\n"
        "      model: model,\n"
        "      messages: [{ role: 'user', content: 'Say hello in exactly one word' }]\n"
        "    });\n"
        "    var reader = streamResult.fullStream.getReader();\n"
        "    var chunks = [];\n"
        "    while (true) {\n"
        "      var item = await reader.read();\n"
        "      if (item.done) break;\n"
        "      if (item.value.type === 'text-delta') {\n"
        "        chunks.push(item.value.content);\n"
        "      }\n"
        "    }\n"
        "    var fullText = chunks.join('');\n"
        "    console.log('Stream result:', JSON.stringify({\n"
        "      text: await streamResult.text,\n"
        "      fullStreamChunks: fullText,\n"
        "      finishReason: await streamResult.finishReason,\n"
        "      usage: await streamResult.usage\n"
        "    }));\n"
        "    var textResult = await streamResult.text;\n"
        "    _stream_result = (textResult && textResult.length > 0 && fullText.length > 0) ? 'ok' : 'fail:empty';\n"
        "    _stream_done = true;\n"
        "  } catch(e) {\n"
        "    console.log('Stream Exception:', e.message || String(e));\n"
        "    _stream_result = 'error:' + (e.message || String(e));\n"
        "    _stream_done = true;\n"
        "  }\n"
        "})();\n",
        cfg.api_key, cfg.api_base, cfg.model);

    rc = qwrt_eval(rt, stream_js, NULL);
    if (rc != 0) {
        fprintf(stderr, "Failed to eval stream test code\n");
        qwrt_destroy(rt); pal_uv_destroy(pal);
        free(cfg.api_base); free(cfg.api_key); free(cfg.model);
        return 1;
    }

    /* Drive event loop for stream test */
    iter = 0;
    while (iter < max_iter) {
        qwrt_tick(rt);
        uv_run(loop, UV_RUN_ONCE);

        char *result = NULL;
        rc = qwrt_eval(rt, "_stream_done", &result);
        if (rc == 0 && result && strcmp(result, "true") == 0) {
            qwrt_free(result);
            break;
        }
        if (result) qwrt_free(result);
        iter++;
    }

    char *stream_result = NULL;
    rc = qwrt_eval(rt, "_stream_result", &stream_result);
    if (rc != 0 || !stream_result) {
        fprintf(stderr, "\nFailed to get stream result (timeout after %d iterations)\n", iter);
        qwrt_destroy(rt); pal_uv_destroy(pal);
        free(cfg.api_base); free(cfg.api_key); free(cfg.model);
        return 1;
    }

    printf("\nStream result after %d ticks: %s\n", iter, stream_result);
    int stream_passed = strstr(stream_result, "ok") != NULL;
    if (stream_passed) {
        printf("TEST 2 PASSED — ace-ai-sdk streamText works via QuickJS (real API)\n");
    } else {
        printf("TEST 2 FAILED\n");
    }

    qwrt_free(stream_result);
    qwrt_destroy(rt);
    pal_uv_destroy(pal);
    free(cfg.api_base); free(cfg.api_key); free(cfg.model);

    int all_passed = passed && stream_passed;
    printf("\n========================================\n");
    printf("E2E Tests: %s\n", all_passed ? "ALL PASSED" : "SOME FAILED");
    printf("========================================\n");
    return all_passed ? 0 : 1;
}

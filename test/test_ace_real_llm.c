/*
 * ACE Agent Real LLM Test
 *
 * End-to-end test that calls a real LLM API via OpenAI-compatible endpoint.
 * Reads config from ~/.alpha/config.json or environment variables.
 *
 * Usage: test_ace_real_llm [adapter.js] [ace-core.js]
 *
 * If no arguments, searches for files in default relative paths.
 * Config is loaded from ~/.alpha/config.json:
 *   llm_endpoint, llm_api_key, llm_model
 * Or from env: LLM_API_BASE, LLM_API_KEY, LLM_MODEL
 *
 * Uses libuv PAL for real HTTP/HTTPS support.
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

/* Find file by searching multiple relative paths */
static int eval_file_search(qwrt_t *rt, const char **paths, int n)
{
    for (int i = 0; i < n; i++) {
        if (eval_file(rt, paths[i]) == 0) return 0;
    }
    return -1;
}

/* Simple JSON string field extractor (no external deps) */
static char *extract_json_string(const char *json, const char *key)
{
    /* Build search pattern: "key" */
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    const char *p = strstr(json, pattern);
    if (!p) return NULL;
    p += strlen(pattern);

    /* Skip whitespace and colon */
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ':') p++;
    if (*p != '"') return NULL;
    p++; /* skip opening quote */

    /* Find closing quote */
    const char *end = p;
    while (*end && *end != '"') {
        if (*end == '\\') end++; /* skip escaped chars */
        end++;
    }

    size_t len = (size_t)(end - p);
    char *result = malloc(len + 1);
    if (!result) return NULL;
    memcpy(result, p, len);
    result[len] = '\0';
    return result;
}

/* Load LLM config from ~/.alpha/config.json */
struct llm_config {
    char *api_base;
    char *api_key;
    char *model;
};

static struct llm_config load_alpha_config(void)
{
    struct llm_config cfg = {NULL, NULL, NULL};

    /* First try env vars */
    const char *env_base = getenv("LLM_API_BASE");
    const char *env_key = getenv("LLM_API_KEY");
    const char *env_model = getenv("LLM_MODEL");

    if (env_base) cfg.api_base = strdup(env_base);
    if (env_key) cfg.api_key = strdup(env_key);
    if (env_model) cfg.model = strdup(env_model);

    /* Then try ~/.alpha/config.json */
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

    /* Defaults */
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
        fprintf(stderr, "Error: No API key found. Set LLM_API_KEY env or ~/.alpha/config.json\n");
        free(cfg.api_base); free(cfg.api_key); free(cfg.model);
        return 1;
    }

    printf("ACE Agent Real LLM Test\n");
    printf("========================\n");
    printf("Endpoint: %s\n", cfg.api_base);
    printf("Model:    %s\n", cfg.model);
    printf("\n");

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
    const char *adapter_path = argc > 1 ? argv[1] : NULL;
    const char *ace_core_path = argc > 2 ? argv[2] : NULL;

    if (adapter_path) {
        if (eval_file(rt, adapter_path) != 0) {
            fprintf(stderr, "Failed to load adapter.js from %s\n", adapter_path);
            qwrt_destroy(rt); pal_uv_destroy(pal);
            free(cfg.api_base); free(cfg.api_key); free(cfg.model);
            return 1;
        }
    } else {
        if (eval_file_search(rt, adapter_paths, 4) != 0) {
            fprintf(stderr, "Failed to find adapter.js\n");
            qwrt_destroy(rt); pal_uv_destroy(pal);
            free(cfg.api_base); free(cfg.api_key); free(cfg.model);
            return 1;
        }
    }

    if (ace_core_path) {
        if (eval_file(rt, ace_core_path) != 0) {
            fprintf(stderr, "Failed to load ace-core-qwrt.js from %s\n", ace_core_path);
            qwrt_destroy(rt); pal_uv_destroy(pal);
            free(cfg.api_base); free(cfg.api_key); free(cfg.model);
            return 1;
        }
    } else {
        if (eval_file_search(rt, ace_core_paths, 4) != 0) {
            fprintf(stderr, "Failed to find ace-core-qwrt.js\n");
            qwrt_destroy(rt); pal_uv_destroy(pal);
            free(cfg.api_base); free(cfg.api_key); free(cfg.model);
            return 1;
        }
    }

    /*
     * Create session with real LLM API and execute a simple task.
     * Uses the "explorer" agent for a quick request.
     *
     * The JS code:
     * 1. Creates session with apiKey and apiBase
     * 2. Registers a simple tool (get_current_time)
     * 3. Asks the LLM a question that may trigger tool use
     * 4. Prints the result
     */
    char code[8192];
    snprintf(code, sizeof(code),
        "var _real_result = null;\n"
        "(async function() {\n"
        "  try {\n"
        "    var session = await aceCore.createAceSession({\n"
        "      config: {\n"
        "        runtime: __createQwrtRuntime(),\n"
        "        apiBase: '%s',\n"
        "        apiKey: '%s',\n"
        "        model: '%s'\n"
        "      },\n"
        "      skipConfigLoad: true,\n"
        "      skipContextInjection: true\n"
        "    });\n"
        "\n"
        "    /* Register a simple tool */\n"
        "    session.ace.registerTool({\n"
        "      name: 'get_current_time',\n"
        "      description: 'Get the current date and time',\n"
        "      parameters: {\n"
        "        type: 'object',\n"
        "        properties: {\n"
        "          timezone: {\n"
        "            type: 'string',\n"
        "            description: 'IANA timezone, e.g. America/New_York'\n"
        "          }\n"
        "        }\n"
        "      },\n"
        "      execute: async function(params) {\n"
        "        var d = new Date();\n"
        "        return d.toLocaleString('en-US', { timeZone: params.timezone || 'UTC' });\n"
        "      }\n"
        "    });\n"
        "\n"
        "    /* Execute with explorer agent */\n"
        "    console.log('Sending prompt to LLM...');\n"
        "    var result = await session.ace.execute({\n"
        "      agent: 'explorer',\n"
        "      input: 'What time is it in Tokyo? Use the get_current_time tool.'\n"
        "    });\n"
        "\n"
        "    console.log('Success:', result.success);\n"
        "    console.log('Content:', result.content ? result.content.substring(0, 200) : '(empty)');\n"
        "    if (result.error) console.log('Error:', result.error);\n"
        "    if (result.metadata) console.log('Tokens:', JSON.stringify(result.metadata));\n"
        "\n"
        "    _real_result = result.success ? 'ok' : 'fail:' + (result.error || 'unknown');\n"
        "  } catch(e) {\n"
        "    console.log('Exception:', e.message || String(e));\n"
        "    _real_result = 'error:' + (e.message || String(e));\n"
        "  }\n"
        "})();\n",
        cfg.api_base, cfg.api_key, cfg.model);

    int rc = qwrt_eval(rt, code, NULL);
    if (rc != 0) {
        fprintf(stderr, "Failed to eval agent code\n");
        qwrt_destroy(rt); pal_uv_destroy(pal);
        free(cfg.api_base); free(cfg.api_key); free(cfg.model);
        return 1;
    }

    /* Drive event loop — run until async work completes or timeout */
    int max_iter = 5000;  /* ~50 seconds max */
    int iter = 0;
    while (iter < max_iter) {
        qwrt_tick(rt);
        uv_run(loop, UV_RUN_ONCE);

        /* Check if result is ready */
        char *result = NULL;
        rc = qwrt_eval(rt, "_real_result", &result);
        if (rc == 0 && result && strcmp(result, "null") != 0) {
            qwrt_free(result);
            break;
        }
        if (result) qwrt_free(result);

        iter++;
    }

    /* Get final result */
    char *final_result = NULL;
    rc = qwrt_eval(rt, "_real_result", &final_result);
    if (rc != 0 || !final_result) {
        fprintf(stderr, "\nFailed to get result (timeout after %d iterations)\n", iter);
        qwrt_destroy(rt); pal_uv_destroy(pal);
        free(cfg.api_base); free(cfg.api_key); free(cfg.model);
        return 1;
    }

    printf("\n");
    int passed = strstr(final_result, "ok") != NULL;
    if (passed) {
        printf("TEST PASSED — agent successfully called real LLM\n");
    } else {
        printf("TEST FAILED — result: %s\n", final_result);
    }

    qwrt_free(final_result);
    qwrt_destroy(rt);
    pal_uv_destroy(pal);
    free(cfg.api_base); free(cfg.api_key); free(cfg.model);
    return passed ? 0 : 1;
}
/*
 * qwrt npm Compatibility Checker — evaluates JS source in qwrt
 * to check for API compatibility. Reports which APIs are available
 * and which are missing.
 *
 * Usage: compat_check [file.js]
 *   If file.js is given, evaluates it and reports compatibility.
 *   If no file, prints the qwrt API surface for reference.
 *
 * Build: cmake --build build --target compat_check
 */

#include <qwrt/qwrt.h>
#include <pal_mock.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* WinterCG APIs expected in qwrt */
static const char *WINTERCG_APIS[] = {
    "fetch", "Request", "Response", "Headers",
    "console", "console.log", "console.error", "console.warn",
    "crypto", "crypto.getRandomValues", "crypto.randomUUID", "crypto.subtle",
    "TextEncoder", "TextDecoder",
    "URL", "URLSearchParams",
    "ReadableStream", "WritableStream", "TransformStream",
    "Blob", "File", "FormData",
    "EventTarget", "Event", "CustomEvent",
    "AbortController", "AbortSignal",
    "setTimeout", "clearTimeout", "setInterval", "clearInterval",
    "performance",
    "structuredClone",
    "MessageChannel", "MessagePort",
    "navigator",
    "WebAssembly",
    NULL
};

/* Node.js APIs that qwrt does NOT provide */
static const char *NODE_APIS[] = {
    "require", "module", "exports", "__dirname", "__filename",
    "process", "Buffer", "setImmediate", "clearImmediate",
    NULL
};

static char *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return NULL; }
    char *s = (char *)malloc((size_t)n + 1);
    if (!s) { fclose(f); return NULL; }
    if (fread(s, 1, (size_t)n, f) != (size_t)n) { free(s); fclose(f); return NULL; }
    s[n] = '\0';
    *out_len = (size_t)n;
    fclose(f);
    return s;
}

int main(int argc, char **argv) {
    qwrt_pal_t *pal = pal_mock_create();
    if (!pal) { fprintf(stderr, "Failed to create PAL\n"); return 1; }
    qwrt_t *rt = qwrt_create(&(qwrt_config_t){ .pal = pal });
    if (!rt) { fprintf(stderr, "Failed to create runtime\n"); pal_mock_destroy(pal); return 1; }

    if (argc < 2) {
        /* API surface dump mode */
        printf("=== Qwrt.js API Surface ===\n\n");
        char *r = NULL;

        printf("## WinterCG APIs\n");
        for (int i = 0; WINTERCG_APIS[i]; i++) {
            char code[256];
            snprintf(code, sizeof(code), "typeof %s !== 'undefined'", WINTERCG_APIS[i]);
            qwrt_eval(rt, code, &r);
            printf("  %-30s %s\n", WINTERCG_APIS[i],
                   (r && strcmp(r, "true") == 0) ? "AVAILABLE" : "MISSING");
            if (r) qwrt_free(r);
        }

        printf("\n## Node.js APIs (expected MISSING)\n");
        for (int i = 0; NODE_APIS[i]; i++) {
            char code[256];
            snprintf(code, sizeof(code), "typeof %s !== 'undefined'", NODE_APIS[i]);
            qwrt_eval(rt, code, &r);
            printf("  %-30s %s\n", NODE_APIS[i],
                   (r && strcmp(r, "true") == 0) ? "FOUND" : "missing (correct)");
            if (r) qwrt_free(r);
        }

        /* Check WASM */
        qwrt_eval(rt, "typeof WebAssembly !== 'undefined'", &r);
        printf("\n## WebAssembly\n");
        printf("  %-30s %s\n", "WebAssembly",
               (r && strcmp(r, "true") == 0) ? "AVAILABLE (WAMR Fast JIT)" : "MISSING");
        if (r) qwrt_free(r);

    } else {
        /* File evaluation mode */
        size_t len = 0;
        char *src = read_file(argv[1], &len);
        if (!src) {
            fprintf(stderr, "Cannot read: %s\n", argv[1]);
            qwrt_destroy(rt); pal_mock_destroy(pal);
            return 1;
        }

        printf("=== Compatibility Check: %s ===\n\n", argv[1]);
        printf("Evaluating %zu bytes in qwrt runtime...\n\n", len);

        char *r = NULL;
        int rc = qwrt_eval(rt, src, &r);
        qwrt_tick(rt);

        if (rc == 0) {
            printf("Result: COMPATIBLE\n");
            printf("Output: %s\n", r ? r : "(no output)");
        } else {
            printf("Result: INCOMPATIBLE\n");
            printf("Error:  %s\n", r ? r : "unknown error");
            printf("\nCommon causes:\n");
            printf("  - require('...') — Node.js modules not available\n");
            printf("  - process.env / Buffer — Node.js APIs not available\n");
            printf("  - document / window — browser APIs not available\n");
            printf("  - fs / path / net — Node.js built-ins not available\n");
        }

        if (r) qwrt_free(r);
        free(src);
    }

    qwrt_destroy(rt);
    pal_mock_destroy(pal);
    return 0;
}

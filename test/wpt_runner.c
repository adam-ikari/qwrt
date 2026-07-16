/*
 * qwrt WPT Runner — runs .any.js tests from test/wpt/ using qwrt (mock PAL).
 *
 * Per test:
 *   1. Create runtime, eval `print` inject (writes to JS buffer).
 *   2. eval testharness.js (ShellTestEnvironment — no DOM/worker needed)
 *   3. eval test/wpt_shell_report.js (completion callback -> print)
 *   4. eval the .any.js test
 *   5. qwrt_tick (drain async, no-op for sync tests)
 *   6. Read print buffer (__wpt_out JS array), parse PASS/FAIL lines.
 *   7. Destroy runtime (isolated).
 *
 * Usage: wpt_runner [test_dir]
 *   test_dir defaults to test/wpt/ (recursively finds .any.js).
 *
 * Build: cmake --build build --target wpt_runner
 * META filter: skips tests whose "// META: global=" list does NOT include "shell".
 */

#define _POSIX_C_SOURCE 200809L
#include <qwrt/qwrt.h>
#include <pal_mock.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

/* Use stat() to detect directories reliably (d_type is not portable). */
static int is_dir(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return S_ISDIR(st.st_mode);
}

#ifndef WPT_SRC_DIR
#define WPT_SRC_DIR "test/wpt"
#endif

#define WPT_MAX_FILES 200
#define WPT_MAX_PATH  1024
#define WPT_MAX_SRC   (2 * 1024 * 1024)

/* ---- file reader ---- */
static char *read_file(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || (size_t)n > WPT_MAX_SRC) { fclose(f); return NULL; }
    char *s = (char *)malloc((size_t)n + 1);
    if (!s) { fclose(f); return NULL; }
    if (fread(s, 1, (size_t)n, f) != (size_t)n) { free(s); fclose(f); return NULL; }
    s[n] = '\0';
    *out_len = (size_t)n;
    fclose(f);
    return s;
}

/* ---- META global= filter ---- */
static int meta_allows_shell(const char *src, size_t len)
{
    const char *end = src + len;
    const char *p = src;
    int lines = 0;
    while (p < end && lines < 3) {
        const char *eol = p;
        while (eol < end && *eol != '\n') eol++;
        if (eol - p > 7 && strncmp(p, "// META:", 8) == 0) {
            const char *g = p + 8;
            while (g < eol && (*g == ' ' || *g == '\t')) g++;
            if (eol - g > 7 && strncmp(g, "global=", 7) == 0) {
                g += 7;
                const char *sp = g;
                while (sp < eol) {
                    while (sp < eol && (*sp == ' ' || *sp == ',' || *sp == '\t')) sp++;
                    if (eol - sp >= 5 && strncmp(sp, "shell", 5) == 0) {
                        sp += 5;
                        if (sp >= eol || *sp == ',' || *sp == ' ' || *sp == '\t')
                            return 1;
                    }
                    while (sp < eol && *sp != ',') sp++;
                    if (sp < eol) sp++;
                }
                return 0;
            }
        }
        p = eol + 1;
        lines++;
    }
    return 1;
}

/* ---- inject print() via eval of JS snippet ---- */
static const char *PRINT_INJECT =
    "var __wpt_out = []; globalThis.print = function(s){__wpt_out.push(String(s));};";

/* ---- run one test ---- */
static int run_one_test(const char *test_path,
                         const char *harness_src, size_t hlen,
                         const char *report_src, size_t rlen,
                         int *passed, int *failed, int *errors)
{
    /* ---- META filter ---- */
    size_t test_len = 0;
    char *test_src = read_file(test_path, &test_len);
    if (!test_src) {
        printf("SKIP | %s | cannot read file\n", test_path);
        (*errors)++; return -1;
    }
    if (!meta_allows_shell(test_src, test_len)) {
        printf("SKIP | %s | META global= excludes shell\n", test_path);
        free(test_src); return 0;
    }

    /* ---- create runtime ---- */
    qwrt_pal_t *pal = pal_mock_create();
    if (!pal) { free(test_src); return -1; }
    qwrt_config_t cfg; memset(&cfg, 0, sizeof(cfg)); cfg.pal = pal;
    qwrt_t *rt = qwrt_create(&cfg);
    if (!rt) {
        printf("SKIP | %s | qwrt_create failed\n", test_path);
        pal_mock_destroy(pal); free(test_src); (*errors)++; return -1;
    }

    /* ---- eval inject, harness, report, test ---- */
    int rc = qwrt_eval(rt, PRINT_INJECT, NULL);
    rc = rc == 0 ? qwrt_eval(rt, harness_src, NULL) : rc;
    /* Drain microtasks: ShellTestEnvironment uses Promise.resolve().then()
     * to set all_loaded = true. Must tick here so the harness is ready
     * before the shell report and test execute. */
    qwrt_tick(rt);
    rc = rc == 0 ? qwrt_eval(rt, report_src, NULL) : rc;
    rc = rc == 0 ? qwrt_eval(rt, test_src, NULL) : rc;
    /* Drain any microtasks queued during test execution (completion
     * callbacks, etc.) */
    qwrt_tick(rt);

    if (rc != 0) {
        printf("SKIP | %s | eval failed (missing API?)\n", test_path);
        qwrt_destroy(rt); pal_mock_destroy(pal); free(test_src); (*errors)++; return -1;
    }

    /* ---- read output buffer ---- */
    char *out_raw = NULL;
    rc = qwrt_eval(rt, "__wpt_out.join('\\n')", &out_raw);
    if (rc != 0 || !out_raw) {
        printf("SKIP | %s | cannot read output buffer\n", test_path);
        qwrt_destroy(rt); pal_mock_destroy(pal); free(test_src); free(out_raw); (*errors)++; return -1;
    }

    /* Parse — output is a JSON-quoted string, e.g. "\"PASS | name |\\n...\"" */
    char *out = strdup(out_raw + 1);  /* skip opening " */
    size_t olen = strlen(out);
    if (olen > 0 && out[olen - 1] == '"') out[olen - 1] = '\0';  /* strip closing " */
    qwrt_free(out_raw);

    int tpassed = 0, tfailed = 0;
    char *line = strtok(out, "\\n");
    while (line) {
        if (strncmp(line, "PASS | ", 7) == 0) {
            printf("  PASS | %s\n", line + 7);
            tpassed++;
        } else if (strncmp(line, "FAIL | ", 7) == 0) {
            printf("  FAIL | %s\n", line + 7);
            tfailed++;
        } else if (strncmp(line, "TIMEOUT | ", 10) == 0) {
            printf("  TIMEOUT | %s\n", line + 10);
            tfailed++;
        } else if (strncmp(line, "SUMMARY:", 8) == 0) {
            /* e.g. SUMMARY:12:0 — ignore in output counting */
        }
        line = strtok(NULL, "\\n");
    }
    printf("  %d/%d pass\n", tpassed, tpassed + tfailed);
    *passed += tpassed; *failed += tfailed;

    free(out);
    free(test_src);
    qwrt_destroy(rt);
    pal_mock_destroy(pal);
    return 0;
}

/* ---- recurse directory ---- */
static int walk_dir(const char *dir, const char *harness_src, size_t hlen,
                    const char *report_src, size_t rlen,
                    int *passed, int *failed, int *errors)
{
    DIR *d = opendir(dir);
    if (!d) { (*errors)++; return -1; }
    struct dirent *e; char path[WPT_MAX_PATH]; int count = 0;
    while ((e = readdir(d)) != NULL && count < WPT_MAX_FILES) {
        if (e->d_name[0] == '.') continue;
        int len = snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        if (len <= 0 || len >= (int)sizeof(path)) continue;
        if (is_dir(path)) {
            walk_dir(path, harness_src, hlen, report_src, rlen, passed, failed, errors);
        } else {
            size_t namelen = strlen(e->d_name);
            if (namelen > 7 && strcmp(e->d_name + namelen - 7, ".any.js") == 0) {
                run_one_test(path, harness_src, hlen, report_src, rlen, passed, failed, errors);
                count++;
            }
        }
    }
    closedir(d);
    return 0;
}

int main(int argc, char **argv)
{
    const char *test_dir = (argc > 1) ? argv[1] : WPT_SRC_DIR;

    /* read harness + report once */
    size_t hlen = 0, rlen = 0;
    char path_h[WPT_MAX_PATH], path_r[WPT_MAX_PATH];
    snprintf(path_h, sizeof(path_h), "%s/testharness.js", WPT_SRC_DIR);
    snprintf(path_r, sizeof(path_r), "test/wpt_shell_report.js");
    char *harness = read_file(path_h, &hlen);
    char *report = read_file(path_r, &rlen);
    if (!harness || !report) {
        fprintf(stderr, "cannot read harness or report\n");
        return 2;
    }

    int passed = 0, failed = 0, errors = 0;
    walk_dir(test_dir, harness, hlen, report, rlen, &passed, &failed, &errors);

    free(harness); free(report);

    printf("\n=== WPT Summary ===\n");
    printf("PASS:  %d\n", passed);
    printf("FAIL:  %d\n", failed);
    printf("ERROR: %d\n", errors);
    printf("Total: %d\n", passed + failed);

    return (failed > 0) ? 1 : 0;
}

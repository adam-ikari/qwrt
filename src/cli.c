/*
 * qwrt CLI — standalone WinterTC runtime
 * 用法: qwrt [options] [script.js [args...]] | qwrt -e 'code'
 *       qwrt（无参数）→ REPL
 */
#define _POSIX_C_SOURCE 200809L

#include <qwrt/qwrt.h>
#include <uv.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define QWRT_CLI_VERSION "qwrt 0.1.0"

static void usage(FILE *out) {
    fprintf(out,
        "Usage: qwrt [options] [script.js [args...]]\n"
        "       qwrt -e 'code' [args...]\n"
        "       qwrt            (start REPL)\n"
        "\n"
        "Options:\n"
        "  -e, --eval <code>   evaluate <code> and exit\n"
        "  -h, --help          show this help\n"
        "  -v, --version       show version\n"
        "\n"
        "Runs a WinterTC-compatible JavaScript runtime (fetch, crypto,\n"
        "streams, timers, fs, ...). No Node.js APIs (process, require, ...).\n");
}

/* ── host state and message channel ── */

typedef struct {
    int done;           /* atomic: eval finished (incl. error) — qwrt thread writes, main spins */
    int exit_code;      /* script error → 1 (qwrt thread writes, main reads after done) */
} cli_host_t;

/* message_cb: runs on the qwrt thread; CLI receives eval results.
 * json: {"ok":true,"v":"..."} or {"ok":false,"e":"..."} */
static void cli_message_cb(qwrt_t *rt, const char *json, size_t len, void *data) {
    (void)data;
    cli_host_t *h = (cli_host_t *)qwrt_get_runtime_data(rt);
    if (!h) return;
    if (strstr(json, "\"ok\":false")) {
        fprintf(stderr, "%.*s\n", (int)len, json);   /* error envelope already holds the message */
        h->exit_code = 1;
    }
    __atomic_store_n(&h->done, 1, __ATOMIC_RELEASE);
}

/* ── WinterTC bridge + onmessage command channel ──
 * - globalThis.arguments: script args (no runtime parts; aligned with the
 *   WinterCG proposal-cli-api direction; excludes the executable and path)
 * - globalThis.env: environment key-values (minimal form)
 * - onmessage eval command channel: host posts {"cmd":"eval","code":...}
 *   → JS eval → postMessage({ok, v|e}) */
static const char *kCliBootstrap =
    "globalThis.arguments = [];\n"      /* Task 7: inject SCRIPT_ARGS_JSON */
    "globalThis.env = {};\n"            /* Task 7: inject ENV_JSON */
    "globalThis.onmessage = function (e) {\n"
    "  var d = e.data;\n"
    "  if (d && d.cmd === 'eval') {\n"
    "    try { postMessage({ok: true, v: JSON.stringify((0, eval)(d.code))}); }\n"
    "    catch (err) { postMessage({ok: false, e: String(err)}); }\n"
    "  }\n"
    "};\n";

/* C string → JSON string literal (with surrounding quotes; escapes backslash,
 * quotes, control chars). Returns a malloc'd string; same semantics as
 * test_host.h's JSON_string. */
static char *json_escape(const char *s) {
    size_t n = strlen(s) * 6 + 3;
    char *out = malloc(n), *p = out;
    *p++ = '"';
    for (const unsigned char *c = (const unsigned char *)s; *c; c++) {
        switch (*c) {
        case '\\': *p++ = '\\'; *p++ = '\\'; break;
        case '"':  *p++ = '\\'; *p++ = '"'; break;
        case '\n': *p++ = '\\'; *p++ = 'n'; break;
        case '\r': *p++ = '\\'; *p++ = 'r'; break;
        case '\t': *p++ = '\\'; *p++ = 't'; break;
        default:
            if (*c < 0x20) p += sprintf(p, "\\u%04x", (unsigned)*c);
            else *p++ = (char)*c;
        }
    }
    *p++ = '"';
    *p = 0;
    return out;
}

/* build the bootstrap, injecting arguments/env (Task 7 completes the injection; minimal placeholder now) */
static char *build_bootstrap(const char *const *args, int nargs) {
    (void)args; (void)nargs;
    return strdup(kCliBootstrap);
}

/* shared execution path: create runtime → eval code → wait for result →
 * wait_idle → destroy. host is bound to rt via qwrt_set_runtime_data;
 * cli_message_cb fetches it with qwrt_get_runtime_data(rt) (callable
 * repeatedly; host is not shared). */
static int run_code(const char *code, const char *const *args, int nargs) {
    cli_host_t host = {0};

    char *bootstrap = build_bootstrap(args, nargs);
    qwrt_config_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.message_cb = cli_message_cb;
    cfg.initial_script = bootstrap;
    qwrt_t *rt = qwrt_create(&cfg);
    free(bootstrap);
    if (!rt) {
        fprintf(stderr, "qwrt: runtime init failed\n");
        return 1;
    }
    qwrt_set_runtime_data(rt, &host);

    char *cmd = malloc(strlen(code) * 2 + 64);
    sprintf(cmd, "{\"cmd\":\"eval\",\"code\":%s}", json_escape(code));
    qwrt_post_message(rt, cmd, strlen(cmd));
    free(cmd);

    /* lock-free wait: spin on done (qwrt thread release-stores, acquire-load here) */
    while (!__atomic_load_n(&host.done, __ATOMIC_ACQUIRE))
        sched_yield();
    int exit_code = host.exit_code;

    /* wait for pending async work (fetch/timer) to complete; the runtime
     * auto-exits when the loop is empty and the thread is joined here. Do not
     * call qwrt_destroy after this (would double-join); free the struct only. */
    qwrt_wait_idle(rt);
    qwrt_free(rt);
    return exit_code;
}

int main(int argc, char **argv) {
    /* parse -h/-v and -e; the first non-flag argument is the script path; the rest are script args */
    const char *script_path = NULL;
    int script_index = 0;    /* argv index where script args start */
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage(stdout);
            return 0;
        }
        if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--version")) {
            printf("%s\n", QWRT_CLI_VERSION);
            return 0;
        }
        if (!strcmp(argv[i], "-e") || !strcmp(argv[i], "--eval")) {
            if (i + 1 >= argc) { usage(stderr); return 2; }
            /* -e mode: eval the code; remaining args become script args */
            return run_code(argv[i + 1],
                            (const char *const *)argv + i + 2, argc - i - 2);
        }
        /* 未知 flag */
        if (argv[i][0] == '-') { usage(stderr); return 2; }
        script_path = argv[i];
        script_index = i + 1;
        break;
    }

    if (script_path) {
        /* ── script mode ── */
        FILE *f = fopen(script_path, "rb");
        if (!f) { fprintf(stderr, "qwrt: cannot open '%s'\n", script_path); return 1; }
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        char *code = malloc((size_t)sz + 1);
        if (fread(code, 1, (size_t)sz, f) != (size_t)sz) {
            fprintf(stderr, "qwrt: read error\n");
            fclose(f);
            free(code);
            return 1;
        }
        code[sz] = 0;
        fclose(f);
        int rc = run_code(code, (const char *const *)argv + script_index, argc - script_index);
        free(code);
        return rc;
    }

    /* Task 6: REPL (when no args) */
    return 0;
}

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
#include <signal.h>

#define QWRT_CLI_VERSION "qwrt 0.2.0"

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
    char result[8192];  /* eval result: the "v" value (ok) or "e" message (error), decoded */
} cli_host_t;

/* Extract the JSON string literal at *s (which starts with a double quote)
 * into out, decoding escapes (\", \\, \n, \r, \t, \uXXXX). Returns the
 * decoded length, or -1 on malformed input. Used to pull the v/e fields out
 * of the {"ok":...,"v":...,"e":...} eval envelope for the REPL. */
static int json_unescape(const char *s, char *out, size_t out_cap) {
    if (!s || *s != '"') {
        return -1;
    }
    s++;
    size_t n = 0;
    while (*s && *s != '"' && n + 3 < out_cap) {
        if (*s != '\\') {
            out[n++] = *s++;
            continue;
        }
        s++; /* backslash */
        switch (*s) {
        case '"': out[n++] = '"'; s++; break;
        case '\\': out[n++] = '\\'; s++; break;
        case '/':  out[n++] = '/';  s++; break;
        case 'n':  out[n++] = '\n'; s++; break;
        case 'r':  out[n++] = '\r'; s++; break;
        case 't':  out[n++] = '\t'; s++; break;
        case 'u': {
            unsigned code = 0;
            for (int i = 0; i < 4; i++) {
                char c = s[1 + i];
                unsigned d;
                if (c >= '0' && c <= '9') d = (unsigned)(c - '0');
                else if (c >= 'a' && c <= 'f') d = (unsigned)(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') d = (unsigned)(c - 'A' + 10);
                else return -1;
                code = (code << 4) | d;
            }
            /* UTF-16 code unit → UTF-8 (only BMP, matching JSON.stringify
             * default for non-surrogate code points) */
            if (code < 0x80) {
                out[n++] = (char)code;
            } else if (code < 0x800) {
                out[n++] = (char)(0xC0 | (code >> 6));
                out[n++] = (char)(0x80 | (code & 0x3F));
            } else {
                out[n++] = (char)(0xE0 | (code >> 12));
                out[n++] = (char)(0x80 | ((code >> 6) & 0x3F));
                out[n++] = (char)(0x80 | (code & 0x3F));
            }
            s += 5;
            break;
        }
        default:
            return -1;
        }
    }
    if (*s != '"') {
        return -1;
    }
    out[n] = '\0';
    return (int)n;
}

/* message_cb: runs on the qwrt thread; CLI receives eval results.
 * json: {"ok":true,"v":"..."} or {"ok":false,"e":"..."}.
 * Decodes the v/e payload into host->result for printing by the caller
 * (script mode prints errors to stderr; the REPL prints every result). */
static void cli_message_cb(qwrt_t *rt, const char *json, size_t len, void *data) {
    (void)data;
    cli_host_t *h = (cli_host_t *)qwrt_get_runtime_data(rt);
    if (!h) return;
    /* 精确判断：信封由 JSON.stringify 生成，无空格，恒以 {"ok":true 或
     * {"ok":false 开头。不能用 strstr 子串匹配 —— 错误消息/成功值的正文里
     * 可能含 "ok":false 字样导致误判。 */
    int is_error = (strncmp(json, "{\"ok\":false", 11) == 0);
    /* The envelope is {"ok":true,"v":"..."} or {"ok":false,"e":"..."}.
     * v/e hold JSON strings (the bootstrap wraps eval's value in
     * JSON.stringify). The key with its quotes and colon is 4 chars
     * (`"v":`), so the value string literal starts at field + 4.
     * When eval returns undefined, JSON.stringify(undefined) is undefined
     * and the v field is absent — that is a SUCCESS, not an error. */
    const char *field = is_error ? strstr(json, "\"e\":")
                                : strstr(json, "\"v\":");
    if (field && json_unescape(field + 4, h->result, sizeof(h->result)) >= 0) {
        h->exit_code = is_error ? 1 : 0;
    } else if (is_error) {
        /* e field missing or malformed — surface the raw envelope */
        size_t cap = sizeof(h->result) - 1;
        if (len > cap) len = cap;
        memcpy(h->result, json, len);
        h->result[len] = '\0';
        h->exit_code = 1;
    } else {
        /* ok, but no v field (eval returned undefined) — print nothing */
        h->result[0] = '\0';
        h->exit_code = 0;
    }
    __atomic_store_n(&h->done, 1, __ATOMIC_RELEASE);
}

/* ── WinterTC bridge + onmessage command channel ──
 * - globalThis.arguments: script args (no runtime parts; aligned with the
 *   WinterCG proposal-cli-api direction; excludes the executable and path)
 * - globalThis.env: environment key-values (minimal form)
 * - onmessage eval command channel: host posts {"cmd":"eval","code":...}
 *   → JS eval → postMessage({ok, v|e})
 * ARGS_JSON / ENV_JSON are substituted at runtime by build_bootstrap(). */
static const char *kCliBootstrap =
    "globalThis.arguments = %s;\n"
    "globalThis.env = %s;\n"
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

/* build the bootstrap, injecting arguments/env (Task 7: per the WinterCG
 * proposal-cli-api direction, globalThis.arguments holds the script args —
 * excluding the executable and script path — and globalThis.env the process
 * environment as a plain object). */
static char *build_bootstrap(const char *const *args, int nargs) {
    /* arguments → JSON array literal [ "a", "b" ] */
    size_t args_cap = 8;
    for (int i = 0; i < nargs; i++) {
        args_cap += strlen(args[i]) * 6 + 3;
    }
    char *args_json = malloc(args_cap + 1);
    char *p = args_json;
    *p++ = '[';
    for (int i = 0; i < nargs; i++) {
        if (i) *p++ = ',';
        char *q = json_escape(args[i]);
        size_t ql = strlen(q);
        memcpy(p, q, ql);
        p += ql;
        free(q);
    }
    *p++ = ']';
    *p = '\0';

    /* env → JSON object literal { "KEY": "VAL", ... } (minimal form) */
    extern char **environ;
    size_t env_cap = 64;
    for (char **e = environ; e && *e; e++) {
        env_cap += strlen(*e) * 6 + 8;
    }
    char *env_json = malloc(env_cap + 1);
    p = env_json;
    *p++ = '{';
    int first = 1;
    for (char **e = environ; e && *e; e++) {
        const char *eq = strchr(*e, '=');
        if (!eq) continue;
        size_t klen = (size_t)(eq - *e);
        char *k = malloc(klen + 1);
        memcpy(k, *e, klen);
        k[klen] = '\0';
        if (!first) *p++ = ',';
        first = 0;
        char *qk = json_escape(k);
        char *qv = json_escape(eq + 1);
        size_t qkl = strlen(qk), qvl = strlen(qv);
        memcpy(p, qk, qkl); p += qkl;
        *p++ = ':'; *p++ = ' ';
        memcpy(p, qv, qvl); p += qvl;
        free(k); free(qk); free(qv);
    }
    *p++ = '}';
    *p = '\0';

    size_t total = strlen(kCliBootstrap) + strlen(args_json) +
                   strlen(env_json) + 32;
    char *bootstrap = malloc(total);
    snprintf(bootstrap, total, kCliBootstrap, args_json, env_json);
    free(args_json);
    free(env_json);
    return bootstrap;
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

    char *cmd_json = json_escape(code);
    if (!cmd_json) {
        fprintf(stderr, "qwrt: out of memory\n");
        qwrt_wait_idle(rt);
        qwrt_free(rt);
        return 1;
    }
    /* json_escape 上界 strlen*6+3，故按 cmd_json 实际长度 + 固定信封开销
     * 分配（原 strlen*2+64 会溢出）。snprintf 检查返回值防截断。 */
    size_t cmd_cap = strlen(cmd_json) + 64;
    char *cmd = malloc(cmd_cap);
    if (!cmd) {
        free(cmd_json);
        fprintf(stderr, "qwrt: out of memory\n");
        qwrt_wait_idle(rt);
        qwrt_free(rt);
        return 1;
    }
    int wrote = snprintf(cmd, cmd_cap, "{\"cmd\":\"eval\",\"code\":%s}",
                         cmd_json);
    free(cmd_json);
    if (wrote < 0 || (size_t)wrote >= cmd_cap) {
        free(cmd);
        fprintf(stderr, "qwrt: out of memory\n");
        qwrt_wait_idle(rt);
        qwrt_free(rt);
        return 1;
    }
    qwrt_post_message(rt, cmd, strlen(cmd));
    free(cmd);

    /* lock-free wait: spin on done (qwrt thread release-stores, acquire-load here) */
    while (!__atomic_load_n(&host.done, __ATOMIC_ACQUIRE))
        sched_yield();
    int exit_code = host.exit_code;
    if (exit_code) {
        /* script error — cli_message_cb decoded the "e" payload into result */
        fprintf(stderr, "%s\n", host.result);
    }

    /* wait for pending async work (fetch/timer) to complete; the runtime
     * auto-exits when the loop is empty and the thread is joined here. Do not
     * call qwrt_destroy after this (would double-join); free the struct only. */
    qwrt_wait_idle(rt);
    qwrt_free(rt);
    return exit_code;
}

/* ── Task 6: interactive REPL (no script / no -e) ──
 * Banner → read a line → eval over the onmessage channel → print the result
 * (the decoded "v" or "e") → repeat; Ctrl-D/EOF exits. The runtime stays
 * alive for the whole session (no wait_idle); qwrt_destroy on exit. */
static int repl_loop(void) {
    cli_host_t host = {0};

    char *bootstrap = build_bootstrap(NULL, 0);
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

    printf("%s (WinterTC runtime) — type JS, Ctrl-D to exit\n",
           QWRT_CLI_VERSION);
    fflush(stdout);

    char line[8192];
    int exit_code = 0;
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\n")] = 0;
        if (!line[0]) {
            continue;
        }

        __atomic_store_n(&host.done, 0, __ATOMIC_RELEASE);
        char *escaped = json_escape(line);
        if (!escaped) {
            fprintf(stderr, "qwrt: out of memory\n");
            exit_code = 1;
            continue;
        }
        /* 按转义后实际长度 + 固定信封开销分配（原 strlen*2+64 溢出，
         * 且原内联 json_escape 的返回值从未释放 —— 泄漏）。 */
        size_t cmd_cap = strlen(escaped) + 64;
        char *cmd = malloc(cmd_cap);
        if (!cmd) {
            free(escaped);
            fprintf(stderr, "qwrt: out of memory\n");
            exit_code = 1;
            continue;
        }
        int wrote = snprintf(cmd, cmd_cap, "{\"cmd\":\"eval\",\"code\":%s}",
                             escaped);
        free(escaped);
        if (wrote < 0 || (size_t)wrote >= cmd_cap) {
            free(cmd);
            fprintf(stderr, "qwrt: out of memory\n");
            exit_code = 1;
            continue;
        }
        qwrt_post_message(rt, cmd, strlen(cmd));
        free(cmd);
        while (!__atomic_load_n(&host.done, __ATOMIC_ACQUIRE))
            sched_yield();

        printf("%s\n", host.result);
        fflush(stdout);
        if (host.exit_code) {
            exit_code = 1;
        }
    }
    printf("\n");

    qwrt_destroy(rt);
    return exit_code;
}

int main(int argc, char **argv) {
    /* HTTP/TCP 服务写已关闭的对端连接会触发 SIGPIPE(默认杀进程,
     * wrk 压测中断连即崩)。libuv 不忽略它;宿主必须显式忽略。 */
    signal(SIGPIPE, SIG_IGN);
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
        if (sz < 0) {
            /* ftell 失败（如管道/非普通文件）—— sz=-1 转 size_t 后为
             * SIZE_MAX，malloc 巨大缓冲并 fread 崩溃。 */
            fprintf(stderr, "qwrt: cannot size '%s'\n", script_path);
            fclose(f);
            return 1;
        }
        char *code = malloc((size_t)sz + 1);
        if (!code) {
            fprintf(stderr, "qwrt: out of memory\n");
            fclose(f);
            return 1;
        }
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
    return repl_loop();
}

/*
 * test_dap_async — verifies async JS advances while the debugger is paused.
 *
 * The debuggee sets a setTimeout, then hits a breakpoint. While paused, the
 * DAP layer's on_stopped pumps the PAL event loop (dap_pump_pal), so the
 * uv-backed timer fires and the callback sets a global. The parent (DAP
 * client) waits briefly at the breakpoint, then continues; on termination it
 * checks the global reflects the timer having fired during the pause.
 *
 * Build: cmake -B build -DQWRT_BUILD_DEBUGGER=ON -DQWRT_BUILD_TESTS=ON
 * Run:   ctest -R test_dap_async --output-on-failure
 */
#define _POSIX_C_SOURCE 200809L

#include <qwrt/qwrt.h>
#include <quickjs.h>
#include <pal_uv.h>
#include <uv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

/* The debuggee: schedule a 100ms timer that sets globalThis.__fired=1, then
 * loop until it fires, hitting a breakpoint inside the loop. */
static const char *JS_PROGRAM =
    "var __fired = 0;\n"               /* line 1 */
    "setTimeout(function() { __fired = 1; }, 100);\n"  /* line 2 */
    "var n = 0;\n"                      /* line 3 */
    "while (__fired === 0 && n < 1000) {\n"  /* line 4 */
    "  n++;\n"                          /* line 5 <- breakpoint */
    "}\n";                              /* line 6 */

/* ---- DAP framing (parent side) — same shape as test_dap_debugger.c ---- */

static void dap_write(int fd, const char *json)
{
    char header[64];
    int n = snprintf(header, sizeof(header), "Content-Length: %zu\r\n\r\n", strlen(json));
    write(fd, header, (size_t)n);
    write(fd, json, strlen(json));
}

static char *dap_read(FILE *f)
{
    long clen = -1;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        size_t L = strlen(line);
        while (L && (line[L-1] == '\n' || line[L-1] == '\r')) line[--L] = '\0';
        if (L == 0) break;
        if (strncmp(line, "Content-Length:", 15) == 0)
            clen = strtol(line + 15, NULL, 10);
    }
    if (clen < 0) return NULL;
    char *body = malloc((size_t)clen + 1);
    if (!body) return NULL;
    if (fread(body, 1, (size_t)clen, f) != (size_t)clen) { free(body); return NULL; }
    body[clen] = '\0';
    return body;
}

static char *json_get(const char *json, const char *key)
{
    size_t klen = strlen(key);
    const char *p = json;
    while ((p = strstr(p, "\"")) != NULL) {
        if (strncmp(p + 1, key, klen) == 0 && p[1 + klen] == '"') {
            const char *q = p + 1 + klen + 1;
            while (*q && (*q == ' ' || *q == '\t')) q++;
            if (*q == ':') {
                q++;
                while (*q && (*q == ' ' || *q == '\t')) q++;
                if (*q == '"') {
                    const char *e = q + 1;
                    while (*e && (*e != '"' || e[-1] == '\\')) e++;
                    size_t n = (size_t)(e - q - 1);
                    char *out = malloc(n + 1);
                    memcpy(out, q + 1, n);
                    out[n] = '\0';
                    return out;
                } else {
                    const char *e = q;
                    while (*e && *e != ',' && *e != '}' && *e != ']') e++;
                    size_t n = (size_t)(e - q);
                    char *out = malloc(n + 1);
                    memcpy(out, q, n);
                    out[n] = '\0';
                    return out;
                }
            }
        }
        p++;
        while (*p && *p != '"') { if (*p == '\\' && p[1]) p++; p++; }
        if (*p == '"') p++;
    }
    return NULL;
}

/* ---- child: uv-backed embedding host ---- */

static int child_main(int in_fd, int out_fd, const char *js_path)
{
    dup2(in_fd, STDIN_FILENO);
    dup2(out_fd, STDOUT_FILENO);
    close(in_fd);
    close(out_fd);

    uv_loop_t loop;
    uv_loop_init(&loop);
    qwrt_pal_t *pal = pal_uv_create(&loop);
    if (!pal) return 1;
    qwrt_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.pal = pal;
    /* QWRT_DEBUG env is set by the parent; qwrt_create auto-attaches DAP and
     * blocks on the configuration phase before returning. */
    qwrt_t *rt = qwrt_create(&cfg);
    if (!rt) { pal_uv_destroy(pal); return 1; }

    FILE *f = fopen(js_path, "rb");
    if (!f) { qwrt_destroy(rt); pal_uv_destroy(pal); return 1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *src = malloc((size_t)sz + 1);
    fread(src, 1, (size_t)sz, f);
    src[sz] = '\0';
    fclose(f);

    struct JSContext *ctx = qwrt_get_jsctx(rt);
    /* Eval the program; this blocks inside JS_Eval, paused at the breakpoint,
     * while on_stopped pumps the uv loop so the 100ms timer fires. */
    JS_Eval(ctx, src, (size_t)sz, js_path, JS_EVAL_TYPE_GLOBAL);
    /* Drain any remaining deferred work. */
    qwrt_tick(rt, 100);

    /* Read __fired and __n; write the verdict. If the timer fired DURING the
     * pause, the loop exits with small n (the breakpoint is on line 5, first
     * loop iteration). If it didn't fire during pause, the loop runs to
     * n=1000 before the timer fires after resume. So n<50 proves async-across-
     * pause worked. */
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue fired = JS_GetPropertyStr(ctx, global, "__fired");
    JSValue nval = JS_GetPropertyStr(ctx, global, "n");
    int fired_val = 0, n_val = 0;
    JS_ToInt32(ctx, &fired_val, fired);
    JS_ToInt32(ctx, &n_val, nval);
    JS_FreeValue(ctx, fired);
    JS_FreeValue(ctx, nval);
    JS_FreeValue(ctx, global);
    FILE *vf = fopen("/tmp/qwrt_dap_async_result", "w");
    if (vf) { fprintf(vf, "%d %d", fired_val, n_val); fclose(vf); }

    free(src);
    qwrt_destroy(rt);
    pal_uv_destroy(pal);
    uv_loop_close(&loop);
    return 0;
}

/* ---- parent: DAP client ---- */

static int parent_main(int child_out_fd, int child_in_fd, pid_t pid)
{
    FILE *from_child = fdopen(child_out_fd, "r");
    if (!from_child) return 1;
    signal(SIGPIPE, SIG_IGN);

    /* initialize + initialized event (read up to 2 msgs) */
    dap_write(child_in_fd,
        "{\"type\":\"request\",\"seq\":1,\"command\":\"initialize\","
        "\"arguments\":{\"adapterID\":\"qwrt\",\"clientID\":\"test\"}}");
    int got_event = 0, got_response = 0, tries;
    for (tries = 0; tries < 4 && !(got_event && got_response); tries++) {
        char *msg = dap_read(from_child);
        if (!msg) break;
        if (strstr(msg, "\"event\"") && strstr(msg, "\"initialized\"")) got_event = 1;
        if (strstr(msg, "\"response\"") && strstr(msg, "\"initialize\"")) got_response = 1;
        free(msg);
    }
    if (!got_event || !got_response) {
        fprintf(stderr, "FAIL: handshake (event=%d resp=%d)\n", got_event, got_response);
        return 1;
    }

    /* setBreakpoints at line 5 (inside the loop) */
    dap_write(child_in_fd,
        "{\"type\":\"request\",\"seq\":2,\"command\":\"setBreakpoints\","
        "\"arguments\":{\"source\":{\"path\":\"/tmp/qwrt_dap_async.js\"},"
        "\"breakpoints\":[{\"line\":5}]}}");
    char *msg = dap_read(from_child);
    if (!msg) { fprintf(stderr, "FAIL: no setBreakpoints response\n"); return 1; }
    free(msg);

    /* configurationDone → child evals, pauses at entry, then we continue to
     * the breakpoint. */
    dap_write(child_in_fd,
        "{\"type\":\"request\",\"seq\":3,\"command\":\"configurationDone\","
        "\"arguments\":{}}");
    msg = dap_read(from_child);  /* configurationDone response */
    free(msg);
    msg = dap_read(from_child);  /* entry stopped event (reason pause) */
    free(msg);

    /* continue past entry to hit the breakpoint */
    dap_write(child_in_fd,
        "{\"type\":\"request\",\"seq\":4,\"command\":\"continue\","
        "\"arguments\":{\"threadId\":1}}");
    msg = dap_read(from_child);  /* continue response */
    free(msg);

    /* expect stopped at breakpoint */
    msg = dap_read(from_child);
    int at_bp = msg && strstr(msg, "\"stopped\"") &&
                strstr(msg, "\"breakpoint\"");
    free(msg);
    if (!at_bp) {
        fprintf(stderr, "FAIL: did not stop at breakpoint\n");
        return 1;
    }

    /* Now we're paused at the breakpoint. The child's on_stopped is pumping
     * the uv loop, so the 100ms timer should fire while we sit here. Wait
     * 400ms (timer is 100ms) to give it time. */
    usleep(400000);

    /* continue past the loop (the timer has fired, so __fired===1 and the
     * loop exits). */
    dap_write(child_in_fd,
        "{\"type\":\"request\",\"seq\":5,\"command\":\"continue\","
        "\"arguments\":{\"threadId\":1}}");
    msg = dap_read(from_child);
    free(msg);

    /* drain until EOF (child exits when the program finishes) */
    int saw_terminated = 0;
    for (;;) {
        msg = dap_read(from_child);
        if (!msg) break;
        if (strstr(msg, "\"terminated\"")) saw_terminated = 1;
        free(msg);
    }

    fclose(from_child);
    close(child_in_fd);
    int status = 0;
    waitpid(pid, &status, 0);
    signal(SIGPIPE, SIG_DFL);

    /* The child writes "__fired n" to a temp file. If the timer fired DURING
     * the pause, n is small (loop exited early). If not, n~=1000. Require
     * fired && n<50 as proof the PAL loop advanced while paused. */
    FILE *vf = fopen("/tmp/qwrt_dap_async_result", "r");
    int fired = 0, n_val = 1000;
    if (vf) {
        char vbuf[64] = {0};
        fgets(vbuf, sizeof(vbuf), vf);
        fclose(vf);
        sscanf(vbuf, "%d %d", &fired, &n_val);
    }
    if (!fired) {
        fprintf(stderr, "FAIL: timer did not fire (__fired=%d)\n", fired);
        return 1;
    }
    if (n_val >= 50) {
        fprintf(stderr, "FAIL: timer fired after pause, not during (n=%d)\n", n_val);
        return 1;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "FAIL: child exited status=%d\n", status);
        return 1;
    }
    printf("test_dap_async: PASS (timer fired during pause, n=%d)\n", n_val);
    return 0;
}

int main(void)
{
    const char *js_path = "/tmp/qwrt_dap_async.js";
    FILE *f = fopen(js_path, "wb");
    if (!f) { perror("fopen"); return 1; }
    fputs(JS_PROGRAM, f);
    fclose(f);

    int to_child[2], from_child[2];
    if (pipe(to_child) || pipe(from_child)) { perror("pipe"); return 1; }

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return 1; }

    if (pid == 0) {
        close(to_child[1]);
        close(from_child[0]);
        setenv("QWRT_DEBUG", "1", 1);
        const char *trace = getenv("QWRT_DAP_TRACE");
        if (trace) freopen(trace, "w", stderr);
        int rc = child_main(to_child[0], from_child[1], js_path);
        _exit(rc);
    }

    close(to_child[0]);
    close(from_child[1]);
    int rc = parent_main(from_child[0], to_child[1], pid);
    unlink(js_path);
    unlink("/tmp/qwrt_dap_async_result");
    return rc;
}

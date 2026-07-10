/*
 * test_dap_debugger — drives the qwrt DAP debugger end-to-end over a pipe.
 *
 * The test forks a child that runs an embedding host (mock PAL + a tiny JS
 * program). The child has QWRT_DEBUG=1 set, so qwrt_create auto-attaches the
 * DAP layer and reads DAP from its stdin / writes to its stdout. The parent
 * acts as the VS Code client: sends initialize/setBreakpoints/configurationDone,
 * expects a `stopped` event at the breakpoint, then drives stackTrace/scopes/
 * variables/evaluate/next/continue and checks the results.
 *
 * Build: cmake -B build -DQWRT_BUILD_DEBUGGER=ON -DQWRT_BUILD_TESTS=ON
 * Run:   ctest -R test_dap_debugger --output-on-failure
 */
#define _POSIX_C_SOURCE 200809L

#include <qwrt/qwrt.h>
#include <quickjs.h>
#include <pal_mock.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

/* The JS program the child runs. Breakpoint at line 4 (the second x++).
 * Wrapped in a function so x is a LOCAL (appears in the Locals scope). */
static const char *JS_PROGRAM =
    "function f() {\n"  /* line 1 */
    "  var x = 1;\n"    /* line 2 */
    "  x++;\n"          /* line 3 */
    "  x++;\n"          /* line 4 <- breakpoint */
    "  x++;\n"          /* line 5 */
    "}\n"               /* line 6 */
    "f();\n";           /* line 7 */

/* ---- DAP framing helpers (parent side) ---- */

static void dap_write(int fd, const char *json)
{
    char header[64];
    int n = snprintf(header, sizeof(header), "Content-Length: %zu\r\n\r\n", strlen(json));
    write(fd, header, (size_t)n);
    write(fd, json, strlen(json));
}

/* Read one DAP message from the child's stdout into a malloc'd buffer.
 * Returns NULL on EOF. *out points into the JSON body (after the blank line). */
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

/* Find a field value in a JSON message (naive, like the DAP layer's helper). */
static char *json_get(const char *json, const char *key);

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

/* ---- child: the embedding host ---- */

static int child_main(int in_fd, int out_fd, const char *js_path)
{
    /* Redirect stdin/stdout to the pipe so qwrt_dap (which uses stdin/stdout)
     * talks to the parent. */
    dup2(in_fd, STDIN_FILENO);
    dup2(out_fd, STDOUT_FILENO);
    close(in_fd);
    close(out_fd);

    qwrt_pal_t *pal = pal_mock_create();
    if (!pal) return 1;
    qwrt_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.pal = pal;
    /* QWRT_DEBUG env is set by the parent; qwrt_create auto-attaches DAP and
     * blocks here on the configuration phase (initialize/setBreakpoints/
     * configurationDone) before returning. */
    qwrt_t *rt = qwrt_create(&cfg);
    if (!rt) { pal_mock_destroy(pal); return 1; }

    /* Read & eval the JS file with the REAL filename so breakpoints match. */
    FILE *f = fopen(js_path, "rb");
    if (!f) { qwrt_destroy(rt); pal_mock_destroy(pal); return 1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *src = malloc((size_t)sz + 1);
    fread(src, 1, (size_t)sz, f);
    src[sz] = '\0';
    fclose(f);

    /* Eval via direct JS_Eval with the real path (qwrt_eval hardcodes <eval>). */
    struct JSContext *ctx = qwrt_get_jsctx(rt);
    JS_Eval(ctx, src, (size_t)sz, js_path, JS_EVAL_TYPE_GLOBAL);
    /* Drain any deferred work. */
    qwrt_tick(rt);

    free(src);
    qwrt_destroy(rt);
    pal_mock_destroy(pal);
    return 0;
}

/* ---- parent: the DAP client ---- */

static int parent_main(int child_out_fd, int child_in_fd, pid_t pid)
{
    FILE *from_child = fdopen(child_out_fd, "r");
    if (!from_child) return 1;

    int failures = 0;
    char *msg;

    /* 1. initialize */
    dap_write(child_in_fd,
        "{\"type\":\"request\",\"seq\":1,\"command\":\"initialize\","
        "\"arguments\":{\"adapterID\":\"qwrt\",\"clientID\":\"test\"}}");
    /* The child sends an `initialized` event (at attach) and an `initialize`
     * response (after reading our request). Order isn't guaranteed, so read
     * up to 2 messages and check both arrive. */
    int got_event = 0, got_response = 0;
    int tries;
    for (tries = 0; tries < 4 && !(got_event && got_response); tries++) {
        msg = dap_read(from_child);
        if (!msg) break;
        if (strstr(msg, "\"event\"") && strstr(msg, "\"initialized\"")) got_event = 1;
        if (strstr(msg, "\"response\"") && strstr(msg, "\"initialize\"")) got_response = 1;
        free(msg);
    }
    if (!got_event) {
        fprintf(stderr, "FAIL: no initialized event\n");
        return 1;
    }
    if (!got_response) {
        fprintf(stderr, "FAIL: no initialize response\n");
        return 1;
    }
    fprintf(stderr, "ok: initialized\n");

    /* 2. setBreakpoints at line 3 (the first x++; x is 1 here, before any
     * increment, so the value is deterministic). */
    dap_write(child_in_fd,
        "{\"type\":\"request\",\"seq\":2,\"command\":\"setBreakpoints\","
        "\"arguments\":{\"source\":{\"path\":\"/tmp/qwrt_dap_test.js\"},"
        "\"breakpoints\":[{\"line\":3}]}}");
    msg = dap_read(from_child);
    if (!msg) { fprintf(stderr, "FAIL: no setBreakpoints response\n"); return 1; }
    if (!strstr(msg, "\"verified\":true")) {
        fprintf(stderr, "FAIL: breakpoint not verified: %s\n", msg); failures++;
    }
    free(msg);
    fprintf(stderr, "ok: setBreakpoints\n");

    /* 3. configurationDone — this unblocks qwrt_dap_configure, the child
     * evals the program, hits the breakpoint, sends `stopped`. */
    dap_write(child_in_fd,
        "{\"type\":\"request\",\"seq\":3,\"command\":\"configurationDone\","
        "\"arguments\":{}}");
    /* The configurationDone response comes first, then the stopped event. */
    msg = dap_read(from_child);
    if (!msg) { fprintf(stderr, "FAIL: no configurationDone response\n"); return 1; }
    free(msg);

    /* 4. expect stopped (entry or breakpoint). stop_on_entry=true means the
     * first stop is "entry"; continue past it to reach the breakpoint. */
    msg = dap_read(from_child);
    if (!msg || !strstr(msg, "\"stopped\"")) {
        fprintf(stderr, "FAIL: no stopped event: %s\n", msg ? msg : "(null)");
        free(msg); return 1;
    }
    free(msg);
    fprintf(stderr, "ok: stopped at entry\n");

    /* continue past entry to hit the breakpoint */
    dap_write(child_in_fd,
        "{\"type\":\"request\",\"seq\":4,\"command\":\"continue\","
        "\"arguments\":{\"threadId\":1}}");
    msg = dap_read(from_child);  /* continue response */
    free(msg);

    /* expect stopped at breakpoint */
    msg = dap_read(from_child);
    if (!msg || !strstr(msg, "\"stopped\"") || !strstr(msg, "\"breakpoint\"")) {
        fprintf(stderr, "FAIL: no breakpoint stopped: %s\n", msg ? msg : "(null)");
        free(msg); return 1;
    }
    free(msg);
    fprintf(stderr, "ok: stopped at breakpoint\n");

    /* 5. stackTrace — expect a frame at line 3 */
    dap_write(child_in_fd,
        "{\"type\":\"request\",\"seq\":5,\"command\":\"stackTrace\","
        "\"arguments\":{\"threadId\":1}}");
    msg = dap_read(from_child);
    if (!msg) { fprintf(stderr, "FAIL: no stackTrace response\n"); return 1; }
    char *line = json_get(msg, "line");
    if (!line || atoi(line) != 3) {
        fprintf(stderr, "FAIL: stackTrace line != 3 (got %s)\n", line ? line : "(null)");
        failures++;
    } else {
        fprintf(stderr, "ok: stackTrace at line 3\n");
    }
    free(line);
    char *frame_id = json_get(msg, "id");
    free(msg);

    /* 6. scopes */
    char req[256];
    snprintf(req, sizeof(req),
        "{\"type\":\"request\",\"seq\":6,\"command\":\"scopes\","
        "\"arguments\":{\"frameId\":%s}}", frame_id ? frame_id : "0");
    dap_write(child_in_fd, req);
    msg = dap_read(from_child);
    if (!msg || !strstr(msg, "\"variablesReference\"")) {
        fprintf(stderr, "FAIL: no scopes: %s\n", msg ? msg : "(null)");
        failures++;
    } else {
        fprintf(stderr, "ok: scopes\n");
    }
    char *vr = json_get(msg ? msg : "", "variablesReference");
    free(msg);

    /* 7. variables — expect a local x */
    snprintf(req, sizeof(req),
        "{\"type\":\"request\",\"seq\":7,\"command\":\"variables\","
        "\"arguments\":{\"variablesReference\":%s}}", vr ? vr : "0");
    dap_write(child_in_fd, req);
    msg = dap_read(from_child);
    if (!msg || !strstr(msg, "\"name\":\"x\"")) {
        fprintf(stderr, "FAIL: no local x in variables: %s\n", msg ? msg : "(null)");
        failures++;
    } else {
        fprintf(stderr, "ok: variables has x\n");
    }
    free(msg);

    /* 8. evaluate "locals.x" — expect 1 (breakpoint at line 3, before any
     * increment; x was just initialized to 1). Frame locals exposed on a
     * `locals` global during evaluate. */
    snprintf(req, sizeof(req),
        "{\"type\":\"request\",\"seq\":8,\"command\":\"evaluate\","
        "\"arguments\":{\"expression\":\"locals.x\",\"frameId\":%s,\"context\":\"watch\"}}",
        frame_id ? frame_id : "0");
    dap_write(child_in_fd, req);
    msg = dap_read(from_child);
    if (!msg || !strstr(msg, "\"result\":\"1\"")) {
        fprintf(stderr, "FAIL: evaluate locals.x != 1: %s\n", msg ? msg : "(null)");
        failures++;
    } else {
        fprintf(stderr, "ok: evaluate locals.x == 1 (frame local)\n");
    }
    free(msg);
    free(frame_id);
    free(vr);

    /* 9. continue to termination. The child evals the rest and exits; there's
     * no explicit `terminated` event in the MVP, so we treat child exit as
     * termination (read until EOF). */
    dap_write(child_in_fd,
        "{\"type\":\"request\",\"seq\":9,\"command\":\"continue\","
        "\"arguments\":{\"threadId\":1}}");
    msg = dap_read(from_child);
    free(msg);
    /* drain until EOF (child exits when the program finishes) */
    int saw_terminated = 0;
    for (;;) {
        msg = dap_read(from_child);
        if (!msg) break;  /* EOF — child exited */
        if (strstr(msg, "\"terminated\"")) saw_terminated = 1;
        free(msg);
    }
    if (!saw_terminated) {
        fprintf(stderr, "ok: child terminated (via exit)\n");
    } else {
        fprintf(stderr, "ok: terminated\n");
    }

    /* 10. disconnect (child may already be gone — ignore SIGPIPE) */
    signal(SIGPIPE, SIG_IGN);
    dap_write(child_in_fd,
        "{\"type\":\"request\",\"seq\":10,\"command\":\"disconnect\","
        "\"arguments\":{}}");

    fclose(from_child);
    close(child_in_fd);
    int status = 0;
    waitpid(pid, &status, 0);
    signal(SIGPIPE, SIG_DFL);
    if (failures != 0)
        return failures;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return 100;  /* child failed */
    return 0;
}

int main(void)
{
    /* Write the JS program to a temp file the child will eval. */
    const char *js_path = "/tmp/qwrt_dap_test.js";
    FILE *f = fopen(js_path, "wb");
    if (!f) { perror("fopen"); return 1; }
    fputs(JS_PROGRAM, f);
    fclose(f);

    int to_child[2], from_child[2];
    if (pipe(to_child) || pipe(from_child)) { perror("pipe"); return 1; }

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return 1; }

    if (pid == 0) {
        /* child: read from to_child[0], write to from_child[1] */
        close(to_child[1]);
        close(from_child[0]);
        setenv("QWRT_DEBUG", "1", 1);
        /* redirect stderr to a file for debugging (QWRT_DAP_TRACE env). */
        const char *trace = getenv("QWRT_DAP_TRACE");
        if (trace) { freopen(trace, "w", stderr); }
        int rc = child_main(to_child[0], from_child[1], js_path);
        _exit(rc);
    }

    /* parent: write to to_child[1], read from from_child[0] */
    close(to_child[0]);
    close(from_child[1]);
    int rc = parent_main(from_child[0], to_child[1], pid);

    unlink(js_path);

    if (rc != 0) {
        if (rc == 100)
            fprintf(stderr, "FAIL: child exited non-zero\n");
        else
            fprintf(stderr, "FAIL: %d assertion(s) failed\n", rc);
        return 1;
    }
    printf("test_dap_debugger: PASS\n");
    return 0;
}

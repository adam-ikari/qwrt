// test_dap_gtest — DAP debugger end-to-end (new message model, gtest).
//
// The test forks a child embedding host (mock_libuv build). The child passes
// its JS program as qwrt_config.initial_script and sets QWRT_DEBUG=1, so
// qwrt_create auto-attaches the DAP layer and blocks in the configuration
// phase (initialize/setBreakpoints/configurationDone) BEFORE eval'ing the
// initial script. The initial script is eval'd as "<initial>", so the parent
// sets breakpoints on source path "<initial>".
//
// The parent acts as the VS Code client over pipes: initialize →
// setBreakpoints → configurationDone, expects a `stopped` event at entry and
// at the breakpoint, then drives stackTrace/scopes/variables/evaluate and
// continues to exit.
//
// NOTE: async JS (fetch/setTimeout) cannot advance while paused at a
// breakpoint in the new model (the qwrt thread is inside JS_Eval in
// on_stopped) — this test therefore only covers synchronous stepping.
//
// Build: cmake -B build -DQWRT_BUILD_DEBUGGER=ON -DQWRT_BUILD_TESTS=ON
// Run:   ctest -R test_dap_gtest --output-on-failure
#define _POSIX_C_SOURCE 200809L

#include <gtest/gtest.h>

#include <qwrt/qwrt.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <signal.h>
#include <string>
#include <unistd.h>
#include <sys/wait.h>

#ifdef QWRT_DEBUG_SUPPORT

/* The JS program the child runs as initial_script (eval'd as "<initial>").
 * Breakpoint at line 3 (the first x++). Wrapped in a function so x is a LOCAL
 * (appears in the Locals scope). */
static const char *kJsProgram =
    "function f() {\n"  /* line 1 */
    "  var x = 1;\n"    /* line 2 */
    "  x++;\n"          /* line 3 <- breakpoint */
    "  x++;\n"          /* line 4 */
    "  x++;\n"          /* line 5 */
    "}\n"               /* line 6 */
    "f();\n";           /* line 7 */

/* ---- DAP framing helpers (parent side) ---- */

static void dap_write(int fd, const char *json) {
    char header[64];
    int n = snprintf(header, sizeof(header), "Content-Length: %zu\r\n\r\n",
                     strlen(json));
    (void)!write(fd, header, (size_t)n);
    (void)!write(fd, json, strlen(json));
}

/* Read one DAP message from the child's stdout into a malloc'd buffer.
 * Returns NULL on EOF. *out points into the JSON body (after the blank line). */
static char *dap_read(FILE *f) {
    long clen = -1;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        size_t L = strlen(line);
        while (L && (line[L - 1] == '\n' || line[L - 1] == '\r')) line[--L] = '\0';
        if (L == 0) break;
        if (strncmp(line, "Content-Length:", 15) == 0)
            clen = strtol(line + 15, nullptr, 10);
    }
    if (clen < 0) return nullptr;
    char *body = (char *)malloc((size_t)clen + 1);
    if (!body) return nullptr;
    if (fread(body, 1, (size_t)clen, f) != (size_t)clen) { free(body); return nullptr; }
    body[clen] = '\0';
    return body;
}

/* Find a field value in a JSON message (naive, like the DAP layer's helper). */
static char *json_get(const char *json, const char *key) {
    size_t klen = strlen(key);
    const char *p = json;
    while ((p = strstr(p, "\"")) != nullptr) {
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
                    char *out = (char *)malloc(n + 1);
                    memcpy(out, q + 1, n);
                    out[n] = '\0';
                    return out;
                }
                const char *e = q;
                while (*e && *e != ',' && *e != '}' && *e != ']') e++;
                size_t n = (size_t)(e - q);
                char *out = (char *)malloc(n + 1);
                memcpy(out, q, n);
                out[n] = '\0';
                return out;
            }
        }
        p++;
        while (*p && *p != '"') { if (*p == '\\' && p[1]) p++; p++; }
        if (*p == '"') p++;
    }
    return nullptr;
}

/* ---- child: the embedding host ---- */

static int child_main(int in_fd, int out_fd) {
    /* Redirect stdin/stdout to the pipe so qwrt_dap (which uses stdin/stdout)
     * talks to the parent. */
    dup2(in_fd, STDIN_FILENO);
    dup2(out_fd, STDOUT_FILENO);
    close(in_fd);
    close(out_fd);

    qwrt_config_t cfg = {};
    cfg.initial_script = kJsProgram;
    /* QWRT_DEBUG env is set by the parent; qwrt_create auto-attaches DAP and
     * blocks on the configuration phase before eval'ing initial_script. */
    qwrt_t *rt = qwrt_create(&cfg);
    if (!rt) return 1;
    qwrt_destroy(rt);
    return 0;
}

/* ---- parent: the DAP client ---- */

static int parent_main(int child_out_fd, int child_in_fd, pid_t pid) {
    FILE *from_child = fdopen(child_out_fd, "r");
    if (!from_child) return 1;

    int failures = 0;
    char *msg;

    /* 1. initialize — expect the `initialized` event (sent at attach) and the
     * initialize response; order isn't guaranteed, so read up to 4 messages. */
    dap_write(child_in_fd,
        "{\"type\":\"request\",\"seq\":1,\"command\":\"initialize\","
        "\"arguments\":{\"adapterID\":\"qwrt\",\"clientID\":\"test\"}}");
    int got_event = 0, got_response = 0;
    for (int tries = 0; tries < 4 && !(got_event && got_response); tries++) {
        msg = dap_read(from_child);
        if (!msg) break;
        if (strstr(msg, "\"event\"") && strstr(msg, "\"initialized\"")) got_event = 1;
        if (strstr(msg, "\"response\"") && strstr(msg, "\"initialize\"")) got_response = 1;
        free(msg);
    }
    if (!got_event) { fprintf(stderr, "FAIL: no initialized event\n"); return 1; }
    if (!got_response) { fprintf(stderr, "FAIL: no initialize response\n"); return 1; }
    fprintf(stderr, "ok: initialized\n");

    /* 2. setBreakpoints at line 3 of "<initial>" (the first x++; x is 1 here,
     * before any increment, so the value is deterministic). */
    dap_write(child_in_fd,
        "{\"type\":\"request\",\"seq\":2,\"command\":\"setBreakpoints\","
        "\"arguments\":{\"source\":{\"path\":\"<initial>\"},"
        "\"breakpoints\":[{\"line\":3}]}}");
    msg = dap_read(from_child);
    if (!msg) { fprintf(stderr, "FAIL: no setBreakpoints response\n"); return 1; }
    if (!strstr(msg, "\"verified\":true")) {
        fprintf(stderr, "FAIL: breakpoint not verified: %s\n", msg);
        failures++;
    }
    free(msg);
    fprintf(stderr, "ok: setBreakpoints\n");

    /* 3. configurationDone — unblocks qwrt_dap_configure; the child evals
     * initial_script, hits the breakpoint, sends `stopped`. */
    dap_write(child_in_fd,
        "{\"type\":\"request\",\"seq\":3,\"command\":\"configurationDone\","
        "\"arguments\":{}}");
    msg = dap_read(from_child);
    if (!msg) { fprintf(stderr, "FAIL: no configurationDone response\n"); return 1; }
    free(msg);

    /* 4. expect stopped at entry (stop_on_entry=true). */
    msg = dap_read(from_child);
    if (!msg || !strstr(msg, "\"stopped\"")) {
        fprintf(stderr, "FAIL: no stopped event: %s\n", msg ? msg : "(null)");
        free(msg);
        return 1;
    }
    free(msg);
    fprintf(stderr, "ok: stopped at entry\n");

    /* continue past entry to hit the breakpoint */
    dap_write(child_in_fd,
        "{\"type\":\"request\",\"seq\":4,\"command\":\"continue\","
        "\"arguments\":{\"threadId\":1}}");
    msg = dap_read(from_child);  /* continue response */
    free(msg);

    msg = dap_read(from_child);
    if (!msg || !strstr(msg, "\"stopped\"") || !strstr(msg, "\"breakpoint\"")) {
        fprintf(stderr, "FAIL: no breakpoint stopped: %s\n", msg ? msg : "(null)");
        free(msg);
        return 1;
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
     * increment; x was just initialized to 1). Frame locals are exposed on a
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

    /* 9. continue to termination — the child evals the rest and exits; there's
     * no explicit `terminated` event in the MVP, so treat child exit (EOF) as
     * termination. */
    dap_write(child_in_fd,
        "{\"type\":\"request\",\"seq\":9,\"command\":\"continue\","
        "\"arguments\":{\"threadId\":1}}");
    msg = dap_read(from_child);
    free(msg);
    while ((msg = dap_read(from_child)) != nullptr) {
        if (strstr(msg, "\"terminated\"")) { fprintf(stderr, "ok: terminated\n"); }
        free(msg);
    }
    fprintf(stderr, "ok: child terminated (via exit)\n");

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
    if (failures != 0) return failures;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return 100;  /* child failed */
    return 0;
}
/* ---- PauseWhileRunning: the bounded uv_run poll must service DAP requests
 * ---- that arrive while the debuggee is NOT paused ---- */

/* Run-mode child: keeps a setInterval alive so a DAP pause request can land
 * mid-run, then exits after a fixed sleep so the parent's pause/continue
 * sequence has time. The qwrt thread runs the uv loop on its own thread while
 * this process sleeps — exactly the state where the periodic DAP poll timer
 * is needed (an idle uv_run would otherwise block forever and never read the
 * pause off stdin). */
static int child_run_main(int in_fd, int out_fd)
{
    dup2(in_fd, STDIN_FILENO);
    dup2(out_fd, STDOUT_FILENO);
    close(in_fd);
    close(out_fd);

    qwrt_config_t cfg = {};
    /* setInterval keeps the event loop alive; the qwrt thread services it. */
    cfg.initial_script = "setInterval(() => {}, 200);\n1;\n";
    qwrt_t *rt = qwrt_create(&cfg);
    if (!rt) return 1;
    usleep(3 * 1000 * 1000);   /* give the parent time to pause us mid-run */
    qwrt_destroy(rt);
    return 0;
}

static int pause_parent_main(int child_out_fd, int child_in_fd, pid_t pid)
{
    FILE *from_child = fdopen(child_out_fd, "r");
    if (!from_child) return 1;
    char *msg;

    /* 1. initialize */
    dap_write(child_in_fd,
        "{\"type\":\"request\",\"seq\":1,\"command\":\"initialize\","
        "\"arguments\":{\"adapterID\":\"qwrt\",\"clientID\":\"test\"}}");
    int got_event = 0, got_response = 0;
    for (int tries = 0; tries < 4 && !(got_event && got_response); tries++) {
        msg = dap_read(from_child);
        if (!msg) break;
        if (strstr(msg, "\"event\"") && strstr(msg, "\"initialized\"")) got_event = 1;
        if (strstr(msg, "\"response\"") && strstr(msg, "\"initialize\"")) got_response = 1;
        free(msg);
    }
    if (!got_event || !got_response) {
        fprintf(stderr, "FAIL: no initialized/response\n");
        return 1;
    }
    fprintf(stderr, "ok: initialized\n");

    /* 2. configurationDone */
    dap_write(child_in_fd,
        "{\"type\":\"request\",\"seq\":2,\"command\":\"configurationDone\","
        "\"arguments\":{}}");
    msg = dap_read(from_child);
    if (!msg) { fprintf(stderr, "FAIL: no configurationDone response\n"); return 1; }
    free(msg);

    /* 3. stopped at entry (stop_on_entry is on) */
    msg = dap_read(from_child);
    if (!msg || !strstr(msg, "\"stopped\"")) {
        fprintf(stderr, "FAIL: no entry stopped: %s\n", msg ? msg : "(null)");
        free(msg);
        return 1;
    }
    free(msg);
    fprintf(stderr, "ok: stopped at entry\n");

    /* 4. continue past entry — the script finishes evaluating and the qwrt
     * thread settles into uv_run with only the setInterval + DAP poll timers. */
    dap_write(child_in_fd,
        "{\"type\":\"request\",\"seq\":3,\"command\":\"continue\","
        "\"arguments\":{\"threadId\":1}}");
    msg = dap_read(from_child);
    free(msg);

    /* 5. pause mid-run: nothing is paused, so this request sits on stdin until
     * the periodic DAP poll timer wakes the idle uv_run. It must arm the next
     * dispatch checkpoint and we must get stopped(reason "pause"). */
    dap_write(child_in_fd,
        "{\"type\":\"request\",\"seq\":4,\"command\":\"pause\","
        "\"arguments\":{\"threadId\":1}}");
    msg = dap_read(from_child);   /* pause response */
    if (!msg || strstr(msg, "\"success\":false")) {
        fprintf(stderr, "FAIL: pause not accepted: %s\n", msg ? msg : "(null)");
        free(msg);
        return 1;
    }
    free(msg);
    msg = dap_read(from_child);
    if (!msg || !strstr(msg, "\"stopped\"") || !strstr(msg, "\"pause\"")) {
        fprintf(stderr, "FAIL: no pause stopped: %s\n", msg ? msg : "(null)");
        free(msg);
        return 1;
    }
    free(msg);
    fprintf(stderr, "ok: paused while running (uv_run bounded poll)\n");

    /* 6. continue to let the child finish (its sleep elapses, then destroy) */
    dap_write(child_in_fd,
        "{\"type\":\"request\",\"seq\":5,\"command\":\"continue\","
        "\"arguments\":{\"threadId\":1}}");
    msg = dap_read(from_child);
    free(msg);
    while ((msg = dap_read(from_child)) != nullptr) free(msg);
    fprintf(stderr, "ok: child terminated\n");

    fclose(from_child);
    close(child_in_fd);
    signal(SIGPIPE, SIG_IGN);
    int status = 0;
    waitpid(pid, &status, 0);
    signal(SIGPIPE, SIG_DFL);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return 100;
    return 0;
}

TEST(DapDebugger, PauseWhileRunning) {
    int to_child[2], from_child[2];
    ASSERT_EQ(0, pipe(to_child));
    ASSERT_EQ(0, pipe(from_child));

    pid_t pid = fork();
    ASSERT_GE(pid, 0);

    if (pid == 0) {
        close(to_child[1]);
        close(from_child[0]);
        setenv("QWRT_DEBUG", "1", 1);
        const char *trace = getenv("QWRT_DAP_TRACE");
        if (trace) { freopen(trace, "w", stderr); }
        int rc = child_run_main(to_child[0], from_child[1]);
        _exit(rc);
    }

    close(to_child[0]);
    close(from_child[1]);
    int rc = pause_parent_main(from_child[0], to_child[1], pid);
    if (rc == 100) {
        ADD_FAILURE() << "child exited non-zero";
    } else if (rc != 0) {
        ADD_FAILURE() << rc << " DAP assertion(s) failed";
    }
}


TEST(DapDebugger, BreakpointFlow) {
    int to_child[2], from_child[2];
    ASSERT_EQ(0, pipe(to_child));
    ASSERT_EQ(0, pipe(from_child));

    pid_t pid = fork();
    ASSERT_GE(pid, 0);

    if (pid == 0) {
        /* child: read from to_child[0], write to from_child[1] */
        close(to_child[1]);
        close(from_child[0]);
        setenv("QWRT_DEBUG", "1", 1);
        const char *trace = getenv("QWRT_DAP_TRACE");
        if (trace) { freopen(trace, "w", stderr); }
        int rc = child_main(to_child[0], from_child[1]);
        _exit(rc);
    }

    /* parent: write to to_child[1], read from from_child[0] */
    close(to_child[0]);
    close(from_child[1]);
    int rc = parent_main(from_child[0], to_child[1], pid);
    if (rc == 100) {
        ADD_FAILURE() << "child exited non-zero";
    } else if (rc != 0) {
        ADD_FAILURE() << rc << " DAP assertion(s) failed";
    }
}

#else /* !QWRT_DEBUG_SUPPORT */

TEST(DapDebugger, DisabledWithoutDebuggerBuild) {
    GTEST_SKIP() << "QWRT_BUILD_DEBUGGER=OFF — DAP tests not built";
}

#endif /* QWRT_DEBUG_SUPPORT */

/*
 * qwrt — DAP (Debug Adapter Protocol) front-end implementation
 *
 * PAL-agnostic. Provides the DAP callback set + base-protocol I/O + a minimal
 * JSON parser/serializer that qwrt_create installs when debugging is enabled.
 * The DAP stdin pump runs inside on_stopped when JS pauses.
 *
 * Threading: single-threaded. on_stopped blocks reading DAP requests until a
 * flow command (continue/step) sets the step mode and returns, unblocking the
 * interrupt handler → JS resumes.
 *
 * Compiled in only when QWRT_DEBUG_SUPPORT is defined (QWRT_BUILD_DEBUGGER=ON).
 */
#include "qwrt_internal.h"

#ifdef QWRT_DEBUG_SUPPORT

#include "qwrt/qwrt_debug.h"
#include "qwrt/qwrt_debug_dap.h"
#include <quickjs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <poll.h>
#include <unistd.h>

/* ================================================================
 * Minimal JSON (parser + serializer)
 *
 * Hand-rolled to keep protocol I/O independent of the debuggee JS/GC state.
 * Only handles the subset DAP uses: object, array, string, number, bool, null.
 * ================================================================ */

typedef struct {
    const char *p;    /* current parse cursor */
    const char *end;
    int error;
} json_parser;

/* forward decls */
static void json_parse_string(json_parser *j, char **out);
static void json_emit(char **buf, size_t *cap, size_t *len, const char *s, size_t n);

/* Append bytes to a growable buffer. */
static void json_emit(char **buf, size_t *cap, size_t *len, const char *s, size_t n)
{
    if (*len + n + 1 > *cap) {
        size_t nc = *cap ? *cap : 64;
        while (nc < *len + n + 1) nc *= 2;
        char *nb = realloc(*buf, nc);
        if (!nb) { return; }  /* OOM: best effort, drop */
        *buf = nb;
        *cap = nc;
    }
    memcpy(*buf + *len, s, n);
    *len += n;
    (*buf)[*len] = '\0';
}

/* Append a C string literal (auto-length via sizeof — no magic numbers). */
#define je_lit(buf, cap, len, lit) json_emit((buf), (cap), (len), (lit), sizeof((lit)) - 1)

/* Append a JSON string literal of [str,n] (with escaping). */
static void json_emit_string(char **buf, size_t *cap, size_t *len,
                             const char *str, size_t n)
{
    je_lit(buf, cap, len, "\"");
    size_t i;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)str[i];
        switch (c) {
        case '"': je_lit(buf, cap, len, "\\\""); break;
        case '\\': je_lit(buf, cap, len, "\\\\"); break;
        case '\n': je_lit(buf, cap, len, "\\n"); break;
        case '\r': je_lit(buf, cap, len, "\\r"); break;
        case '\t': je_lit(buf, cap, len, "\\t"); break;
        case '\b': je_lit(buf, cap, len, "\\b"); break;
        case '\f': je_lit(buf, cap, len, "\\f"); break;
        default:
            if (c < 0x20) {
                char esc[8];
                int m = snprintf(esc, sizeof(esc), "\\u%04x", c);
                json_emit(buf, cap, len, esc, (size_t)m);
            } else {
                json_emit(buf, cap, len, (const char *)&str[i], 1);
            }
        }
    }
    je_lit(buf, cap, len, "\"");
}

/* Parse a "..." string literal (j->p on the opening "). On success j->p is
 * past the closing "; *out is a malloc'd, unescaped C string. */
static void json_parse_string(json_parser *j, char **out)
{
    *out = NULL;
    if (j->p >= j->end || *j->p != '"') { j->error = 1; return; }
    j->p++;
    size_t cap = 16, len = 0;
    char *buf = malloc(cap);
    if (!buf) { j->error = 1; return; }
    while (j->p < j->end && *j->p != '"') {
        char c = *j->p++;
        if (c == '\\' && j->p < j->end) {
            char e = *j->p++;
            switch (e) {
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case 't': c = '\t'; break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case '"': c = '"'; break;
            case '\\': c = '\\'; break;
            case '/': c = '/'; break;
            case 'u': {
                if (j->p + 4 <= j->end) {
                    unsigned int u = 0; int k;
                    for (k = 0; k < 4; k++) {
                        char h = j->p[k];
                        u <<= 4;
                        if (h >= '0' && h <= '9') u |= (unsigned)(h - '0');
                        else if (h >= 'a' && h <= 'f') u |= (unsigned)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') u |= (unsigned)(h - 'A' + 10);
                        else { j->error = 1; free(buf); return; }
                    }
                    j->p += 4;
                    if (u < 0x80) {
                        c = (char)u;
                    } else {
                        if (len + 4 >= cap) {
                            while (len + 4 >= cap) cap *= 2;
                            char *nb = realloc(buf, cap);
                            if (!nb) { j->error = 1; free(buf); return; }
                            buf = nb;
                        }
                        if (u < 0x800) {
                            buf[len++] = (char)(0xC0 | (u >> 6));
                            buf[len++] = (char)(0x80 | (u & 0x3F));
                        } else {
                            buf[len++] = (char)(0xE0 | (u >> 12));
                            buf[len++] = (char)(0x80 | ((u >> 6) & 0x3F));
                            buf[len++] = (char)(0x80 | (u & 0x3F));
                        }
                        continue;
                    }
                } else { j->error = 1; free(buf); return; }
                break;
            }
            default: c = e; break;
            }
        }
        if (len + 1 >= cap) {
            cap *= 2;
            char *nb = realloc(buf, cap);
            if (!nb) { j->error = 1; free(buf); return; }
            buf = nb;
        }
        buf[len++] = c;
    }
    if (j->p >= j->end || *j->p != '"') { j->error = 1; free(buf); return; }
    j->p++;
    buf[len] = '\0';
    *out = buf;
}

/* Lookup a field in a JSON object string: finds "key" : <value>. Returns a
 * malloc'd copy of the value: for strings, the raw unquoted+unescaped text;
 * for objects/arrays, the verbatim JSON substring; for scalars, the token.
 * NULL if not found. */
static char *json_get(const char *json, const char *key)
{
    if (!json || !key) return NULL;
    size_t klen = strlen(key);
    const char *p = json;
    while ((p = strstr(p, "\"")) != NULL) {
        if (strncmp(p + 1, key, klen) == 0 && p[1 + klen] == '"') {
            const char *q = p + 1 + klen + 1;
            while (*q && isspace((unsigned char)*q)) q++;
            if (*q == ':') {
                q++;
                while (*q && isspace((unsigned char)*q)) q++;
                if (*q == '"') {
                    /* string value: unquote + unescape */
                    json_parser j = { q, json + strlen(json), 0 };
                    char *out = NULL;
                    json_parse_string(&j, &out);
                    if (j.error) { free(out); return NULL; }
                    return out;
                } else if (*q == '{' || *q == '[') {
                    /* object/array value: copy verbatim, tracking depth/strings */
                    int depth = 0, in_str = 0;
                    const char *start = q;
                    do {
                        char ch = *q;
                        if (in_str) {
                            if (ch == '\\' && q[1]) { q += 2; continue; }
                            if (ch == '"') in_str = 0;
                            q++;
                        } else {
                            if (ch == '"') in_str = 1;
                            else if (ch == '{' || ch == '[') depth++;
                            else if (ch == '}' || ch == ']') depth--;
                            q++;
                        }
                    } while (*q && depth > 0);
                    size_t n = (size_t)(q - start);
                    char *out = malloc(n + 1);
                    if (!out) return NULL;
                    memcpy(out, start, n);
                    out[n] = '\0';
                    return out;
                } else {
                    /* number/bool/null token */
                    const char *e = q;
                    while (*e && *e != ',' && *e != '}' && *e != ']' &&
                           !isspace((unsigned char)*e)) e++;
                    size_t n = (size_t)(e - q);
                    char *out = malloc(n + 1);
                    if (!out) return NULL;
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

/* Lookup a numeric field. Returns 0 if found, -1 otherwise; value in *out. */
static int json_get_int(const char *json, const char *key, long *out)
{
    char *v = json_get(json, key);
    if (!v) return -1;
    *out = strtol(v, NULL, 10);
    free(v);
    return 0;
}

/* ================================================================
 * DAP session state
 * ================================================================ */

typedef struct qwrt_dap {
    qwrt_t *rt;
    qwrt_debug_t *dbg;
    FILE *in;
    FILE *out;
    int seq;             /* outbound message sequence counter */
    int configured;      /* 1 after configurationDone */
} qwrt_dap_t;

/* ================================================================
 * DAP message sending
 * ================================================================ */

static void dap_send(qwrt_dap_t *d, const char *json)
{
    size_t n = strlen(json);
    fprintf(d->out, "Content-Length: %zu\r\n\r\n%s", n, json);
    fflush(d->out);
}

/* Build & send an event: {"type":"event","event":name,"body":body,...} */
static void dap_send_event(qwrt_dap_t *d, const char *event, const char *body_json)
{
    char *buf = NULL; size_t cap = 0, len = 0;
    je_lit(&buf, &cap, &len, "{\"type\":\"event\",\"seq\":");
    char seqbuf[32];
    int m = snprintf(seqbuf, sizeof(seqbuf), "%d", ++d->seq);
    json_emit(&buf, &cap, &len, seqbuf, (size_t)m);
    je_lit(&buf, &cap, &len, ",\"event\":\"");
    json_emit(&buf, &cap, &len, event, strlen(event));
    je_lit(&buf, &cap, &len, "\"");
    if (body_json && body_json[0]) {
        je_lit(&buf, &cap, &len, ",\"body\":");
        json_emit(&buf, &cap, &len, body_json, strlen(body_json));
    }
    je_lit(&buf, &cap, &len, "}");
    dap_send(d, buf);
    free(buf);
}

/* Build & send a response: type "response", success, command, body, message. */
static void dap_send_response(qwrt_dap_t *d, int request_seq, const char *command,
                              int success, const char *body_json, const char *error_msg)
{
    char *buf = NULL; size_t cap = 0, len = 0;
    je_lit(&buf, &cap, &len, "{\"type\":\"response\",\"request_seq\":");
    char tmp[32];
    int m = snprintf(tmp, sizeof(tmp), "%d", request_seq);
    json_emit(&buf, &cap, &len, tmp, (size_t)m);
    je_lit(&buf, &cap, &len, ",\"seq\":");
    m = snprintf(tmp, sizeof(tmp), "%d", ++d->seq);
    json_emit(&buf, &cap, &len, tmp, (size_t)m);
    je_lit(&buf, &cap, &len, ",\"command\":\"");
    json_emit(&buf, &cap, &len, command, strlen(command));
    je_lit(&buf, &cap, &len, "\",\"success\":");
    json_emit(&buf, &cap, &len, success ? "true" : "false", success ? 4 : 5);
    if (body_json && body_json[0]) {
        je_lit(&buf, &cap, &len, ",\"body\":");
        json_emit(&buf, &cap, &len, body_json, strlen(body_json));
    }
    if (error_msg) {
        je_lit(&buf, &cap, &len, ",\"message\":");
        json_emit_string(&buf, &cap, &len, error_msg, strlen(error_msg));
    }
    je_lit(&buf, &cap, &len, "}");
    dap_send(d, buf);
    free(buf);
}

/* ================================================================
 * DAP base protocol read
 * ================================================================ */

/* Read one DAP message (Content-Length header + JSON body). Returns malloc'd
 * JSON string (caller frees), or NULL on EOF/error. Sets *out_seq to the
 * request seq, *out_command to a malloc'd command string. */
static char *dap_read_message(qwrt_dap_t *d, int *out_seq, char **out_command,
                              char **out_arguments)
{
    *out_seq = 0;
    *out_command = NULL;
    *out_arguments = NULL;
    /* Read headers until blank line. */
    long content_length = -1;
    char line[1024];
    while (fgets(line, sizeof(line), d->in)) {
        /* strip CRLF */
        size_t L = strlen(line);
        while (L > 0 && (line[L-1] == '\n' || line[L-1] == '\r')) line[--L] = '\0';
        if (L == 0) break;  /* end of headers */
        if (strncmp(line, "Content-Length:", 15) == 0) {
            content_length = strtol(line + 15, NULL, 10);
        }
    }
    if (feof(d->in)) return NULL;
    if (content_length < 0) return NULL;
    char *body = malloc((size_t)content_length + 1);
    if (!body) return NULL;
    if (fread(body, 1, (size_t)content_length, d->in) != (size_t)content_length) {
        free(body);
        return NULL;
    }
    body[content_length] = '\0';

    {
        char *s = json_get(body, "seq");
        if (s) { *out_seq = (int)strtol(s, NULL, 10); free(s); }
    }
    *out_command = json_get(body, "command");
    *out_arguments = json_get(body, "arguments");
    return body;
}

/* ================================================================
 * on_stopped pump — the paused DAP request loop
 * ================================================================ */

/* file-static active session (single-threaded, single-session is fine for MVP).
 * Defined below; on_stopped recovers the session from here. */
static qwrt_dap_t *g_qwrt_dap_active = NULL;

/* Drive one non-blocking iteration of the PAL event loop so async JS
 * (fetch/setTimeout) can advance while the debugger is paused. Single-threaded:
 * runs on the JS thread (we're inside on_stopped, inside JS_Eval). The PAL's
 * run_cycle pumps I/O and enqueues deferred JS callbacks; qwrt_tick drains them.
 *
 * Re-entrancy: PAL-driven JS may hit a breakpoint → on_dispatch. The debug core
 * checks dbg->stopped and skips nested stops while already paused (debugger.c),
 * so this won't recursively enter on_stopped. */
static void dap_pump_pal(qwrt_dap_t *d)
{
    if (!d || !d->rt) return;
    qwrt_ctx_t *ctx = qwrt_get_active_ctx(d->rt);
    if (!ctx || !ctx->pal) return;
    const qwrt_pal_t *pal = ctx->pal;
    if (pal->run_cycle) {
        pal->run_cycle((qwrt_pal_t *)pal, 0);  /* non-blocking */
    }
    qwrt_tick(d->rt);
}

/* Poll stdin for a DAP message with a timeout. Returns:
 *   1 = message available (call dap_read_message to get it)
 *   0 = timeout (no message yet — caller can pump PAL)
 *  -1 = EOF / error */
static int dap_poll_message(qwrt_dap_t *d, int timeout_ms)
{
    if (!d || !d->in) return -1;
    int fd = fileno(d->in);
    if (fd < 0) return -1;
    struct pollfd pfd = { fd, POLLIN, 0 };
    int rc = poll(&pfd, 1, timeout_ms);
    if (rc < 0) return -1;
    if (rc == 0) return 0;  /* timeout */
    if (pfd.revents & (POLLERR | POLLNVAL | POLLHUP)) return -1;
    return 1;  /* readable */
}

/* Forward: handle a single DAP request; returns 1 if it was a flow command
 * (continue/step/stop) that should end the paused pump, 0 otherwise. */
static int dap_handle_request(qwrt_dap_t *d, const char *command,
                              const char *args, int req_seq);

/* The DAP callback for on_stopped. Pumps DAP requests until a flow command.
 * Between DAP polls, drives the PAL event loop so async JS advances while
 * paused (single-threaded — no separate loop thread). */
static void dap_on_stopped(qwrt_debug_t *dbg, const char *reason, int thread_id)
{
    (void)dbg; (void)thread_id;
    qwrt_dap_t *d = g_qwrt_dap_active;
    if (!d) return;

    /* emit stopped event */
    char body[256];
    snprintf(body, sizeof(body),
        "{\"reason\":\"%s\",\"threadId\":1,\"allThreadsStopped\":true}", reason);
    dap_send_event(d, "stopped", body);

    /* pump until a flow command */
    for (;;) {
        int pr = dap_poll_message(d, 50);  /* 50ms poll */
        if (pr < 0) break;                 /* EOF */
        if (pr == 0) {
            /* no DAP message yet — advance async JS while we wait */
            dap_pump_pal(d);
            continue;
        }
        int req_seq = 0; char *cmd = NULL, *args = NULL;
        char *msg = dap_read_message(d, &req_seq, &cmd, &args);
        if (!msg) break;
        if (cmd) {
            if (dap_handle_request(d, cmd, args, req_seq)) {
                free(msg); free(cmd); free(args);
                break;
            }
        }
        free(msg); free(cmd); free(args);
    }
}

/* ================================================================
 * Request handlers
 * ================================================================ */

/* Each returns 1 if it's a flow command (continue/step/stop) that ends the
 * paused pump, 0 otherwise. */

static int dap_handle_request(qwrt_dap_t *d, const char *command,
                              const char *args, int req_seq)
{
    if (strcmp(command, "continue") == 0) {
        qwrt_debug_continue(d->dbg);
        dap_send_response(d, req_seq, "continue", 1, "{\"allThreadsContinued\":true}", NULL);
        return 1;
    }
    if (strcmp(command, "next") == 0) {
        qwrt_debug_step_over(d->dbg);
        dap_send_response(d, req_seq, "next", 1, "{}", NULL);
        return 1;
    }
    if (strcmp(command, "stepIn") == 0) {
        qwrt_debug_step_into(d->dbg);
        dap_send_response(d, req_seq, "stepIn", 1, "{}", NULL);
        return 1;
    }
    if (strcmp(command, "stepOut") == 0) {
        qwrt_debug_step_out(d->dbg);
        dap_send_response(d, req_seq, "stepOut", 1, "{}", NULL);
        return 1;
    }
    if (strcmp(command, "pause") == 0) {
        qwrt_debug_pause(d->dbg);
        dap_send_response(d, req_seq, "pause", 1, "{}", NULL);
        return 0;
    }
    if (strcmp(command, "threads") == 0) {
        dap_send_response(d, req_seq, "threads", 1,
            "{\"threads\":[{\"id\":1,\"name\":\"main\"}]}", NULL);
        return 0;
    }
    if (strcmp(command, "stackTrace") == 0) {
        qwrt_debug_frame *frames = NULL; int n = 0;
        qwrt_debug_get_call_frames(d->dbg, &frames, &n);
        char *buf = NULL; size_t cap = 0, len = 0;
        je_lit(&buf, &cap, &len, "{\"stackFrames\":[");
        int i;
        for (i = 0; i < n; i++) {
            if (i) je_lit(&buf, &cap, &len, ",");
            char fb[512];
            int m = snprintf(fb, sizeof(fb),
                "{\"id\":%d,\"name\":", frames[i].id);
            json_emit(&buf, &cap, &len, fb, (size_t)m);
            json_emit_string(&buf, &cap, &len,
                frames[i].name ? frames[i].name : "<anonymous>",
                frames[i].name ? strlen(frames[i].name) : 12);
            m = snprintf(fb, sizeof(fb),
                ",\"line\":%d,\"column\":%d,\"source\":{\"path\":",
                frames[i].line, frames[i].column);
            json_emit(&buf, &cap, &len, fb, (size_t)m);
            json_emit_string(&buf, &cap, &len,
                frames[i].source_path ? frames[i].source_path : "",
                frames[i].source_path ? strlen(frames[i].source_path) : 0);
            je_lit(&buf, &cap, &len, "}}");
        }
        je_lit(&buf, &cap, &len, "],\"totalFrames\":");
        char tb[32]; int tm = snprintf(tb, sizeof(tb), "%d", n);
        json_emit(&buf, &cap, &len, tb, (size_t)tm);
        je_lit(&buf, &cap, &len, "}");
        dap_send_response(d, req_seq, "stackTrace", 1, buf, NULL);
        free(buf);
        qwrt_debug_free_frames(frames, n);
        return 0;
    }
    if (strcmp(command, "scopes") == 0) {
        long fid = 0;
        json_get_int(args, "frameId", &fid);
        qwrt_debug_scope *scopes = NULL; int n = 0;
        int rc = qwrt_debug_get_scopes(d->dbg, (int)fid, &scopes, &n);
        if (rc < 0) {
            dap_send_response(d, req_seq, "scopes", 1, "{\"scopes\":[]}", NULL);
            return 0;
        }
        char *buf = NULL; size_t cap = 0, len = 0;
        je_lit(&buf, &cap, &len, "{\"scopes\":[");
        int i;
        for (i = 0; i < n; i++) {
            if (i) je_lit(&buf, &cap, &len, ",");
            char fb[256];
            int m = snprintf(fb, sizeof(fb),
                "{\"name\":");
            json_emit(&buf, &cap, &len, fb, (size_t)m);
            json_emit_string(&buf, &cap, &len, scopes[i].name, strlen(scopes[i].name));
            m = snprintf(fb, sizeof(fb),
                ",\"variablesReference\":%d,\"expensive\":%s}",
                scopes[i].variables_reference,
                scopes[i].expensive ? "true" : "false");
            json_emit(&buf, &cap, &len, fb, (size_t)m);
        }
        je_lit(&buf, &cap, &len, "]}");
        dap_send_response(d, req_seq, "scopes", 1, buf, NULL);
        free(buf);
        qwrt_debug_free_scopes(scopes, n);
        return 0;
    }
    if (strcmp(command, "variables") == 0) {
        long vr = 0;
        json_get_int(args, "variablesReference", &vr);
        qwrt_debug_var *vars = NULL; int n = 0;
        int rc = qwrt_debug_get_variables(d->dbg, (int)vr, &vars, &n);
        if (rc < 0) {
            dap_send_response(d, req_seq, "variables", 1, "{\"variables\":[]}", NULL);
            return 0;
        }
        char *buf = NULL; size_t cap = 0, len = 0;
        je_lit(&buf, &cap, &len, "{\"variables\":[");
        int i;
        for (i = 0; i < n; i++) {
            if (i) je_lit(&buf, &cap, &len, ",");
            je_lit(&buf, &cap, &len, "{\"name\":");
            json_emit_string(&buf, &cap, &len, vars[i].name, vars[i].name ? strlen(vars[i].name) : 0);
            je_lit(&buf, &cap, &len, ",\"value\":");
            json_emit_string(&buf, &cap, &len,
                vars[i].value_json ? vars[i].value_json : "undefined",
                vars[i].value_json ? strlen(vars[i].value_json) : 9);
            char fb[64];
            int m = snprintf(fb, sizeof(fb), ",\"variablesReference\":0,\"type\":\"%s\"}",
                vars[i].type ? vars[i].type : "object");
            json_emit(&buf, &cap, &len, fb, (size_t)m);
        }
        je_lit(&buf, &cap, &len, "]}");
        dap_send_response(d, req_seq, "variables", 1, buf, NULL);
        free(buf);
        qwrt_debug_free_vars(vars, n);
        return 0;
    }
    if (strcmp(command, "evaluate") == 0) {
        char *expr = json_get(args, "expression");
        long fid = 0;
        json_get_int(args, "frameId", &fid);
        char *val = NULL, *err = NULL;
        int rc = qwrt_debug_evaluate(d->dbg, (int)fid, expr ? expr : "", &val, &err);
        if (rc == 0 && val) {
            char *buf = NULL; size_t cap = 0, len = 0;
            je_lit(&buf, &cap, &len, "{\"result\":");
            json_emit_string(&buf, &cap, &len, val, strlen(val));
            je_lit(&buf, &cap, &len, ",\"variablesReference\":0}");
            dap_send_response(d, req_seq, "evaluate", 1, buf, NULL);
            free(buf);
        } else {
            char *buf = NULL; size_t cap = 0, len = 0;
            je_lit(&buf, &cap, &len, "{\"result\":");
            json_emit_string(&buf, &cap, &len, err ? err : "error", err ? strlen(err) : 5);
            je_lit(&buf, &cap, &len, ",\"variablesReference\":0}");
            dap_send_response(d, req_seq, "evaluate", 0, buf, err ? err : "evaluate failed");
            free(buf);
        }
        free(expr); free(val); free(err);
        return 0;
    }
    if (strcmp(command, "disconnect") == 0) {
        dap_send_response(d, req_seq, "disconnect", 1, "{}", NULL);
        qwrt_debug_continue(d->dbg);  /* unblock so JS can exit */
        return 1;
    }
    /* unknown / unsupported (setExceptionBreakpoints, setFunctionBreakpoints,
     * source, etc.) — acknowledge success to keep VS Code happy. */
    dap_send_response(d, req_seq, command, 1, "{}", NULL);
    return 0;
}

/* ================================================================
 * Attach / main loop
 * ================================================================ */

int qwrt_dap_attach(qwrt_t *rt, const qwrt_dap_config_t *cfg)
{
    if (!rt) return -1;
    qwrt_dap_t *d = calloc(1, sizeof(*d));
    if (!d) return -1;
    d->rt = rt;
    d->in = (cfg && cfg->in) ? cfg->in : stdin;
    d->out = (cfg && cfg->out) ? cfg->out : stdout;
    d->seq = 0;

    qwrt_debug_cbs cbs;
    memset(&cbs, 0, sizeof(cbs));
    cbs.on_stopped = dap_on_stopped;
    d->dbg = qwrt_debug_attach(rt, &cbs);
    if (!d->dbg) { free(d); return -1; }

    if (cfg && cfg->stop_on_entry)
        qwrt_debug_stop_on_entry(d->dbg);

    g_qwrt_dap_active = d;

    /* Send initialized event so VS Code knows it can configure breakpoints. */
    dap_send_event(d, "initialized", NULL);
    return 0;
}

void qwrt_dap_detach(qwrt_t *rt)
{
    (void)rt;
    if (g_qwrt_dap_active) {
        if (g_qwrt_dap_active->dbg)
            qwrt_debug_detach(g_qwrt_dap_active->rt, g_qwrt_dap_active->dbg);
        free(g_qwrt_dap_active);
        g_qwrt_dap_active = NULL;
    }
}

/* Process DAP requests that arrive BEFORE the program starts running (the
 * configuration phase: initialize, setBreakpoints, attach, configurationDone).
 * Called by the host (qwrt_create auto-attach path) after qwrt_dap_attach.
 * Returns when configurationDone is received. */
int qwrt_dap_configure(qwrt_t *rt)
{
    (void)rt;
    qwrt_dap_t *d = g_qwrt_dap_active;
    if (!d) return -1;
    for (;;) {
        int req_seq = 0; char *cmd = NULL, *args = NULL;
        char *msg = dap_read_message(d, &req_seq, &cmd, &args);
        if (!msg) return -1;
        if (!cmd) { free(msg); continue; }

        if (strcmp(cmd, "initialize") == 0) {
            dap_send_response(d, req_seq, "initialize", 1,
                "{\"supportsConfigurationDoneRequest\":true,"
                "\"supportsEvaluateForHovers\":true,"
                "\"supportsStepBack\":false,"
                "\"supportsSetVariable\":false}", NULL);
        } else if (strcmp(cmd, "attach") == 0) {
            dap_send_response(d, req_seq, "attach", 1, "{}", NULL);
        } else if (strcmp(cmd, "setBreakpoints") == 0) {
            /* args.source.path + args.breakpoints[].line */
            char *path = json_get(args, "path");
            /* parse breakpoints array: each element is { "line":N, "condition":"..." } */
            qwrt_debug_clear_breakpoints(d->dbg);
            if (path) {
                const char *bp = strstr(args ? args : "", "\"breakpoints\"");
                if (bp) {
                    const char *p = strchr(bp, '[');
                    const char *e = p ? strchr(p, ']') : NULL;
                    const char *q = p ? p + 1 : NULL;
                    while (q && (!e || q < e)) {
                        /* find the next breakpoint object '{' */
                        const char *obj = strchr(q, '{');
                        if (!obj || (e && obj >= e)) break;
                        const char *obje = strchr(obj, '}');
                        if (!obje) break;
                        /* extract line within [obj, obje] */
                        const char *lp = strstr(obj, "\"line\"");
                        if (lp && lp < obje) {
                            lp = strchr(lp, ':');
                            if (lp && lp < obje) {
                                long ln = strtol(lp + 1, NULL, 10);
                                if (ln > 0) {
                                    /* extract optional condition within this object */
                                    char *cond = NULL;
                                    const char *cp = strstr(obj, "\"condition\"");
                                    if (cp && cp < obje) {
                                        cp = strchr(cp, ':');
                                        if (cp && cp < obje) {
                                            /* parse the string value */
                                            const char *cs = cp + 1;
                                            while (*cs && (*cs == ' ' || *cs == '\t')) cs++;
                                            if (*cs == '"') {
                                                json_parser jp = { cs, obje, 0 };
                                                json_parse_string(&jp, &cond);
                                            }
                                        }
                                    }
                                    qwrt_debug_add_breakpoint(d->dbg, path, (int)ln, cond);
                                    free(cond);
                                }
                            }
                        }
                        q = obje + 1;
                    }
                }
            }
            /* respond with verified breakpoints (echo lines) */
            char *buf = NULL; size_t cap = 0, len = 0;
            je_lit(&buf, &cap, &len, "{\"breakpoints\":[");
            int first = 1;
            if (path) {
                const char *bp = strstr(args ? args : "", "\"breakpoints\"");
                if (bp) {
                    const char *p = strchr(bp, '[');
                    const char *e = p ? strchr(p, ']') : NULL;
                    const char *q = p ? p + 1 : NULL;
                    while (q && (!e || q < e)) {
                        const char *obj = strchr(q, '{');
                        if (!obj || (e && obj >= e)) break;
                        const char *obje = strchr(obj, '}');
                        if (!obje) break;
                        const char *lp = strstr(obj, "\"line\"");
                        if (lp && lp < obje) {
                            lp = strchr(lp, ':');
                            if (lp && lp < obje) {
                                long ln = strtol(lp + 1, NULL, 10);
                                if (!first) je_lit(&buf, &cap, &len, ",");
                                first = 0;
                                char fb[64];
                                int m = snprintf(fb, sizeof(fb), "{\"verified\":true,\"line\":%ld}", ln);
                                json_emit(&buf, &cap, &len, fb, (size_t)m);
                            }
                        }
                        q = obje + 1;
                    }
                }
            }
            je_lit(&buf, &cap, &len, "]}");
            dap_send_response(d, req_seq, "setBreakpoints", 1, buf, NULL);
            free(buf); free(path);
        } else if (strcmp(cmd, "configurationDone") == 0) {
            dap_send_response(d, req_seq, "configurationDone", 1, "{}", NULL);
            free(msg); free(cmd); free(args);
            return 0;
        } else if (strcmp(cmd, "disconnect") == 0) {
            dap_send_response(d, req_seq, "disconnect", 1, "{}", NULL);
            free(msg); free(cmd); free(args);
            return 1;  /* aborted */
        } else {
            /* acknowledge unknown pre-run requests */
            dap_send_response(d, req_seq, cmd, 1, "{}", NULL);
        }
        free(msg); free(cmd); free(args);
    }
}

#endif /* QWRT_DEBUG_SUPPORT */

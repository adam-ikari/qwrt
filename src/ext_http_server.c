/*
 * qwrt HTTP Server Extension — uvhttp-backed
 *
 * Registers the WinterCG-style `serve(options, handler)` global. The handler
 * receives a WHATWG `Request` and may return a `Response`, a string, or a
 * Promise resolving to one; the runtime serializes it back to the socket.
 *
 * Features:
 *  - HTTP/1.1 over libuv
 *  - HTTPS via mbedTLS (options.tls = {cert, key})
 *  - Static file serving (options.static = {root, index, maxFileSize,...})
 *  - Automatic gzip compression for responses with body >= threshold
 *  - WebSocket support (v2: via ws.onopen/ws.onmessage/ws.onclose hooks)
 */

#include "qwrt_internal.h"
#include "qwrt/ext_http_server.h"
#include <uvhttp.h>
#include <uv.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if QWRT_WITH_HTTPSERVER

#define HTTP_SERVER_HANDLER_KEY "__qwrt_http_server_handler"
#define HTTP_SERVER_PENDING_KEY "__qwrt_http_server_pending"

/* ============ extension state (single active server) ============ */
typedef struct {
    qwrt_t *rt;
    uvhttp_server_t *server;
    uvhttp_tls_context_t *tls_ctx;
    uvhttp_static_context_t *static_ctx;
    int static_enabled;
    int running;
    /* Cached, rooted JS references to avoid per-request global property
     * lookups (JS_GetGlobalObject + JS_GetPropertyStr on every request).
     * Set at serve() time, freed on server_close / ext destroy. */
    JSValue handler;
    JSValue request_cls;
    JSValue headers_cls;
} http_server_state_t;

static http_server_state_t g_state;

/* ============ pending async responses ============ */
typedef struct pending_entry {
    int id;
    uvhttp_connection_t *conn;
    uvhttp_response_t *res;
    uvhttp_request_t *req;
    struct pending_entry *next;
} pending_entry_t;

static pending_entry_t *g_pending = NULL;
static int g_next_id = 1;

/* Called from uvhttp's connection destroy path (single-threaded, same loop).
 * The connection (and its request/response objects) are about to be freed, so
 * drop every pending async response bound to it — otherwise a later Promise
 * resolution would serialize into freed memory (heap-use-after-free). */
static void qwrt_conn_destroy(uvhttp_connection_t *conn) {
    pending_entry_t **pp = &g_pending;
    while (*pp) {
        pending_entry_t *e = *pp;
        if (e->conn == conn) {
            *pp = e->next;
            free(e);
        } else {
            pp = &e->next;
        }
    }
}

static pending_entry_t *pending_add(uvhttp_connection_t *conn,
                                    uvhttp_request_t *req,
                                    uvhttp_response_t *res) {
    pending_entry_t *e = calloc(1, sizeof(*e));
    if (!e) return NULL;
    e->id = g_next_id++;
    e->conn = conn;
    e->req = req;
    e->res = res;
    e->next = g_pending;
    g_pending = e;
    return e;
}

static uvhttp_response_t *pending_take(int id, uvhttp_request_t **out_req) {
    pending_entry_t **pp = &g_pending;
    while (*pp) {
        if ((*pp)->id == id) {
            pending_entry_t *e = *pp;
            uvhttp_response_t *res = e->res;
            if (out_req) *out_req = e->req;
            *pp = e->next;
            free(e);
            return res;
        }
        pp = &(*pp)->next;
    }
    return NULL;
}

/* ============ WebSocket bridge ============ */
/* uvhttp copies uvhttp_ws_handler_t (user_data included) into its ws route
 * table; at handshake it wraps ws_conn->user_data in a {conn, user_handler}
 * struct. We mirror that layout to reach the handler from the callbacks. */
typedef struct {
    void *conn;
    void *user_handler;
} qwrt_ws_wrapper_t;

typedef struct ws_bridge {
    int fn_id; /* index of __qwrt_ws_fn_<n> on the global object */
} ws_bridge_t;

typedef struct ws_entry {
    int id;
    uvhttp_ws_connection_t *conn;
    struct ws_entry *next;
} ws_entry_t;

static ws_entry_t *g_ws_entries = NULL;
static int g_ws_next_id = 1;

static ws_entry_t *ws_find(int id) {
    for (ws_entry_t *e = g_ws_entries; e; e = e->next) {
        if (e->id == id)
            return e;
    }
    return NULL;
}

static ws_entry_t *ws_find_by_conn(uvhttp_ws_connection_t *conn) {
    for (ws_entry_t *e = g_ws_entries; e; e = e->next) {
        if (e->conn == conn)
            return e;
    }
    return NULL;
}

static int ws_register(uvhttp_ws_connection_t *conn) {
    ws_entry_t *e = calloc(1, sizeof(*e));
    if (!e)
        return -1;
    e->id = g_ws_next_id++;
    e->conn = conn;
    e->next = g_ws_entries;
    g_ws_entries = e;
    return e->id;
}

static void ws_unregister(int id) {
    ws_entry_t **pp = &g_ws_entries;
    while (*pp) {
        if ((*pp)->id == id) {
            ws_entry_t *e = *pp;
            *pp = e->next;
            free(e);
            return;
        }
        pp = &(*pp)->next;
    }
}

/* ============ small helpers ============ */
static void send_text(uvhttp_response_t *res, int status, const char *text) {
    uvhttp_response_set_status(res, status);
    uvhttp_response_set_header(res, "Content-Type", "text/plain; charset=utf-8");
    if (text && text[0]) {
        uvhttp_response_set_body(res, text, strlen(text));
    }
    uvhttp_response_send(res);
}

/* ============ Response serialization (JS Response -> uvhttp) ============ */
static void serialize_js_response(JSContext *ctx, uvhttp_response_t *res,
                                  uvhttp_request_t *req, JSValue val) {
    int status = 200;

    if (JS_IsString(val)) {
        uvhttp_response_set_status(res, 200);
        uvhttp_response_set_header(res, "Content-Type",
                                   "text/plain; charset=utf-8");
        size_t len = 0;
        const char *s = JS_ToCStringLen(ctx, &len, val);
        if (s && len) {
            uvhttp_response_set_body(res, s, len);
        }
        if (s) JS_FreeCString(ctx, s);
        /* gzip when the client asks for it and the body is big enough
         * (uvhttp's internal compress_threshold) — same path as the
         * Response-object branch below */
        if (req) {
            const char *accept_enc =
                uvhttp_request_get_header(req, "Accept-Encoding");
            if (accept_enc && strstr(accept_enc, "gzip")) {
                uvhttp_response_set_compress(res, 1);
                uvhttp_response_set_compress_algorithm(res, 1);
            }
        }
        uvhttp_response_send(res);
        return;
    }

    if (!JS_IsObject(val)) {
        send_text(res, 500, "Invalid response type");
        return;
    }

    /* status */
    JSValue sv = JS_GetPropertyStr(ctx, val, "_status");
    if (JS_IsNumber(sv)) {
        JS_ToInt32(ctx, &status, sv);
    }
    JS_FreeValue(ctx, sv);
    uvhttp_response_set_status(res, status);

    /* headers */
    JSValue headers = JS_GetPropertyStr(ctx, val, "_headers");
    if (JS_IsObject(headers)) {
        JSValue map = JS_GetPropertyStr(ctx, headers, "_map");
        if (JS_IsObject(map)) {
            JSValue entries_fn = JS_GetPropertyStr(ctx, map, "entries");
            if (JS_IsFunction(ctx, entries_fn)) {
                JSValue it = JS_Call(ctx, entries_fn, map, 0, NULL);
                for (;;) {
                    JSValue next_fn = JS_GetPropertyStr(ctx, it, "next");
                    if (!JS_IsFunction(ctx, next_fn)) { JS_FreeValue(ctx, next_fn); break; }
                    JSValue r = JS_Call(ctx, next_fn, it, 0, NULL);
                    JS_FreeValue(ctx, next_fn);
                    JSValue done = JS_GetPropertyStr(ctx, r, "done");
                    int d = JS_ToBool(ctx, done);
                    JS_FreeValue(ctx, done);
                    if (d) { JS_FreeValue(ctx, r); break; }
                    JSValue kv = JS_GetPropertyStr(ctx, r, "value");
                    JSValue k = JS_GetPropertyUint32(ctx, kv, 0);
                    JSValue v = JS_GetPropertyUint32(ctx, kv, 1);
                    const char *ks = JS_ToCString(ctx, k);
                    const char *vs = JS_ToCString(ctx, v);
                    if (ks && vs) uvhttp_response_set_header(res, ks, vs);
                    if (ks) JS_FreeCString(ctx, ks);
                    if (vs) JS_FreeCString(ctx, vs);
                    JS_FreeValue(ctx, k); JS_FreeValue(ctx, v);
                    JS_FreeValue(ctx, kv);
                    JS_FreeValue(ctx, r);
                }
                JS_FreeValue(ctx, it);
            }
            JS_FreeValue(ctx, entries_fn);
        }
        JS_FreeValue(ctx, map);
    }
    JS_FreeValue(ctx, headers);

    /* compression: enable gzip when Accept-Encoding includes gzip */
    if (req) {
        const char *accept_enc = uvhttp_request_get_header(req, "Accept-Encoding");
        if (accept_enc && strstr(accept_enc, "gzip")) {
            uvhttp_response_set_compress(res, 1);
            uvhttp_response_set_compress_algorithm(res, 1);
        }
    }

    /* body */
    JSValue body = JS_GetPropertyStr(ctx, val, "_body");
    if (JS_IsString(body)) {
        size_t len = 0;
        const char *s = JS_ToCStringLen(ctx, &len, body);
        if (s && len) {
            uvhttp_response_set_body(res, s, len);
        }
        if (s) JS_FreeCString(ctx, s);
    }
    JS_FreeValue(ctx, body);

    uvhttp_response_send(res);
}

/* ============ WebSocket callbacks (uvhttp ws handler) ============ */

/* helper: read a rooted global value by key */
static JSValue ws_global_get(JSContext *ctx, const char *key) {
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue v = JS_GetPropertyStr(ctx, global, key);
    JS_FreeValue(ctx, global);
    return v;
}

static void ws_global_set(JSContext *ctx, const char *key, JSValue v) {
    /* JS_SetPropertyStr takes ownership of the value; dup so callers keep
     * their own reference */
    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, key, JS_DupValue(ctx, v));
    JS_FreeValue(ctx, global);
}

/* ws.send(data) / ws.close(code, reason) — func_data[0] = ws entry id */
static JSValue ws_js_send(JSContext *ctx, JSValueConst this_val, int argc,
                          JSValueConst *argv, int magic,
                          JSValueConst *func_data) {
    (void)this_val;
    (void)magic;
    int id = 0;
    JS_ToInt32(ctx, &id, (JSValue)func_data[0]);
    ws_entry_t *e = ws_find(id);
    if (!e)
        return JS_UNDEFINED;
    if (argc > 0 && JS_IsString(argv[0])) {
        size_t len = 0;
        const char *s = JS_ToCStringLen(ctx, &len, argv[0]);
        if (s && len)
            uvhttp_server_ws_send(e->conn, s, len);
        if (s)
            JS_FreeCString(ctx, s);
    }
    return JS_UNDEFINED;
}

static JSValue ws_js_close(JSContext *ctx, JSValueConst this_val, int argc,
                           JSValueConst *argv, int magic,
                           JSValueConst *func_data) {
    (void)this_val;
    (void)magic;
    int id = 0;
    JS_ToInt32(ctx, &id, (JSValue)func_data[0]);
    ws_entry_t *e = ws_find(id);
    if (!e)
        return JS_UNDEFINED;
    int code = 1000;
    if (argc > 0 && JS_IsNumber(argv[0]))
        JS_ToInt32(ctx, &code, argv[0]);
    const char *reason = "";
    if (argc > 1 && JS_IsString(argv[1]))
        reason = JS_ToCString(ctx, argv[1]);
    uvhttp_server_ws_close(e->conn, code, reason);
    if (argc > 1 && JS_IsString(argv[1]))
        JS_FreeCString(ctx, reason);
    return JS_UNDEFINED;
}

/* call a user-assigned callback property (onopen/onmessage/onclose/onerror)
 * on the ws object; obj_key is __qwrt_ws_obj_<id> */
static void ws_call_prop(JSContext *ctx, int id, const char *prop,
                         JSValueConst *args, int argc) {
    char key[64];
    snprintf(key, sizeof(key), "__qwrt_ws_obj_%d", id);
    JSValue obj = ws_global_get(ctx, key);
    if (JS_IsObject(obj)) {
        JSValue fn = JS_GetPropertyStr(ctx, obj, prop);
        if (JS_IsFunction(ctx, fn)) {
            JSValue r = JS_Call(ctx, fn, obj, argc, args);
            if (JS_IsException(r))
                JS_FreeValue(ctx, r);
            else
                JS_FreeValue(ctx, r);
        }
        JS_FreeValue(ctx, fn);
    }
    JS_FreeValue(ctx, obj);
}

static int ws_on_connect(uvhttp_ws_connection_t *conn) {
    JSContext *ctx = qwrt_get_active_jsctx(g_state.rt);
    if (!ctx)
        return 0;
    /* reach the bridge through uvhttp's internal {conn, user_handler} wrapper */
    qwrt_ws_wrapper_t *w = (qwrt_ws_wrapper_t *)conn->user_data;
    uvhttp_ws_handler_t *uh = w ? (uvhttp_ws_handler_t *)w->user_handler : NULL;
    ws_bridge_t *br = uh ? (ws_bridge_t *)uh->user_data : NULL;
    if (!br)
        return 0;

    int id = ws_register(conn);
    if (id < 0)
        return 0;

    /* build the JS ws object: { send, close } with callbacks assigned by JS */
    JSValue obj = JS_NewObject(ctx);
    JSValue id_v = JS_NewInt32(ctx, id);
    JSValue send_fn = JS_NewCFunctionData(ctx, ws_js_send, 1, 0, 1,
                                          (JSValueConst[]){id_v});
    JSValue close_fn = JS_NewCFunctionData(ctx, ws_js_close, 2, 0, 1,
                                           (JSValueConst[]){id_v});
    JS_FreeValue(ctx, id_v);
    JS_SetPropertyStr(ctx, obj, "send", send_fn);
    JS_SetPropertyStr(ctx, obj, "close", close_fn);
    /* JS_SetPropertyStr took ownership of send_fn/close_fn; do NOT free */

    char key[64];
    snprintf(key, sizeof(key), "__qwrt_ws_obj_%d", id);
    ws_global_set(ctx, key, obj);
    JS_FreeValue(ctx, obj);

    /* call the user handler: fn(ws) */
    char fnkey[64];
    snprintf(fnkey, sizeof(fnkey), "__qwrt_ws_fn_%d", br->fn_id);
    JSValue fn = ws_global_get(ctx, fnkey);
    if (JS_IsFunction(ctx, fn)) {
        JSValue robj = ws_global_get(ctx, key);
        JSValue r = JS_Call(ctx, fn, JS_UNDEFINED, 1,
                            (JSValueConst[]){robj});
        if (JS_IsException(r))
            JS_FreeValue(ctx, r);
        else
            JS_FreeValue(ctx, r);
        JS_FreeValue(ctx, robj);
    }
    JS_FreeValue(ctx, fn);

    /* trigger onopen if the user assigned it inside fn(ws) */
    ws_call_prop(ctx, id, "onopen", NULL, 0);
    return 0;
}

static int ws_on_message(uvhttp_ws_connection_t *conn, const char *data,
                         size_t len, int opcode) {
    (void)opcode;
    JSContext *ctx = qwrt_get_active_jsctx(g_state.rt);
    if (!ctx)
        return 0;
    ws_entry_t *e = ws_find_by_conn(conn);
    if (!e)
        return 0;
    JSValue msg = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, msg, "data", JS_NewStringLen(ctx, data, len));
    JSValue args[1] = {msg};
    ws_call_prop(ctx, e->id, "onmessage", args, 1);
    JS_FreeValue(ctx, msg);
    return 0;
}

static int ws_on_close(uvhttp_ws_connection_t *conn) {
    JSContext *ctx = qwrt_get_active_jsctx(g_state.rt);
    if (!ctx)
        return 0;
    ws_entry_t *e = ws_find_by_conn(conn);
    if (!e)
        return 0;
    ws_call_prop(ctx, e->id, "onclose", NULL, 0);
    /* drop the rooted object */
    char key[64];
    snprintf(key, sizeof(key), "__qwrt_ws_obj_%d", e->id);
    ws_global_set(ctx, key, JS_UNDEFINED);
    ws_unregister(e->id);
    return 0;
}

static int ws_on_error(uvhttp_ws_connection_t *conn, int code,
                       const char *msg) {
    JSContext *ctx = qwrt_get_active_jsctx(g_state.rt);
    if (!ctx)
        return 0;
    ws_entry_t *e = ws_find_by_conn(conn);
    if (!e)
        return 0;
    JSValue err = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, err, "code", JS_NewInt32(ctx, code));
    JS_SetPropertyStr(ctx, err, "message",
                      JS_NewString(ctx, msg ? msg : ""));
    JSValue args[1] = {err};
    ws_call_prop(ctx, e->id, "onerror", args, 1);
    JS_FreeValue(ctx, err);
    return 0;
}

/* ============ Promise continuations ============ */
static JSValue on_response_ready(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv, int magic,
                                 JSValueConst *func_data) {
    (void)this_val;
    (void)magic;
    int id = 0;
    JS_ToInt32(ctx, &id, (JSValue)func_data[0]);
    uvhttp_request_t *req = NULL;
    uvhttp_response_t *res = pending_take(id, &req);
    if (!res)
        return JS_UNDEFINED;
    if (argc > 0) {
        serialize_js_response(ctx, res, req, (JSValue)argv[0]);
    } else {
        send_text(res, 500, "Empty response");
    }
    return JS_UNDEFINED;
}

static JSValue on_response_rejected(JSContext *ctx, JSValueConst this_val,
                                    int argc, JSValueConst *argv, int magic,
                                    JSValueConst *func_data) {
    (void)this_val;
    (void)argc;
    (void)argv;
    (void)magic;
    int id = 0;
    JS_ToInt32(ctx, &id, (JSValue)func_data[0]);
    uvhttp_response_t *res = pending_take(id, NULL);
    if (!res)
        return JS_UNDEFINED;
    send_text(res, 500, "Internal Server Error");
    return JS_UNDEFINED;
}

/* ============ Request building ============ */
static JSValue build_request_object(JSContext *ctx, uvhttp_request_t *req) {
    JSValue req_cls;
    if (JS_IsFunction(ctx, g_state.request_cls)) {
        /* Cached, rooted reference — dup so the trailing JS_FreeValue
         * releases our own ref, not the cached one. */
        req_cls = JS_DupValue(ctx, g_state.request_cls);
    } else {
        /* Fallback: look up the Request constructor from globalThis. */
        JSValue global = JS_GetGlobalObject(ctx);
        req_cls = JS_GetPropertyStr(ctx, global, "Request");
        JS_FreeValue(ctx, global);
        if (!JS_IsFunction(ctx, req_cls)) {
            JS_FreeValue(ctx, req_cls);
            return JS_NULL;
        }
    }

    const char *path = uvhttp_request_get_url(req);
    if (!path)
        path = "/";
    const char *host = uvhttp_request_get_header(req, "Host");
    if (!host)
        host = "localhost";

    char url[4096];
    snprintf(url, sizeof(url), "http://%s%s", host, path);
    JSValue url_v = JS_NewString(ctx, url);

    JSValue init = JS_NewObject(ctx);
    const char *method = uvhttp_request_get_method(req);
    JS_SetPropertyStr(ctx, init, "method",
                      JS_NewString(ctx, method ? method : "GET"));

    /* Build a real Headers instance and write its _map directly (bypassing
     * normalizeName/normalizeValue regex — the per-request bottleneck).
     * new Request(init) then copies the _map via the instanceof-Headers
     * branch, which does no regex normalization. */
    JSValue headers;
    if (JS_IsFunction(ctx, g_state.headers_cls)) {
        headers = JS_CallConstructor(ctx, g_state.headers_cls, 0, NULL);
    } else {
        JSValue global = JS_GetGlobalObject(ctx);
        JSValue hcls = JS_GetPropertyStr(ctx, global, "Headers");
        JS_FreeValue(ctx, global);
        headers = JS_IsFunction(ctx, hcls)
            ? JS_CallConstructor(ctx, hcls, 0, NULL)
            : JS_NULL;
        JS_FreeValue(ctx, hcls);
    }
    if (!JS_IsException(headers) && !JS_IsNull(headers)) {
        JSValue map = JS_GetPropertyStr(ctx, headers, "_map");
        if (JS_IsObject(map)) {
            JSValue set_fn = JS_GetPropertyStr(ctx, map, "set");
            if (JS_IsFunction(ctx, set_fn)) {
                size_t hcount = uvhttp_request_get_header_count(req);
                for (size_t i = 0; i < hcount; i++) {
                    uvhttp_header_t *h = uvhttp_request_get_header_at(req, i);
                    if (h && h->name[0] && h->value[0]) {
                        /* uvhttp keeps header names as-is (e.g.
                         * "Content-Type"); the plain-object path lowercased
                         * them via normalizeName in Headers#append. Do the
                         * same ASCII tolower here so Map keys stay
                         * case-insensitive. */
                        char lname[UVHTTP_MAX_HEADER_NAME_SIZE];
                        size_t nl = strlen(h->name);
                        if (nl >= sizeof(lname)) nl = sizeof(lname) - 1;
                        for (size_t k = 0; k < nl; k++) {
                            char c = h->name[k];
                            lname[k] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
                        }
                        lname[nl] = '\0';
                        JSValue name = JS_NewString(ctx, lname);
                        JSValue val = JS_NewString(ctx, h->value);
                        JSValue argv2[2] = {name, val};
                        /* Map.prototype.set returns the map itself (a caller-
                         * owned JSValue): capture and release it, and stop
                         * iterating if the call throws. */
                        JSValue r = JS_Call(ctx, set_fn, map, 2, argv2);
                        JS_FreeValue(ctx, name);
                        JS_FreeValue(ctx, val);
                        if (JS_IsException(r)) {
                            JS_FreeValue(ctx, r);
                            break;
                        }
                        JS_FreeValue(ctx, r);
                    }
                }
            }
            JS_FreeValue(ctx, set_fn);
        }
        JS_FreeValue(ctx, map);
        JS_SetPropertyStr(ctx, init, "headers", headers);
    } else {
        JS_FreeValue(ctx, headers);
        JSValue headers_obj = JS_NewObject(ctx);
        size_t hcount = uvhttp_request_get_header_count(req);
        for (size_t i = 0; i < hcount; i++) {
            uvhttp_header_t *h = uvhttp_request_get_header_at(req, i);
            if (h && h->name[0] && h->value[0]) {
                JS_SetPropertyStr(ctx, headers_obj, h->name,
                                  JS_NewString(ctx, h->value));
            }
        }
        JS_SetPropertyStr(ctx, init, "headers", headers_obj);
    }

    size_t blen = uvhttp_request_get_body_length(req);
    const char *body = uvhttp_request_get_body(req);
    if (blen > 0 && body) {
        JS_SetPropertyStr(ctx, init, "body", JS_NewStringLen(ctx, body, blen));
    }

    JSValue argv[2] = {url_v, init};
    JSValue request = JS_CallConstructor(ctx, req_cls, 2, argv);
    JS_FreeValue(ctx, req_cls);
    JS_FreeValue(ctx, url_v);
    JS_FreeValue(ctx, init);
    return request;
}

/* ============ HTTP request handler (uvhttp callback) ============ */
static int qwrt_http_on_request(uvhttp_request_t *req,
                                uvhttp_response_t *res) {
    JSContext *ctx = qwrt_get_active_jsctx(g_state.rt);
    if (!ctx || !g_state.rt) {
        send_text(res, 500, "Internal Server Error");
        return 0;
    }

    /* Bind this connection's destroy notification once, so pending async
     * responses are dropped when uvhttp frees conn->request/response. */
    uvhttp_connection_t *conn = NULL;
    if (res && res->client) {
        conn = (uvhttp_connection_t*)((uv_stream_t*)res->client)->data;
        if (conn && !conn->on_destroy) {
            conn->on_destroy = qwrt_conn_destroy;
        }
    }

    JSValue handler;
    if (JS_IsFunction(ctx, g_state.handler)) {
        handler = JS_DupValue(ctx, g_state.handler);
    } else {
        JSValue global = JS_GetGlobalObject(ctx);
        handler = JS_GetPropertyStr(ctx, global, HTTP_SERVER_HANDLER_KEY);
        JS_FreeValue(ctx, global);
    }
    if (!JS_IsFunction(ctx, handler)) {
        JS_FreeValue(ctx, handler);
        send_text(res, 404, "Not Found");
        return 0;
    }

    JSValue request = build_request_object(ctx, req);
    if (JS_IsNull(request)) {
        JS_FreeValue(ctx, handler);
        send_text(res, 500, "Request unavailable");
        return 0;
    }

    JSValue ret = JS_Call(ctx, handler, JS_UNDEFINED, 1,
                          (JSValueConst[]){request});
    JS_FreeValue(ctx, handler);
    JS_FreeValue(ctx, request);

    if (JS_IsException(ret)) {
        JS_FreeValue(ctx, ret);
        send_text(res, 500, "Internal Server Error");
        return 0;
    }

    if (JS_IsPromise(ret)) {
        pending_entry_t *e = pending_add(conn, req, res);
        if (!e) {
            JS_FreeValue(ctx, ret);
            send_text(res, 500, "Internal Server Error");
            return 0;
        }
        JSValue id_v = JS_NewInt32(ctx, e->id);
        JSValue on_ok = JS_NewCFunctionData(ctx, on_response_ready, 1, 0, 1,
                                            (JSValueConst[]){id_v});
        JSValue on_err = JS_NewCFunctionData(ctx, on_response_rejected, 1, 0, 1,
                                             (JSValueConst[]){id_v});
        JS_FreeValue(ctx, id_v);
        JSValue then_fn = JS_GetPropertyStr(ctx, ret, "then");
        if (JS_IsFunction(ctx, then_fn)) {
            JSValue argv[2] = {on_ok, on_err};
            JSValue p2 = JS_Call(ctx, then_fn, ret, 2, argv);
            if (JS_IsException(p2)) {
                pending_take(e->id, NULL);
                send_text(res, 500, "Internal Server Error");
            }
            JS_FreeValue(ctx, p2);
        } else {
            pending_take(e->id, NULL);
            send_text(res, 500, "Internal Server Error");
        }
        JS_FreeValue(ctx, then_fn);
        JS_FreeValue(ctx, on_ok);
        JS_FreeValue(ctx, on_err);
        JS_FreeValue(ctx, ret);
    } else {
        serialize_js_response(ctx, res, req, ret);
        JS_FreeValue(ctx, ret);
    }
    return 0;
}

/* ============ server.close() ============ */
static JSValue js_server_close(JSContext *ctx, JSValueConst this_val, int argc,
                               JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    if (!g_state.running || !g_state.server) return JS_UNDEFINED;
    uvhttp_server_stop(g_state.server);
    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, HTTP_SERVER_HANDLER_KEY, JS_UNDEFINED);
    JS_FreeValue(ctx, global);
    if (JS_IsFunction(ctx, g_state.handler)) JS_FreeValue(ctx, g_state.handler);
    g_state.handler = JS_UNDEFINED;
    if (JS_IsFunction(ctx, g_state.request_cls)) JS_FreeValue(ctx, g_state.request_cls);
    g_state.request_cls = JS_UNDEFINED;
    if (JS_IsFunction(ctx, g_state.headers_cls)) JS_FreeValue(ctx, g_state.headers_cls);
    g_state.headers_cls = JS_UNDEFINED;
    g_state.running = 0;
    return JS_UNDEFINED;
}

/* ============ serve() ============ */
static JSValue js_serve(JSContext *ctx, JSValueConst this_val, int argc,
                        JSValueConst *argv) {
    (void)this_val;
    if (g_state.running) {
        JS_ThrowTypeError(ctx, "serve: a server is already running");
        return JS_EXCEPTION;
    }
    if (argc < 2 || !JS_IsObject(argv[0]) || !JS_IsFunction(ctx, argv[1])) {
        JS_ThrowTypeError(ctx, "serve(options, handler) expected");
        return JS_EXCEPTION;
    }

JSValue opts = (JSValue)argv[0];

    /* --- options.port --- */
    int port = 8080;
    JSValue pv = JS_GetPropertyStr(ctx, opts, "port");
    if (JS_IsNumber(pv)) JS_ToInt32(ctx, &port, pv);
    JS_FreeValue(ctx, pv);

    /* Validate port range up-front (1-65535): an out-of-range value such as
     * -1 would otherwise reach uvhttp_server_listen and bind without
     * diagnostics (missing range check — see httpserver plan T7.2). */
    if (port < 1 || port > 65535) {
        JS_ThrowRangeError(ctx, "serve: port must be in 1..65535 (got %d)", port);
        return JS_EXCEPTION;
    }

    /* --- options.hostname --- */
    const char *host = "0.0.0.0";
    JSValue hv = JS_GetPropertyStr(ctx, opts, "hostname");
    if (JS_IsString(hv)) host = JS_ToCString(ctx, hv);

    /* --- server creation --- */
    uvhttp_server_t *server = NULL;
    uvhttp_error_t err = uvhttp_server_new(&g_state.rt->loop, &server);
    if (err != UVHTTP_OK || !server) {
        if (JS_IsString(hv)) JS_FreeCString(ctx, host);
        JS_ThrowTypeError(ctx, "serve: uvhttp_server_new failed (%d)", err);
        return JS_EXCEPTION;
    }
    g_state.server = server;

    /* Create router if needed */
    if (!server->router) {
        uvhttp_router_new(&server->router);
    }
    uvhttp_server_set_handler(server, qwrt_http_on_request);

    /* --- TLS --- */
    JSValue tls = JS_GetPropertyStr(ctx, opts, "tls");
    if (JS_IsObject(tls)) {
        JSValue cert = JS_GetPropertyStr(ctx, tls, "cert");
        JSValue key = JS_GetPropertyStr(ctx, tls, "key");
        if (JS_IsString(cert) && JS_IsString(key)) {
            const char *cert_s = JS_ToCString(ctx, cert);
            const char *key_s = JS_ToCString(ctx, key);
            uvhttp_tls_context_t *tls_ctx = NULL;
            err = uvhttp_tls_context_new(&tls_ctx);
            if (err == UVHTTP_OK) err = uvhttp_tls_context_load_cert_chain(tls_ctx, cert_s);
            if (err == UVHTTP_OK) err = uvhttp_tls_context_load_private_key(tls_ctx, key_s);
            if (err == UVHTTP_OK) err = uvhttp_server_enable_tls(server, tls_ctx);
            if (err == UVHTTP_OK) g_state.tls_ctx = tls_ctx;
            JS_FreeCString(ctx, cert_s);
            JS_FreeCString(ctx, key_s);
            if (err != UVHTTP_OK) {
                uvhttp_tls_context_free(tls_ctx);
                JS_FreeValue(ctx, cert);
                JS_FreeValue(ctx, key);
                JS_FreeValue(ctx, tls);
                JS_ThrowTypeError(ctx, "serve: TLS init failed (%d)", err);
                return JS_EXCEPTION;
            }
        }
        JS_FreeValue(ctx, cert);
        JS_FreeValue(ctx, key);
    }
    JS_FreeValue(ctx, tls);

    /* --- static files --- */
    JSValue st = JS_GetPropertyStr(ctx, opts, "static");
    if (JS_IsObject(st)) {
        uvhttp_static_config_t cfg = {0};
        cfg.max_cache_size = 10 * 1024 * 1024;
        cfg.cache_ttl = 3600;
        cfg.enable_etag = 1;
        cfg.enable_last_modified = 1;
        cfg.enable_sendfile = 1;
        strncpy(cfg.index_file, "index.html", sizeof(cfg.index_file) - 1);

        JSValue root = JS_GetPropertyStr(ctx, st, "root");
        if (JS_IsString(root)) {
            const char *root_s = JS_ToCString(ctx, root);
            strncpy(cfg.root_directory, root_s, sizeof(cfg.root_directory) - 1);
            JS_FreeCString(ctx, root_s);
        }

        JSValue idx = JS_GetPropertyStr(ctx, st, "index");
        if (JS_IsString(idx)) {
            const char *idx_s = JS_ToCString(ctx, idx);
            strncpy(cfg.index_file, idx_s, sizeof(cfg.index_file) - 1);
            JS_FreeCString(ctx, idx_s);
        }

        JSValue mfs = JS_GetPropertyStr(ctx, st, "maxFileSize");
        if (JS_IsNumber(mfs)) {
            int64_t v = 0;
            JS_ToInt64(ctx, &v, mfs);
            cfg.max_file_size = (size_t)v;
        } else {
            cfg.max_file_size = 10 * 1024 * 1024;
        }
        JS_FreeValue(ctx, mfs);

        JS_FreeValue(ctx, root);
        JS_FreeValue(ctx, idx);

        if (cfg.root_directory[0]) {
            uvhttp_static_context_t *sctx = NULL;
            err = uvhttp_static_create(&cfg, &sctx);
            if (err == UVHTTP_OK && sctx) {
                g_state.static_ctx = sctx;
                g_state.static_enabled = 1;
                if (server->router) server->router->static_context = sctx;
            }
        }
    }
    JS_FreeValue(ctx, st);

    /* --- WebSocket endpoints: options.ws = { path: (ws) => {} } --- */
    JSValue ws_map = JS_GetPropertyStr(ctx, opts, "ws");
    if (JS_IsObject(ws_map)) {
        JSPropertyEnum *props = NULL;
        uint32_t nprops = 0;
        if (JS_GetOwnPropertyNames(ctx, &props, &nprops, ws_map,
                                   JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) ==
            0) {
            for (uint32_t i = 0; i < nprops; i++) {
                const char *path = JS_AtomToCString(ctx, props[i].atom);
                JSValue fn = JS_GetProperty(ctx, ws_map, props[i].atom);
                if (path && JS_IsFunction(ctx, fn)) {
                    /* root the JS handler on the global object */
                    ws_bridge_t *br = calloc(1, sizeof(*br));
                    if (br) {
                        br->fn_id = g_ws_next_id + 1000;
                        char fnkey[64];
                        snprintf(fnkey, sizeof(fnkey), "__qwrt_ws_fn_%d",
                                 br->fn_id);
                        ws_global_set(ctx, fnkey, JS_DupValue(ctx, fn));

                        uvhttp_ws_handler_t wh;
                        memset(&wh, 0, sizeof(wh));
                        wh.on_connect = ws_on_connect;
                        wh.on_message = ws_on_message;
                        wh.on_close = ws_on_close;
                        wh.on_error = ws_on_error;
                        wh.user_data = br;
                        uvhttp_server_register_ws_handler(server, path, &wh);
                    }
                }
                if (path)
                    JS_FreeCString(ctx, path);
                JS_FreeValue(ctx, fn);
                JS_FreeAtom(ctx, props[i].atom);
            }
            js_free(ctx, props);
        }
    }
    JS_FreeValue(ctx, ws_map);

    /* --- listen --- */
    err = uvhttp_server_listen(server, host, port);
    if (JS_IsString(hv)) JS_FreeCString(ctx, host);
    JS_FreeValue(ctx, hv);

    if (err != UVHTTP_OK) {
        uvhttp_server_free(server);
        g_state.server = NULL;
        JS_ThrowTypeError(ctx, "serve: listen on %s:%d failed (%d)", host, port, err);
        return JS_EXCEPTION;
    }

    /* Keep handler rooted */
    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, HTTP_SERVER_HANDLER_KEY, JS_DupValue(ctx, argv[1]));
    /* Cache rooted refs to avoid per-request global property lookups. */
    if (JS_IsFunction(ctx, g_state.handler)) JS_FreeValue(ctx, g_state.handler);
    g_state.handler = JS_DupValue(ctx, argv[1]);
    if (JS_IsFunction(ctx, g_state.request_cls)) JS_FreeValue(ctx, g_state.request_cls);
    g_state.request_cls = JS_GetPropertyStr(ctx, global, "Request");
    if (JS_IsFunction(ctx, g_state.headers_cls)) JS_FreeValue(ctx, g_state.headers_cls);
    g_state.headers_cls = JS_GetPropertyStr(ctx, global, "Headers");
    JS_FreeValue(ctx, global);

    g_state.running = 1;

    /* Return server handle with close() */
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "close",
                      JS_NewCFunction(ctx, js_server_close, "close", 0));
    return obj;
}

/* ============ extension lifecycle ============ */
static int http_server_ext_init(qwrt_ext_t *ext, qwrt_t *rt) {
    (void)ext;
    g_state.rt = rt;
    g_state.server = NULL;
    g_state.tls_ctx = NULL;
    g_state.static_ctx = NULL;
    g_state.static_enabled = 0;
    g_state.running = 0;
    g_state.handler = JS_UNDEFINED;
    g_state.request_cls = JS_UNDEFINED;
    g_state.headers_cls = JS_UNDEFINED;
    JSContext *ctx = qwrt_get_active_jsctx(rt);
    if (!ctx) return -1;
    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "serve",
                      JS_NewCFunction(ctx, js_serve, "serve", 2));
    JS_FreeValue(ctx, global);
    return 0;
}

static void http_server_ext_destroy(qwrt_ext_t *ext, qwrt_t *rt) {
    (void)ext; (void)rt;
    /* drop rooted ws objects */
    JSContext *ctx = qwrt_get_active_jsctx(rt);
    if (ctx) {
        while (g_ws_entries) {
            ws_entry_t *e = g_ws_entries;
            char key[64];
            snprintf(key, sizeof(key), "__qwrt_ws_obj_%d", e->id);
            ws_global_set(ctx, key, JS_UNDEFINED);
            g_ws_entries = e->next;
            free(e);
        }
    } else {
        while (g_ws_entries) {
            ws_entry_t *e = g_ws_entries;
            g_ws_entries = e->next;
            free(e);
        }
    }
    if (g_state.server) {
        if (g_state.running) uvhttp_server_stop(g_state.server);
        uvhttp_server_free(g_state.server);
        g_state.server = NULL;
    }
    if (ctx) {
        if (JS_IsFunction(ctx, g_state.handler)) JS_FreeValue(ctx, g_state.handler);
        if (JS_IsFunction(ctx, g_state.request_cls)) JS_FreeValue(ctx, g_state.request_cls);
        if (JS_IsFunction(ctx, g_state.headers_cls)) JS_FreeValue(ctx, g_state.headers_cls);
    }
    g_state.handler = JS_UNDEFINED;
    g_state.request_cls = JS_UNDEFINED;
    g_state.headers_cls = JS_UNDEFINED;
    if (g_state.tls_ctx) {
        uvhttp_tls_context_free(g_state.tls_ctx);
        g_state.tls_ctx = NULL;
    }
    if (g_state.static_ctx) {
        uvhttp_static_free(g_state.static_ctx);
        g_state.static_ctx = NULL;
    }
    while (g_pending) {
        pending_entry_t *e = g_pending;
        g_pending = e->next;
        free(e);
    }
    g_state.rt = NULL;
}

const qwrt_ext_t qwrt_http_server_ext = {
    "http_server",
    http_server_ext_init,
    http_server_ext_destroy,
    NULL, /* suspend */
    NULL, /* resume */
    NULL, /* user_data */
};

#endif /* QWRT_WITH_HTTPSERVER */
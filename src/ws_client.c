/**
 * qwrt WebSocket Client — uvhttp-backed
 *
 * Implements pal.wsConnect for the standard WebSocket API polyfill.
 * Uses uvhttp WebSocket client functions for handshake + frame I/O.
 */

#include "qwrt_internal.h"
#include "qwrt/ext_http_server.h"
#include <uvhttp.h>
#include <uvhttp_context.h>
#include <uvhttp_websocket.h>
#include <uv.h>
#include <string.h>
#include <stdlib.h>

#if QWRT_WITH_HTTPSERVER

/* Per-connection state for the JS-level WebSocket API */
typedef struct qwrt_ws_client {
    qwrt_t *rt;
    uvhttp_ws_connection_t *ws_conn;
    uv_tcp_t *tcp;
    uvhttp_context_t *wctx;
    JSContext *jsctx;
    JSValue ws_obj;         /* the JS WebSocket object (rooted) */
    JSValue onmessage;      /* JS callback for incoming frames */
    JSValue onclose;
    JSValue onerror;
    int closed;
    char host[256];
    char path[512];
    int port;
    struct qwrt_ws_client *next;
} qwrt_ws_client_t;

/* Placeholder for future WS client registry */
/* static qwrt_ws_client_t *g_ws_clients = NULL; */
/* static int g_ws_client_id = 1; */

/* Forward declarations */
static void ws_read_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf);
static void ws_read_cb(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf);
static void on_handshake_write(uv_write_t *req, int status);
static void on_connect(uv_connect_t *req, int status);
static int ws_client_on_message(uvhttp_ws_connection_t *conn, const char *data, size_t len, int binary);
static int ws_client_on_close(uvhttp_ws_connection_t *conn, int code, const char *reason);
static int ws_client_on_error(uvhttp_ws_connection_t *conn, int error_code, const char *error_msg);
static int ws_client_on_message(uvhttp_ws_connection_t *conn, const char *data, size_t len, int binary);

static void ws_client_free(qwrt_ws_client_t *c) {
    if (c->ws_conn) uvhttp_ws_connection_free(c->ws_conn);
    if (c->tcp) {
        uv_close((uv_handle_t *)c->tcp, (uv_close_cb)free);
    }
    if (c->wctx) {
        uvhttp_context_cleanup_websocket(c->wctx);
        uvhttp_context_destroy(c->wctx);
    }
    if (c->jsctx) {
        JS_FreeValue(c->jsctx, c->ws_obj);
        JS_FreeValue(c->jsctx, c->onmessage);
        JS_FreeValue(c->jsctx, c->onclose);
        JS_FreeValue(c->jsctx, c->onerror);
    }
    free(c);
}

/* Write callback: request sent, start reading response */
static void on_handshake_write(uv_write_t *req, int status) {
    qwrt_ws_client_t *c = (qwrt_ws_client_t *)req->data;
    free(req);
    if (status < 0 || c->closed) {
        if (JS_IsFunction(c->jsctx, c->onerror))
            JS_Call(c->jsctx, c->onerror, c->ws_obj, 0, NULL);
        ws_client_free(c);
        return;
    }
    /* Start reading the response */
    uv_read_start((uv_stream_t *)c->tcp, ws_read_alloc, ws_read_cb);
}

static void ws_read_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    (void)handle;
    buf->base = (char *)malloc(suggested_size);
    buf->len = suggested_size;
}

/* Response buffer (per-connection, stored in ws_client) */
#define WS_RESP_BUF_SIZE 4096

static void ws_read_cb(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
    qwrt_ws_client_t *c = (qwrt_ws_client_t *)(((uv_tcp_t *)stream)->data);
    if (!c) { free(buf->base); return; }

    if (nread < 0) {
        free(buf->base);
        if (JS_IsFunction(c->jsctx, c->onerror))
            JS_Call(c->jsctx, c->onerror, c->ws_obj, 0, NULL);
        ws_client_free(c);
        return;
    }

    /* Check for the end of HTTP headers (double CRLF / double LF) */
    const char *resp = buf->base;
    size_t resp_len = (size_t)nread;

    const char *header_end = NULL;
    for (size_t i = 0; i + 4 <= resp_len; i++) {
        if (resp[i] == '\r' && resp[i+1] == '\n' && resp[i+2] == '\r' && resp[i+3] == '\n') {
            header_end = resp + i + 4;
            break;
        }
        if (resp[i] == '\n' && resp[i+1] == '\n') {
            header_end = resp + i + 2;
            break;
        }
    }

    if (!header_end) {
        /* Need more data — in a real impl we'd accumulate. For now, fail. */
        free(buf->base);
        if (JS_IsFunction(c->jsctx, c->onerror))
            JS_Call(c->jsctx, c->onerror, c->ws_obj, 0, NULL);
        ws_client_free(c);
        return;
    }

    /* Verify the handshake response */
    size_t resp_header_len = (size_t)(header_end - resp);
    uvhttp_error_t verr = uvhttp_ws_verify_handshake_response(c->ws_conn, resp, resp_header_len);
    free(buf->base);

    if (verr != UVHTTP_OK) {
        if (JS_IsFunction(c->jsctx, c->onerror))
            JS_Call(c->jsctx, c->onerror, c->ws_obj, 0, NULL);
        ws_client_free(c);
        return;
    }

    /* Handshake successful! Call onopen */
    uv_read_stop(stream);
    if (JS_IsFunction(c->jsctx, c->onmessage)) {
        /* Start reading frames — simplified: the uvhttp ws callbacks
         * will handle frame parsing. For now, we use the raw fd. */
    }
}

/* Called when the TCP connection is established */
static void on_connect(uv_connect_t *req, int status) {
    qwrt_ws_client_t *c = (qwrt_ws_client_t *)req->data;
    free(req);

    if (status < 0) {
        if (JS_IsFunction(c->jsctx, c->onerror)) {
            JSValue err = JS_NewString(c->jsctx, uv_strerror(status));
            JS_Call(c->jsctx, c->onerror, c->ws_obj, 1, (JSValueConst[]){err});
            JS_FreeValue(c->jsctx, err);
        }
        ws_client_free(c);
        return;
    }

    /* Create uvhttp WebSocket connection */
    c->ws_conn = uvhttp_ws_connection_create(c->tcp->io_watcher.fd, NULL, 0, NULL);
    if (!c->ws_conn) {
        ws_client_free(c);
        return;
    }

    /* Set callbacks */
    uvhttp_ws_set_callbacks(c->ws_conn, ws_client_on_message, ws_client_on_close, ws_client_on_error);
    c->ws_conn->user_data = c;

    /* Generate the WebSocket upgrade request */
    uvhttp_context_t *ctx_ptr = NULL;
    uvhttp_error_t ctx_err = uvhttp_context_create(&c->rt->loop, &ctx_ptr);
    if (ctx_err != UVHTTP_OK || !ctx_ptr) {
        ws_client_free(c);
        return;
    }
    c->wctx = ctx_ptr;
    uvhttp_context_init_websocket(c->wctx);

    char request[4096];
    size_t request_len = sizeof(request);
    uvhttp_error_t err = uvhttp_ws_handshake_client(c->wctx, c->ws_conn,
        c->host, c->path, request, &request_len);

    if (err != UVHTTP_OK) {
        ws_client_free(c);
        return;
    }

    /* Send upgrade request over TCP */
    uv_buf_t wbuf = uv_buf_init(request, (unsigned int)request_len);
    uv_write_t *wreq = (uv_write_t *)malloc(sizeof(uv_write_t));
    if (!wreq) { ws_client_free(c); return; }
    wreq->data = c;
    uv_write(wreq, (uv_stream_t *)c->tcp, &wbuf, 1, on_handshake_write);
}

static int ws_client_on_message(uvhttp_ws_connection_t *conn, const char *data, size_t len, int binary) {
    qwrt_ws_client_t *c = (qwrt_ws_client_t *)conn->user_data;
    if (c->closed) return 0;
    (void)binary;
    if (JS_IsFunction(c->jsctx, c->onmessage)) {
        JSValue msg = JS_NewStringLen(c->jsctx, data, len);
        JS_Call(c->jsctx, c->onmessage, c->ws_obj, 1, (JSValueConst[]){msg});
        JS_FreeValue(c->jsctx, msg);
    }
    return 0;
}

static int ws_client_on_close(uvhttp_ws_connection_t *conn, int code, const char *reason) {
    qwrt_ws_client_t *c = (qwrt_ws_client_t *)conn->user_data;
    c->closed = 1;
    if (JS_IsFunction(c->jsctx, c->onclose)) {
        JSValue ev = JS_NewObject(c->jsctx);
        JS_SetPropertyStr(c->jsctx, ev, "code", JS_NewInt32(c->jsctx, code));
        JS_SetPropertyStr(c->jsctx, ev, "reason", JS_NewString(c->jsctx, reason ? reason : ""));
        JS_Call(c->jsctx, c->onclose, c->ws_obj, 1, (JSValueConst[]){ev});
        JS_FreeValue(c->jsctx, ev);
    }
    ws_client_free(c);
    return 0;
}

static int ws_client_on_error(uvhttp_ws_connection_t *conn, int error_code, const char *error_msg) {
    qwrt_ws_client_t *c = (qwrt_ws_client_t *)conn->user_data;
    c->closed = 1;
    (void)error_code;
    if (JS_IsFunction(c->jsctx, c->onerror)) {
        JSValue err = JS_NewString(c->jsctx, error_msg ? error_msg : "WebSocket error");
        JS_Call(c->jsctx, c->onerror, c->ws_obj, 1, (JSValueConst[]){err});
        JS_FreeValue(c->jsctx, err);
    }
    return 0;
}

/* pal.wsConnect(url, callbacks) — called from JS */
JSValue js_pal_ws_connect(JSContext *ctx, JSValueConst this_val,
                                 int argc, JSValueConst *argv) {
    (void)this_val;
    if (argc < 1 || !JS_IsString(argv[0])) {
        return JS_ThrowTypeError(ctx, "wsConnect: url required");
    }

    const char *url = JS_ToCString(ctx, argv[0]);
    if (!url) return JS_EXCEPTION;

    /* Parse ws:// URL */
    const char *host = url;
    const char *path = "/";
    if (strncmp(url, "ws://", 5) == 0) {
        host = url + 5;
    } else if (strncmp(url, "wss://", 6) == 0) {
        JS_FreeCString(ctx, url);
        return JS_ThrowTypeError(ctx, "wss:// not supported yet");
    } else {
        JS_FreeCString(ctx, url);
        return JS_ThrowTypeError(ctx, "wsConnect: invalid URL (must start with ws://)");
    }

    char host_buf[256] = {0};
    int port = 80;
    const char *path_start = strchr(host, '/');
    const char *port_start = strchr(host, ':');

    if (port_start && (!path_start || port_start < path_start)) {
        size_t host_len = (size_t)(port_start - host);
        if (host_len >= sizeof(host_buf)) host_len = sizeof(host_buf) - 1;
        memcpy(host_buf, host, host_len);
        host_buf[host_len] = '\0';
        port = atoi(port_start + 1);
        if (port <= 0) port = 80;
        if (path_start) path = path_start;
    } else if (path_start) {
        size_t host_len = (size_t)(path_start - host);
        if (host_len >= sizeof(host_buf)) host_len = sizeof(host_buf) - 1;
        memcpy(host_buf, host, host_len);
        host_buf[host_len] = '\0';
        path = path_start;
    } else {
        strncpy(host_buf, host, sizeof(host_buf) - 1);
        host_buf[sizeof(host_buf) - 1] = '\0';
    }

    JS_FreeCString(ctx, url);

    qwrt_t *rt = qwrt_get_rt_from_ctx(ctx);
    if (!rt) return JS_ThrowTypeError(ctx, "wsConnect: no runtime");

    qwrt_ws_client_t *c = calloc(1, sizeof(*c));
    if (!c) return JS_ThrowTypeError(ctx, "wsConnect: OOM");

    c->rt = rt;
    c->jsctx = ctx;
    c->closed = 0;
    c->onmessage = JS_UNDEFINED;
    c->onclose = JS_UNDEFINED;
    c->onerror = JS_UNDEFINED;
    strncpy(c->host, host_buf, sizeof(c->host) - 1);
    c->host[sizeof(c->host) - 1] = '\0';
    strncpy(c->path, path, sizeof(c->path) - 1);
    c->path[sizeof(c->path) - 1] = '\0';
    c->port = port;

    /* Create the JS WebSocket handle object */
    char url_buf[512];
    snprintf(url_buf, sizeof(url_buf), "ws://%s:%d%s", host_buf, port, path);
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "readyState", JS_NewInt32(ctx, 0));
    JS_SetPropertyStr(ctx, obj, "url", JS_NewString(ctx, url_buf));
    c->ws_obj = JS_DupValue(ctx, obj);

    /* Create TCP socket */
    c->tcp = calloc(1, sizeof(uv_tcp_t));
    if (!c->tcp) { free(c); return JS_ThrowTypeError(ctx, "wsConnect: OOM"); }
    uv_tcp_init(&rt->loop, c->tcp);

    struct sockaddr_in dest;
    uv_ip4_addr(host_buf, port, &dest);

    uv_connect_t *connect_req = calloc(1, sizeof(uv_connect_t));
    if (!connect_req) { ws_client_free(c); return JS_ThrowTypeError(ctx, "wsConnect: OOM"); }
    connect_req->data = c;

    int r = uv_tcp_connect(connect_req, c->tcp, (const struct sockaddr *)&dest, on_connect);
    if (r != 0) {
        ws_client_free(c);
        free(connect_req);
        return JS_ThrowTypeError(ctx, "wsConnect: connection failed (%s)", uv_strerror(r));
    }

    return obj;
}

#endif /* QWRT_WITH_HTTPSERVER */
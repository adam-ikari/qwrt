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

/* Per-connection state for the JS-level WebSocket API.
 *
 * Memory rule (project convention): all allocations go through the
 * QuickJS wrapped allocator (js_mallocz/js_malloc) and are released with
 * js_free on the same JSContext. The uv_tcp_t is embedded so that its
 * async close can be completed in ws_client_close_cb, which performs the
 * single final free of the whole client struct. */
typedef struct qwrt_ws_client {
    qwrt_t *rt;
    uvhttp_ws_connection_t *ws_conn;
    uv_tcp_t tcp;           /* embedded; closed async before final free */
    int tcp_active;         /* 1 once uv_tcp_init succeeded -> close required */
    uvhttp_context_t *wctx;
    JSContext *jsctx;
    JSValue ws_obj;         /* the JS WebSocket object (rooted) */
    JSValue onopen;         /* JS callback for connection open */
    JSValue onmessage;      /* JS callback for incoming frames */
    JSValue onclose;
    JSValue onerror;
    char *write_buf;        /* heap buffer for async uv_write (handshake request) */
    int closed;             /* close handshake complete (on_close/on_error fired) */
    int close_sent;         /* JS ws.close() called; teardown still deferred until
                             * the peer's close-frame echo completes the handshake */
    int freed;              /* ws_client_free idempotency guard (avoids re-free from a late
                             * read callback that fires between uv_close-initiate and
                             * ws_client_close_cb) */
    int handshake_done;
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

/* Final teardown: runs from uv_close completion, when it is safe to free
 * the memory backing the (embedded) handle itself. */
static void ws_client_close_cb(uv_handle_t *handle) {
    qwrt_ws_client_t *c = (qwrt_ws_client_t *)handle->data;
    if (c && c->jsctx)
        js_free(c->jsctx, c);
}

static void ws_client_free(qwrt_ws_client_t *c) {
    if (!c || c->freed) return;
    c->freed = 1;
    if (c->ws_conn) { uvhttp_ws_connection_free(c->ws_conn); c->ws_conn = NULL; }
    if (c->wctx) {
        uvhttp_context_cleanup_websocket(c->wctx);
        uvhttp_context_destroy(c->wctx);
        c->wctx = NULL;
    }
    if (c->jsctx) {
        JS_FreeValue(c->jsctx, c->ws_obj);
        JS_FreeValue(c->jsctx, c->onopen);
        JS_FreeValue(c->jsctx, c->onmessage);
        JS_FreeValue(c->jsctx, c->onclose);
        JS_FreeValue(c->jsctx, c->onerror);
    }
    if (c->write_buf) { js_free(c->jsctx, c->write_buf); c->write_buf = NULL; }
    if (c->tcp_active) {
        /* Stop delivering read callbacks before the async close completes;
         * a late EOF/error read between uv_close and ws_client_close_cb
         * would otherwise re-enter ws_read_cb and double-free. */
        c->tcp_active = 0;
        uv_read_stop((uv_stream_t *)&c->tcp);
        /* Keep c->tcp.data = c so a late read callback can still js_free its
         * buffer via c->jsctx (c is alive until ws_client_close_cb); the
         * ws_read_cb freed-guard bails on c->freed. */
        uv_close((uv_handle_t *)&c->tcp, ws_client_close_cb);
    } else {
        js_free(c->jsctx, c);
    }
}

/* Write callback: request sent, start reading response */
static void on_handshake_write(uv_write_t *req, int status) {
    qwrt_ws_client_t *c = (qwrt_ws_client_t *)req->data;
    js_free(c->jsctx, req);
    if (c->write_buf) { js_free(c->jsctx, c->write_buf); c->write_buf = NULL; }
    if (status < 0 || c->closed) {
        if (JS_IsFunction(c->jsctx, c->onerror))
            JS_Call(c->jsctx, c->onerror, c->ws_obj, 0, NULL);
        ws_client_free(c);
        return;
    }
    /* Start reading the response */
    uv_read_start((uv_stream_t *)&c->tcp, ws_read_alloc, ws_read_cb);
}

static void ws_read_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    qwrt_ws_client_t *c = (qwrt_ws_client_t *)handle->data;
    buf->base = (char *)js_malloc(c->jsctx, suggested_size);
    buf->len = buf->base ? suggested_size : 0;
}

/* Response buffer size (handshake response parsing) */

static void ws_read_cb(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
    qwrt_ws_client_t *c = (qwrt_ws_client_t *)(((uv_tcp_t *)stream)->data);
    if (!c) { free(buf->base); return; }  /* handle->data should never be NULL;
                                          * if it is, js_free is unreachable
                                          * so plain free is the best we can do
                                          * (allocator mismatch is survivable). */
    if (c->freed) { js_free(c->jsctx, buf->base); return; }  /* teardown in flight */

    if (nread < 0) {
        js_free(c->jsctx, buf->base);
        if (JS_IsFunction(c->jsctx, c->onerror))
            JS_Call(c->jsctx, c->onerror, c->ws_obj, 0, NULL);
        ws_client_free(c);
        return;
    }

    /* Post-handshake: raw TCP bytes are WebSocket frames. Feed them to
     * uvhttp_ws_process_data which parses frames and invokes the
     * on_message/on_close/on_error callbacks set at connect time. */
    if (c->handshake_done) {
        if (nread > 0) {
            /* process_data may synchronously fire on_close/on_error (which only
             * set c->closed — teardown is deferred), so it is safe to touch c
             * again after it returns. */
            uvhttp_error_t perr = uvhttp_ws_process_data(c->ws_conn, (const uint8_t *)buf->base,
                                   (size_t)nread);
            if (perr != UVHTTP_OK) {
                /* Protocol error (bad frame, oversized, etc.) — fire onerror
                 * and teardown. The deferred check below handles the free. */
                if (!c->closed && JS_IsFunction(c->jsctx, c->onerror)) {
                    JSValue err = JS_NewString(c->jsctx, uvhttp_error_string(perr));
                    JS_Call(c->jsctx, c->onerror, c->ws_obj, 1, (JSValueConst[]){err});
                    JS_FreeValue(c->jsctx, err);
                }
                c->closed = 1;
            }
        }
        js_free(c->jsctx, buf->base);
        /* Deferred teardown: frees ws_conn/wctx (process_data is done with
         * conn now) and starts the async uv_close of the embedded tcp. */
        if (c->closed && !c->freed)
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
        js_free(c->jsctx, buf->base);
        if (JS_IsFunction(c->jsctx, c->onerror))
            JS_Call(c->jsctx, c->onerror, c->ws_obj, 0, NULL);
        ws_client_free(c);
        return;
    }

    /* Verify the handshake response */
    size_t resp_header_len = (size_t)(header_end - resp);
    size_t frame_len = resp_len - resp_header_len;
    uvhttp_error_t verr = uvhttp_ws_verify_handshake_response(c->ws_conn, resp, resp_header_len);

    if (verr != UVHTTP_OK) {
        js_free(c->jsctx, buf->base);
        if (JS_IsFunction(c->jsctx, c->onerror))
            JS_Call(c->jsctx, c->onerror, c->ws_obj, 0, NULL);
        ws_client_free(c);
        return;
    }

    /* Handshake successful: mark done, invoke onopen, and continue
     * reading raw TCP data — subsequent reads are fed to
     * uvhttp_ws_process_data which parses frames and calls the
     * on_message/on_close/on_error callbacks. */
    c->handshake_done = 1;
    c->closed = 0;
    if (JS_IsFunction(c->jsctx, c->onopen))
        JS_Call(c->jsctx, c->onopen, c->ws_obj, 0, NULL);

    /* If the server pipelined frame bytes after the headers, process them.
     * process_data may synchronously fire onclose/on_error (deferred teardown),
     * so buf must stay alive until it returns. */
    if (frame_len > 0) {
        uvhttp_error_t perr = uvhttp_ws_process_data(c->ws_conn, (const uint8_t *)header_end, frame_len);
        if (perr != UVHTTP_OK) {
            if (!c->closed && JS_IsFunction(c->jsctx, c->onerror))
                JS_Call(c->jsctx, c->onerror, c->ws_obj, 0, NULL);
            c->closed = 1;
        }
    }
    js_free(c->jsctx, buf->base);
    if (c->closed && !c->freed)
        ws_client_free(c);
}

/* Called when the TCP connection is established */
static void on_connect(uv_connect_t *req, int status) {
    qwrt_ws_client_t *c = (qwrt_ws_client_t *)req->data;
    js_free(c->jsctx, req);

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
    c->ws_conn = uvhttp_ws_connection_create(c->tcp.io_watcher.fd, NULL, 0, NULL);
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

    /* Send upgrade request over TCP. uv_write is async, so the request
     * bytes must live on the heap (not a stack local) until the write
     * callback fires — free them there. */
    c->write_buf = (char *)js_malloc(c->jsctx, request_len ? request_len : 1);
    if (!c->write_buf) { ws_client_free(c); return; }
    memcpy(c->write_buf, request, request_len);
    uv_buf_t wbuf = uv_buf_init(c->write_buf, (unsigned int)request_len);
    uv_write_t *wreq = (uv_write_t *)js_malloc(c->jsctx, sizeof(uv_write_t));
    if (!wreq) { ws_client_free(c); return; }
    wreq->data = c;
    uv_write(wreq, (uv_stream_t *)&c->tcp, &wbuf, 1, on_handshake_write);
}

static int ws_client_on_message(uvhttp_ws_connection_t *conn, const char *data, size_t len, int binary) {
    qwrt_ws_client_t *c = (qwrt_ws_client_t *)conn->user_data;
    if (c->closed) return 0;
    (void)binary;
    /* Callbacks were captured from the options object at connect time;
     * the JS wrappers dispatch to the user-set handler dynamically. */
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
        /* Polyfill onclose(code, reason) packs these into a CloseEvent.
         * Pass them as two args, not an object, to match that signature. */
        JSValue codev = JS_NewInt32(c->jsctx, code);
        JSValue reasonv = JS_NewString(c->jsctx, reason ? reason : "");
        JS_Call(c->jsctx, c->onclose, c->ws_obj, 2, (JSValueConst[]){codev, reasonv});
        JS_FreeValue(c->jsctx, codev);
        JS_FreeValue(c->jsctx, reasonv);
    }
    /* NOTE: do NOT free here — uvhttp_ws_process_data continues using conn
     * after this callback returns (close-frame echo + state update). Defer
     * the teardown to ws_read_cb, which frees after process_data returns. */
    return 0;
}

static int ws_client_on_error(uvhttp_ws_connection_t *conn, int error_code, const char *error_msg) {
    qwrt_ws_client_t *c = (qwrt_ws_client_t *)conn->user_data;
    /* Same rule as on_close: called from inside uvhttp_ws_process_data, so
     * teardown is deferred to ws_read_cb (after process_data returns). */
    c->closed = 1;
    (void)error_code;
    (void)error_msg;
    if (JS_IsFunction(c->jsctx, c->onerror))
        JS_Call(c->jsctx, c->onerror, c->ws_obj, 0, NULL);
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

    qwrt_ws_client_t *c = js_mallocz(ctx, sizeof(*c));
    if (!c) return JS_ThrowTypeError(ctx, "wsConnect: OOM");

    c->rt = rt;
    c->jsctx = ctx;
    c->closed = 0;
    c->handshake_done = 0;
    c->onopen = JS_UNDEFINED;
    c->onmessage = JS_UNDEFINED;
    c->onclose = JS_UNDEFINED;
    c->onerror = JS_UNDEFINED;

    /* Extract callbacks from the options object (argv[1]):
     * { onopen, onmessage, onerror, onclose } */
    if (argc >= 2 && JS_IsObject(argv[1])) {
        JSValue fn;
        fn = JS_GetPropertyStr(ctx, argv[1], "onopen");
        if (JS_IsFunction(ctx, fn)) c->onopen = fn;
        else JS_FreeValue(ctx, fn);
        fn = JS_GetPropertyStr(ctx, argv[1], "onmessage");
        if (JS_IsFunction(ctx, fn)) c->onmessage = fn;
        else JS_FreeValue(ctx, fn);
        fn = JS_GetPropertyStr(ctx, argv[1], "onclose");
        if (JS_IsFunction(ctx, fn)) c->onclose = fn;
        else JS_FreeValue(ctx, fn);
        fn = JS_GetPropertyStr(ctx, argv[1], "onerror");
        if (JS_IsFunction(ctx, fn)) c->onerror = fn;
        else JS_FreeValue(ctx, fn);
    }
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
    /* Hidden back-pointer so pal.wsSend/wsClose can recover the C state. */
    JS_SetPropertyStr(ctx, obj, "_wsClient",
                      JS_NewInt64(ctx, (int64_t)(uintptr_t)c));
    c->ws_obj = JS_DupValue(ctx, obj);

    /* Create TCP socket (embedded handle) */
    memset(&c->tcp, 0, sizeof(c->tcp));
    if (uv_tcp_init(&rt->loop, &c->tcp) != 0) {
        js_free(ctx, c);
        return JS_ThrowTypeError(ctx, "wsConnect: tcp init failed");
    }
    c->tcp_active = 1;
    c->tcp.data = c;  /* read/close callbacks recover the client state via handle->data */

    struct sockaddr_in dest;
    if (uv_ip4_addr(host_buf, port, &dest) != 0) {
        ws_client_free(c);
        return JS_ThrowTypeError(ctx, "wsConnect: cannot resolve host '%s' — only IPv4 literals supported (use uv_getaddrinfo for DNS)", host_buf);
    }

    uv_connect_t *connect_req = js_mallocz(ctx, sizeof(uv_connect_t));
    if (!connect_req) { ws_client_free(c); return JS_ThrowTypeError(ctx, "wsConnect: OOM"); }
    connect_req->data = c;

    int r = uv_tcp_connect(connect_req, &c->tcp, (const struct sockaddr *)&dest, on_connect);
    if (r != 0) {
        ws_client_free(c);
        js_free(ctx, connect_req);
        return JS_ThrowTypeError(ctx, "wsConnect: connection failed (%s)", uv_strerror(r));
    }

    return obj;
}

/* pal.wsSend(conn, data) — send a text frame */
JSValue js_pal_ws_send(JSContext *ctx, JSValueConst this_val,
                       int argc, JSValueConst *argv) {
    (void)this_val;
    if (argc < 2 || !JS_IsObject(argv[0]) || !JS_IsString(argv[1]))
        return JS_ThrowTypeError(ctx, "wsSend: conn (object) and data (string) required");

    JSValue pv = JS_GetPropertyStr(ctx, argv[0], "_wsClient");
    if (!JS_IsNumber(pv)) { JS_FreeValue(ctx, pv); return JS_UNDEFINED; }
    int64_t ptr = 0;
    JS_ToInt64(ctx, &ptr, pv);
    JS_FreeValue(ctx, pv);
    qwrt_ws_client_t *c = (qwrt_ws_client_t *)(uintptr_t)ptr;
    if (!c || c->closed || c->close_sent || !c->ws_conn || !c->wctx)
        return JS_UNDEFINED;

    const char *text = JS_ToCString(ctx, argv[1]);
    if (!text) return JS_EXCEPTION;
    uvhttp_ws_send_text(c->wctx, c->ws_conn, text, strlen(text));
    JS_FreeCString(ctx, text);
    return JS_UNDEFINED;
}

/* pal.wsClose(conn, code, reason) — send close frame */
JSValue js_pal_ws_close(JSContext *ctx, JSValueConst this_val,
                        int argc, JSValueConst *argv) {
    (void)this_val;
    if (argc < 1 || !JS_IsObject(argv[0]))
        return JS_ThrowTypeError(ctx, "wsClose: conn (object) required");

    JSValue pv = JS_GetPropertyStr(ctx, argv[0], "_wsClient");
    if (!JS_IsNumber(pv)) { JS_FreeValue(ctx, pv); return JS_UNDEFINED; }
    int64_t ptr = 0;
    JS_ToInt64(ctx, &ptr, pv);
    JS_FreeValue(ctx, pv);
    qwrt_ws_client_t *c = (qwrt_ws_client_t *)(uintptr_t)ptr;
    if (!c || c->closed || c->close_sent || !c->ws_conn || !c->wctx)
        return JS_UNDEFINED;

    int code = 1000;
    const char *reason = "";
    if (argc >= 2 && JS_IsNumber(argv[1]))
        JS_ToInt32(ctx, &code, argv[1]);
    if (argc >= 3 && JS_IsString(argv[2]))
        reason = JS_ToCString(ctx, argv[2]);

    uvhttp_ws_close(c->wctx, c->ws_conn, code, reason);
    if (reason && argc >= 3 && JS_IsString(argv[2]))
        JS_FreeCString(ctx, reason);
    c->close_sent = 1;   /* close initiated; teardown deferred until handshake completes */
    return JS_UNDEFINED;
}

#endif /* QWRT_WITH_HTTPSERVER */
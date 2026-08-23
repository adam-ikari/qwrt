/**
 * qwrt TCP socket PAL — raw libuv TCP for JS-level protocol implementations
 *
 * Provides pal.tcpConnect/tcpWrite/tcpClose so the JS polyfill can implement
 * application-layer protocols (e.g. RFC 6455 WebSocket) on top of raw TCP,
 * without coupling the WS client to uvhttp's WS client library.
 *
 * Design: C handles the libuv async I/O (DNS, connect, read, write, close);
 * the JS layer handles protocol framing, masking, and handshake parsing.
 * This mirrors the fetch API pattern where C provides transport and JS
 * provides protocol semantics.
 */

#include "qwrt_internal.h"
#include <uv.h>
#include <string.h>
#include <stdlib.h>

/* ── Per-connection state ── */
typedef struct qwrt_tcp_client qwrt_tcp_client_t;

/* Wrapper for uv_write requests: embeds the data pointer so the write
 * callback can free both the request and the buffer after the write completes. */
typedef struct {
    uv_write_t req;
    qwrt_tcp_client_t *client;
    void *data;
} tcp_write_req_t;

struct qwrt_tcp_client {
    qwrt_t *rt;
    uv_tcp_t tcp;            /* embedded; closed async, freed in close_cb */
    int tcp_active;          /* 1 once uv_tcp_init succeeded */
    int freed;               /* tcp_client_free idempotency guard */
    int closed;              /* onclose delivered */
    JSContext *jsctx;
    JSValue handle_obj;      /* rooted JS handle object */
    JSValue ondata;          /* callback: ondata(data: ArrayBuffer) */
    JSValue onerror;         /* callback: onerror(msg: string) */
    JSValue onclose;         /* callback: onclose(code: int) */
    JSValue onconnect;       /* callback: onconnect() — TCP established */
    uv_getaddrinfo_t addr_req;  /* DNS resolution */
    uv_connect_t connect_req;   /* TCP connect */
    char host[256];
    int port;
    struct qwrt_tcp_client *next;
};

/* ── Forward declarations ── */
static void tcp_alloc_cb(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf);
static void tcp_read_cb(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf);
static void tcp_connect_cb(uv_connect_t *req, int status);
static void tcp_dns_cb(uv_getaddrinfo_t *req, int status, struct addrinfo *res);
static void tcp_write_cb(uv_write_t *req, int status);
static void tcp_close_cb(uv_handle_t *handle);
static void tcp_error(qwrt_tcp_client_t *c, const char *msg);

/* ── Teardown ── */
static void tcp_client_free(qwrt_tcp_client_t *c) {
    if (!c || c->freed) return;
    c->freed = 1;
    c->closed = 1;

    if (c->jsctx) {
        JS_FreeValue(c->jsctx, c->handle_obj);
        JS_FreeValue(c->jsctx, c->ondata);
        JS_FreeValue(c->jsctx, c->onerror);
        JS_FreeValue(c->jsctx, c->onclose);
        JS_FreeValue(c->jsctx, c->onconnect);
    }
    if (c->tcp_active) {
        c->tcp_active = 0;
        uv_read_stop((uv_stream_t *)&c->tcp);
        uv_close((uv_handle_t *)&c->tcp, tcp_close_cb);
    } else {
        js_free(c->jsctx, c);
    }
}

/* Run after uv_close completes — safe to free the struct. */
static void tcp_close_cb(uv_handle_t *handle) {
    qwrt_tcp_client_t *c = (qwrt_tcp_client_t *)handle->data;
    if (c && c->jsctx)
        js_free(c->jsctx, c);
}

/* Fire the JS onerror callback, then teardown. */
static void tcp_error(qwrt_tcp_client_t *c, const char *msg) {
    if (c->closed) return;
    c->closed = 1;
    if (c->jsctx && JS_IsFunction(c->jsctx, c->onerror)) {
        JSValue err = JS_NewString(c->jsctx, msg);
        JS_Call(c->jsctx, c->onerror, c->handle_obj, 1, (JSValueConst[]){err});
        JS_FreeValue(c->jsctx, err);
    }
    tcp_client_free(c);
}

/* ── Alloc callback ── */
static void tcp_alloc_cb(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    qwrt_tcp_client_t *c = (qwrt_tcp_client_t *)handle->data;
    if (!c || c->freed) { buf->base = NULL; buf->len = 0; return; }
    buf->base = (char *)js_malloc(c->jsctx, suggested_size);
    buf->len = buf->base ? suggested_size : 0;
}

/* ── Read callback ── */
static void tcp_read_cb(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
    qwrt_tcp_client_t *c = (qwrt_tcp_client_t *)(((uv_tcp_t *)stream)->data);
    if (!c) {
        if (buf->base) free(buf->base);
        return;
    }
    if (c->freed) {
        if (buf->base) js_free(c->jsctx, buf->base);
        return;
    }

    if (nread < 0) {
        if (buf->base) js_free(c->jsctx, buf->base);
        if (nread == UV_EOF) {
            if (!c->closed) {
                c->closed = 1;
                if (JS_IsFunction(c->jsctx, c->onclose))
                    JS_Call(c->jsctx, c->onclose, c->handle_obj, 0, NULL);
            }
            tcp_client_free(c);
        } else {
            tcp_error(c, uv_strerror((int)nread));
        }
        return;
    }

    if (nread > 0 && !c->closed) {
        /* Copy data into a JS-owned ArrayBuffer and deliver to ondata. */
        JSValue ab = JS_NewArrayBufferCopy(c->jsctx, (const uint8_t *)buf->base, (size_t)nread);
        js_free(c->jsctx, buf->base);  /* our copy is no longer needed */
        if (JS_IsException(ab)) {
            tcp_error(c, "OOM in ondata");
            return;
        }
        if (JS_IsFunction(c->jsctx, c->ondata))
            JS_Call(c->jsctx, c->ondata, c->handle_obj, 1, (JSValueConst[]){ab});
        JS_FreeValue(c->jsctx, ab);
        return;
    }

    if (buf->base) js_free(c->jsctx, buf->base);
}

/* ── Write callback: free the request struct AND the data buffer ── */
static void tcp_write_cb(uv_write_t *req, int status) {
    (void)status;
    tcp_write_req_t *wr = (tcp_write_req_t *)req;
    qwrt_tcp_client_t *c = wr->client;
    if (c && c->jsctx) {
        js_free(c->jsctx, wr->data);
        js_free(c->jsctx, wr);
    } else {
        free(wr->data);
        free(wr);
    }
}

/* ── Connect callback ── */
static void tcp_connect_cb(uv_connect_t *req, int status) {
    qwrt_tcp_client_t *c = (qwrt_tcp_client_t *)req->data;

    if (!c || c->freed) return;

    if (status < 0) {
        char errbuf[128];
        snprintf(errbuf, sizeof(errbuf), "TCP connect failed: %s", uv_strerror(status));
        tcp_error(c, errbuf);
        return;
    }

    /* Fire onconnect so the JS layer can send its protocol handshake */
    if (c->jsctx && JS_IsFunction(c->jsctx, c->onconnect))
        JS_Call(c->jsctx, c->onconnect, c->handle_obj, 0, NULL);

    int r = uv_read_start((uv_stream_t *)&c->tcp, tcp_alloc_cb, tcp_read_cb);
    if (r != 0) {
        tcp_error(c, uv_strerror(r));
    }
}

/* ── DNS callback ── */
static void tcp_dns_cb(uv_getaddrinfo_t *req, int status, struct addrinfo *res) {
    qwrt_tcp_client_t *c = (qwrt_tcp_client_t *)req->data;
    if (!c || c->freed) {
        if (res) uv_freeaddrinfo(res);
        return;
    }

    if (status < 0 || !res) {
        char buf[512];
        snprintf(buf, sizeof(buf), "DNS resolution failed for '%s': %s",
                 c->host, uv_strerror(status));
        if (res) uv_freeaddrinfo(res);
        tcp_error(c, buf);
        return;
    }

    struct sockaddr_in *addr = (struct sockaddr_in *)res->ai_addr;
    c->connect_req.data = c;
    uv_tcp_connect(&c->connect_req, &c->tcp, (const struct sockaddr *)addr, tcp_connect_cb);
    uv_freeaddrinfo(res);
}

/* ── PAL: tcpConnect(host, port, callbacks) ── */
JSValue js_pal_tcp_connect(JSContext *ctx, JSValueConst this_val,
                           int argc, JSValueConst *argv) {
    (void)this_val;
    if (argc < 3 || !JS_IsString(argv[0]) || !JS_IsNumber(argv[1]) || !JS_IsObject(argv[2]))
        return JS_ThrowTypeError(ctx, "tcpConnect(host, port, callbacks) required");

    const char *host = JS_ToCString(ctx, argv[0]);
    if (!host) return JS_EXCEPTION;

    int32_t port = 0;
    JS_ToInt32(ctx, &port, argv[1]);
    if (port <= 0 || port > 65535) {
        JS_FreeCString(ctx, host);
        return JS_ThrowTypeError(ctx, "tcpConnect: invalid port %d", (int)port);
    }

    qwrt_t *rt = qwrt_get_rt_from_ctx(ctx);
    if (!rt) { JS_FreeCString(ctx, host); return JS_ThrowTypeError(ctx, "tcpConnect: no runtime"); }

    qwrt_tcp_client_t *c = js_mallocz(ctx, sizeof(*c));
    if (!c) { JS_FreeCString(ctx, host); return JS_ThrowTypeError(ctx, "tcpConnect: OOM"); }

    c->rt = rt;
    c->jsctx = ctx;
    c->ondata = JS_UNDEFINED;
    c->onerror = JS_UNDEFINED;
    c->onclose = JS_UNDEFINED;
    c->onconnect = JS_UNDEFINED;

    /* Extract callbacks */
    JSValue fn;
    fn = JS_GetPropertyStr(ctx, argv[2], "onconnect");
    if (JS_IsFunction(ctx, fn)) c->onconnect = fn; else JS_FreeValue(ctx, fn);
    fn = JS_GetPropertyStr(ctx, argv[2], "ondata");
    if (JS_IsFunction(ctx, fn)) c->ondata = fn; else JS_FreeValue(ctx, fn);
    fn = JS_GetPropertyStr(ctx, argv[2], "onerror");
    if (JS_IsFunction(ctx, fn)) c->onerror = fn; else JS_FreeValue(ctx, fn);
    fn = JS_GetPropertyStr(ctx, argv[2], "onclose");
    if (JS_IsFunction(ctx, fn)) c->onclose = fn; else JS_FreeValue(ctx, fn);

    /* Create the JS handle object */
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "_tcpClient", JS_NewInt64(ctx, (int64_t)(uintptr_t)c));
    c->handle_obj = JS_DupValue(ctx, obj);

    /* Create TCP socket */
    if (uv_tcp_init(&rt->loop, &c->tcp) != 0) {
        js_free(ctx, c);
        JS_FreeCString(ctx, host);
        JS_FreeValue(ctx, obj);
        return JS_ThrowTypeError(ctx, "tcpConnect: tcp init failed");
    }
    c->tcp_active = 1;
    c->tcp.data = c;

    strncpy(c->host, host, sizeof(c->host) - 1);
    c->host[sizeof(c->host) - 1] = '\0';
    c->port = (int)port;
    JS_FreeCString(ctx, host);

    /* Start DNS resolution */
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = 0;

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", (int)port);

    c->addr_req.data = c;
    int r = uv_getaddrinfo(&rt->loop, &c->addr_req, tcp_dns_cb, c->host, port_str, &hints);
    if (r != 0) {
        tcp_error(c, "uv_getaddrinfo failed");
        tcp_client_free(c);
        JS_FreeValue(ctx, obj);
        return JS_ThrowTypeError(ctx, "tcpConnect: DNS resolution failed");
    }

    return obj;
}

/* ── PAL: tcpWrite(handle, data) ── */
JSValue js_pal_tcp_write(JSContext *ctx, JSValueConst this_val,
                         int argc, JSValueConst *argv) {
    (void)this_val;
    if (argc < 2 || !JS_IsObject(argv[0]))
        return JS_ThrowTypeError(ctx, "tcpWrite(handle, data) required");

    JSValue pv = JS_GetPropertyStr(ctx, argv[0], "_tcpClient");
    if (!JS_IsNumber(pv)) { JS_FreeValue(ctx, pv); return JS_UNDEFINED; }
    int64_t ptr = 0;
    JS_ToInt64(ctx, &ptr, pv);
    JS_FreeValue(ctx, pv);
    qwrt_tcp_client_t *c = (qwrt_tcp_client_t *)(uintptr_t)ptr;
    if (!c || c->closed || c->freed || !c->tcp_active)
        return JS_UNDEFINED;

    /* Extract the raw bytes from the argument (string or ArrayBuffer) */
    size_t len;
    void *data;

    if (JS_IsString(argv[1])) {
        const char *str = JS_ToCString(ctx, argv[1]);
        if (!str) return JS_EXCEPTION;
        len = strlen(str);
        data = js_malloc(ctx, len);
        if (!data) { JS_FreeCString(ctx, str); return JS_ThrowTypeError(ctx, "tcpWrite: OOM"); }
        memcpy(data, str, len);
        JS_FreeCString(ctx, str);
    } else if (JS_IsArrayBuffer(argv[1])) {
        size_t ab_len;
        uint8_t *src = JS_GetArrayBuffer(ctx, &ab_len, argv[1]);
        if (!src) { return JS_ThrowTypeError(ctx, "tcpWrite: invalid ArrayBuffer"); }
        data = js_malloc(ctx, ab_len);
        if (!data) { return JS_ThrowTypeError(ctx, "tcpWrite: OOM"); }
        memcpy(data, src, ab_len);
        len = ab_len;
    } else {
        return JS_ThrowTypeError(ctx, "tcpWrite: data must be string or ArrayBuffer");
    }

    /* Allocate the write request wrapper (freed in tcp_write_cb with data) */
    tcp_write_req_t *wr = (tcp_write_req_t *)js_malloc(ctx, sizeof(tcp_write_req_t));
    if (!wr) {
        js_free(ctx, data);
        return JS_ThrowTypeError(ctx, "tcpWrite: OOM");
    }
    wr->client = c;
    wr->data = data;

    uv_buf_t wbuf = uv_buf_init((char *)data, (unsigned int)len);
    int r = uv_write(&wr->req, (uv_stream_t *)&c->tcp, &wbuf, 1, tcp_write_cb);
    if (r != 0) {
        js_free(ctx, wr);
        js_free(ctx, data);
        return JS_UNDEFINED;
    }
    return JS_UNDEFINED;
}

/* ── PAL: tcpClose(handle) ── */
JSValue js_pal_tcp_close(JSContext *ctx, JSValueConst this_val,
                         int argc, JSValueConst *argv) {
    (void)this_val;
    if (argc < 1 || !JS_IsObject(argv[0]))
        return JS_ThrowTypeError(ctx, "tcpClose(handle) required");

    JSValue pv = JS_GetPropertyStr(ctx, argv[0], "_tcpClient");
    if (!JS_IsNumber(pv)) { JS_FreeValue(ctx, pv); return JS_UNDEFINED; }
    int64_t ptr = 0;
    JS_ToInt64(ctx, &ptr, pv);
    JS_FreeValue(ctx, pv);
    qwrt_tcp_client_t *c = (qwrt_tcp_client_t *)(uintptr_t)ptr;
    if (!c || c->closed || c->freed)
        return JS_UNDEFINED;

    c->closed = 1;
    if (c->jsctx && JS_IsFunction(c->jsctx, c->onclose))
        JS_Call(c->jsctx, c->onclose, c->handle_obj, 0, NULL);
    tcp_client_free(c);
    return JS_UNDEFINED;
}

/* ── Module init ── */
void qwrt_tcp_io_init(JSContext *ctx, JSValue pal) {
    JS_SetPropertyStr(ctx, pal, "tcpConnect", JS_NewCFunction(ctx, js_pal_tcp_connect, "tcpConnect", 3));
    JS_SetPropertyStr(ctx, pal, "tcpWrite", JS_NewCFunction(ctx, js_pal_tcp_write, "tcpWrite", 2));
    JS_SetPropertyStr(ctx, pal, "tcpClose", JS_NewCFunction(ctx, js_pal_tcp_close, "tcpClose", 1));
}
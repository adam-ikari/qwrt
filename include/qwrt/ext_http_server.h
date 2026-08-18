#ifndef QWRT_EXT_HTTP_SERVER_H
#define QWRT_EXT_HTTP_SERVER_H

#include "qwrt/qwrt.h"

/* HTTP/HTTPS/WebSocket server extension — uvhttp-backed.
 *
 * When compiled with QWRT_WITH_HTTPSERVER, registers the WinterCG-style
 * `serve({ port, hostname, tls, static, ws }, handler)` global. The handler
 * receives a WHATWG `Request` and may return a `Response`, a string, or a
 * Promise resolving to one; the runtime serializes it back to the socket.
 *
 * Features (all of uvhttp): HTTP/1.1, HTTPS via mbedTLS, WebSocket endpoints,
 * static-file serving, gzip compression, LRU cache.
 *
 * Registered automatically when QWRT_WITH_HTTPSERVER is on (it is in the
 * default QWRT_EXTENSIONS set; see qwrt_ext_registry.h).
 */

extern const qwrt_ext_t qwrt_http_server_ext;

#endif /* QWRT_EXT_HTTP_SERVER_H */

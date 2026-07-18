---
title: Shared PAL Helpers
description: Shared utility functions for Qwrt.js PAL implementations — JSON helpers, string utilities, and common patterns.
---

# Shared PAL Helpers

`pal_common.h` and `pal_common.c` provide shared utilities used by all three built-in PAL implementations. Use them in your own PAL to avoid duplicating JSON construction and URL parsing code.

## JSON Helpers

### `pal_build_http_json`

Build a complete HTTP response JSON string.

```c
char *pal_build_http_json(int status, const char *headers_json,
                          const char *body, size_t body_len);
```

Returns a `malloc`'d string in the format:
```json
{"status":200,"headers":{...},"body":"..."}
```

The caller must `free()` the returned string.

| Parameter | Description |
|-----------|-------------|
| `status` | HTTP status code (200, 404, etc.) |
| `headers_json` | Pre-built JSON object string of headers, or `"{}"` for none |
| `body` | Response body bytes |
| `body_len` | Length of body in bytes |

Returns NULL on allocation failure.

### `pal_build_headers_json`

Build a JSON object from parallel key/value arrays.

```c
char *pal_build_headers_json(const char *const *keys,
                             const char *const *vals, int count);
```

```c
const char *keys[] = {"Content-Type", "Content-Length"};
const char *vals[] = {"text/html", "1024"};
char *headers = pal_build_headers_json(keys, vals, 2);
// headers = "{\"Content-Type\":\"text/html\",\"Content-Length\":\"1024\"}"
free(headers);
```

Returns a `malloc`'d string. Caller must `free()`. Returns `"{}"` if count is 0.

### `pal_json_escape`

Escape a string for inclusion in JSON.

```c
char *pal_json_escape(const char *src, size_t len, size_t *out_len);
```

- `src` — raw bytes (may contain quotes, backslashes, control chars)
- `len` — length of `src` in bytes
- `out_len` — if non-NULL, receives the escaped string length

Returns a `malloc`'d string, or NULL on allocation failure. The caller must `free()`.

Escapes: `"`, `\`, `\n`, `\r`, `\t`, and control characters (as `\u00XX`).

## URL Parsing

### `pal_parse_url`

Parse a URL into its components.

```c
typedef struct {
    char *host;       // hostname (malloc'd, caller frees)
    int port;         // port number (defaults based on scheme)
    const char *path; // path within the URL (points into the original string)
    int tls;          // 1 if https://, 0 if http://
} pal_url_t;

int pal_parse_url(const char *url, pal_url_t *out);
```

Returns `QWRT_OK` on success, `QWRT_ERR_INVALID_ARG` on malformed URL.

```c
pal_url_t parsed;
if (pal_parse_url("https://example.com:8443/api/data", &parsed) == QWRT_OK) {
    printf("host: %s\n", parsed.host);   // "example.com"
    printf("port: %d\n", parsed.port);   // 8443
    printf("path: %s\n", parsed.path);   // "/api/data"
    printf("tls:  %d\n", parsed.tls);    // 1
    pal_url_free(&parsed);
}
```

- Default ports: 443 for HTTPS, 80 for HTTP
- IPv6 addresses are supported: `http://[::1]:8080/path`
- The `path` pointer is into the original URL string — don't free it

### `pal_url_free`

Free a parsed URL's allocated memory.

```c
void pal_url_free(pal_url_t *u);
```

## Linking

Link your PAL against `qwrt_pal_common`:

```cmake
target_link_libraries(qwrt_mypal PRIVATE qwrt_pal_common)
```

And include the header:

```c
#include "pal_common.h"  // from platform/ directory
```

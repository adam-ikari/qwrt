# PAL Error Codes

All PAL methods use standardized error codes from the `qwrt_pal_err_t` enum. Return these instead of magic numbers like `-1`, `-2`, etc.

## Enum Definition

```c
typedef enum {
    QWRT_OK                 =  0,   /* success */
    QWRT_ERR_GENERIC        = -1,   /* unknown / unspecified error */
    QWRT_ERR_NOT_FOUND      = -2,   /* file, key, or resource not found */
    QWRT_ERR_IO             = -3,   /* read / write / close failure */
    QWRT_ERR_PERMISSION     = -4,   /* access denied (EACCES, etc.) */
    QWRT_ERR_NETWORK        = -5,   /* DNS, connect, or TLS failure */
    QWRT_ERR_INVALID_ARG    = -6,   /* bad parameter (NULL where required, etc.) */
    QWRT_ERR_CANCELLED      = -7,   /* operation cancelled by caller */
    QWRT_ERR_BUSY           = -8,   /* resource temporarily unavailable */
    QWRT_ERR_NOT_SUPPORTED  = -9,   /* operation not implemented by this PAL */
    QWRT_ERR_TIMEOUT        = -10,  /* operation timed out */
    QWRT_ERR_NO_MEMORY      = -11,  /* allocation failed */
} qwrt_pal_err_t;
```

## Design Rationale

- **Negative values** distinguish errors from byte counts / event counts (which are ≥ 0)
- **`int` width** — compatible with all PAL callback signatures
- **Sparse range** — room for future error codes without renumbering

## Usage

### In PAL Implementations

```c
#include <qwrt/qwrt.h>

static void my_http_request(qwrt_pal_t *pal, const char *url, ...) {
    my_state_t *s = (my_state_t *)pal->user_data;

    if (!url) {
        cb(cb_data, QWRT_ERR_INVALID_ARG, NULL, 0);
        return;
    }

    int sock = connect_to_host(url);
    if (sock < 0) {
        cb(cb_data, QWRT_ERR_NETWORK, NULL, 0);
        return;
    }

    // ... on success:
    cb(cb_data, QWRT_OK, response_data, response_len);
}
```

### In bridge.c (JS↔PAL Glue)

```c
// Before: magic numbers
if (status < 0) {
    if (status == -5) { /* network error */ }
}

// After: named constants
if (status < 0) {
    if (status == QWRT_ERR_NETWORK) { /* network error */ }
}
```

## Error to JS Mapping

bridge.c maps PAL error codes to JavaScript exceptions:

| PAL Error | JS Result |
|-----------|-----------|
| `QWRT_OK` | Promise resolved with data |
| `QWRT_ERR_NOT_FOUND` | Rejected with `"Not Found"` |
| `QWRT_ERR_NETWORK` | Rejected with `"Network Error"` |
| `QWRT_ERR_TIMEOUT` | Rejected with `"Timeout"` |
| `QWRT_ERR_CANCELLED` | Rejected with `"AbortError"` (DOMException) |
| `QWRT_ERR_PERMISSION` | Rejected with `"Permission Denied"` |
| `QWRT_ERR_NO_MEMORY` | Rejected with `"Out of Memory"` |
| Other `QWRT_ERR_*` | Rejected with generic error message |

## Best Practices

1. **Always use the enum constants**, never raw integers
2. **Check for `QWRT_OK`** rather than `status >= 0` — it's more explicit
3. **Default to `QWRT_ERR_GENERIC`** when the exact error category is unclear
4. **Use `QWRT_ERR_NOT_SUPPORTED`** for unimplemented optional methods rather than leaving them NULL

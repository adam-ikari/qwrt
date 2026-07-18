# Error Codes

All PAL callbacks and synchronous methods use standardized `qwrt_pal_err_t` return values.

## `qwrt_pal_err_t`

```c
typedef enum {
    QWRT_OK                 =  0,
    QWRT_ERR_GENERIC        = -1,
    QWRT_ERR_NOT_FOUND      = -2,
    QWRT_ERR_IO             = -3,
    QWRT_ERR_PERMISSION     = -4,
    QWRT_ERR_NETWORK        = -5,
    QWRT_ERR_INVALID_ARG    = -6,
    QWRT_ERR_CANCELLED      = -7,
    QWRT_ERR_BUSY            = -8,
    QWRT_ERR_NOT_SUPPORTED   = -9,
    QWRT_ERR_TIMEOUT         = -10,
    QWRT_ERR_NO_MEMORY       = -11,
} qwrt_pal_err_t;
```

## Code Reference

| Constant | Value | Description |
|----------|-------|-------------|
| `QWRT_OK` | 0 | Success |
| `QWRT_ERR_GENERIC` | -1 | Unknown or unspecified error |
| `QWRT_ERR_NOT_FOUND` | -2 | File, key, or resource not found |
| `QWRT_ERR_IO` | -3 | Read, write, or close failure |
| `QWRT_ERR_PERMISSION` | -4 | Access denied (EACCES, etc.) |
| `QWRT_ERR_NETWORK` | -5 | DNS, connect, or TLS failure |
| `QWRT_ERR_INVALID_ARG` | -6 | Bad parameter (NULL where required, etc.) |
| `QWRT_ERR_CANCELLED` | -7 | Operation cancelled by caller |
| `QWRT_ERR_BUSY` | -8 | Resource temporarily unavailable |
| `QWRT_ERR_NOT_SUPPORTED` | -9 | Operation not implemented by this PAL |
| `QWRT_ERR_TIMEOUT` | -10 | Operation timed out |
| `QWRT_ERR_NO_MEMORY` | -11 | Allocation failed |

## Design Notes

Error codes use **negative values** so they're distinguishable from byte counts and event counts, which are ≥ 0 on success. This lets PAL implementations return a single `int` that the caller can check with:

```c
if (status < 0) {
    // handle error
} else {
    // status is a byte count or event count
}
```

Every PAL implementation **MUST** return these values so callers can branch on named constants instead of magic numbers.

/* ================================================================
 * Platform Interface (qwrt_pi_t)
 *
 * THE universal platform abstraction layer. 25 primitives + 13 URI
 * schemes. Every resource qwrt needs flows through this interface.
 * qwrt makes NO direct system calls.
 *
 * Execution primitives:
 *   spawn       — create a new execution unit
 *   exit        — terminate the current execution unit
 *   yield       — relinquish CPU to other execution units
 *   sleep       — suspend the current execution unit for a duration
 *
 * Memory management:
 *   allocate    — allocate memory
 *   deallocate  — free memory
 *   protect     — set memory access permissions (WASM linear memory)
 *   share       — share memory with other execution units
 *
 * Synchronization & communication:
 *   atomic_read  — atomically read a memory location
 *   atomic_write — atomically write a memory location
 *   wait         — wait for a condition on an address
 *   notify       — wake waiters on an address
 *
 * Byte streams (URI-based I/O):
 *   stream_open  — open a stream by URI
 *   stream_close — close a stream
 *   stream_read  — read from a stream (async callback)
 *   stream_write — write to a stream
 *   stream_seek  — seek within a stream
 *
 * Time & randomness:
 *   clock_ns     — monotonic nanosecond clock
 *   random_bytes — cryptographically secure random bytes
 *
 * Cryptography:
 *   hash         — compute a hash digest
 *   aead_encrypt — authenticated encryption
 *   aead_decrypt — authenticated decryption
 *   sign         — asymmetric signature generation
 *   verify       — asymmetric signature verification
 *   kdf          — key derivation function
 *
 * URI schemes:
 *   tcp://host:port       TCP connection
 *   udp://host:port       UDP socket
 *   tls://host:port       TLS encrypted connection
 *   http://host/path      HTTP request
 *   https://host/path     HTTPS request
 *   ws://host/path        WebSocket connection
 *   wss://host/path       Secure WebSocket connection
 *   file:///path          Logical file resource
 *   pair://name           Bidirectional inter-process communication
 *   pipe://name           Unidirectional pipe
 *   shm://name            Shared memory region
 *   null://               Null device (/dev/null equivalent)
 *   platform://name       Platform-specific resource
 *
 * Contract levels:
 *   [REQUIRED]  Must be non-NULL. qwrt_create rejects NULL.
 *   [OPTIONAL]  May be NULL. Returns QWRT_ERR_NOT_SUPPORTED.
 * ================================================================ */

#ifndef QWRT_PI_H
#define QWRT_PI_H

#include <stdint.h>
#include <stddef.h>

/* Forward declarations */
typedef struct qwrt_pi_t qwrt_pi_t;

/* ── Error codes ───────────────────────────────────────────────── */

typedef enum {
    QWRT_OK                 =  0,
    QWRT_ERR_GENERIC        = -1,
    QWRT_ERR_NOT_FOUND      = -2,
    QWRT_ERR_IO             = -3,
    QWRT_ERR_PERMISSION     = -4,
    QWRT_ERR_NETWORK        = -5,
    QWRT_ERR_INVALID_ARG    = -6,
    QWRT_ERR_CANCELLED      = -7,
    QWRT_ERR_BUSY           = -8,
    QWRT_ERR_NOT_SUPPORTED  = -9,
    QWRT_ERR_TIMEOUT        = -10,
    QWRT_ERR_NO_MEMORY      = -11,
} qwrt_err_t;

/* ── Clock IDs ─────────────────────────────────────────────────── */

typedef enum {
    QWRT_CLOCK_MONOTONIC = 0,  /* monotonic time, unaffected by NTP */
    QWRT_CLOCK_REALTIME  = 1,  /* wall-clock time, may jump */
} qwrt_clock_id_t;

/* ── Memory protection flags ───────────────────────────────────── */

typedef enum {
    QWRT_PROT_NONE  = 0,
    QWRT_PROT_READ  = 1,
    QWRT_PROT_WRITE = 2,
    QWRT_PROT_EXEC  = 4,
} qwrt_prot_t;

/* ── Stream open mode ──────────────────────────────────────────── */

typedef enum {
    QWRT_STREAM_READ  = 1,
    QWRT_STREAM_WRITE = 2,
} qwrt_stream_mode_t;

/* ── Hash algorithms ───────────────────────────────────────────── */

typedef enum {
    QWRT_HASH_SHA256 = 0,
    QWRT_HASH_SHA384 = 1,
    QWRT_HASH_SHA512 = 2,
} qwrt_hash_algo_t;

/* ── AEAD algorithms ───────────────────────────────────────────── */

typedef enum {
    QWRT_AEAD_AES128_GCM = 0,
    QWRT_AEAD_AES256_GCM = 1,
} qwrt_aead_algo_t;

/* ── Signature algorithms ──────────────────────────────────────── */

typedef enum {
    QWRT_SIGN_ECDSA_P256_SHA256 = 0,
    QWRT_SIGN_ECDSA_P384_SHA384 = 1,
    QWRT_SIGN_RSA_PSS_SHA256    = 2,
} qwrt_sign_algo_t;

/* ── KDF algorithms ────────────────────────────────────────────── */

typedef enum {
    QWRT_KDF_PBKDF2_SHA256 = 0,
    QWRT_KDF_HKDF_SHA256   = 1,
} qwrt_kdf_algo_t;

/* ── Async callback type ───────────────────────────────────────── */

typedef void (*qwrt_pi_cb_t)(void *user_data, int status,
                              const char *data, size_t data_len);

/* ================================================================
 * Platform Interface struct
 * ================================================================ */

struct qwrt_pi_t {
    /* ── Identity ───────────────────────────────────────────────── */
    void *user_data;
    uint32_t version;       /* must be 2 for this ABI */
    const char *name;       /* OPTIONAL */

    /* ── Execution context [REQUIRED] ──────────────────────────── */
    void *(*spawn)(qwrt_pi_t *pi, void (*entry)(void *arg), void *arg);
    void  (*exit)(qwrt_pi_t *pi, int code);
    void  (*yield)(qwrt_pi_t *pi);
    void  (*sleep)(qwrt_pi_t *pi, uint64_t ns);

    /* ── Memory management [OPTIONAL: malloc/free if NULL] ─────── */
    void *(*allocate)(qwrt_pi_t *pi, size_t size);
    void  (*deallocate)(qwrt_pi_t *pi, void *ptr);
    int   (*protect)(qwrt_pi_t *pi, void *addr, size_t len, int prot);
    void *(*share)(qwrt_pi_t *pi, void *addr, size_t len);

    /* ── Synchronization [OPTIONAL] ────────────────────────────── */
    uint64_t (*atomic_read)(qwrt_pi_t *pi, void *addr, int bits);
    void     (*atomic_write)(qwrt_pi_t *pi, void *addr, uint64_t val, int bits);
    int      (*wait)(qwrt_pi_t *pi, void *addr, uint64_t expected, int64_t timeout_ns);
    void     (*notify)(qwrt_pi_t *pi, void *addr, uint32_t count);

    /* ── Byte streams [OPTIONAL] ───────────────────────────────── */
    void *(*stream_open)(qwrt_pi_t *pi, const char *uri, int mode);
    void  (*stream_close)(qwrt_pi_t *pi, void *handle);
    void  (*stream_read)(qwrt_pi_t *pi, void *handle, char *buf, size_t len,
                         qwrt_pi_cb_t cb, void *cb_data);
    void  (*stream_write)(qwrt_pi_t *pi, void *handle, const char *data,
                          size_t len, qwrt_pi_cb_t cb, void *cb_data);
    int   (*stream_seek)(qwrt_pi_t *pi, void *handle, int64_t offset, int whence);

    /* ── Time & randomness [REQUIRED] ──────────────────────────── */
    uint64_t (*clock_ns)(qwrt_pi_t *pi, int clock_id);
    void     (*random_bytes)(qwrt_pi_t *pi, uint8_t *buf, size_t len);

    /* ── Cryptography [OPTIONAL] ───────────────────────────────── */
    void (*hash)(qwrt_pi_t *pi, int algo, const uint8_t *data, size_t len,
                 uint8_t *digest, size_t digest_len);
    void (*aead_encrypt)(qwrt_pi_t *pi, int algo,
                         const uint8_t *key, size_t key_len,
                         const uint8_t *nonce, size_t nonce_len,
                         const uint8_t *plain, size_t plain_len,
                         const uint8_t *aad, size_t aad_len,
                         uint8_t *cipher, size_t *cipher_len,
                         uint8_t *tag, size_t tag_len);
    int  (*aead_decrypt)(qwrt_pi_t *pi, int algo,
                         const uint8_t *key, size_t key_len,
                         const uint8_t *nonce, size_t nonce_len,
                         const uint8_t *cipher, size_t cipher_len,
                         const uint8_t *aad, size_t aad_len,
                         const uint8_t *tag, size_t tag_len,
                         uint8_t *plain, size_t *plain_len);
    void (*sign)(qwrt_pi_t *pi, int algo,
                 const uint8_t *key, size_t key_len,
                 const uint8_t *data, size_t data_len,
                 uint8_t *sig, size_t *sig_len);
    int  (*verify)(qwrt_pi_t *pi, int algo,
                   const uint8_t *key, size_t key_len,
                   const uint8_t *data, size_t data_len,
                   const uint8_t *sig, size_t sig_len);
    void (*kdf)(qwrt_pi_t *pi, int algo,
                const uint8_t *password, size_t pw_len,
                const uint8_t *salt, size_t salt_len,
                uint32_t iterations, size_t key_len,
                uint8_t *derived_key);

    /* ── Reserved ───────────────────────────────────────────────── */
    void *reserved[4];
};

#endif /* QWRT_PI_H */
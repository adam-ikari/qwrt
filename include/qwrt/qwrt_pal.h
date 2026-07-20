/* ================================================================
 * PAL (Platform Abstraction Layer) — qwrt_pal_t
 *
 * THE universal platform interface. 25 primitives + 13 URI schemes.
 * Every resource qwrt needs flows through this interface.
 * qwrt makes NO direct system calls.
 *
 * Design principles:
 *   Core never knows thread vs process — Host decides.
 *   yield = cooperative, immediate return to scheduler.
 *   sleep = time-based, used for timers and short sleeps.
 *   wait  = Core suspends, Host wakes via OS I/O or notify.
 *   stream_read returns -2 for "no data yet" — Core decides wait or yield.
 *
 * Execution context (4):
 *   spawn       create new execution unit (Host: thread/process, Core: opaque)
 *   exit        terminate current execution unit
 *   yield       cooperative CPU yield (immediate scheduler return)
 *   sleep       time-based CPU yield (timers, short sleeps, anti-busywait)
 *
 * Memory management (4):
 *   allocate    allocate memory (READ/WRITE/EXEC/SHARED flags)
 *   deallocate  free memory
 *   protect     set memory access permissions (MEM_EXEC optional)
 *   share       share memory with other execution units
 *
 * Synchronization & communication (4):
 *   atomic_read   atomic 64-bit read with memory barrier
 *   atomic_write  atomic 64-bit write with memory barrier
 *   wait          suspend Core, Host wakes via OS I/O or notify
 *   notify        wake suspended Cores (Host calls on I/O ready / event arrival)
 *
 * Byte streams (5):
 *   stream_open   open stream by URI (scheme determines type & semantics)
 *   stream_close  close stream
 *   stream_read   read data (>0=bytes, 0=EOF, -1=error, -2=no data yet)
 *   stream_write  write data
 *   stream_seek   reposition offset (file://, shm:// only)
 *
 * stream_read contract:
 *   -2 = no data available now. Core should wait or yield.
 *   Host notifies via notify() when data arrives on pipe:// events.
 *
 * Time & randomness (2):
 *   clock_ns      monotonic nanosecond clock
 *   random_bytes  cryptographically secure random bytes
 *
 * Cryptography (6):
 *   hash          compute hash digest (SHA-256/384/512)
 *   aead_encrypt  AEAD encrypt (AES-GCM, ChaCha20-Poly1305)
 *   aead_decrypt  AEAD decrypt
 *   sign          asymmetric sign (ECDSA P-256/P-384, Ed25519)
 *   verify        asymmetric verify
 *   kdf           key derivation (HKDF, PBKDF2)
 *
 * URI schemes (13):
 *   tcp://host:port       TCP connection
 *   udp://host:port       UDP socket
 *   tls://host:port       TLS encrypted connection
 *   http://host/path      HTTP request
 *   https://host/path     HTTPS request
 *   ws://host/path        WebSocket connection
 *   wss://host/path       Secure WebSocket connection
 *   file:///path          Logical file (Host maps to real/virtual file)
 *   pair://name           Bidirectional IPC (message boundaries, full-duplex REQ-REP)
 *   pipe://name           Unidirectional pipe (message boundaries, events/notifications)
 *   shm://name            Shared memory region
 *   null://               Null device
 *   platform://name       Platform resource (not file-like, seek may error)
 *
 * Key behavioral contracts:
 *   spawn:      Core doesn't specify thread/process, Host decides
 *   yield:      cooperative yield only, returns to scheduler immediately
 *   sleep:      time-based yield, Worker uses for min-timeout when no runnable Core
 *   wait:       Core removes itself from ready queue, Host wakes via OS or notify
 *   file://:    logical path, no platform info, Host maps to real/virtual
 *   platform://: not file-like, seek may return error
 *   pipe://:    message boundaries, unidirectional. Reserved prefixes:
 *               pipe://lifecycle, pipe://notification/* (Host-managed)
 *   pair://:    message boundaries, bidirectional, one-to-one
 *   crypto:     Core polyfill assembles WinterTC SubtleCrypto from these.
 *               PAL does NOT expose algorithm objects or key lifecycle.
 *
 * Contract levels:
 *   [REQUIRED]  Must be non-NULL. qwrt_create rejects NULL.
 *   [OPTIONAL]  May be NULL. Returns QWRT_ERR_NOT_SUPPORTED.
 * ================================================================ */

#ifndef QWRT_PAL_H
#define QWRT_PAL_H

#include <stdint.h>
#include <stddef.h>

/* Forward declarations */
typedef struct qwrt_pal_t qwrt_pal_t;

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
    QWRT_CLOCK_MONOTONIC = 0,  /* monotonic, unaffected by NTP */
    QWRT_CLOCK_REALTIME  = 1,  /* wall-clock, may jump */
} qwrt_clock_id_t;

/* ── Memory flags ──────────────────────────────────────────────── */

#define QWRT_MEM_READ   1
#define QWRT_MEM_WRITE  2
#define QWRT_MEM_EXEC   4
#define QWRT_MEM_SHARED 8

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
    QWRT_AEAD_AES128_GCM        = 0,
    QWRT_AEAD_AES256_GCM        = 1,
    QWRT_AEAD_CHACHA20_POLY1305 = 2,
} qwrt_aead_algo_t;

/* ── Signature algorithms ──────────────────────────────────────── */

typedef enum {
    QWRT_SIGN_ECDSA_P256_SHA256 = 0,
    QWRT_SIGN_ECDSA_P384_SHA384 = 1,
    QWRT_SIGN_ED25519           = 2,
} qwrt_sign_algo_t;

/* ── KDF algorithms ────────────────────────────────────────────── */

typedef enum {
    QWRT_KDF_HKDF_SHA256   = 0,
    QWRT_KDF_PBKDF2_SHA256 = 1,
} qwrt_kdf_algo_t;

/* ── Async callback ────────────────────────────────────────────── */

typedef void (*qwrt_pal_cb_t)(void *user_data, int status,
                               const char *data, size_t data_len);

/* ================================================================
 * PAL struct (25 primitives)
 * ================================================================ */

struct qwrt_pal_t {
    /* ── Identity ───────────────────────────────────────────────── */
    void       *user_data;
    uint32_t    version;       /* must be 2 for this ABI */
    const char *name;          /* [OPTIONAL] */

    /* ── Execution context (4) [REQUIRED] ──────────────────────── */
    void *(*spawn)(qwrt_pal_t *pal, void (*entry)(void *arg), void *arg);
    void  (*exit)(qwrt_pal_t *pal, int code);
    void  (*yield)(qwrt_pal_t *pal);
    void  (*sleep)(qwrt_pal_t *pal, uint64_t ns);

    /* ── Memory management (4) [OPTIONAL: malloc/free if NULL] ─── */
    void *(*allocate)(qwrt_pal_t *pal, size_t size, int flags);
    void  (*deallocate)(qwrt_pal_t *pal, void *ptr);
    int   (*protect)(qwrt_pal_t *pal, void *addr, size_t len, int prot);
    void *(*share)(qwrt_pal_t *pal, void *addr, size_t len);

    /* ── Synchronization (4) [OPTIONAL] ────────────────────────── */
    uint64_t (*atomic_read)(qwrt_pal_t *pal, void *addr);
    void     (*atomic_write)(qwrt_pal_t *pal, void *addr, uint64_t val);
    int      (*wait)(qwrt_pal_t *pal, void *addr, uint64_t expected,
                     int64_t timeout_ns);
    void     (*notify)(qwrt_pal_t *pal, void *addr, uint32_t count);

    /* ── Byte streams (5) [OPTIONAL] ───────────────────────────── */
    void *(*stream_open)(qwrt_pal_t *pal, const char *uri, int mode);
    void  (*stream_close)(qwrt_pal_t *pal, void *handle);
    int   (*stream_read)(qwrt_pal_t *pal, void *handle, char *buf, size_t len);
    int   (*stream_write)(qwrt_pal_t *pal, void *handle, const char *data,
                          size_t len);
    int   (*stream_seek)(qwrt_pal_t *pal, void *handle, int64_t offset,
                         int whence);

    /* ── Time & randomness (2) [REQUIRED] ──────────────────────── */
    uint64_t (*clock_ns)(qwrt_pal_t *pal, int clock_id);
    void     (*random_bytes)(qwrt_pal_t *pal, uint8_t *buf, size_t len);

    /* ── Cryptography (6) [OPTIONAL] ───────────────────────────── */
    void (*hash)(qwrt_pal_t *pal, int algo,
                 const uint8_t *data, size_t len,
                 uint8_t *digest, size_t digest_len);
    void (*aead_encrypt)(qwrt_pal_t *pal, int algo,
                         const uint8_t *key, size_t key_len,
                         const uint8_t *nonce, size_t nonce_len,
                         const uint8_t *plain, size_t plain_len,
                         const uint8_t *aad, size_t aad_len,
                         uint8_t *cipher, size_t *cipher_len,
                         uint8_t *tag, size_t tag_len);
    int  (*aead_decrypt)(qwrt_pal_t *pal, int algo,
                         const uint8_t *key, size_t key_len,
                         const uint8_t *nonce, size_t nonce_len,
                         const uint8_t *cipher, size_t cipher_len,
                         const uint8_t *aad, size_t aad_len,
                         const uint8_t *tag, size_t tag_len,
                         uint8_t *plain, size_t *plain_len);
    void (*sign)(qwrt_pal_t *pal, int algo,
                 const uint8_t *key, size_t key_len,
                 const uint8_t *data, size_t data_len,
                 uint8_t *sig, size_t *sig_len);
    int  (*verify)(qwrt_pal_t *pal, int algo,
                   const uint8_t *key, size_t key_len,
                   const uint8_t *data, size_t data_len,
                   const uint8_t *sig, size_t sig_len);
    void (*kdf)(qwrt_pal_t *pal, int algo,
                const uint8_t *password, size_t pw_len,
                const uint8_t *salt, size_t salt_len,
                uint32_t iterations, size_t key_len,
                uint8_t *derived_key);

    /* ── Reserved ───────────────────────────────────────────────── */
    void *reserved[4];
};

#endif /* QWRT_PAL_H */
/*
 * qwrt Polyfill Bytecode Loader (mode: rodata | compressed | external | host)
 *
 * Provides the unified interface qwrt_polyfill_load / qwrt_polyfill_unload.
 * Which storage backend is used depends on the compile-time macro
 * QWRT_POLYFILL_MODE (set by CMake's -DQWRT_POLYFILL_MODE=<mode>).
 *
 *   rodata (default)   — const array in .rodata, no heap allocation.
 *   compressed         — lz4-block-compressed array in .rodata, decompressed
 *                        to heap at load (raw LZ4 block via LZ4_decompress_safe;
 *                        build.js produces the block via cmake target
 *                        qwrt_lz4_compress — same vendored lz4, same format).
 *   external           — external .polyfill file read into heap at load.
 *   host               — delegates to the weak qwrt_polyfill_load_custom().
 */

#include "qwrt_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#if QWRT_POLYFILL_MODE == QWRT_POLYFILL_MODE_COMPRESSED
# include "lz4.h"
#endif

/* ================================================================
 * Mode C — const array in .rodata (default)
 * ================================================================ */
#if QWRT_POLYFILL_MODE == QWRT_POLYFILL_MODE_RODATA

int qwrt_polyfill_load(const uint8_t **out, size_t *out_len, void **owner)
{
    *out    = qwrt_default_polyfill;
    *out_len = qwrt_default_polyfill_len;
    *owner  = NULL;
    return 0;
}

void qwrt_polyfill_unload(void *owner)
{
    (void)owner;  /* mode C: no heap allocation, nothing to free */
}

/* ================================================================
 * compressed — lz4-block-compressed array → heap decompress
 * ================================================================ */
#elif QWRT_POLYFILL_MODE == QWRT_POLYFILL_MODE_COMPRESSED

int qwrt_polyfill_load(const uint8_t **out, size_t *out_len, void **owner)
{
    uint8_t *decomp = malloc(qwrt_default_polyfill_orig_len);
    if (!decomp) {
        fprintf(stderr, "[qwrt] polyfill: lz4 decompress OOM (%zu bytes)\n",
                qwrt_default_polyfill_orig_len);
        return QWRT_ERR_NO_MEMORY;
    }
    int rc = LZ4_decompress_safe((const char *)qwrt_default_polyfill_compressed,
                                 (char *)decomp,
                                 (int)qwrt_default_polyfill_compressed_len,
                                 (int)qwrt_default_polyfill_orig_len);
    if (rc < 0 || (size_t)rc != qwrt_default_polyfill_orig_len) {
        free(decomp);
        fprintf(stderr, "[qwrt] polyfill: lz4 decompression failed "
                "(rc=%d, compressed %zu, expected %zu)\n",
                rc, qwrt_default_polyfill_compressed_len,
                qwrt_default_polyfill_orig_len);
        return QWRT_ERR_GENERIC;
    }
    *out    = (const uint8_t *)decomp;
    *out_len = qwrt_default_polyfill_orig_len;
    *owner  = decomp;
    return 0;
}

void qwrt_polyfill_unload(void *owner)
{
    free(owner);
}

/* ================================================================
 * Mode B — external .polyfill file
 * ================================================================ */
#elif QWRT_POLYFILL_MODE == QWRT_POLYFILL_MODE_EXTERNAL

/*
 * File path resolution (first match wins):
 *   1. QWRT_POLYFILL_FILE  environment variable
 *   2. QWRT_POLYFILL_FILE  compile-time macro (if defined)
 *   3. error — no path available
 */
static const char *polyfill_file_path(void)
{
    const char *env = getenv("QWRT_POLYFILL_FILE");
    if (env && env[0]) return env;
#ifdef QWRT_POLYFILL_FILE
    return QWRT_POLYFILL_FILE;
#else
    return NULL;
#endif
}

int qwrt_polyfill_load(const uint8_t **out, size_t *out_len, void **owner)
{
    const char *path = polyfill_file_path();
    if (!path || !path[0]) {
        fprintf(stderr, "[qwrt] polyfill: QWRT_POLYFILL_FILE not set "
                "(define the macro or set the environment variable)\n");
        return QWRT_ERR_NOT_FOUND;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "[qwrt] polyfill: cannot open %s: %s\n",
                path, strerror(errno));
        return QWRT_ERR_NOT_FOUND;
    }

    /* Get file size */
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        fprintf(stderr, "[qwrt] polyfill: fseek failed on %s\n", path);
        return QWRT_ERR_IO;
    }
    long file_size = ftell(f);
    if (file_size < 0) {
        fclose(f);
        fprintf(stderr, "[qwrt] polyfill: ftell failed on %s\n", path);
        return QWRT_ERR_IO;
    }
    rewind(f);

    /* Allocate buffer */
    size_t sz = (size_t)file_size;
    uint8_t *buf = (uint8_t *)malloc(sz);
    if (!buf) {
        fclose(f);
        return QWRT_ERR_NO_MEMORY;
    }

    /* Read contents */
    size_t nread = fread(buf, 1, sz, f);
    fclose(f);
    if (nread != sz) {
        free(buf);
        fprintf(stderr, "[qwrt] polyfill: short read from %s "
                "(%zu != %zu)\n", path, nread, sz);
        return QWRT_ERR_IO;
    }

    *out    = buf;
    *out_len = sz;
    *owner  = buf;
    return 0;
}

void qwrt_polyfill_unload(void *owner)
{
    free(owner);
}

/* ================================================================
 * Mode D — host-provided custom hook (weak symbols)
 * ================================================================ */
#else /* QWRT_POLYFILL_MODE_HOST */

__attribute__((weak))
int qwrt_polyfill_load_custom(const uint8_t **out, size_t *out_len, void **owner)
{
    (void)out; (void)out_len; (void)owner;
    fprintf(stderr, "[qwrt] polyfill: mode D but qwrt_polyfill_load_custom "
            "is not defined by the host\n");
    return QWRT_ERR_NOT_SUPPORTED;
}

__attribute__((weak))
void qwrt_polyfill_unload_custom(void *owner)
{
    (void)owner;
}

int qwrt_polyfill_load(const uint8_t **out, size_t *out_len, void **owner)
{
    return qwrt_polyfill_load_custom(out, out_len, owner);
}

void qwrt_polyfill_unload(void *owner)
{
    qwrt_polyfill_unload_custom(owner);
}

#endif
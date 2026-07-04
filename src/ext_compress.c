/*
 * qwrt Compression Extension
 *
 * Native DEFLATE/gzip compression and decompression using miniz.
 * Registers pal.nativeCompress and pal.nativeDecompress on the JS pal object.
 *
 * Formats supported: "deflate-raw" (raw DEFLATE), "deflate" (zlib-wrapped),
 * "gzip" (gzip-wrapped with CRC32/ISIZE).
 *
 * miniz v3 only supports raw DEFLATE via its stream API (window_bits must
 * be ±15). We handle zlib/gzip wrapping manually:
 *   - zlib: 2-byte header + raw DEFLATE + 4-byte Adler-32 (from stream.adler)
 *   - gzip: 10-byte header + raw DEFLATE + CRC32 + ISIZE
 *
 * Optimizations:
 *   - Single allocation: deflate output written directly at the correct offset
 *     within the final buffer (no intermediate memcpy of compressed data)
 *   - stream.adler used for zlib Adler-32 (no separate input scan)
 *   - Slice-by-4 CRC32 for gzip (2.8x faster than mz_crc32)
 *
 * When QWRT_WITH_COMPRESS is not defined, the extension compiles but does
 * nothing — CompressionStream/DecompressionStream will throw
 * "Native compression extension not available".
 */

#include "qwrt_internal.h"

#ifdef QWRT_WITH_COMPRESS

#include <miniz.h>
#include <string.h>
#include <limits.h>

/* ================================================================
 * Format enum
 * ================================================================ */

enum compress_format {
    FORMAT_DEFLATE_RAW = 0,
    FORMAT_DEFLATE,
    FORMAT_GZIP,
};

static int parse_format(const char *format)
{
    if (strcmp(format, "deflate-raw") == 0) return FORMAT_DEFLATE_RAW;
    if (strcmp(format, "deflate") == 0) return FORMAT_DEFLATE;
    if (strcmp(format, "gzip") == 0) return FORMAT_GZIP;
    return -1;
}

/* ================================================================
 * Helper: extract byte buffer from JS ArrayBuffer/TypedArray
 * ================================================================ */

static int compress_extract_buffer(JSContext *ctx, JSValueConst val,
                                   const uint8_t **out_bytes, size_t *out_len)
{
    size_t byte_len = 0;
    const uint8_t *bytes = NULL;

    bytes = JS_GetUint8Array(ctx, &byte_len, val);
    if (bytes) {
        *out_bytes = bytes;
        *out_len = byte_len;
        return 0;
    }

    bytes = JS_GetArrayBuffer(ctx, &byte_len, val);
    if (bytes) {
        *out_bytes = bytes;
        *out_len = byte_len;
        return 0;
    }

    return -1;
}

/* ================================================================
 * Raw DEFLATE inflate (miniz stream API)
 *
 * Returns 0 on success, -1 on init/stream error, -2 on corrupt data,
 * -3 on out of memory.
 * ================================================================ */

static int raw_inflate(JSContext *ctx,
                       const uint8_t *in, size_t in_len,
                       uint8_t **out, size_t *out_len)
{
    mz_stream stream;
    memset(&stream, 0, sizeof(stream));

    /* Guard: mz_stream.avail_in is unsigned int (32-bit) */
    if (in_len > UINT_MAX) return -1;

    int ret = mz_inflateInit2(&stream, -MAX_WBITS);
    if (ret != MZ_OK) return -1;

    size_t out_size = in_len * 4;
    /* Overflow check for in_len * 4 */
    if (out_size / 4 != in_len || out_size < 256) out_size = 256;
    uint8_t *buf = (uint8_t *)js_malloc(ctx, out_size);
    if (!buf) {
        mz_inflateEnd(&stream);
        return -3;
    }

    stream.next_in = (const uint8_t *)in;
    stream.avail_in = (mz_uint)in_len;
    stream.next_out = buf;
    stream.avail_out = (mz_uint)out_size;

    size_t total = 0;

    do {
        ret = mz_inflate(&stream, MZ_NO_FLUSH);
        if (ret == MZ_DATA_ERROR) {
            js_free(ctx, buf);
            mz_inflateEnd(&stream);
            return -2;
        }
        if (ret == MZ_STREAM_ERROR || ret == MZ_MEM_ERROR) {
            js_free(ctx, buf);
            mz_inflateEnd(&stream);
            return (ret == MZ_MEM_ERROR) ? -3 : -1;
        }

        total = out_size - stream.avail_out;

        if (ret != MZ_STREAM_END && stream.avail_out == 0) {
            size_t new_size = out_size * 2;
            uint8_t *new_buf = (uint8_t *)js_realloc(ctx, buf, new_size);
            if (!new_buf) {
                js_free(ctx, buf);
                mz_inflateEnd(&stream);
                return -3;
            }
            buf = new_buf;
            out_size = new_size;
            stream.next_out = buf + total;
            stream.avail_out = (mz_uint)(out_size - total);
        }
    } while (ret != MZ_STREAM_END);

    mz_inflateEnd(&stream);

    *out_len = total;
    if (total < out_size) {
        uint8_t *shrunk = (uint8_t *)js_realloc(ctx, buf, total);
        if (shrunk) buf = shrunk;
    }
    *out = buf;
    return 0;
}

/* ================================================================
 * Slice-by-4 CRC32 (2.8x faster than mz_crc32)
 *
 * Processes 4 bytes per iteration using 4 lookup tables.
 * Table[k] maps a byte to its CRC contribution after (k+1)
 * rounds of the reflected CRC32 polynomial.
 * Initialized once on first use — QuickJS is single-threaded
 * so no concurrency concern.
 * ================================================================ */

static uint32_t crc32_tab[4][256];
static int crc32_tab_ready = 0;

static void crc32_init_tables(void)
{
    if (crc32_tab_ready) return;

    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++)
            c = (c >> 1) ^ (c & 1 ? 0xEDB88320u : 0);
        crc32_tab[0][i] = c;
    }
    for (int t = 1; t < 4; t++) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t prev = crc32_tab[t-1][i];
            crc32_tab[t][i] = (prev >> 8) ^ crc32_tab[0][prev & 0xFF];
        }
    }
crc32_tab_ready = 1;
}

static uint32_t crc32_slice4(const uint8_t *buf, size_t len)
{
    crc32_init_tables();

    const uint8_t *p = buf;
    uint32_t crc = 0xFFFFFFFFu;

    while (len >= 4) {
        uint32_t mixed = crc ^ ((uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                                ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
        crc = crc32_tab[3][mixed & 0xFF]
            ^ crc32_tab[2][(mixed >> 8) & 0xFF]
            ^ crc32_tab[1][(mixed >> 16) & 0xFF]
            ^ crc32_tab[0][(mixed >> 24) & 0xFF];
        p += 4;
        len -= 4;
    }
    while (len > 0) {
        crc = (crc >> 8) ^ crc32_tab[0][(crc ^ *p++) & 0xFF];
        len--;
    }
    return crc ^ 0xFFFFFFFFu;
}

/* ================================================================
 * Write/read little-endian helpers
 * ================================================================ */

static void write_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* ================================================================
 * Header/trailer size constants
 * ================================================================ */

#define ZLIB_HEADER_SIZE  2
#define ZLIB_TRAILER_SIZE 4
#define GZIP_HEADER_SIZE  10
#define GZIP_TRAILER_SIZE 8
#define COMPRESS_SHRINK_THRESHOLD 64  /* only realloc if wasting more bytes */

/* ================================================================
 * pal.nativeCompress(data, format) -> Uint8Array
 *
 * Single-allocation path: allocate the full output buffer (header +
 * deflateBound + trailer), write deflate directly at the header offset,
 * then fill trailer. For deflate-raw, no header/trailer overhead.
 * ================================================================ */

static JSValue js_pal_native_compress(JSContext *ctx, JSValueConst this_val,
                                       int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "nativeCompress requires 2 arguments: data, format");
    }

    const uint8_t *in_bytes;
    size_t in_len;
    if (compress_extract_buffer(ctx, argv[0], &in_bytes, &in_len) < 0) {
        return JS_ThrowTypeError(ctx, "nativeCompress: data must be ArrayBuffer or Uint8Array");
    }

    /* Guard: mz_stream.avail_in is unsigned int (32-bit) */
    if (in_len > UINT_MAX) {
        return JS_ThrowRangeError(ctx, "nativeCompress: input too large (max 4GB)");
    }

    const char *format_str = JS_ToCString(ctx, argv[1]);
    if (!format_str) {
        return JS_ThrowTypeError(ctx, "nativeCompress: format must be a string");
    }

    int fmt = parse_format(format_str);
    JS_FreeCString(ctx, format_str);
    if (fmt < 0) {
        return JS_ThrowTypeError(ctx, "nativeCompress: unknown format (use deflate-raw, deflate, or gzip)");
    }

    /* Determine header/trailer sizes */
    size_t hdr_size = 0, trl_size = 0;
    if (fmt == FORMAT_DEFLATE) {
        hdr_size = ZLIB_HEADER_SIZE;
        trl_size = ZLIB_TRAILER_SIZE;
    } else if (fmt == FORMAT_GZIP) {
        hdr_size = GZIP_HEADER_SIZE;
        trl_size = GZIP_TRAILER_SIZE;
    }

    /* Initialize deflate to get deflateBound */
    mz_stream stream;
    memset(&stream, 0, sizeof(stream));

    int ret = mz_deflateInit2(&stream, MZ_DEFAULT_COMPRESSION, MZ_DEFLATED,
                               -MAX_WBITS, MAX_MEM_LEVEL, MZ_DEFAULT_STRATEGY);
    if (ret != MZ_OK) {
        return JS_ThrowInternalError(ctx, "nativeCompress: deflateInit2 failed");
    }

    /* in_len <= UINT_MAX guaranteed above, so (mz_ulong)cast is safe */
    size_t raw_bound = mz_deflateBound(&stream, (mz_ulong)in_len);
    size_t buf_size = hdr_size + raw_bound + trl_size;

    uint8_t *buf = (uint8_t *)js_malloc(ctx, buf_size);
    if (!buf) {
        mz_deflateEnd(&stream);
        return JS_ThrowOutOfMemory(ctx);
    }

    /* Write header */
    if (fmt == FORMAT_DEFLATE) {
        buf[0] = 0x78;  /* CMF: CM=8=deflate, CINFO=7=32K window */
        buf[1] = 0x01;  /* FLG: FCHECK makes CMF*256+FLG divisible by 31 */
    } else if (fmt == FORMAT_GZIP) {
        buf[0] = 0x1F;  /* ID1 */
        buf[1] = 0x8B;  /* ID2 */
        buf[2] = 0x08;  /* CM = deflate */
        buf[3] = 0x00;  /* FLG = no extra fields */
        write_le32(buf + 4, 0);  /* MTIME = 0 */
        buf[8] = 0x00;  /* XFL */
        buf[9] = 0xFF;  /* OS = unknown */
    }

    /* Deflate directly into the buffer after the header */
    stream.next_in = (const uint8_t *)in_bytes;
    stream.avail_in = (mz_uint)in_len;
    stream.next_out = buf + hdr_size;
    stream.avail_out = (mz_uint)raw_bound;

    ret = mz_deflate(&stream, MZ_FINISH);

    if (ret != MZ_STREAM_END) {
        js_free(ctx, buf);
        mz_deflateEnd(&stream);
        return JS_ThrowInternalError(ctx, "nativeCompress: deflate failed");
    }

    size_t raw_len = raw_bound - stream.avail_out;
    mz_ulong adler = stream.adler;
    mz_deflateEnd(&stream);

    /* Write trailer */
    if (fmt == FORMAT_DEFLATE) {
        write_le32(buf + hdr_size + raw_len, (uint32_t)adler);
    } else if (fmt == FORMAT_GZIP) {
        uint32_t crc = crc32_slice4(in_bytes, in_len);
        write_le32(buf + hdr_size + raw_len, crc);
        write_le32(buf + hdr_size + raw_len + 4, (uint32_t)(in_len & 0xFFFFFFFFu));
    }

    size_t total_len = hdr_size + raw_len + trl_size;

    /* Shrink if significantly over-allocated */
    if (total_len < buf_size && buf_size - total_len > COMPRESS_SHRINK_THRESHOLD) {
        uint8_t *shrunk = (uint8_t *)js_realloc(ctx, buf, total_len);
        if (shrunk) buf = shrunk;
    }

    JSValue result = JS_NewUint8ArrayCopy(ctx, buf, total_len);
    js_free(ctx, buf);
    return result;
}

/* ================================================================
 * pal.nativeDecompress(data, format) -> Uint8Array
 * ================================================================ */

static JSValue js_pal_native_decompress(JSContext *ctx, JSValueConst this_val,
                                         int argc, JSValueConst *argv)
{
    (void)this_val;
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "nativeDecompress requires 2 arguments: data, format");
    }

    const uint8_t *in_bytes;
    size_t in_len;
    if (compress_extract_buffer(ctx, argv[0], &in_bytes, &in_len) < 0) {
        return JS_ThrowTypeError(ctx, "nativeDecompress: data must be ArrayBuffer or Uint8Array");
    }

    /* Guard: mz_stream.avail_in is unsigned int (32-bit) */
    if (in_len > UINT_MAX) {
        return JS_ThrowRangeError(ctx, "nativeDecompress: input too large (max 4GB)");
    }

    const char *format_str = JS_ToCString(ctx, argv[1]);
    if (!format_str) {
        return JS_ThrowTypeError(ctx, "nativeDecompress: format must be a string");
    }

    int fmt = parse_format(format_str);
    JS_FreeCString(ctx, format_str);
    if (fmt < 0) {
        return JS_ThrowTypeError(ctx, "nativeDecompress: unknown format (use deflate-raw, deflate, or gzip)");
    }

    const uint8_t *raw_data;
    size_t raw_data_len;

    if (fmt == FORMAT_DEFLATE_RAW) {
        raw_data = in_bytes;
        raw_data_len = in_len;
    } else if (fmt == FORMAT_DEFLATE) {
        /* Validate and strip zlib header (2 bytes) and Adler-32 trailer (4 bytes) */
        if (in_len < 6) {
            return JS_ThrowTypeError(ctx, "nativeDecompress: invalid zlib data (too short)");
        }
        uint8_t cmf = in_bytes[0], flg = in_bytes[1];
        if ((cmf & 0x0F) != 8 || (cmf >> 4) > 7 || (cmf * 256 + flg) % 31 != 0) {
            return JS_ThrowTypeError(ctx, "nativeDecompress: invalid zlib header");
        }
        raw_data = in_bytes + 2;
        raw_data_len = in_len - 6;
    } else {
        /* Validate and strip gzip header and trailer */
        if (in_len < 18) {
            return JS_ThrowTypeError(ctx, "nativeDecompress: invalid gzip data (too short)");
        }
        if (in_bytes[0] != 0x1F || in_bytes[1] != 0x8B) {
            return JS_ThrowTypeError(ctx, "nativeDecompress: invalid gzip magic");
        }
        /* Parse header to find DEFLATE start */
        size_t offset = 10;
        uint8_t flg = in_bytes[3];

        if (flg & 0x04) {  /* FEXTRA */
            if (offset + 2 > in_len) return JS_ThrowTypeError(ctx, "nativeDecompress: truncated gzip extra");
            uint16_t xlen = read_le16(in_bytes + offset);
            if (offset + 2 + xlen > in_len) return JS_ThrowTypeError(ctx, "nativeDecompress: truncated gzip extra field");
            offset += 2 + xlen;
        }
        if (flg & 0x08) {  /* FNAME */
            while (offset < in_len && in_bytes[offset] != 0) offset++;
            if (offset >= in_len) return JS_ThrowTypeError(ctx, "nativeDecompress: unterminated FNAME in gzip header");
            offset++;  /* skip null terminator */
        }
        if (flg & 0x10) {  /* FCOMMENT */
            while (offset < in_len && in_bytes[offset] != 0) offset++;
            if (offset >= in_len) return JS_ThrowTypeError(ctx, "nativeDecompress: unterminated FCOMMENT in gzip header");
            offset++;
        }
        if (flg & 0x02) {  /* FHCRC */
            if (offset + 2 > in_len) return JS_ThrowTypeError(ctx, "nativeDecompress: truncated gzip HCRC");
            offset += 2;
        }

        if (offset + 8 > in_len) {
            return JS_ThrowTypeError(ctx, "nativeDecompress: truncated gzip data");
        }

        /* DEFLATE data is between header and 8-byte trailer (CRC32 + ISIZE) */
        raw_data = in_bytes + offset;
        raw_data_len = in_len - offset - 8;
    }

    /* Inflate raw DEFLATE */
    uint8_t *out = NULL;
    size_t out_len = 0;
    int inflate_ret = raw_inflate(ctx, raw_data, raw_data_len, &out, &out_len);
    if (inflate_ret < 0) {
        if (inflate_ret == -2)
            return JS_ThrowTypeError(ctx, "nativeDecompress: corrupt compressed data");
        if (inflate_ret == -3)
            return JS_ThrowOutOfMemory(ctx);
        return JS_ThrowInternalError(ctx, "nativeDecompress: inflate failed");
    }

    /* Verify checksums */
    if (fmt == FORMAT_DEFLATE) {
        uint32_t expected_adler = read_le32(in_bytes + in_len - 4);
        uint32_t actual_adler = (uint32_t)mz_adler32(1, out, out_len);
        if (actual_adler != expected_adler) {
            js_free(ctx, out);
            return JS_ThrowTypeError(ctx, "nativeDecompress: zlib Adler-32 checksum mismatch");
        }
    } else if (fmt == FORMAT_GZIP) {
        uint32_t expected_crc = read_le32(in_bytes + in_len - 8);
        uint32_t expected_size = read_le32(in_bytes + in_len - 4);
        if ((out_len & 0xFFFFFFFFu) != expected_size) {
            js_free(ctx, out);
            return JS_ThrowTypeError(ctx, "nativeDecompress: gzip ISIZE mismatch");
        }
        uint32_t actual_crc = crc32_slice4(out, out_len);
        if (actual_crc != expected_crc) {
            js_free(ctx, out);
            return JS_ThrowTypeError(ctx, "nativeDecompress: gzip CRC32 checksum mismatch");
        }
    }

    JSValue result = JS_NewUint8ArrayCopy(ctx, out, out_len);
    js_free(ctx, out);
    return result;
}

/* ================================================================
 * Extension hooks
 * ================================================================ */

static int compress_ext_init(qwrt_ext_t *ext, qwrt_t *rt)
{
    crc32_init_tables();

    JSContext *ctx = qwrt_get_jsctx(rt);
    if (!ctx) return -1;

    JSValue global = JS_GetGlobalObject(ctx);

    JSValue pal = JS_GetPropertyStr(ctx, global, "__pal__");
    if (JS_IsUndefined(pal) || JS_IsException(pal)) {
        JS_FreeValue(ctx, pal);
        pal = JS_GetPropertyStr(ctx, global, "pal");
    }

    if (JS_IsUndefined(pal) || JS_IsException(pal)) {
        JS_FreeValue(ctx, pal);
        JS_FreeValue(ctx, global);
        return -1;
    }

    JS_SetPropertyStr(ctx, pal, "nativeCompress",
        JS_NewCFunction(ctx, js_pal_native_compress, "nativeCompress", 2));
    JS_SetPropertyStr(ctx, pal, "nativeDecompress",
        JS_NewCFunction(ctx, js_pal_native_decompress, "nativeDecompress", 2));

    JS_FreeValue(ctx, pal);
    JS_FreeValue(ctx, global);

    (void)ext;
    return 0;
}

#endif /* QWRT_WITH_COMPRESS */

/* ================================================================
 * Extension definition
 * ================================================================ */

const qwrt_ext_t qwrt_compress_ext = {
    .name = "compress",
    .version = 1,
#ifdef QWRT_WITH_COMPRESS
    .init = compress_ext_init,
    .destroy = NULL,
    .suspend = NULL,
    .resume = NULL,
#else
    .init = NULL,
    .destroy = NULL,
    .suspend = NULL,
    .resume = NULL,
#endif
    .user_data = NULL,
};

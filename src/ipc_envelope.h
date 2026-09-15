/*
 * IPC Envelope — FlatBuffers wire-format codec for the multi-process model
 * (docs/plans/2026-09-04-multi-process-model.md §4, milestone M-P0).
 *
 * Fixed schema (schema frozen by the design doc; field ids are wire contract):
 *
 *   table Envelope {
 *     source: int32;    // id 0 — hop-relative address (0=host, 1=parent, >1=child slot)
 *     target: int32;    // id 1 — same semantics, interpreted by the receiving node
 *     kind:   int8;     // id 2 — 0=MESSAGE 1=PORT_TRANSFER 2=ERROR 3=CONTROL
 *     payload:[ubyte];  // id 3 — structured-clone bytes / error text / control blob
 *   }
 *
 * Handwritten vtable/uoffset layout, byte-level FlatBuffers wire format
 * (spec: https://flatbuffers.dev/flatbuffers_internals.html) — zero deps, no
 * codegen step. A fixed 4-field schema needs no generic builder: the encoder
 * emits one canonical layout, the decoder is layout-agnostic (reads any valid
 * fb Envelope, including buffers produced by the official flatbuffers
 * builders with fields omitted / vtable dedup), so C↔Python cross-decoding
 * works (M-P0 verification gate).
 *
 * Canonical encoded layout (little-endian, 40 + payload_len bytes):
 *
 *   +0   u32 root uoffset = 16        (root table at +16)
 *   +4   u16 vtable_len   = 10
 *   +6   u16 table_len    = 20
 *   +8   u16 voff source  = 4        (vtable slots, field id order)
 *   +10  u16 voff target  = 8
 *   +12  u16 voff kind    = 12
 *   +14  u16 voff payload = 16
 *   +16  i32 soffset = 12            (vtable = table - soffset = +4)
 *   +20  i32 source
 *   +24  i32 target
 *   +28  i8  kind (+3 pad)
 *   +32  u32 payload uoffset = 4     (vector header at +36)
 *   +36  u32 payload vector length
 *   +40  payload bytes
 */

#ifndef QWRT_IPC_ENVELOPE_H
#define QWRT_IPC_ENVELOPE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* kind — matches design doc §4.1 (0=MESSAGE 1=PORT_TRANSFER 2=ERROR 3=CONTROL).
 * M-P3 implements PORT_TRANSFER: frames that carry a MessagePort transfer or a
 * message posted through a transferred port. Only the ends of the tree edge
 * interpret it today — the flat topology (host→mainRT→worker, one level) has no
 * intermediate relay, so the receiving C layer hands the frame to JS with `kind`
 * intact (see bridge.c/rt_main.c dispatch pass-through) and the JS port layer
 * routes by the payload header below.
 *
 * PORT_TRANSFER payload schema (§8.2 peerEndpoint routing — a variable-length
 * little-endian header carrying *path chains*, then the opaque structured-clone
 * bytes). Endpoint identity is a §8.2 path = chain of parent-assigned slot ids
 * from the rt-tree root (root = empty path); routing compares the destination
 * path against the local path by prefix (up / local / down), so an LCA relay
 * emerges without any routing table:
 *
 *   +0  u8  op            // 1 = PORT_MESSAGE (SC bytes of the posted value)
 *                         // 2 = PORT_TRANSFER (SC bytes of {__qwrt_ports, ...})
 *   +1  u8  dest_len      // op=1: element count of dest_path
 *   +2  u8  key_len       // op=1: element count of key_path
 *   +3  u8  reserved = 0
 *   +4  u16 dest_path[dest_len]  // endpoint the target port currently lives on
 *   ... u16 key_path[key_len]    // target port's owner/home endpoint — the half
 *                                // of the (owner, id) identity that disambiguates
 *                                // ids allocated independently per process
 *   ... u32 key_port             // op=1: target port's id within key_path
 *   ... SC bytes
 *
 * Elements are u16 (PROCESS worker ids are 1000+; >65535 → widen to u32).
 * Total header = 8 + 2*(dest_len + key_len), max 8 + 2*2*QWRT_SELF_PATH_MAX.
 *
 * The header exists so a routing node can decide deliver-locally vs forward
 * WITHOUT decoding the structured-clone payload (§7.2: payload bytes never
 * change, relay only rewrites the envelope head). The JS port layer encodes and
 * decodes it (message-channel.js); C passes `kind` through untouched. */
#define IPC_ENV_KIND_MESSAGE        0
#define IPC_ENV_KIND_PORT_TRANSFER  1
#define IPC_ENV_KIND_CONTROL        3
#define IPC_ENV_KIND_STORAGE        4   /* M-P4: §10.2 单所有者 storage 代理 ——
                                         * payload = structured clone{op,key,
                                         * value?,storageDomain}（§4.1 kind=4）。
                                         * worker→主RT 方向 = 请求；主RT→worker
                                         * 方向 = 执行结果。 */

/* PORT_TRANSFER routing header: fixed 4-byte prologue (op/dest_len/key_len/pad)
 * + u16 path chains + u32 key_port. JS encodes/decodes (message-channel.js);
 * these macros are the shared C-side contract (unchanged by flat topology). */
#define IPC_PORT_XFER_HDR_PROLOGUE  4u
#define IPC_PORT_XFER_HDR_TAIL      4u   /* key_port u32 */
#define IPC_PORT_XFER_OP_MESSAGE    1u
#define IPC_PORT_XFER_OP_TRANSFER   2u

/* Canonical encoding is always fully populated: 40 bytes of header/table
 * plus the payload vector. IPC_ENVELOPE_ENCODED_SIZE gives the exact size
 * ipc_envelope_encode will return for a given payload_len. */
#define IPC_ENVELOPE_FIXED_SIZE 40u
#define IPC_ENVELOPE_ENCODED_SIZE(payload_len) \
    (IPC_ENVELOPE_FIXED_SIZE + (size_t)(payload_len))

/* Decoded view — payload points INTO the caller's buffer (zero-copy slice,
 * valid while the buffer is alive and unmodified). Absent fields decode to
 * fb defaults (0); absent payload → NULL/0. */
typedef struct {
    int32_t source;
    int32_t target;
    int8_t kind;
    const uint8_t *payload;
    uint32_t payload_len;
} ipc_envelope_view_t;

/* Encode one Envelope into out (canonical layout above). Returns the number
 * of bytes written (== IPC_ENVELOPE_ENCODED_SIZE(payload_len)), or 0 on
 * failure (NULL out, or cap too small). payload may be NULL iff
 * payload_len == 0. */
size_t ipc_envelope_encode(uint8_t *out, size_t cap,
                           int32_t source, int32_t target, int8_t kind,
                           const uint8_t *payload, uint32_t payload_len);

/* Decode any valid fb Envelope buffer into view. Returns 0 on success,
 * -1 on malformed/truncated input (out left with defaults where known). */
int ipc_envelope_decode(const uint8_t *buf, size_t len,
                        ipc_envelope_view_t *view);

#ifdef __cplusplus
}
#endif

#endif /* QWRT_IPC_ENVELOPE_H */

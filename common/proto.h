/*
 * mc protocol: TLV message format + fd-passing.
 * Shared by mc-compositor and libmc.
 */
#ifndef MC_PROTO_H
#define MC_PROTO_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

#define MC_MAGIC          0x4D43       /* 'M''C' big-endian-as-u16 in wire */
#define MC_VERSION_MAJOR  1
#define MC_VERSION_MINOR  0
#define MC_PROTO_VERSION  ((MC_VERSION_MAJOR << 16) | MC_VERSION_MINOR)

#define MC_MAX_PAYLOAD    (64 * 1024)
#define MC_MAX_FDS        8
#define MC_HDR_BYTES      12

/* ---- message types ---- */
/* Client -> Server */
#define MC_CL_HELLO            0x01
#define MC_CL_CREATE_SURFACE   0x02
#define MC_CL_DESTROY_SURFACE  0x03
#define MC_CL_COMMIT           0x04
#define MC_CL_SET_ROLE         0x05
#define MC_CL_REQUEST_FOCUS    0x06
#define MC_CL_ACK_LIFECYCLE    0x07
#define MC_CL_BUS_SUB          0x10
#define MC_CL_BUS_PUB          0x11
#define MC_CL_BUS_UNSUB        0x12
#define MC_CL_BYE              0xFF

/* Server -> Client */
#define MC_SV_WELCOME          0x81
#define MC_SV_SURFACE_OK       0x82
#define MC_SV_FRAME_DONE       0x83
#define MC_SV_LIFECYCLE        0x84
#define MC_SV_INPUT            0x85
#define MC_SV_FOCUS            0x86
#define MC_SV_BUS_MSG          0x90
#define MC_SV_ERROR            0xEE

/* ---- tags (high byte = group, low byte = field) ---- */
/* Generic (0x01xx) */
#define MC_T_NAME              0x0101  /* string */
#define MC_T_VERSION           0x0102  /* u32 */
#define MC_T_PID               0x0103  /* u32 */
#define MC_T_CAPS              0x0104  /* u32 */
#define MC_T_CLIENT_ID         0x0105  /* u32 */
#define MC_T_PIXEL_DPI         0x010A  /* u16 */
#define MC_T_CODE              0x010B  /* u32 */
#define MC_T_MSG               0x010C  /* string */
#define MC_T_SERVER_CAPS       0x010D  /* u32 */

/* Surface (0x02xx) */
#define MC_T_WIDTH             0x0201  /* u16 */
#define MC_T_HEIGHT            0x0202  /* u16 */
#define MC_T_FORMAT            0x0203  /* u8 */
#define MC_T_ROLE              0x0204  /* u8 */
#define MC_T_N_BUF             0x0205  /* u8 */
#define MC_T_POPUP_X           0x0206  /* i16 */
#define MC_T_POPUP_Y           0x0207  /* i16 */
#define MC_T_MODAL             0x0208  /* u8 */
#define MC_T_BUF_TYPE          0x0209  /* u8 */
#define MC_T_FLIP_Y            0x020A  /* u8: 1 = client buffer is stored
                                        * bottom-up (GL FBO origin in the
                                        * lower-left), compositor inverts
                                        * src_y when blitting. CPU clients
                                        * leave at 0 (default). */

/* Buffer (0x03xx) */
#define MC_T_SID               0x0301  /* u32 */
#define MC_T_BUF_IDX           0x0302  /* u8 */
#define MC_T_DAMAGE            0x0303  /* rect[] (i16 x4 each) */
#define MC_T_STRIDE            0x0304  /* u32 */
#define MC_T_SIZE              0x0305  /* u32 */
#define MC_T_SEQ               0x0306  /* u32 */
#define MC_T_PHYS_LIST         0x0307  /* u32[n_buf] phys-addr per buf;
                                        * when present, client must mmap
                                        * the SCM_RIGHTS-passed fd with
                                        * the corresponding entry as the
                                        * mmap offset. Absent ⇒ mmap at
                                        * offset 0 (memfd path). */

/* Input (0x04xx) */
#define MC_T_INPUT_TYPE        0x0401  /* u8 */
#define MC_T_INPUT_X           0x0402  /* i16 */
#define MC_T_INPUT_Y           0x0403  /* i16 */
#define MC_T_INPUT_SLOT        0x0404  /* u8 */
#define MC_T_INPUT_PRESSURE    0x0405  /* u8 */
#define MC_T_INPUT_TIME        0x0406  /* u32 */

/* Lifecycle (0x05xx) */
#define MC_T_LC_STATE          0x0501  /* u8 */

/* Bus (0x06xx) */
#define MC_T_BUS_TOPIC         0x0601  /* string */
#define MC_T_BUS_PAYLOAD       0x0602  /* bytes */
#define MC_T_BUS_SENDER        0x0603  /* string */

/* ---- error codes ---- */
#define MC_OK             0x00
#define MC_E_PROTO        0x01
#define MC_E_INVAL        0x02
#define MC_E_NOMEM        0x03
#define MC_E_NOENT        0x04
#define MC_E_BUSY         0x05
#define MC_E_PERM         0x06
#define MC_E_TOOLARGE     0x07
#define MC_E_NOTSUP       0x08
#define MC_E_INTERNAL     0xFE

/* ---- caps bits ---- */
#define MC_CAP_DMABUF          (1u << 0)
#define MC_CAP_FENCE           (1u << 1)
#define MC_CAP_BUS             (1u << 2)
#define MC_CAP_MULTI_SURFACE   (1u << 3)

/* ---- wire header (little-endian) ---- */
struct mc_msg_hdr {
    uint16_t magic;        /* MC_MAGIC */
    uint16_t type;         /* MC_CL_* / MC_SV_* */
    uint32_t payload_len;  /* TLV bytes following hdr */
    uint32_t serial;       /* request/response correlation */
} __attribute__((packed));

/* ---- builder/parser API ---- */

/*
 * Builder: write TLVs into a caller-provided buffer.
 * After done, fill the header (magic/type/len/serial) at the start and
 * send hdr+payload via mc_send_frame().
 */
struct mc_builder {
    uint8_t *buf;
    size_t   cap;
    size_t   len;          /* TLV bytes written (after MC_HDR_BYTES) */
    int      err;          /* sticky error */
};

void mc_builder_init(struct mc_builder *b, void *buf, size_t cap);
int  mc_put_u8 (struct mc_builder *b, uint16_t tag, uint8_t  v);
int  mc_put_u16(struct mc_builder *b, uint16_t tag, uint16_t v);
int  mc_put_i16(struct mc_builder *b, uint16_t tag, int16_t  v);
int  mc_put_u32(struct mc_builder *b, uint16_t tag, uint32_t v);
int  mc_put_u64(struct mc_builder *b, uint16_t tag, uint64_t v);
int  mc_put_str(struct mc_builder *b, uint16_t tag, const char *s);
int  mc_put_bin(struct mc_builder *b, uint16_t tag, const void *p, size_t n);

/* Finalize: write header into buf[0..MC_HDR_BYTES) and return total length. */
size_t mc_builder_finalize(struct mc_builder *b,
                           uint16_t type, uint32_t serial);

/* Parser: scan TLVs in payload. Returns 0 on success, -1 on malformed. */
struct mc_parser {
    const uint8_t *p;
    size_t         remain;
};

void mc_parser_init(struct mc_parser *pr,
                    const void *payload, size_t payload_len);

/*
 * Find a tag and copy/borrow value.
 * For numeric types, returns 0 if found and copied, -1 if not found,
 * -2 if length mismatch.
 * For string/bin, *out_ptr borrows into the payload buffer (no copy).
 * String values are NOT NUL-terminated on the wire; callers should copy
 * if they need C strings.
 */
int mc_get_u8 (const void *payload, size_t plen, uint16_t tag, uint8_t  *out);
int mc_get_u16(const void *payload, size_t plen, uint16_t tag, uint16_t *out);
int mc_get_i16(const void *payload, size_t plen, uint16_t tag, int16_t  *out);
int mc_get_u32(const void *payload, size_t plen, uint16_t tag, uint32_t *out);
int mc_get_u64(const void *payload, size_t plen, uint16_t tag, uint64_t *out);
int mc_get_bin(const void *payload, size_t plen, uint16_t tag,
               const void **out_ptr, size_t *out_len);
/* Copy string into caller buffer with NUL terminator. Returns string length. */
int mc_get_str(const void *payload, size_t plen, uint16_t tag,
               char *out, size_t out_cap);

/* ---- I/O with fd-passing ---- */

/*
 * Send a complete frame (hdr + payload already laid out in buf[0..len)).
 * If n_fds > 0, attach fds via SCM_RIGHTS on the first sendmsg call.
 * Returns 0 on success, -errno on failure.
 *
 * Note: this function loops on partial writes; suitable for blocking sockets.
 */
int mc_send_frame(int sock, const void *buf, size_t len,
                  const int *fds, int n_fds);

/*
 * Receive one complete frame.
 * - Reads exactly MC_HDR_BYTES, then payload_len bytes.
 * - Any SCM_RIGHTS fds delivered alongside are placed in out_fds[].
 *
 * `buf` must be at least MC_HDR_BYTES + MC_MAX_PAYLOAD.
 * `out_fds` should have capacity MC_MAX_FDS.
 *
 * On success returns total frame length (>=12); *out_n_fds is set.
 * Returns 0 on clean EOF, -errno on error, -EPROTO on bad magic/length.
 */
ssize_t mc_recv_frame(int sock, void *buf, size_t buf_cap,
                      int *out_fds, int *out_n_fds);

#endif /* MC_PROTO_H */

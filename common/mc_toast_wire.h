/*
 * Wire format for the "ui/toast" bus message. Little-endian, hand-packed
 * (no struct-padding reliance). Shared by libmc (mc_toast), the toast
 * daemon (decode), and the unit test.
 *
 * Layout:  [0..1] magic 'TS' LE | [2] version | [3] pos
 *          [4..7] duration_ms LE | [8..] UTF-8 text (no NUL, len = total-8)
 */
#ifndef MC_TOAST_WIRE_H
#define MC_TOAST_WIRE_H

#include <stdint.h>
#include <string.h>

#define MC_TOAST_MAGIC       0x5453u   /* 'T','S' little-endian */
#define MC_TOAST_VERSION     1
#define MC_TOAST_HDR_BYTES   8
#define MC_TOAST_MAX_TEXT    512        /* well under the 4KB bus cap */
#define MC_TOAST_DEFAULT_MS  2000

/* pos values mirror mc_toast_pos_t (0=bottom,1=center,2=top). */

/* Given a proposed text length `n` (an EXCLUSIVE end index — the count of
 * bytes we intend to keep), back it off so the cut never lands in the middle
 * of a UTF-8 multibyte sequence. We inspect the byte AT index `n` (the first
 * byte that would be dropped): while it is a continuation byte (10xxxxxx), the
 * boundary is mid-sequence, so shrink `n` until it points at a lead byte.
 * Returns a safe length <= n. */
static inline size_t mc_toast_utf8_floor(const char *s, size_t n)
{
    while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) n--;
    return n;
}

/* Encode into `out` (needs outcap >= MC_TOAST_HDR_BYTES; text is clamped to
 * min(outcap - MC_TOAST_HDR_BYTES, MC_TOAST_MAX_TEXT) bytes at a UTF-8
 * boundary). Returns total byte count, or -1 on bad args. duration<=0 and
 * out-of-range pos are normalized by the CALLER (mc_toast); here we just pack. */
static inline int mc_toast_wire_encode(uint8_t *out, size_t outcap,
                                       const char *text,
                                       uint32_t duration_ms, uint8_t pos)
{
    if (!out || !text) return -1;
    if (outcap < MC_TOAST_HDR_BYTES) return -1;
    size_t tlen = strlen(text);
    size_t maxt = outcap - MC_TOAST_HDR_BYTES;
    if (maxt > MC_TOAST_MAX_TEXT) maxt = MC_TOAST_MAX_TEXT;
    if (tlen > maxt) tlen = mc_toast_utf8_floor(text, maxt);
    out[0] = MC_TOAST_MAGIC & 0xff;
    out[1] = (MC_TOAST_MAGIC >> 8) & 0xff;
    out[2] = MC_TOAST_VERSION;
    out[3] = pos;
    out[4] =  duration_ms        & 0xff;
    out[5] = (duration_ms >> 8)  & 0xff;
    out[6] = (duration_ms >> 16) & 0xff;
    out[7] = (duration_ms >> 24) & 0xff;
    memcpy(out + MC_TOAST_HDR_BYTES, text, tlen);
    return (int)(MC_TOAST_HDR_BYTES + tlen);
}

/* Decode a received payload. `text_out` gets a NUL-terminated copy
 * (cap text_cap). Returns 0 on success, -1 on malformed/short/bad-magic. */
static inline int mc_toast_wire_decode(const void *payload, size_t len,
                                       uint32_t *duration_ms, uint8_t *pos,
                                       char *text_out, size_t text_cap)
{
    const uint8_t *p = (const uint8_t *)payload;
    if (!p || len < MC_TOAST_HDR_BYTES) return -1;
    uint16_t magic = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
    if (magic != MC_TOAST_MAGIC) return -1;
    if (p[2] != MC_TOAST_VERSION) return -1;
    if (pos) *pos = p[3];
    if (duration_ms)
        *duration_ms = (uint32_t)p[4] | ((uint32_t)p[5] << 8)
                     | ((uint32_t)p[6] << 16) | ((uint32_t)p[7] << 24);
    size_t tlen = len - MC_TOAST_HDR_BYTES;
    if (text_out && text_cap) {
        if (tlen > text_cap - 1) tlen = mc_toast_utf8_floor((const char *)(p + 8), text_cap - 1);
        memcpy(text_out, p + MC_TOAST_HDR_BYTES, tlen);
        text_out[tlen] = '\0';
    }
    return 0;
}

#endif /* MC_TOAST_WIRE_H */

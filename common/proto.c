#define _GNU_SOURCE
#include "proto.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/uio.h>

/* -------- builder -------- */

void mc_builder_init(struct mc_builder *b, void *buf, size_t cap)
{
    b->buf = (uint8_t *)buf;
    b->cap = cap;
    b->len = 0;
    b->err = 0;
    /* reserve space for header */
    if (cap < MC_HDR_BYTES) {
        b->err = -ENOSPC;
    }
}

static int put_tlv(struct mc_builder *b, uint16_t tag,
                   const void *val, uint16_t vlen)
{
    if (b->err) return b->err;
    size_t need = MC_HDR_BYTES + b->len + 4 + vlen;
    if (need > b->cap) { b->err = -ENOSPC; return -ENOSPC; }
    uint8_t *p = b->buf + MC_HDR_BYTES + b->len;
    p[0] = tag & 0xff;        p[1] = (tag >> 8) & 0xff;
    p[2] = vlen & 0xff;       p[3] = (vlen >> 8) & 0xff;
    if (vlen) memcpy(p + 4, val, vlen);
    b->len += 4 + vlen;
    return 0;
}

int mc_put_u8 (struct mc_builder *b, uint16_t tag, uint8_t v)
{ return put_tlv(b, tag, &v, 1); }

int mc_put_u16(struct mc_builder *b, uint16_t tag, uint16_t v)
{ uint8_t bs[2] = { v & 0xff, (v >> 8) & 0xff };
  return put_tlv(b, tag, bs, 2); }

int mc_put_i16(struct mc_builder *b, uint16_t tag, int16_t v)
{ return mc_put_u16(b, tag, (uint16_t)v); }

int mc_put_u32(struct mc_builder *b, uint16_t tag, uint32_t v)
{ uint8_t bs[4] = { v & 0xff, (v >> 8) & 0xff,
                    (v >> 16) & 0xff, (v >> 24) & 0xff };
  return put_tlv(b, tag, bs, 4); }

int mc_put_u64(struct mc_builder *b, uint16_t tag, uint64_t v)
{ uint8_t bs[8];
  for (int i = 0; i < 8; i++) bs[i] = (v >> (8 * i)) & 0xff;
  return put_tlv(b, tag, bs, 8); }

int mc_put_str(struct mc_builder *b, uint16_t tag, const char *s)
{ size_t n = s ? strlen(s) : 0;
  if (n > 0xffff) { b->err = -E2BIG; return -E2BIG; }
  return put_tlv(b, tag, s, (uint16_t)n); }

int mc_put_bin(struct mc_builder *b, uint16_t tag, const void *p, size_t n)
{ if (n > 0xffff) { b->err = -E2BIG; return -E2BIG; }
  return put_tlv(b, tag, p, (uint16_t)n); }

size_t mc_builder_finalize(struct mc_builder *b,
                           uint16_t type, uint32_t serial)
{
    if (b->err) return 0;
    uint8_t *h = b->buf;
    /* magic LE */
    h[0] = MC_MAGIC & 0xff;
    h[1] = (MC_MAGIC >> 8) & 0xff;
    h[2] = type & 0xff;
    h[3] = (type >> 8) & 0xff;
    uint32_t plen = (uint32_t)b->len;
    h[4] = plen & 0xff;       h[5] = (plen >> 8) & 0xff;
    h[6] = (plen >> 16) & 0xff; h[7] = (plen >> 24) & 0xff;
    h[8] = serial & 0xff;     h[9] = (serial >> 8) & 0xff;
    h[10] = (serial >> 16) & 0xff; h[11] = (serial >> 24) & 0xff;
    return MC_HDR_BYTES + b->len;
}

/* -------- parser -------- */

void mc_parser_init(struct mc_parser *pr,
                    const void *payload, size_t payload_len)
{
    pr->p = (const uint8_t *)payload;
    pr->remain = payload_len;
}

/* Scan the payload looking for a tag. On match, returns pointer to value
 * and writes vlen; otherwise returns NULL. */
static const uint8_t *find_tag(const void *payload, size_t plen,
                               uint16_t tag, uint16_t *vlen)
{
    const uint8_t *p = (const uint8_t *)payload;
    size_t remain = plen;
    while (remain >= 4) {
        uint16_t t  = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
        uint16_t vl = (uint16_t)p[2] | ((uint16_t)p[3] << 8);
        if (remain < (size_t)(4 + vl)) return NULL;  /* malformed */
        if (t == tag) {
            if (vlen) *vlen = vl;
            return p + 4;
        }
        p += 4 + vl;
        remain -= 4 + vl;
    }
    return NULL;
}

int mc_get_u8(const void *payload, size_t plen, uint16_t tag, uint8_t *out)
{
    uint16_t vl;
    const uint8_t *v = find_tag(payload, plen, tag, &vl);
    if (!v)        return -1;
    if (vl != 1)   return -2;
    *out = v[0];
    return 0;
}

int mc_get_u16(const void *payload, size_t plen, uint16_t tag, uint16_t *out)
{
    uint16_t vl;
    const uint8_t *v = find_tag(payload, plen, tag, &vl);
    if (!v)        return -1;
    if (vl != 2)   return -2;
    *out = (uint16_t)v[0] | ((uint16_t)v[1] << 8);
    return 0;
}

int mc_get_i16(const void *payload, size_t plen, uint16_t tag, int16_t *out)
{
    uint16_t u;
    int r = mc_get_u16(payload, plen, tag, &u);
    if (r == 0) *out = (int16_t)u;
    return r;
}

int mc_get_u32(const void *payload, size_t plen, uint16_t tag, uint32_t *out)
{
    uint16_t vl;
    const uint8_t *v = find_tag(payload, plen, tag, &vl);
    if (!v)        return -1;
    if (vl != 4)   return -2;
    *out =  (uint32_t)v[0]        | ((uint32_t)v[1] << 8)
         | ((uint32_t)v[2] << 16) | ((uint32_t)v[3] << 24);
    return 0;
}

int mc_get_u64(const void *payload, size_t plen, uint16_t tag, uint64_t *out)
{
    uint16_t vl;
    const uint8_t *v = find_tag(payload, plen, tag, &vl);
    if (!v)        return -1;
    if (vl != 8)   return -2;
    uint64_t r = 0;
    for (int i = 0; i < 8; i++) r |= ((uint64_t)v[i]) << (8 * i);
    *out = r;
    return 0;
}

int mc_get_bin(const void *payload, size_t plen, uint16_t tag,
               const void **out_ptr, size_t *out_len)
{
    uint16_t vl;
    const uint8_t *v = find_tag(payload, plen, tag, &vl);
    if (!v) return -1;
    *out_ptr = v;
    *out_len = vl;
    return 0;
}

int mc_get_str(const void *payload, size_t plen, uint16_t tag,
               char *out, size_t out_cap)
{
    uint16_t vl;
    const uint8_t *v = find_tag(payload, plen, tag, &vl);
    if (!v) return -1;
    if (out_cap == 0) return vl;
    size_t copy = vl < (out_cap - 1) ? vl : (out_cap - 1);
    memcpy(out, v, copy);
    out[copy] = '\0';
    return (int)vl;
}

/* -------- I/O with fd-passing -------- */

int mc_send_frame(int sock, const void *buf, size_t len,
                  const int *fds, int n_fds)
{
    if (len < MC_HDR_BYTES || len > MC_HDR_BYTES + MC_MAX_PAYLOAD)
        return -EINVAL;
    if (n_fds < 0 || n_fds > MC_MAX_FDS)
        return -EINVAL;

    const uint8_t *p = (const uint8_t *)buf;
    size_t remaining = len;
    int first = 1;

    while (remaining > 0) {
        struct iovec iov;
        iov.iov_base = (void *)p;
        iov.iov_len  = remaining;

        struct msghdr mh;
        memset(&mh, 0, sizeof(mh));
        mh.msg_iov    = &iov;
        mh.msg_iovlen = 1;

        char cbuf[CMSG_SPACE(sizeof(int) * MC_MAX_FDS)];
        if (first && n_fds > 0) {
            memset(cbuf, 0, sizeof(cbuf));
            mh.msg_control    = cbuf;
            mh.msg_controllen = CMSG_SPACE(sizeof(int) * n_fds);
            struct cmsghdr *cm = CMSG_FIRSTHDR(&mh);
            cm->cmsg_level = SOL_SOCKET;
            cm->cmsg_type  = SCM_RIGHTS;
            cm->cmsg_len   = CMSG_LEN(sizeof(int) * n_fds);
            memcpy(CMSG_DATA(cm), fds, sizeof(int) * n_fds);
        }

        ssize_t n = sendmsg(sock, &mh, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -errno;
        }
        p += n;
        remaining -= (size_t)n;
        first = 0;  /* fds delivered on first send only */
    }
    return 0;
}

/* Read exactly n bytes; collect any SCM_RIGHTS along the way. */
static ssize_t recv_exact(int sock, uint8_t *buf, size_t n,
                          int *fds, int *n_fds_inout)
{
    size_t got = 0;
    int    cap_fds = *n_fds_inout;
    int    cur_fds = 0;

    while (got < n) {
        struct iovec iov;
        iov.iov_base = buf + got;
        iov.iov_len  = n - got;

        struct msghdr mh;
        memset(&mh, 0, sizeof(mh));
        mh.msg_iov    = &iov;
        mh.msg_iovlen = 1;

        char cbuf[CMSG_SPACE(sizeof(int) * MC_MAX_FDS)];
        mh.msg_control    = cbuf;
        mh.msg_controllen = sizeof(cbuf);

        ssize_t r = recvmsg(sock, &mh, MSG_CMSG_CLOEXEC);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -errno;
        }
        if (r == 0) {
            /* peer closed */
            *n_fds_inout = cur_fds;
            return (ssize_t)got;
        }

        /* harvest fds */
        for (struct cmsghdr *cm = CMSG_FIRSTHDR(&mh);
             cm != NULL;
             cm = CMSG_NXTHDR(&mh, cm)) {
            if (cm->cmsg_level == SOL_SOCKET &&
                cm->cmsg_type  == SCM_RIGHTS) {
                int n_new = (cm->cmsg_len - CMSG_LEN(0)) / sizeof(int);
                int *src  = (int *)CMSG_DATA(cm);
                for (int i = 0; i < n_new; i++) {
                    if (cur_fds < cap_fds) {
                        fds[cur_fds++] = src[i];
                    } else {
                        /* overflow: close to avoid leak */
                        close(src[i]);
                    }
                }
            }
        }

        if (mh.msg_flags & MSG_CTRUNC) {
            /* dropped cmsg data: protocol error */
            *n_fds_inout = cur_fds;
            return -EPROTO;
        }

        got += (size_t)r;
    }
    *n_fds_inout = cur_fds;
    return (ssize_t)got;
}

ssize_t mc_recv_frame(int sock, void *buf, size_t buf_cap,
                      int *out_fds, int *out_n_fds)
{
    if (buf_cap < MC_HDR_BYTES + MC_MAX_PAYLOAD) return -EINVAL;
    int fd_cap = MC_MAX_FDS;
    *out_n_fds = fd_cap;
    uint8_t *p = (uint8_t *)buf;

    /* Read header */
    ssize_t r = recv_exact(sock, p, MC_HDR_BYTES, out_fds, out_n_fds);
    if (r < 0) return r;
    if (r == 0) return 0;  /* clean EOF */
    if (r < (ssize_t)MC_HDR_BYTES) return -EPROTO;

    uint16_t magic = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
    if (magic != MC_MAGIC) return -EPROTO;

    uint32_t plen =  (uint32_t)p[4]         | ((uint32_t)p[5] << 8)
                  | ((uint32_t)p[6] << 16)  | ((uint32_t)p[7] << 24);
    if (plen > MC_MAX_PAYLOAD) return -EPROTO;

    /* Read payload, continuing to harvest fds */
    int more_cap = fd_cap - *out_n_fds;
    int more_got = more_cap;
    if (plen > 0) {
        ssize_t r2 = recv_exact(sock, p + MC_HDR_BYTES, plen,
                                out_fds + *out_n_fds, &more_got);
        if (r2 < 0) return r2;
        if (r2 < (ssize_t)plen) return -EPROTO;  /* short read = peer gone */
        *out_n_fds += more_got;
    }

    return (ssize_t)(MC_HDR_BYTES + plen);
}

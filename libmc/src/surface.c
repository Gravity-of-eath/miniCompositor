#define _GNU_SOURCE
#include "internal.h"

#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <unistd.h>

uint32_t mc_internal_next_serial(mc_ctx_t *c);

static int wait_create_reply(mc_ctx_t *c, mc_surface_t *sf)
{
    int fds[MC_MAX_FDS]; int n_fds = 0;
    ssize_t got = mc_internal_recv(c, fds, &n_fds);
    if (got <= 0) {
        mc_internal_mark_dead(c, "recv during CREATE_SURFACE");
        return MC_E_PROTO;
    }

    uint16_t type = (uint16_t)c->rxbuf[2] | ((uint16_t)c->rxbuf[3] << 8);
    uint32_t plen = (uint32_t)c->rxbuf[4]
                  | ((uint32_t)c->rxbuf[5] << 8)
                  | ((uint32_t)c->rxbuf[6] << 16)
                  | ((uint32_t)c->rxbuf[7] << 24);
    const void *payload = c->rxbuf + MC_HDR_BYTES;

    if (type == MC_SV_ERROR) {
        uint32_t code = MC_E_INTERNAL;
        mc_get_u32(payload, plen, MC_T_CODE, &code);
        for (int i = 0; i < n_fds; i++) close(fds[i]);
        return (int)code;
    }
    if (type != MC_SV_SURFACE_OK) {
        for (int i = 0; i < n_fds; i++) close(fds[i]);
        return MC_E_PROTO;
    }

    uint32_t sid = 0, stride = 0, size = 0;
    uint8_t  n_buf = 0;
    mc_get_u32(payload, plen, MC_T_SID,    &sid);
    mc_get_u32(payload, plen, MC_T_STRIDE, &stride);
    mc_get_u32(payload, plen, MC_T_SIZE,   &size);
    mc_get_u8 (payload, plen, MC_T_N_BUF,  &n_buf);

    /* Optional per-buf mmap offsets (server uses these for the g2d-mem
     * allocator). Absent ⇒ all zeros (memfd / dma-buf path). */
    const uint8_t *off_blob = NULL;
    size_t         off_len  = 0;
    uint32_t       mmap_offs[MC_CLI_MAX_BUFS] = {0};
    mc_get_bin(payload, plen, MC_T_PHYS_LIST,
               (const void **)&off_blob, &off_len);
    if (off_blob && off_len >= (size_t)n_buf * sizeof(uint32_t)) {
        for (int i = 0; i < n_buf; i++) {
            mmap_offs[i] =  (uint32_t)off_blob[i*4]
                         | ((uint32_t)off_blob[i*4 + 1] << 8)
                         | ((uint32_t)off_blob[i*4 + 2] << 16)
                         | ((uint32_t)off_blob[i*4 + 3] << 24);
        }
    }

    /* Expected: n_buf shm fds + 1 eventfd. With the g2d-mem allocator on
     * the server, the n_buf "shm fds" are all dups of the SAME /dev/g2d
     * fd; each is mmapped at a distinct physical offset. */
    if (n_buf < 1 || n_buf > MC_CLI_MAX_BUFS || n_fds != n_buf + 1) {
        for (int i = 0; i < n_fds; i++) close(fds[i]);
        return MC_E_PROTO;
    }

    sf->sid    = sid;
    sf->stride = stride;
    sf->n_buf  = n_buf;

    int ok = 1;
    for (int i = 0; i < n_buf; i++) {
        sf->bufs[i].shm_fd = fds[i];
        sf->bufs[i].size   = size;
        sf->bufs[i].state  = 0;  /* FREE */
        void *m = mmap(NULL, size, PROT_READ | PROT_WRITE,
                       MAP_SHARED, fds[i], mmap_offs[i]);
        if (m == MAP_FAILED) { ok = 0; break; }
        sf->bufs[i].map = m;
    }
    sf->event_fd = fds[n_buf];
    sf->cur_writing = -1;
    sf->next_acquire = 0;

    if (!ok) {
        for (int i = 0; i < n_buf; i++) {
            if (sf->bufs[i].map) munmap(sf->bufs[i].map, sf->bufs[i].size);
            close(sf->bufs[i].shm_fd);
        }
        close(sf->event_fd);
        return MC_E_NOMEM;
    }
    return MC_OK;
}

mc_surface_t *mc_surface_create_shm_ex(mc_ctx_t *c, int w, int h,
                                       mc_format_t fmt, mc_role_t role,
                                       int n_buf, uint32_t flags)
{
    if (!c || w <= 0 || h <= 0) { mc_set_last_error(MC_E_INVAL); return NULL; }
    if (n_buf < 1 || n_buf > MC_CLI_MAX_BUFS) n_buf = 2;

    mc_surface_t *sf = calloc(1, sizeof(*sf));
    if (!sf) { mc_set_last_error(MC_E_NOMEM); return NULL; }
    sf->ctx = c;
    sf->w = (uint16_t)w; sf->h = (uint16_t)h;
    sf->format = (uint8_t)fmt;
    sf->role   = (uint8_t)role;

    uint8_t buf[128];
    struct mc_builder b;
    mc_builder_init(&b, buf, sizeof(buf));
    mc_put_u16(&b, MC_T_WIDTH,    (uint16_t)w);
    mc_put_u16(&b, MC_T_HEIGHT,   (uint16_t)h);
    mc_put_u8 (&b, MC_T_FORMAT,   (uint8_t)fmt);
    mc_put_u8 (&b, MC_T_ROLE,     (uint8_t)role);
    mc_put_u8 (&b, MC_T_N_BUF,    (uint8_t)n_buf);
    mc_put_u8 (&b, MC_T_BUF_TYPE, 1);  /* SHM */
    if (flags & MC_SURF_FLAG_FLIP_Y) {
        mc_put_u8(&b, MC_T_FLIP_Y, 1);
    }
    uint32_t serial = mc_internal_next_serial(c);
    size_t flen = mc_builder_finalize(&b, MC_CL_CREATE_SURFACE, serial);

    if (mc_send_frame(c->sock, buf, flen, NULL, 0) < 0) {
        mc_internal_mark_dead(c, "send during CREATE_SURFACE");
        free(sf); mc_set_last_error(MC_E_PROTO); return NULL;
    }
    int r = wait_create_reply(c, sf);
    if (r != MC_OK) { free(sf); mc_set_last_error(r); return NULL; }

    c->surface = sf;
    return sf;
}

mc_surface_t *mc_surface_create_shm(mc_ctx_t *c, int w, int h,
                                    mc_format_t fmt, mc_role_t role, int n_buf)
{
    return mc_surface_create_shm_ex(c, w, h, fmt, role, n_buf, 0);
}

int mc_surface_request_focus(mc_surface_t *sf)
{
    if (!sf) return MC_E_INVAL;
    if (!sf->ctx->alive) return MC_E_PROTO;
    uint8_t buf[32];
    struct mc_builder b;
    mc_builder_init(&b, buf, sizeof(buf));
    mc_put_u32(&b, MC_T_SID, sf->sid);
    size_t flen = mc_builder_finalize(&b, MC_CL_REQUEST_FOCUS,
                                      mc_internal_next_serial(sf->ctx));
    if (mc_send_frame(sf->ctx->sock, buf, flen, NULL, 0) < 0) {
        mc_internal_mark_dead(sf->ctx, "send during REQUEST_FOCUS");
        return MC_E_PROTO;
    }
    return MC_OK;
}

int mc_surface_set_popup_pos(mc_surface_t *sf, int x, int y)
{
    if (!sf) return MC_E_INVAL;
    if (!sf->ctx->alive) return MC_E_PROTO;
    uint8_t buf[64];
    struct mc_builder b;
    mc_builder_init(&b, buf, sizeof(buf));
    mc_put_u32(&b, MC_T_SID,     sf->sid);
    mc_put_u8 (&b, MC_T_ROLE,    sf->role);
    mc_put_u8 (&b, MC_T_MODAL,   0);
    mc_put_i16(&b, MC_T_POPUP_X, (int16_t)x);
    mc_put_i16(&b, MC_T_POPUP_Y, (int16_t)y);
    size_t flen = mc_builder_finalize(&b, MC_CL_SET_ROLE,
                                      mc_internal_next_serial(sf->ctx));
    if (mc_send_frame(sf->ctx->sock, buf, flen, NULL, 0) < 0) {
        mc_internal_mark_dead(sf->ctx, "send during SET_ROLE");
        return MC_E_PROTO;
    }
    return MC_OK;
}

/* Drain the eventfd. The counter is just a "something was released" hint;
 * the actual buffer we wait for is determined by round-robin indexing,
 * not by trying to figure out which buffer the compositor freed. */
static void drain_event_fd(mc_surface_t *sf)
{
    uint64_t v;
    while (read(sf->event_fd, &v, sizeof(v)) > 0) {
        /* For round-robin scheme: client knows which buffer is "its turn".
         * The eventfd tick proves *some* release happened, which usually
         * means the buffer we're about to acquire is now FREE on server side.
         * Mark our next_acquire buffer FREE (it can only become FREE on the
         * server side by being released, so this is the only buffer the
         * server could have released). */
        for (uint64_t k = 0; k < v; k++) {
            /* The next_acquire buffer is by definition the one the server
             * just released (under round-robin invariant). */
            int idx = sf->next_acquire;
            if (sf->bufs[idx].state == 2) {
                sf->bufs[idx].state = 0;
            }
            /* If we somehow get more eventfd ticks than expected (e.g.,
             * server force_full doubled compose), the extras are harmless
             * since state is already FREE. */
        }
    }
}

/*
 * Round-robin acquire. The client and server are kept in sync by both
 * advancing the same way: client commits idx, server rotates pending=idx
 * into scanout and releases the previous one (idx-1 mod n_buf, i.e. exactly
 * the buffer the client is about to ask for next).
 *
 * Old "scan for first FREE" logic was racy: when the compositor's eventfd
 * signaled a release, we couldn't know *which* buffer was freed, so we'd
 * pick the first one that *looked* FREE in client state, which could
 * disagree with the server's view and cause the client to commit a buffer
 * that was still SCANOUT on the server (prev==newi, no release, deadlock).
 */
void *mc_surface_acquire(mc_surface_t *sf, int *out_stride)
{
    if (!sf || !sf->ctx->alive) return NULL;
    if (out_stride) *out_stride = (int)sf->stride;

    drain_event_fd(sf);

    int idx = sf->next_acquire;
    int attempts = 0;
    while (sf->bufs[idx].state == 2 /* BUSY */) {
        if (!sf->ctx->alive) return NULL;
        struct pollfd p = { .fd = sf->event_fd, .events = POLLIN };
        int r = poll(&p, 1, 200);
        if (r < 0) {
            if (errno == EINTR) return NULL;
            return NULL;
        }
        drain_event_fd(sf);
        if (++attempts > 25) {
            /* compositor stalled or gone */
            mc_internal_mark_dead(sf->ctx, "acquire timeout");
            return NULL;
        }
    }

    sf->bufs[idx].state = 1;
    sf->cur_writing = idx;
    return sf->bufs[idx].map;
}

int mc_surface_commit(mc_surface_t *sf, const mc_rect_t *damage, int n_rect)
{
    if (!sf || sf->cur_writing < 0) return MC_E_INVAL;
    int idx = sf->cur_writing;

    uint8_t buf[256];
    struct mc_builder b;
    mc_builder_init(&b, buf, sizeof(buf));
    mc_put_u32(&b, MC_T_SID,     sf->sid);
    mc_put_u8 (&b, MC_T_BUF_IDX, (uint8_t)idx);

    /* Damage encoded as one big rect for Phase 0 (compositor ignores). */
    if (damage && n_rect > 0) {
        uint8_t rects[8 * 8];  /* up to 8 rects */
        int n = n_rect > 8 ? 8 : n_rect;
        for (int i = 0; i < n; i++) {
            uint8_t *p = rects + i * 8;
            int16_t v;
            v = damage[i].x; p[0] = v & 0xff; p[1] = (v >> 8) & 0xff;
            v = damage[i].y; p[2] = v & 0xff; p[3] = (v >> 8) & 0xff;
            v = damage[i].w; p[4] = v & 0xff; p[5] = (v >> 8) & 0xff;
            v = damage[i].h; p[6] = v & 0xff; p[7] = (v >> 8) & 0xff;
        }
        mc_put_bin(&b, MC_T_DAMAGE, rects, (size_t)(n * 8));
    }

    size_t flen = mc_builder_finalize(&b, MC_CL_COMMIT,
                                      mc_internal_next_serial(sf->ctx));
    if (mc_send_frame(sf->ctx->sock, buf, flen, NULL, 0) < 0) {
        mc_internal_mark_dead(sf->ctx, "send during COMMIT");
        return MC_E_PROTO;
    }

    sf->bufs[idx].state = 2;
    sf->cur_writing = -1;
    sf->next_acquire = (idx + 1) % sf->n_buf;
    return MC_OK;
}

void *mc_surface_buf_at(mc_surface_t *sf, int idx, int *out_stride)
{
    if (!sf || idx < 0 || idx >= sf->n_buf) return NULL;
    if (out_stride) *out_stride = (int)sf->stride;
    return sf->bufs[idx].map;
}

int mc_surface_buf_fd(mc_surface_t *sf, int idx)
{
    if (!sf || idx < 0 || idx >= sf->n_buf) return -1;
    return sf->bufs[idx].shm_fd;
}

int mc_surface_buf_stride(mc_surface_t *sf)
{
    return sf ? (int)sf->stride : 0;
}

int mc_surface_n_buf(mc_surface_t *sf) { return sf ? sf->n_buf : 0; }

int mc_surface_size_bytes(mc_surface_t *sf)
{
    if (!sf) return 0;
    return (int)sf->bufs[0].size;
}

int mc_surface_wait_buf_free(mc_surface_t *sf, int idx)
{
    if (!sf || idx < 0 || idx >= sf->n_buf) return MC_E_INVAL;
    if (!sf->ctx->alive) return MC_E_PROTO;
    drain_event_fd(sf);
    int slow_ticks = 0;
    while (sf->bufs[idx].state == 2 /* BUSY */) {
        if (!sf->ctx->alive) return MC_E_PROTO;
        /* Poll both eventfd (buffer release) AND socket (in case the
         * compositor died and we'll see POLLHUP). Either wakes us up. */
        struct pollfd p[2] = {
            { .fd = sf->event_fd, .events = POLLIN },
            { .fd = sf->ctx->sock, .events = 0 },   /* HUP/ERR delivered anyway */
        };
        int r = poll(p, 2, 1000);
        if (r < 0) {
            if (errno == EINTR) return MC_E_INTERNAL;
            return MC_E_INTERNAL;
        }
        if (p[1].revents & (POLLHUP | POLLERR | POLLNVAL)) {
            mc_internal_mark_dead(sf->ctx, "socket HUP while waiting buffer");
            return MC_E_PROTO;
        }
        if (r == 0) {
            if (++slow_ticks > 5) {
                mc_internal_mark_dead(sf->ctx, "wait_buf_free stalled 5s");
                return MC_E_BUSY;
            }
            continue;
        }
        drain_event_fd(sf);
    }
    sf->bufs[idx].state = 1;
    sf->cur_writing = idx;
    return MC_OK;
}

int mc_surface_commit_idx(mc_surface_t *sf, int idx,
                          const mc_rect_t *damage, int n_rect)
{
    if (!sf || idx < 0 || idx >= sf->n_buf) return MC_E_INVAL;
    if (!sf->ctx->alive) return MC_E_PROTO;

    uint8_t buf[256];
    struct mc_builder b;
    mc_builder_init(&b, buf, sizeof(buf));
    mc_put_u32(&b, MC_T_SID,     sf->sid);
    mc_put_u8 (&b, MC_T_BUF_IDX, (uint8_t)idx);

    if (damage && n_rect > 0) {
        uint8_t rects[8 * 8];
        int n = n_rect > 8 ? 8 : n_rect;
        for (int i = 0; i < n; i++) {
            uint8_t *p = rects + i * 8;
            int16_t v;
            v = damage[i].x; p[0] = v & 0xff; p[1] = (v >> 8) & 0xff;
            v = damage[i].y; p[2] = v & 0xff; p[3] = (v >> 8) & 0xff;
            v = damage[i].w; p[4] = v & 0xff; p[5] = (v >> 8) & 0xff;
            v = damage[i].h; p[6] = v & 0xff; p[7] = (v >> 8) & 0xff;
        }
        mc_put_bin(&b, MC_T_DAMAGE, rects, (size_t)(n * 8));
    }

    size_t flen = mc_builder_finalize(&b, MC_CL_COMMIT,
                                      mc_internal_next_serial(sf->ctx));
    if (mc_send_frame(sf->ctx->sock, buf, flen, NULL, 0) < 0) {
        mc_internal_mark_dead(sf->ctx, "send during COMMIT (idx)");
        return MC_E_PROTO;
    }

    sf->bufs[idx].state = 2;
    sf->cur_writing = -1;
    sf->next_acquire = (idx + 1) % sf->n_buf;
    return MC_OK;
}

void mc_surface_destroy(mc_surface_t *sf)
{
    if (!sf) return;
    /* Best-effort destroy message */
    uint8_t buf[64];
    struct mc_builder b;
    mc_builder_init(&b, buf, sizeof(buf));
    mc_put_u32(&b, MC_T_SID, sf->sid);
    size_t flen = mc_builder_finalize(&b, MC_CL_DESTROY_SURFACE,
                                      mc_internal_next_serial(sf->ctx));
    (void)mc_send_frame(sf->ctx->sock, buf, flen, NULL, 0);

    for (int i = 0; i < sf->n_buf; i++) {
        if (sf->bufs[i].map) munmap(sf->bufs[i].map, sf->bufs[i].size);
        if (sf->bufs[i].shm_fd > 0) close(sf->bufs[i].shm_fd);
    }
    if (sf->event_fd > 0) close(sf->event_fd);
    if (sf->ctx && sf->ctx->surface == sf) sf->ctx->surface = NULL;
    free(sf);
}

ssize_t mc_internal_recv(mc_ctx_t *c, int *fds, int *n_fds);

int mc_dispatch(mc_ctx_t *c, mc_event_t *out, int timeout_ms)
{
    (void)timeout_ms;
    if (!c || !out) return -1;
    if (!c->alive) return -1;
    out->kind = MC_EV_NONE;
    out->sid = 0;
    if (c->surface) drain_event_fd(c->surface);

    /* Non-blocking peek at the socket. */
    struct pollfd p = { .fd = c->sock, .events = POLLIN };
    int r = poll(&p, 1, 0);
    if (r < 0) return 0;
    if (p.revents & (POLLHUP | POLLERR | POLLNVAL)) {
        mc_internal_mark_dead(c, "socket HUP/ERR during dispatch");
        return -1;
    }
    if (!(p.revents & POLLIN)) return 0;

    int fds[MC_MAX_FDS]; int n_fds = 0;
    ssize_t got = mc_internal_recv(c, fds, &n_fds);
    for (int i = 0; i < n_fds; i++) close(fds[i]);
    if (got <= 0) {
        mc_internal_mark_dead(c, "recv returned 0/error during dispatch");
        return -1;
    }

    uint16_t type = (uint16_t)c->rxbuf[2] | ((uint16_t)c->rxbuf[3] << 8);
    uint32_t plen = (uint32_t)c->rxbuf[4]
                  | ((uint32_t)c->rxbuf[5] << 8)
                  | ((uint32_t)c->rxbuf[6] << 16)
                  | ((uint32_t)c->rxbuf[7] << 24);
    const void *payload = c->rxbuf + MC_HDR_BYTES;

    if (type == MC_SV_INPUT) {
        uint32_t sid = 0, t_ms = 0;
        int16_t  x = 0, y = 0;
        uint8_t  state = 0, slot = 0;
        mc_get_u32(payload, plen, MC_T_SID,           &sid);
        mc_get_u8 (payload, plen, MC_T_INPUT_TYPE,    &state);
        mc_get_i16(payload, plen, MC_T_INPUT_X,       &x);
        mc_get_i16(payload, plen, MC_T_INPUT_Y,       &y);
        mc_get_u8 (payload, plen, MC_T_INPUT_SLOT,    &slot);
        mc_get_u32(payload, plen, MC_T_INPUT_TIME,    &t_ms);

        out->kind = MC_EV_TOUCH;
        out->sid  = sid;
        out->touch.x     = x;
        out->touch.y     = y;
        out->touch.state = state;
        out->touch.slot  = slot;
        out->touch.t_ms  = t_ms;
        return 1;
    }

    if (type == MC_SV_LIFECYCLE) {
        uint32_t sid = 0;
        uint8_t  state = 0;
        mc_get_u32(payload, plen, MC_T_SID,      &sid);
        mc_get_u8 (payload, plen, MC_T_LC_STATE, &state);
        out->kind     = MC_EV_LIFECYCLE;
        out->sid      = sid;
        out->lc.state = state;
        return 1;
    }

    if (type == MC_SV_BUS_MSG) {
        /* Topic/sender go into ctx-owned NUL-terminated scratch buffers so
         * the caller can treat them as C strings. Payload borrows directly
         * into rxbuf (binary-safe) and stays valid until next dispatch. */
        mc_get_str(payload, plen, MC_T_BUS_TOPIC,
                   c->ev_topic, sizeof(c->ev_topic));
        mc_get_str(payload, plen, MC_T_BUS_SENDER,
                   c->ev_sender, sizeof(c->ev_sender));
        const void *pl_ptr = NULL; size_t pl_len = 0;
        mc_get_bin(payload, plen, MC_T_BUS_PAYLOAD, &pl_ptr, &pl_len);

        out->kind   = MC_EV_BUS;
        out->bus.topic  = c->ev_topic;
        out->bus.sender = c->ev_sender;
        out->bus.data   = pl_ptr;
        out->bus.len    = (uint32_t)pl_len;
        return 1;
    }
    /* Unknown async msg type; silently swallow so we don't desync. */
    return 0;
}

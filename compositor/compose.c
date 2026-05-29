#define _GNU_SOURCE
#include "compose.h"
#include "surface.h"
#include "backend.h"
#include "transport.h"
#include "accel.h"
#include "mc_alloc.h"
#include "log.h"

#include <stdint.h>
#include <string.h>
#include <unistd.h>

/* ---- rect helpers ---- */

static int rect_empty(const struct mc_rect_i *r) {
    return r->w <= 0 || r->h <= 0;
}

static int rects_intersect_into(const struct mc_rect_i *a,
                                const struct mc_rect_i *b,
                                struct mc_rect_i *out)
{
    int x0 = a->x > b->x ? a->x : b->x;
    int y0 = a->y > b->y ? a->y : b->y;
    int x1 = (a->x + a->w) < (b->x + b->w) ? (a->x + a->w) : (b->x + b->w);
    int y1 = (a->y + a->h) < (b->y + b->h) ? (a->y + a->h) : (b->y + b->h);
    if (x1 <= x0 || y1 <= y0) { out->w = 0; out->h = 0; return 0; }
    out->x = (int16_t)x0; out->y = (int16_t)y0;
    out->w = (int16_t)(x1 - x0); out->h = (int16_t)(y1 - y0);
    return 1;
}

static void rect_union_into(struct mc_rect_i *a, const struct mc_rect_i *b)
{
    if (rect_empty(b)) return;
    if (rect_empty(a)) { *a = *b; return; }
    int x0 = a->x < b->x ? a->x : b->x;
    int y0 = a->y < b->y ? a->y : b->y;
    int x1 = (a->x + a->w) > (b->x + b->w) ? (a->x + a->w) : (b->x + b->w);
    int y1 = (a->y + a->h) > (b->y + b->h) ? (a->y + a->h) : (b->y + b->h);
    a->x = (int16_t)x0; a->y = (int16_t)y0;
    a->w = (int16_t)(x1 - x0); a->h = (int16_t)(y1 - y0);
}

/* ---- accel hook ----
 * Resolved once at first compose. Falls back to CPU if anything goes wrong.
 * The compositor never touches pixels directly anymore. */
static const struct mc_accel_ops *g_accel;

static const struct mc_accel_ops *ensure_accel(void)
{
    if (g_accel) return g_accel;
    g_accel = mc_accel_select();
    if (!g_accel) {
        LOG_E("accel: select returned NULL; this should never happen "
              "(cpu backend should always succeed)");
        g_accel = &mc_accel_cpu;
        (void)g_accel->init();
    }
    return g_accel;
}

/* Wrap each accel call with an automatic CPU fallback. We do this per-op
 * (not per-frame) so a single ioctl glitch doesn't tear the whole frame
 * apart; we just degrade that one rect to CPU. */
static void do_fill(const struct mc_accel_ops *ops,
                    const struct mc_accel_surface *dst,
                    int x, int y, int w, int h, uint32_t bgra)
{
    if (ops != &mc_accel_cpu && ops->fill && ops->fill(dst, x, y, w, h, bgra) == 0) return;
    (void)mc_accel_cpu.fill(dst, x, y, w, h, bgra);
}

static void do_blit(const struct mc_accel_ops *ops,
                    const struct mc_accel_surface *dst, int dx, int dy,
                    const struct mc_accel_surface *src, int sx, int sy,
                    int w, int h)
{
    /* Y-flipped sources only the CPU path handles today. HW backends
     * (G2D / RGA) do not expose a "vertical mirror" mode in the ops we
     * call so we keep them on the normal path; flipped surfaces fall
     * back to CPU automatically. */
    if (!src->flip_y && ops != &mc_accel_cpu && ops->blit
            && ops->blit(dst, dx, dy, src, sx, sy, w, h) == 0) return;
    (void)mc_accel_cpu.blit(dst, dx, dy, src, sx, sy, w, h);
}

static void do_blend(const struct mc_accel_ops *ops,
                     const struct mc_accel_surface *dst, int dx, int dy,
                     const struct mc_accel_surface *src, int sx, int sy,
                     int w, int h)
{
    if (!src->flip_y && ops != &mc_accel_cpu && ops->blend
            && ops->blend(dst, dx, dy, src, sx, sy, w, h) == 0) return;
    (void)mc_accel_cpu.blend(dst, dx, dy, src, sx, sy, w, h);
}

/* ---- frame state ---- */

static int               s_force_full = 0;

void mc_request_recompose(struct mc_server *s)
{
    if (!s || !s->backend) return;
    s_force_full = 1;
    mc_compose_frame(s, s->backend);
    s_force_full = 1;
    mc_compose_frame(s, s->backend);
    s_force_full = 0;
}

void mc_compose_frame(struct mc_server *s, struct mc_backend *be)
{
    if (!be) return;

    /* GPU-backed compositor: completely separate code path. We just
     * walk visible surfaces bottom-up and ask the backend to draw
     * each as a textured quad. No CPU clear/blit/blend, no damage
     * tracking (Mali draws the full screen each frame -- it's ~5ms
     * for fullscreen + popup, faster than the CPU damage-aware path
     * was when busy). */
#ifdef MC_ENABLE_EGL
    if (s->gpu_compose) {
        struct mc_surface *vis[MC_MAX_SURFACES];
        int nv = 0;
        for (int i = 0; i < MC_MAX_SURFACES; i++) {
            if (s->surfaces[i].sid && s->surfaces[i].visible)
                vis[nv++] = &s->surfaces[i];
        }
        for (int i = 1; i < nv; i++) {
            struct mc_surface *cur = vis[i];
            int j = i;
            while (j > 0 &&
                   (vis[j-1]->z_order >  cur->z_order ||
                    (vis[j-1]->z_order == cur->z_order &&
                     vis[j-1]->focus_stamp > cur->focus_stamp))) {
                vis[j] = vis[j-1]; j--;
            }
            vis[j] = cur;
        }
        mc_backend_egl_begin_frame(be);
        for (int i = 0; i < nv; i++) {
            int idx = vis[i]->pending_idx >= 0
                    ? vis[i]->pending_idx : vis[i]->cur_scanout;
            if (idx < 0) continue;
            mc_backend_egl_draw_surface(be, vis[i]);
        }
        mc_backend_egl_end_frame(be);
        be->present(be);
        for (int i = 0; i < MC_MAX_SURFACES; i++) {
            struct mc_surface *sf = &s->surfaces[i];
            if (sf->sid == 0) continue;
            if (sf->pending_idx < 0) continue;
            int prev = sf->cur_scanout;
            int newi = sf->pending_idx;
            sf->cur_scanout = newi;
            sf->pending_idx = -1;
            sf->pending_dmg_n = 0;
            sf->bufs[newi].state = MC_BUF_SCANOUT;
            if (prev >= 0 && prev != newi) {
                sf->bufs[prev].state = MC_BUF_FREE;
                uint64_t one = 1;
                if (sf->event_fd > 0) { ssize_t wr = write(sf->event_fd, &one, sizeof(one)); (void)wr; }
            }
        }
        return;
    }
#endif

    const struct mc_accel_ops *ops = ensure_accel();

    struct mc_rect_i screen = { 0, 0, (int16_t)s->screen_w, (int16_t)s->screen_h };

    /* Per-surface damage: each surface computes its own per-frame
     * dmg bbox (this frame's pending OR full surface if no info), then
     * unioned with its OWN previous frame's dmg to catch up the fb half
     * that was last touched two frames ago. We do NOT union across
     * surfaces; that's what caused dashboard+popup to expand the compose
     * area to nearly the whole screen even when each surface had a small
     * local update.
     *
     * We still keep a single rect per surface for simplicity -- a tight
     * bbox per surface is dramatically smaller than the cross-surface
     * union and matches how AWTK/LVGL actually compute their dirty
     * regions (one widget invalidate per frame in typical interaction). */

    uint8_t *fb = be->get_buffer(be);
    if (!fb) return;
    uint32_t fb_phys = be->get_buffer_phys ? be->get_buffer_phys(be) : 0;

    /* Build the dst surface descriptor (the fb back-buffer). When the fb
     * backend exposes a physical address (fbdev's smem_start), HW accel
     * can write straight into framebuffer DRAM. */
    struct mc_accel_surface dst = {
        .virt = fb, .dmabuf_fd = -1, .phys = fb_phys,
        .w = s->screen_w, .h = s->screen_h, .stride = s->fb_stride,
        .format = MC_ACCEL_FMT_BGRA8888,
    };

    /* Walk visible surfaces bottom-up; each surface paints its own
     * local damage region (current + previous frame's), not a global
     * union. */
    struct mc_surface *list[MC_MAX_SURFACES];
    int n_vis = 0;
    for (int i = 0; i < MC_MAX_SURFACES; i++) {
        if (s->surfaces[i].sid && s->surfaces[i].visible)
            list[n_vis++] = &s->surfaces[i];
    }
    /* Sort bottom -> top. Primary key: z_order asc; tiebreaker:
     * focus_stamp asc (larger stamp = "more recently focused" = on top). */
    for (int i = 1; i < n_vis; i++) {
        struct mc_surface *cur = list[i];
        int j = i;
        while (j > 0 &&
               (list[j-1]->z_order >  cur->z_order ||
                (list[j-1]->z_order == cur->z_order &&
                 list[j-1]->focus_stamp > cur->focus_stamp))) {
            list[j] = list[j-1]; j--;
        }
        list[j] = cur;
    }

    /* Cull occluded surfaces. Walk from the top down and find the highest
     * fully-opaque fullscreen that covers the screen; anything beneath it
     * never reaches the screen and is wasted work. On T507 the fb DRAM
     * is uncached so each fullscreen blit is ~50ms of pure memcpy; with
     * 3 fullscreens stacked the compose was ~150ms even though 2 of them
     * were never visible. After culling we paint at most one fullscreen
     * plus any overlapping popup. */
    int start = 0;
    for (int i = n_vis - 1; i >= 0; i--) {
        struct mc_surface *sf = list[i];
        if (sf->role != 1 /* FULLSCREEN */) continue;
        int idx = (sf->pending_idx >= 0) ? sf->pending_idx : sf->cur_scanout;
        if (idx < 0) continue;  /* nothing committed yet -> not a real occluder */
        /* FULLSCREEN at our scale is always the full screen, but be defensive. */
        if (sf->x <= 0 && sf->y <= 0
                && sf->x + sf->w >= s->screen_w
                && sf->y + sf->h >= s->screen_h) {
            start = i;
            break;
        }
    }

    struct timespec compose_t0, compose_t1;
    int trace_compose = getenv("MC_COMPOSE_TRACE") != NULL;
    if (trace_compose) clock_gettime(CLOCK_MONOTONIC, &compose_t0);

    /* Per-surface dmg bbox: union the surface's own pending rects PLUS
     * its previous frame's bbox (catch-up for the other fb half on
     * page-flip). Don't union across surfaces -- that's what caused
     * dashboard+popup to expand the compose area unnecessarily.
     * Per-rect painting was tried but ended up painting MORE pixels
     * because each popup rect triggered an under-fullscreen catch-up. */
    struct mc_rect_i this_dmg[MC_MAX_SURFACES];
    int              this_dmg_valid[MC_MAX_SURFACES];
    memset(this_dmg_valid, 0, sizeof(this_dmg_valid));

    for (int i = start; i < n_vis; i++) {
        struct mc_surface *sf = list[i];
        if (sf->pending_idx < 0 && sf->cur_scanout < 0) continue;
        struct mc_rect_i d;
        if (s_force_full || sf->pending_dmg_n == 0 || sf->pending_idx < 0) {
            d = (struct mc_rect_i){0, 0, (int16_t)sf->w, (int16_t)sf->h};
        } else {
            d = sf->pending_dmg[0];
            for (int k = 1; k < sf->pending_dmg_n; k++) {
                rect_union_into(&d, &sf->pending_dmg[k]);
            }
        }
        if (sf->prev_dmg_valid) {
            rect_union_into(&d, &sf->prev_dmg);
        }
        struct mc_rect_i sr = {0, 0, (int16_t)sf->w, (int16_t)sf->h};
        if (rects_intersect_into(&d, &sr, &this_dmg[i])) {
            this_dmg_valid[i] = 1;
        }
    }

    /* Track each surface's painted-this-frame screen rect to enable
     * popup-skip when its area is fully covered by the underlying
     * fullscreen's own paint. */
    struct mc_rect_i painted_screen[MC_MAX_SURFACES];
    int              painted_screen_valid[MC_MAX_SURFACES];
    memset(painted_screen_valid, 0, sizeof(painted_screen_valid));

    long painted_px = 0;
    for (int i = start; i < n_vis; i++) {
        if (!this_dmg_valid[i]) continue;
        struct mc_surface *sf = list[i];
        int idx = (sf->pending_idx >= 0) ? sf->pending_idx : sf->cur_scanout;
        if (idx < 0) continue;

        struct mc_rect_i scr = {
            .x = (int16_t)(this_dmg[i].x + sf->x),
            .y = (int16_t)(this_dmg[i].y + sf->y),
            .w = this_dmg[i].w, .h = this_dmg[i].h,
        };
        struct mc_rect_i clipped;
        if (!rects_intersect_into(&scr, &screen, &clipped)) continue;

        struct mc_accel_surface src = {
            .virt      = sf->bufs[idx].map,
            .dmabuf_fd = sf->bufs[idx].shm_fd,
            .is_dmabuf = mc_alloc_is_dmabuf(),
            .phys      = sf->bufs[idx].phys,
            .w = sf->w, .h = sf->h, .stride = (int)sf->stride,
            .format = MC_ACCEL_FMT_BGRA8888,
            .flip_y = sf->flip_y,
        };
        int src_x = clipped.x - sf->x;
        int src_y = clipped.y - sf->y;

        if (sf->role == 1 /* FULLSCREEN, opaque */) {
            do_blit(ops, &dst, clipped.x, clipped.y,
                    &src, src_x, src_y, clipped.w, clipped.h);
            painted_screen[i] = clipped;
            painted_screen_valid[i] = 1;
        } else {
            /* Popup needs fresh fullscreen pixels under it. Skip the
             * under-blit if the fullscreen we'd pull from JUST painted
             * (this frame) a region that fully covers this popup rect. */
            int painted_under = 0;
            for (int j = i - 1; j >= start; j--) {
                struct mc_surface *under = list[j];
                if (under->role != 1) continue;
                if (painted_screen_valid[j]) {
                    struct mc_rect_i ps = painted_screen[j];
                    if (ps.x <= clipped.x && ps.y <= clipped.y &&
                        ps.x + ps.w >= clipped.x + clipped.w &&
                        ps.y + ps.h >= clipped.y + clipped.h) {
                        painted_under = 1;
                        break;
                    }
                }
                int u_idx = under->pending_idx >= 0
                          ? under->pending_idx : under->cur_scanout;
                if (u_idx < 0) continue;
                struct mc_rect_i u_rect = {
                    .x = (int16_t)under->x, .y = (int16_t)under->y,
                    .w = (int16_t)under->w, .h = (int16_t)under->h,
                };
                struct mc_rect_i u_overlap;
                if (!rects_intersect_into(&clipped, &u_rect, &u_overlap))
                    continue;
                struct mc_accel_surface u_src = {
                    .virt      = under->bufs[u_idx].map,
                    .dmabuf_fd = under->bufs[u_idx].shm_fd,
                    .is_dmabuf = mc_alloc_is_dmabuf(),
                    .phys      = under->bufs[u_idx].phys,
                    .w = under->w, .h = under->h,
                    .stride = (int)under->stride,
                    .format = MC_ACCEL_FMT_BGRA8888,
                    .flip_y = under->flip_y,
                };
                do_blit(ops, &dst, u_overlap.x, u_overlap.y,
                        &u_src,
                        u_overlap.x - under->x, u_overlap.y - under->y,
                        u_overlap.w, u_overlap.h);
                painted_px += (long)u_overlap.w * u_overlap.h;
                painted_under = 1;
                break;
            }
            if (!painted_under) {
                do_fill(ops, &dst, clipped.x, clipped.y,
                        clipped.w, clipped.h, 0);
            }
            do_blend(ops, &dst, clipped.x, clipped.y,
                     &src, src_x, src_y, clipped.w, clipped.h);
            painted_screen[i] = clipped;
            painted_screen_valid[i] = 1;
        }
        painted_px += (long)clipped.w * clipped.h;
    }

    /* Make sure HW is done writing fb before we page-flip. */
    if (ops->sync) (void)ops->sync();

    if (trace_compose) {
        clock_gettime(CLOCK_MONOTONIC, &compose_t1);
        long us = (long)((compose_t1.tv_sec - compose_t0.tv_sec) * 1000000
                       + (compose_t1.tv_nsec - compose_t0.tv_nsec) / 1000);
        LOG_I("compose: n_vis=%d start=%d painted=%ldpx took=%ldus",
              n_vis, start, painted_px, us);
    }

    /* 4. Present. */
    be->present(be);

    /* Remember this frame's per-surface dmg bbox for next compose. */
    for (int i = 0; i < n_vis; i++) {
        struct mc_surface *sf = list[i];
        if (this_dmg_valid[i]) {
            sf->prev_dmg = this_dmg[i];
            sf->prev_dmg_valid = 1;
        } else {
            sf->prev_dmg_valid = 0;
        }
    }

    /* 6. Rotate scanout. */
    for (int i = 0; i < MC_MAX_SURFACES; i++) {
        struct mc_surface *sf = &s->surfaces[i];
        if (sf->sid == 0) continue;
        if (sf->pending_idx < 0) continue;

        int prev = sf->cur_scanout;
        int newi = sf->pending_idx;
        sf->cur_scanout = newi;
        sf->pending_idx = -1;
        sf->pending_dmg_n = 0;
        sf->bufs[newi].state = MC_BUF_SCANOUT;

        if (prev >= 0 && prev != newi) {
            sf->bufs[prev].state = MC_BUF_FREE;
            uint64_t one = 1;
            if (sf->event_fd > 0) {
                ssize_t wr = write(sf->event_fd, &one, sizeof(one));
                (void)wr;
            }
            LOG_D("rotate sid=%u prev=%d -> newi=%d, buf%d FREE",
                  sf->sid, prev, newi, prev);
        }
    }

    static unsigned frame_seq = 0;
    if ((g_log_level >= 3) && (++frame_seq % 60 == 0)) {
        long total = (long)s->screen_w * s->screen_h;
        LOG_D("frame#%u accel=%s painted=%ld/%ld (%ld%%)",
              frame_seq, ops->name,
              painted_px, total, total ? painted_px * 100 / total : 0);
    }
}

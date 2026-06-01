/*
 * G2D compositor backend for Allwinner T113 (sun8iw20).
 *
 * T113's G2D is the *RCQ* driver and exposes ONLY the enhanced ("_H")
 * ABI (g2d_image_enh + G2D_CMD_BITBLT_H/FILLRECT_H/BLD_H); there is no
 * 1.0 BITBLT(0x50)/FILLRECT(0x51). It also has a G2D IOMMU, so buffers
 * are handed over as dma-buf fds (image.fd, use_phy_addr=0) -- no
 * physical address required. See compositor/g2d_uapi.h for the full
 * story (this replaced an earlier, wrong, 1.0/phys implementation that
 * was transcribed from the T5 PDF).
 *
 * Display path (B1): /dev/fb0 on T113 exposes no smem_start, so G2D
 * cannot target it directly. We allocate our own dma-buf back-buffer
 * (via mc_alloc -> ion/dma-heap), let G2D composite every surface into
 * it, then DMA-BUF-sync + memcpy that buffer into the fb on present and
 * page-flip. The expensive per-surface blit/blend runs on G2D; only one
 * linear copy per frame stays on the CPU.
 *
 *   begin_frame  -> G2D_CMD_FILLRECT_H clears the dma-buf back-buffer.
 *   draw_surface -> G2D_CMD_BITBLT_H copies one client dma-buf into it.
 *   present      -> dma-buf sync + memcpy back-buffer -> fb, FBIOPAN.
 *
 * If G2D or a real dma-buf allocator is unavailable, `broken` is set and
 * everything falls back to plain CPU memcpy into the fb back-buffer, so
 * the stack still renders (just without HW accel).
 */
#define _GNU_SOURCE
#include "backend.h"
#include "surface.h"
#include "log.h"
#include "mc_alloc.h"
#include "g2d_uapi.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

/* dma-buf CPU-access sync (cache maintenance). Define locally so we don't
 * depend on <linux/dma-buf.h> being in the sysroot. */
#ifndef DMA_BUF_IOCTL_SYNC
struct dma_buf_sync { uint64_t flags; };
#define DMA_BUF_SYNC_READ  (1 << 0)
#define DMA_BUF_SYNC_WRITE (2 << 0)
#define DMA_BUF_SYNC_START (0 << 2)
#define DMA_BUF_SYNC_END   (1 << 2)
#define DMA_BUF_IOCTL_SYNC _IOW('b', 0, struct dma_buf_sync)
#endif

struct g2d_priv {
    /* fb (final scanout target). */
    int       fb_fd;
    uint8_t  *fb_map;
    size_t    fb_map_size;
    int       w, h;
    int       stride;
    int       double_buf;
    int       back_idx;
    uint8_t  *fb_buf[2];

    /* g2d. */
    int       g2d_fd;
    int       broken;          /* 1: CPU fallback only */

    /* dma-buf compose target (G2D writes here, we memcpy to fb). */
    struct mc_alloc_buf back;  /* .fd dma-buf, .map CPU view, .size */
};

/* ------------------------------------------------------------------ */
/* fb plumbing                                                        */
/* ------------------------------------------------------------------ */

static int g2d_open_fb(struct g2d_priv *p, const char *fbdev)
{
    p->fb_fd = open(fbdev, O_RDWR | O_CLOEXEC);
    if (p->fb_fd < 0) {
        LOG_E("backend_g2d: open %s: %s", fbdev, strerror(errno));
        return -errno;
    }
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    if (ioctl(p->fb_fd, FBIOGET_VSCREENINFO, &vinfo) < 0 ||
        ioctl(p->fb_fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
        LOG_E("backend_g2d: fb ioctl getinfo: %s", strerror(errno));
        return -errno;
    }
    if (vinfo.bits_per_pixel != 32) {
        LOG_E("backend_g2d: fb is %dbpp (need 32)", vinfo.bits_per_pixel);
        return -ENOTSUP;
    }
    p->w           = vinfo.xres;
    p->h           = vinfo.yres;
    p->stride      = finfo.line_length;
    p->double_buf  = (vinfo.yres_virtual >= 2 * vinfo.yres) ? 1 : 0;
    p->fb_map_size = (size_t)p->stride * (p->double_buf ? 2 : 1) * p->h;
    p->fb_map = mmap(NULL, p->fb_map_size, PROT_READ | PROT_WRITE,
                     MAP_SHARED, p->fb_fd, 0);
    if (p->fb_map == MAP_FAILED) {
        LOG_E("backend_g2d: fb mmap: %s", strerror(errno));
        return -errno;
    }
    p->fb_buf[0] = p->fb_map;
    p->fb_buf[1] = p->fb_map + (p->double_buf ? (size_t)p->stride * p->h : 0);
    p->back_idx  = p->double_buf ? 1 : 0;
    LOG_I("backend_g2d: fb %s %dx%d stride=%d double=%d",
          fbdev, p->w, p->h, p->stride, p->double_buf);
    return 0;
}

/* ------------------------------------------------------------------ */
/* G2D helpers                                                        */
/* ------------------------------------------------------------------ */

static void mark_broken(struct g2d_priv *p, const char *why)
{
    if (!p->broken) {
        LOG_W("backend_g2d: disabling G2D (%s) -- compose falls back to "
              "CPU memcpy per surface.", why);
        p->broken = 1;
    }
}

/* Fill a g2d_image_enh for an mc surface buffer (dma-buf fd path). */
static void fill_img_surface(g2d_image_enh *img, const struct mc_surface *sf,
                             const struct mc_buf *b)
{
    memset(img, 0, sizeof(*img));
    img->fd           = b->shm_fd;
    img->use_phy_addr = 0;
    img->format       = G2D_FORMAT_BGRA8888;
    img->width        = sf->w;
    img->height       = sf->h;
    img->clip_rect.x  = 0;
    img->clip_rect.y  = 0;
    img->clip_rect.w  = sf->w;
    img->clip_rect.h  = sf->h;
    img->alpha        = 0xff;
    img->mode         = G2D_PIXEL_ALPHA;   /* honour client per-pixel alpha */
}

/* Fill a g2d_image_enh for our dma-buf back-buffer, operating on `r`. */
static void fill_img_back(struct g2d_priv *p, g2d_image_enh *img,
                          int x, int y, int w, int h)
{
    memset(img, 0, sizeof(*img));
    img->fd           = p->back.fd;
    img->use_phy_addr = 0;
    img->format       = G2D_FORMAT_BGRA8888;
    img->width        = p->w;
    img->height       = p->h;
    img->clip_rect.x  = x;
    img->clip_rect.y  = y;
    img->clip_rect.w  = w;
    img->clip_rect.h  = h;
    img->alpha        = 0xff;
    img->mode         = G2D_GLOBAL_ALPHA;
}

static void dmabuf_sync(int fd, uint64_t flags)
{
    struct dma_buf_sync s = { .flags = flags };
    (void)ioctl(fd, DMA_BUF_IOCTL_SYNC, &s);   /* best effort */
}

/* ------------------------------------------------------------------ */
/* mc_backend ops                                                     */
/* ------------------------------------------------------------------ */

static int g2d_open(struct mc_backend *be, const char *arg,
                    int w_hint, int h_hint,
                    int *out_w, int *out_h, int *out_stride)
{
    (void)w_hint; (void)h_hint;
    if (!arg) arg = "/dev/fb0";

    struct g2d_priv *p = calloc(1, sizeof(*p));
    if (!p) return -ENOMEM;
    p->g2d_fd = -1;
    p->fb_fd  = -1;

    int rc = g2d_open_fb(p, arg);
    if (rc < 0) { free(p); return rc; }

    p->g2d_fd = open(MC_G2D_DEV_PATH, O_RDWR | O_CLOEXEC);
    if (p->g2d_fd < 0) {
        LOG_W("backend_g2d: %s not openable (%s) -- compose runs on CPU",
              MC_G2D_DEV_PATH, strerror(errno));
        p->broken = 1;
    }

    /* dma-buf back-buffer that G2D composites into. Same size/stride as
     * the fb so the present memcpy is a straight linear copy. */
    if (!p->broken) {
        if (mc_alloc_create(&p->back, (size_t)p->stride * p->h) < 0) {
            LOG_W("backend_g2d: back-buffer alloc failed -- CPU fallback");
            p->broken = 1;
        } else if (!mc_alloc_is_dmabuf() || p->back.map == NULL) {
            LOG_W("backend_g2d: allocator '%s' is not dma-buf backed -- "
                  "G2D can't target it; CPU fallback",
                  mc_alloc_backend_name());
            mc_alloc_destroy(&p->back);
            memset(&p->back, 0, sizeof(p->back));
            p->broken = 1;
        } else {
            LOG_I("backend_g2d: G2D enabled (_H ABI, dma-buf fd via IOMMU); "
                  "compose target fd=%d alloc=%s",
                  p->back.fd, mc_alloc_backend_name());
        }
    }

    *out_w      = p->w;
    *out_h      = p->h;
    *out_stride = p->stride;
    be->priv = p;
    return 0;
}

static uint8_t *g2d_get_buffer(struct mc_backend *be)
{
    struct g2d_priv *p = be->priv;
    if (!p) return NULL;
    /* CPU fallback composes straight into the fb back-buffer. */
    return p->broken ? p->fb_buf[p->back_idx] : (uint8_t *)p->back.map;
}

static uint32_t g2d_get_buffer_phys(struct mc_backend *be)
{
    struct g2d_priv *p = be->priv;
    return (p && !p->broken) ? p->back.phys : 0;
}

static int g2d_present(struct mc_backend *be)
{
    struct g2d_priv *p = be->priv;
    if (!p) return 0;

    /* HW path: pull G2D's output out of the dma-buf and into the fb. */
    if (!p->broken) {
        dmabuf_sync(p->back.fd, DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ);
        memcpy(p->fb_buf[p->back_idx], p->back.map, (size_t)p->stride * p->h);
        dmabuf_sync(p->back.fd, DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ);
    }

    if (!p->double_buf) return 0;
    struct fb_var_screeninfo vinfo;
    if (ioctl(p->fb_fd, FBIOGET_VSCREENINFO, &vinfo) < 0) return -errno;
    vinfo.yoffset = (p->back_idx == 1) ? p->h : 0;
    if (ioctl(p->fb_fd, FBIOPAN_DISPLAY, &vinfo) < 0)
        LOG_W("backend_g2d: FBIOPAN_DISPLAY: %s", strerror(errno));
    p->back_idx ^= 1;
    return 0;
}

static void g2d_close(struct mc_backend *be)
{
    struct g2d_priv *p = be->priv;
    if (!p) return;
    if (p->back.fd > 0) mc_alloc_destroy(&p->back);
    if (p->fb_map && p->fb_map != MAP_FAILED) munmap(p->fb_map, p->fb_map_size);
    if (p->fb_fd  >= 0) close(p->fb_fd);
    if (p->g2d_fd >= 0) close(p->g2d_fd);
    free(p);
    be->priv = NULL;
}

/* ------------------------------------------------------------------ */
/* hw_compose ops                                                     */
/* ------------------------------------------------------------------ */

static void g2d_begin_frame(struct mc_backend *be)
{
    struct g2d_priv *p = be->priv;
    if (!p) return;

    if (p->broken) {
        uint8_t *back = p->fb_buf[p->back_idx];
        if (back) memset(back, 0, (size_t)p->stride * p->h);
        return;
    }

    g2d_fillrect_h fr;
    memset(&fr, 0, sizeof(fr));
    fill_img_back(p, &fr.dst_image_h, 0, 0, p->w, p->h);
    fr.dst_image_h.color = 0xff000000u;   /* opaque black */

    if (ioctl(p->g2d_fd, G2D_CMD_FILLRECT_H, &fr) < 0) {
        LOG_W("backend_g2d: FILLRECT_H failed: %s", strerror(errno));
        mark_broken(p, "FILLRECT_H ioctl failed");
        uint8_t *back = p->fb_buf[p->back_idx];
        if (back) memset(back, 0, (size_t)p->stride * p->h);
    }
}

/* CPU per-surface blit fallback. dst_base is the active compose target. */
static void draw_surface_cpu(struct g2d_priv *p, uint8_t *dst_base,
                             struct mc_surface *sf, const struct mc_buf *b)
{
    if (!b->map || !dst_base) return;
    int sx = 0, sy = 0, dx = sf->x, dy = sf->y, w = sf->w, h = sf->h;
    if (dx < 0)        { sx -= dx; w += dx; dx = 0; }
    if (dy < 0)        { sy -= dy; h += dy; dy = 0; }
    if (dx + w > p->w) w = p->w - dx;
    if (dy + h > p->h) h = p->h - dy;
    if (w <= 0 || h <= 0) return;

    const uint8_t *src = b->map + (size_t)sy * sf->stride + (size_t)sx * 4;
    uint8_t       *dst = dst_base + (size_t)dy * p->stride + (size_t)dx * 4;
    for (int row = 0; row < h; row++) {
        memcpy(dst, src, (size_t)w * 4);
        src += sf->stride;
        dst += p->stride;
    }
}

static void g2d_draw_surface(struct mc_backend *be, struct mc_surface *sf)
{
    struct g2d_priv *p = be->priv;
    if (!p || !sf) return;

    int idx = (sf->pending_idx >= 0) ? sf->pending_idx : sf->cur_scanout;
    if (idx < 0) return;
    const struct mc_buf *b = &sf->bufs[idx];

    if (p->broken) {
        draw_surface_cpu(p, p->fb_buf[p->back_idx], sf, b);
        return;
    }

    /* G2D path: BITBLT_H from the client dma-buf into our back-buffer. */
    if (b->shm_fd >= 0) {
        g2d_blt_h blt;
        memset(&blt, 0, sizeof(blt));
        blt.flag_h = G2D_BLT_NONE_H;               /* mixer blit */
        fill_img_surface(&blt.src_image_h, sf, b);
        fill_img_back(p, &blt.dst_image_h, sf->x, sf->y, sf->w, sf->h);

        if (ioctl(p->g2d_fd, G2D_CMD_BITBLT_H, &blt) == 0)
            return;
        LOG_W("backend_g2d: BITBLT_H failed: %s (sid=%u %dx%d@%d,%d)",
              strerror(errno), sf->sid, sf->w, sf->h, sf->x, sf->y);
        mark_broken(p, "BITBLT_H ioctl failed");
    }

    /* Fell through: CPU blit straight into the fb (we've given up on G2D). */
    draw_surface_cpu(p, p->fb_buf[p->back_idx], sf, b);
}

static void g2d_end_frame(struct mc_backend *be)
{
    /* BITBLT_H/FILLRECT_H run synchronously in the RCQ driver (it waits
     * for cmd completion internally), so the back-buffer is ready by the
     * time present() reads it. No explicit sync ioctl needed. */
    (void)be;
}

static const struct mc_backend_hw_compose_ops g2d_hw_compose_ops = {
    .begin_frame  = g2d_begin_frame,
    .draw_surface = g2d_draw_surface,
    .end_frame    = g2d_end_frame,
};

struct mc_backend backend_g2d = {
    .name            = "g2d",
    .open            = g2d_open,
    .get_buffer      = g2d_get_buffer,
    .get_buffer_phys = g2d_get_buffer_phys,
    .present         = g2d_present,
    .close           = g2d_close,
    .hw_compose      = &g2d_hw_compose_ops,
};

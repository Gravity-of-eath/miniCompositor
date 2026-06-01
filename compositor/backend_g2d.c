/*
 * G2D compositor backend (Allwinner sun8iw / T113 primarily; will also
 * run on T507 if you prefer G2D over Mali EGL for some reason).
 *
 * Same model as backend_egl, but using the /dev/g2d 2D engine instead
 * of Mali. compose.c walks visible surfaces bottom-up and calls our
 * hw_compose ops; each draw_surface() is one G2D BITBLT. begin_frame
 * clears the back buffer via G2D FILLRECT. present() page-flips the
 * fb the same way backend_fb does.
 *
 * G2D 1.0 vs 2.0 ABI:
 *   - T113 (sun8iw20) is NOT in the G2D_V2X_SUPPORT list (verified in
 *     deps_source/T113/sunxi_g2d-main/g2d_driver_i.h), so it only
 *     accepts the 1.0 ioctls: G2D_CMD_BITBLT (0x50) / G2D_CMD_FILLRECT
 *     (0x51). The 2.0 _H series (0x55+) returns -EINVAL.
 *   - This backend currently targets 1.0 only. T507 builds normally
 *     use backend_egl; if you ever want backend_g2d on T507 too, the
 *     ioctl numbers happen to coexist in the same driver so adding a
 *     2.0 code path is a follow-up, not a rewrite.
 *
 * Surface buffer requirements:
 *   - G2D needs either a physical DMA address or a dma-buf fd for both
 *     src and dst. The dst here is /dev/fb0's smem_start (always phys,
 *     read out of FBIOGET_FSCREENINFO).
 *   - For src, mc surfaces today are usually memfd-backed (no phys, no
 *     dma-buf). When that's the case G2D will reject the blit; we
 *     mark_broken() and fall back to CPU memcpy per surface so things
 *     keep working visually while you migrate clients to dma-heap /
 *     ion allocations. mc_alloc already supports those via MC_ALLOC
 *     env, this just isn't the default yet.
 */
#define _GNU_SOURCE
#include "backend.h"
#include "surface.h"
#include "log.h"
#include "g2d_uapi.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/utsname.h>
#include <unistd.h>

struct g2d_priv {
    /* fb side -- mirrors backend_fb. */
    int       fb_fd;
    uint8_t  *fb_map;
    size_t    fb_map_size;
    int       w, h;
    int       stride;
    int       double_buf;
    int       back_idx;
    uint8_t  *fb_buf[2];
    uint32_t  fb_phys[2];

    /* g2d side. */
    int       g2d_fd;          /* /dev/g2d, -1 if not opened. */
    int       broken;          /* 1: G2D ioctl rejected; CPU fallback only */
    int       kernel_510_plus; /* 1: addr[0] interpreted as dma-buf fd */
};

/* ------------------------------------------------------------------ */
/* Kernel-version probe (decides addr[0] meaning, see g2d_uapi.h).    */
/* ------------------------------------------------------------------ */

static int detect_kernel_510_plus(void)
{
    struct utsname u;
    if (uname(&u) < 0) return 0;
    int maj = 0, min = 0;
    if (sscanf(u.release, "%d.%d", &maj, &min) != 2) return 0;
    return (maj > 5) || (maj == 5 && min >= 10);
}

/* ------------------------------------------------------------------ */
/* fb plumbing (lifted from backend_fb).                              */
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
    p->w          = vinfo.xres;
    p->h          = vinfo.yres;
    p->stride     = finfo.line_length;
    p->double_buf = (vinfo.yres_virtual >= 2 * vinfo.yres) ? 1 : 0;
    p->fb_map_size = p->stride * (p->double_buf ? 2 : 1) * p->h;
    p->fb_map = mmap(NULL, p->fb_map_size, PROT_READ | PROT_WRITE,
                     MAP_SHARED, p->fb_fd, 0);
    if (p->fb_map == MAP_FAILED) {
        LOG_E("backend_g2d: fb mmap: %s", strerror(errno));
        return -errno;
    }
    p->fb_buf[0] = p->fb_map;
    p->fb_buf[1] = p->fb_map + (p->double_buf ? p->stride * p->h : 0);
    p->back_idx  = p->double_buf ? 1 : 0;
    if (finfo.smem_start) {
        p->fb_phys[0] = (uint32_t)finfo.smem_start;
        p->fb_phys[1] = (uint32_t)finfo.smem_start
                      + (p->double_buf ? p->stride * p->h : 0);
    }
    LOG_I("backend_g2d: fb %s %dx%d stride=%d double=%d phys=0x%x/0x%x",
          fbdev, p->w, p->h, p->stride, p->double_buf,
          p->fb_phys[0], p->fb_phys[1]);
    return 0;
}

/* ------------------------------------------------------------------ */
/* G2D helpers                                                        */
/* ------------------------------------------------------------------ */

static void mark_broken(struct g2d_priv *p, const char *why)
{
    if (!p->broken) {
        LOG_W("backend_g2d: disabling G2D (%s) -- compose falls back to "
              "CPU memcpy per surface. Most likely cause: surfaces are "
              "memfd_create-backed; switch the compositor to MC_ALLOC=ion "
              "or MC_ALLOC=dma-heap so G2D can see them.", why);
        p->broken = 1;
    }
}

/* Resolve a surface buffer's G2D addr[0] value.
 * Returns 0 on success, -1 if neither phys nor dma-buf fd is available
 * (caller falls back to CPU). */
static int surface_addr(const struct g2d_priv *p, const struct mc_buf *b,
                        uint32_t *out_addr)
{
    if (p->kernel_510_plus) {
        if (b->shm_fd >= 0) { *out_addr = (uint32_t)b->shm_fd; return 0; }
        if (b->phys != 0)    { *out_addr = b->phys;             return 0; }
    } else {
        if (b->phys != 0)    { *out_addr = b->phys;             return 0; }
    }
    return -1;
}

static void fill_g2d_image_from_surface(g2d_image *img,
                                        const struct mc_surface *sf,
                                        uint32_t addr0)
{
    memset(img, 0, sizeof(*img));
    img->addr[0]   = addr0;
    img->w         = sf->w;
    img->h         = sf->h;
    img->format    = G2D_FMT_BGRA_VUYA8888;   /* mc surfaces are BGRA8888 */
    img->pixel_seq = G2D_SEQ_NORMAL;
}

static void fill_g2d_image_fb(g2d_image *img, const struct g2d_priv *p)
{
    memset(img, 0, sizeof(*img));
    img->addr[0]   = p->fb_phys[p->back_idx];   /* phys regardless of kernel */
    img->w         = p->w;
    img->h         = p->h;
    img->format    = G2D_FMT_BGRA_VUYA8888;
    img->pixel_seq = G2D_SEQ_NORMAL;
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

    if (!p->fb_phys[0]) {
        LOG_W("backend_g2d: fb has no smem_start; G2D needs a phys dst "
              "address. Falling back to CPU compose only.");
        p->broken = 1;
    }

    p->g2d_fd = open(MC_G2D_DEV_PATH, O_RDWR | O_CLOEXEC);
    if (p->g2d_fd < 0) {
        LOG_W("backend_g2d: %s not openable (%s) -- compose runs on CPU",
              MC_G2D_DEV_PATH, strerror(errno));
        p->broken = 1;
    } else {
        LOG_I("backend_g2d: opened %s (1.0 ABI: BITBLT=0x%02x FILLRECT=0x%02x)",
              MC_G2D_DEV_PATH, G2D_CMD_BITBLT, G2D_CMD_FILLRECT);
    }

    p->kernel_510_plus = detect_kernel_510_plus();
    LOG_I("backend_g2d: kernel addr[0] mode: %s",
          p->kernel_510_plus ? "dma-buf fd (5.10+)" : "phys addr (<= 5.4)");

    *out_w      = p->w;
    *out_h      = p->h;
    *out_stride = p->stride;
    be->priv = p;
    return 0;
}

static uint8_t *g2d_get_buffer(struct mc_backend *be)
{
    /* hw_compose path doesn't call this, but expose the CPU mapping so
     * the per-surface CPU fallback in draw_surface() can write to it. */
    struct g2d_priv *p = be->priv;
    return p ? p->fb_buf[p->back_idx] : NULL;
}

static uint32_t g2d_get_buffer_phys(struct mc_backend *be)
{
    struct g2d_priv *p = be->priv;
    return p ? p->fb_phys[p->back_idx] : 0;
}

static int g2d_present(struct mc_backend *be)
{
    struct g2d_priv *p = be->priv;
    if (!p || !p->double_buf) return 0;
    struct fb_var_screeninfo vinfo;
    if (ioctl(p->fb_fd, FBIOGET_VSCREENINFO, &vinfo) < 0) return -errno;
    vinfo.yoffset = (p->back_idx == 1) ? p->h : 0;
    if (ioctl(p->fb_fd, FBIOPAN_DISPLAY, &vinfo) < 0) {
        LOG_W("backend_g2d: FBIOPAN_DISPLAY: %s", strerror(errno));
    }
    p->back_idx ^= 1;
    return 0;
}

static void g2d_close(struct mc_backend *be)
{
    struct g2d_priv *p = be->priv;
    if (!p) return;
    if (p->fb_map && p->fb_map != MAP_FAILED) munmap(p->fb_map, p->fb_map_size);
    if (p->fb_fd  >= 0) close(p->fb_fd);
    if (p->g2d_fd >= 0) close(p->g2d_fd);
    free(p);
    be->priv = NULL;
}

/* ------------------------------------------------------------------ */
/* hw_compose ops                                                     */
/* ------------------------------------------------------------------ */

static void g2d_clear_cpu(struct g2d_priv *p)
{
    uint8_t *back = p->fb_buf[p->back_idx];
    if (back) memset(back, 0, p->stride * p->h);
}

static void g2d_begin_frame(struct mc_backend *be)
{
    struct g2d_priv *p = be->priv;
    if (!p) return;

    if (p->broken || p->g2d_fd < 0) {
        g2d_clear_cpu(p);
        return;
    }

    /* G2D FILLRECT: clear the whole fb back-buffer to opaque black. */
    g2d_fillrect fr;
    memset(&fr, 0, sizeof(fr));
    fr.flag        = G2D_FIL_NONE;        /* solid fill, no alpha mode */
    fr.color       = 0xff000000u;         /* ARGB: opaque black        */
    fr.alpha       = 0xff;
    fr.dst_rect.x  = 0;
    fr.dst_rect.y  = 0;
    fr.dst_rect.w  = p->w;
    fr.dst_rect.h  = p->h;
    fill_g2d_image_fb(&fr.dst_image, p);

    if (ioctl(p->g2d_fd, G2D_CMD_FILLRECT, &fr) < 0) {
        LOG_W("backend_g2d: FILLRECT failed: %s", strerror(errno));
        mark_broken(p, "FILLRECT ioctl failed");
        g2d_clear_cpu(p);
    }
}

/* CPU per-surface blit fallback. Mirrors the simplest path in
 * accel_cpu.c so something is on screen even when G2D can't see the
 * client buffer. */
static void draw_surface_cpu(struct g2d_priv *p, struct mc_surface *sf,
                             const struct mc_buf *b)
{
    if (!b->map) return;
    uint8_t *dst_base = p->fb_buf[p->back_idx];
    if (!dst_base) return;

    int sx = 0, sy = 0;
    int dx = sf->x, dy = sf->y;
    int w  = sf->w, h = sf->h;
    /* clip to screen */
    if (dx < 0)              { sx -= dx; w += dx; dx = 0; }
    if (dy < 0)              { sy -= dy; h += dy; dy = 0; }
    if (dx + w > p->w)         w = p->w - dx;
    if (dy + h > p->h)         h = p->h - dy;
    if (w <= 0 || h <= 0)    return;

    const uint8_t *src = b->map + sy * sf->stride + sx * 4;
    uint8_t       *dst = dst_base + dy * p->stride + dx * 4;
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

    /* G2D path. Requires a HW-visible src buffer (phys or dma-buf fd). */
    if (!p->broken && p->g2d_fd >= 0) {
        uint32_t src_addr;
        if (surface_addr(p, b, &src_addr) == 0) {
            g2d_blt blt;
            memset(&blt, 0, sizeof(blt));
            /* Role 1 == FULLSCREEN: assume opaque, plain copy.
             * Role 2 == POPUP (and bg): blend per-pixel alpha. */
            int opaque = (sf->role == 1 /* FULLSCREEN */);
            blt.flag  = opaque ? G2D_BLT_NONE : G2D_BLT_PIXEL_ALPHA;
            blt.alpha = 0xff;

            fill_g2d_image_from_surface(&blt.src_image, sf, src_addr);
            blt.src_rect.x = 0;
            blt.src_rect.y = 0;
            blt.src_rect.w = sf->w;
            blt.src_rect.h = sf->h;

            fill_g2d_image_fb(&blt.dst_image, p);
            blt.dst_x = sf->x;
            blt.dst_y = sf->y;

            if (ioctl(p->g2d_fd, G2D_CMD_BITBLT, &blt) == 0) {
                return;   /* HW path succeeded */
            }
            LOG_W("backend_g2d: BITBLT failed: %s (sid=%u %dx%d@%d,%d)",
                  strerror(errno), sf->sid, sf->w, sf->h, sf->x, sf->y);
            mark_broken(p, "BITBLT ioctl failed");
        }
    }

    /* CPU fallback. */
    draw_surface_cpu(p, sf, b);
}

static void g2d_end_frame(struct mc_backend *be)
{
    /* G2D 1.0 BITBLT / FILLRECT are synchronous: the driver waits
     * internally (see g2d_wait_cmd_finish() in the BSP). No per-frame
     * sync ioctl needed. present() page-flips. */
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

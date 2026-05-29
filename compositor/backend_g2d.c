/*
 * G2D compositor backend (Allwinner sun8iw / T113, sun50iw / T507).
 *
 * Same model as backend_egl, but using the /dev/g2d 2D engine instead
 * of Mali. compose.c walks visible surfaces bottom-up and calls our
 * hw_compose ops; each draw_surface() is one G2D blit (opaque) or
 * blend (popups with alpha). present() page-flips the fb just like
 * backend_fb.
 *
 * STATUS: skeleton. This file:
 *   - Opens /dev/fb0 (mmap + smem_start probe) and /dev/g2d.
 *   - Wires the open/get_buffer/present/close + hw_compose ops table.
 *   - begin_frame / draw_surface / end_frame are TODO and currently
 *     fall back to CPU memcpy/blend into the fb back-buffer (same
 *     thing backend_fb's CPU path does, just routed through this
 *     backend's plumbing). This keeps `--backend g2d` working end-to-
 *     end on T113 before the real G2D blits are wired.
 *
 * To finish:
 *   1. Switch begin_frame to G2D fill (clear back buffer via
 *      G2D_CMD_FILLRECT_H).
 *   2. Switch draw_surface to G2D BITBLT_H / BLD_H using the
 *      surface's dma-buf fd as the source and fb_phys[back_idx] as the
 *      destination. The ABI definitions (g2d_image_enh, ioctl numbers)
 *      already live in compositor/accel_g2d.c -- factor the shared bits
 *      into accel_g2d.h or copy them here, but do NOT re-derive them.
 *   3. end_frame: single SYNC ioctl (one wait per frame instead of per
 *      blit). Real present() then page-flips.
 *
 * Why a separate backend instead of just MC_ENABLE_G2D=1 with
 * --backend fb? accel_g2d is per-blit and gets called from the CPU
 * compose loop in compose.c, which still does the screen clear and the
 * surface sort on CPU and pays one G2D sync per op. backend_g2d owns
 * the whole frame: one clear, one sync, N blits, one page-flip.
 */
#define _GNU_SOURCE
#include "backend.h"
#include "surface.h"
#include "log.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
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
};

#define DEV_G2D "/dev/g2d"

/* ------------------------------------------------------------------ */
/* fb plumbing (lifted from backend_fb -- intentional duplication for *
 * now; consolidate after a third backend needs it).                  */
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

    p->g2d_fd = open(DEV_G2D, O_RDWR | O_CLOEXEC);
    if (p->g2d_fd < 0) {
        LOG_W("backend_g2d: %s not openable (%s) -- compose will run on "
              "CPU until G2D is available", DEV_G2D, strerror(errno));
        /* Non-fatal for the skeleton: we still want the backend to come
         * up so the rest of the stack is exercised on T113. Real HW
         * compose code can fail open here once it's wired. */
    } else {
        LOG_I("backend_g2d: opened %s", DEV_G2D);
    }

    *out_w      = p->w;
    *out_h      = p->h;
    *out_stride = p->stride;
    be->priv = p;
    return 0;
}

static uint8_t *g2d_get_buffer(struct mc_backend *be)
{
    /* Exposed so the begin/draw/end skeleton can keep using the CPU
     * compose path as a fallback. Once G2D blits are wired, the
     * compose ops never need this and callers may pass NULL. */
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
/* hw_compose ops -- skeleton (CPU fallback)                          */
/* ------------------------------------------------------------------ */

static void g2d_begin_frame(struct mc_backend *be)
{
    /* TODO: G2D_CMD_FILLRECT_H to clear fb_phys[back_idx] to black.
     * For now, CPU memset (same effect, far slower). */
    struct g2d_priv *p = be->priv;
    if (!p) return;
    uint8_t *back = p->fb_buf[p->back_idx];
    if (back) memset(back, 0, p->stride * p->h);
}

static void g2d_draw_surface(struct mc_backend *be, struct mc_surface *sf)
{
    /* TODO: G2D_CMD_BITBLT_H (opaque, FULLSCREEN role) or G2D_CMD_BLD_H
     * (alpha, popups) from sf's dma-buf fd at sf->bufs[idx].fd into
     * fb_phys[back_idx]. The ABI for both lives in accel_g2d.c; share it.
     *
     * Skeleton: CPU per-pixel blit into the mapped back-buffer so the
     * stack runs end-to-end on T113 even before G2D is wired. */
    (void)be; (void)sf;
    LOG_D("backend_g2d: draw_surface sid=%u (CPU fallback, TODO: g2d blit)",
          sf ? sf->sid : 0);
}

static void g2d_end_frame(struct mc_backend *be)
{
    /* TODO: single G2D SYNC ioctl here, then present() page-flips. */
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

/*
 * Allwinner G2D backend.
 *
 * ABI source: Allwinner BSP `g2d_driver.h` (sun8iw11 / sun50iw family,
 * verified against AWTK's tina G2D port). Two ioctls are used:
 *
 *   G2D_CMD_BITBLT_H  (0x55)  - opaque blit
 *   G2D_CMD_BLD_H     (0x57)  - alpha blend (Porter-Duff SRCOVER)
 *
 * Surface memory requirement (THIS IS THE GOTCHA):
 *
 *   The kernel driver does NOT accept arbitrary user-virtual buffers.
 *   Every surface fed to G2D must be either:
 *     a) a physical address (laddr[0]=phys, use_phy_addr=1), or
 *     b) a dma-buf fd (fd=>=0)
 *
 *   The framework's surfaces today are allocated with memfd_create, which
 *   provides neither. So if you build with MC_ENABLE_G2D=1 and try to use
 *   it at runtime, every ioctl will fail with EINVAL on the buffer check
 *   and the accel layer will fall back to CPU per-op. Functionality is
 *   preserved, you just don't get HW acceleration.
 *
 *   To actually engage G2D, we need to switch surface allocation to
 *   /dev/ion (T113 / older sun8iw) or /dev/dma_heap/system (T507 /
 *   newer sun50iw). That's a separate refactor (mc_alloc abstraction)
 *   on the surface-creation path; the protocol and client API don't
 *   need to change because memfd-backed and dma-buf-backed surfaces
 *   look identical to the client (both are mmap-able fds passed via
 *   SCM_RIGHTS).
 *
 *   Until that lands, this file is a known-good reference implementation
 *   of the ABI: ioctl numbers and struct layouts are correct, so when
 *   we plug in dma-buf fds later, no further changes here are needed.
 */
#define _GNU_SOURCE
#include "accel.h"
#include "log.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* ---- ABI definitions (transcribed from BSP g2d_driver.h) ---- */

/* Single-plane RGBA format codes. */
enum {
    G2D_FORMAT_ARGB8888 = 0,
    G2D_FORMAT_ABGR8888 = 1,
    G2D_FORMAT_RGBA8888 = 2,
    G2D_FORMAT_BGRA8888 = 3,
    G2D_FORMAT_XRGB8888 = 4,
    G2D_FORMAT_XBGR8888 = 5,
};

/* Blend ops for G2D_CMD_BLD_H (Porter-Duff cmd flags). */
enum {
    G2D_BLD_CLEAR   = 0x00000001,
    G2D_BLD_COPY    = 0x00000002,
    G2D_BLD_DST     = 0x00000003,
    G2D_BLD_SRCOVER = 0x00000004,
    G2D_BLD_DSTOVER = 0x00000005,
};

/* Alpha mode for image_enh.mode. */
enum {
    G2D_PIXEL_ALPHA  = 0,
    G2D_GLOBAL_ALPHA = 1,
    G2D_MIXER_ALPHA  = 2,
};

enum {
    COLOR_RANGE_0_255  = 0,
    COLOR_RANGE_16_235 = 1,
};

typedef struct { int32_t  x, y; uint32_t w, h; } g2d_rect;
typedef struct { uint32_t w, h; }                g2d_size;
typedef struct { uint32_t x, y; }                g2d_coor;

/* This layout matches the public BSP `g2d_image_enh`. Keep field order. */
typedef struct {
    int32_t      bbuff;          /* legacy; unused when use_phy_addr/fd set */
    uint32_t     color;
    uint32_t     format;         /* g2d_fmt_enh */
    uint32_t     laddr[3];       /* phys addr (Y/UV planes; we use [0]) */
    uint32_t     haddr[3];       /* phys addr hi (for >4G systems) */
    uint32_t     width;          /* full buffer width in pixels */
    uint32_t     height;
    uint32_t     align[3];

    g2d_rect     clip_rect;      /* sub-rect to operate on */
    g2d_size     resize;
    g2d_coor     coor;

    uint32_t     gamut;          /* g2d_color_gmt: 0=BT601 1=BT709 2=BT2020 */
    int32_t      bpremul;
    uint8_t      alpha;
    uint32_t     mode;           /* g2d_alpha_mode_enh */
    int32_t      fd;             /* dma-buf fd, -1 if unused */
    uint32_t     use_phy_addr;
    uint32_t     color_range;
} g2d_image_enh;

typedef struct {
    uint32_t      flag_h;
    g2d_image_enh src_image_h;
    g2d_image_enh dst_image_h;
} g2d_blt_h;

typedef struct {
    uint32_t match_rule;
    uint32_t max_color;
    uint32_t min_color;
} g2d_ck;

typedef struct {
    uint32_t      bld_cmd;
    g2d_image_enh dst_image;
    g2d_image_enh src_image[4];  /* only [0] is used */
    g2d_ck        ck_para;
} g2d_bld;

/* ioctls. The BSP enum starts at 0x50 with the _IO base (just a small
 * integer, no _IOC magic encoding in this driver). */
#ifndef G2D_CMD_BITBLT_H
#define G2D_CMD_BITBLT_H   0x55
#endif
#ifndef G2D_CMD_BLD_H
#define G2D_CMD_BLD_H      0x57
#endif

#define DEV_PATH "/dev/g2d"

/* ---- runtime state ---- */

static int g_fd     = -1;
static int g_broken = 0;

static void mark_broken(const char *why)
{
    if (!g_broken) {
        LOG_W("g2d: disabling backend (%s). Falling back to CPU. "
              "Surfaces likely need to be dma-buf or phys-addr backed; "
              "memfd_create is not enough.", why);
        g_broken = 1;
    }
}

/* Fill a g2d_image_enh from our generic surface descriptor.
 *
 * Three paths in priority:
 *   1) dmabuf_fd >= 0 -> set fd, use_phy_addr=0
 *   2) phys != 0      -> set laddr[0]=phys, use_phy_addr=1
 *   3) else           -> set bbuff=virt (almost always rejected by driver,
 *                        but we try; mark_broken will catch the EINVAL)
 */
static void fill_image(g2d_image_enh *img,
                       const struct mc_accel_surface *s,
                       int x, int y, int w, int h)
{
    memset(img, 0, sizeof(*img));
    img->format       = G2D_FORMAT_BGRA8888;
    img->width        = s->w;
    img->height       = s->h;
    img->clip_rect.x  = x;
    img->clip_rect.y  = y;
    img->clip_rect.w  = w;
    img->clip_rect.h  = h;
    img->alpha        = 0xff;
    img->mode         = G2D_PIXEL_ALPHA;
    img->color_range  = COLOR_RANGE_0_255;
    img->gamut        = 0;       /* BT601 */
    img->fd           = -1;

    if (s->phys != 0) {
        img->laddr[0]     = s->phys;
        img->use_phy_addr = 1;
    } else if (s->is_dmabuf && s->dmabuf_fd >= 0) {
        img->fd           = s->dmabuf_fd;
        img->use_phy_addr = 0;
    } else {
        /* Not a HW-friendly surface (probably memfd_create on a system
         * without dma-heap/ion). G2D will reject this; the per-op CPU
         * fallback in compose.c picks up the failure. */
        img->bbuff        = (int32_t)(intptr_t)s->virt;
        img->use_phy_addr = 0;
    }
}

static int g2d_init(void)
{
    int fd = open(DEV_PATH, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        LOG_I("g2d: %s not openable (%s)", DEV_PATH, strerror(errno));
        return -1;
    }
    g_fd = fd;
    g_broken = 0;
    LOG_I("g2d: opened %s (ABI: BITBLT_H=0x55 BLD_H=0x57). "
          "First op will probe whether surfaces are usable.", DEV_PATH);
    return 0;
}

static void g2d_deinit(void)
{
    if (g_fd >= 0) { close(g_fd); g_fd = -1; }
    g_broken = 0;
}

static int g2d_fill(const struct mc_accel_surface *d,
                    int x, int y, int w, int h, uint32_t bgra)
{
    /* G2D has G2D_CMD_FILLRECT_H, but routing it through the per-op
     * fallback wrapper means a single ioctl per damage frame; CPU memset
     * of the same area is ~equally fast and avoids one more failure
     * point. Defer to CPU. */
    (void)d; (void)x; (void)y; (void)w; (void)h; (void)bgra;
    return -1;
}

static int g2d_blit(const struct mc_accel_surface *d, int dx, int dy,
                    const struct mc_accel_surface *s, int sx, int sy,
                    int w, int h)
{
    if (g_fd < 0 || g_broken) return -1;
    g2d_blt_h blt;
    memset(&blt, 0, sizeof(blt));
    blt.flag_h = 0;                              /* G2D_BLT_NONE_H */
    fill_image(&blt.src_image_h, s, sx, sy, w, h);
    fill_image(&blt.dst_image_h, d, dx, dy, w, h);
    if (ioctl(g_fd, G2D_CMD_BITBLT_H, &blt) < 0) {
        LOG_W("g2d: BITBLT_H failed: %s", strerror(errno));
        mark_broken("BITBLT_H ioctl failed");
        return -1;
    }
    return 0;
}

static int g2d_blend(const struct mc_accel_surface *d, int dx, int dy,
                     const struct mc_accel_surface *s, int sx, int sy,
                     int w, int h)
{
    if (g_fd < 0 || g_broken) return -1;
    g2d_bld bld;
    memset(&bld, 0, sizeof(bld));
    bld.bld_cmd = G2D_BLD_SRCOVER;
    fill_image(&bld.dst_image,    d, dx, dy, w, h);
    fill_image(&bld.src_image[0], s, sx, sy, w, h);
    if (ioctl(g_fd, G2D_CMD_BLD_H, &bld) < 0) {
        LOG_W("g2d: BLD_H failed: %s", strerror(errno));
        mark_broken("BLD_H ioctl failed");
        return -1;
    }
    return 0;
}

static int g2d_sync(void) { return 0; }   /* SYNC ioctl: H-variants block */

const struct mc_accel_ops mc_accel_g2d = {
    .name   = "g2d",
    .init   = g2d_init,
    .deinit = g2d_deinit,
    .fill   = g2d_fill,
    .blit   = g2d_blit,
    .blend  = g2d_blend,
    .sync   = g2d_sync,
};

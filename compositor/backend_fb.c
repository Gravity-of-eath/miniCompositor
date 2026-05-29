#define _GNU_SOURCE
#include "backend.h"
#include "log.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

struct fb_priv {
    int      fd;
    uint8_t *map;
    size_t   map_size;
    int      w, h;
    int      stride;
    int      bpp;          /* bits */
    int      double_buf;   /* yres_virtual == 2*yres ? */
    int      back_idx;     /* which half is the back buffer */
    uint8_t *fb_buf[2];    /* fb_buf[0] = first half, [1] = second */
    uint32_t fb_phys[2];   /* phys addr per half, 0 if unknown */
};

static int fb_open(struct mc_backend *be, const char *arg,
                   int w_hint, int h_hint,
                   int *out_w, int *out_h, int *out_stride)
{
    (void)w_hint; (void)h_hint;
    if (!arg) arg = "/dev/fb0";

    struct fb_priv *p = calloc(1, sizeof(*p));
    if (!p) return -ENOMEM;

    p->fd = open(arg, O_RDWR | O_CLOEXEC);
    if (p->fd < 0) {
        int e = -errno;
        LOG_E("backend_fb: open %s: %s", arg, strerror(errno));
        free(p);
        return e;
    }

    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    if (ioctl(p->fd, FBIOGET_VSCREENINFO, &vinfo) < 0 ||
        ioctl(p->fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
        LOG_E("backend_fb: ioctl getinfo failed: %s", strerror(errno));
        close(p->fd); free(p); return -errno;
    }

    p->w      = vinfo.xres;
    p->h      = vinfo.yres;
    p->bpp    = vinfo.bits_per_pixel;
    p->stride = finfo.line_length;
    p->double_buf = (vinfo.yres_virtual >= 2 * vinfo.yres) ? 1 : 0;

    if (p->bpp != 32) {
        LOG_E("backend_fb: only 32bpp supported, fb is %dbpp", p->bpp);
        close(p->fd); free(p); return -ENOTSUP;
    }

    p->map_size = p->stride * (p->double_buf ? 2 : 1) * p->h;
    p->map = mmap(NULL, p->map_size, PROT_READ | PROT_WRITE,
                  MAP_SHARED, p->fd, 0);
    if (p->map == MAP_FAILED) {
        LOG_E("backend_fb: mmap: %s", strerror(errno));
        close(p->fd); free(p); return -errno;
    }

    p->fb_buf[0] = p->map;
    p->fb_buf[1] = p->map + (p->double_buf ? p->stride * p->h : 0);
    p->back_idx  = p->double_buf ? 1 : 0;

    /* smem_start is the dma/phys address of the framebuffer as published
     * by the fb driver. The framebuffer device file represents memory in
     * physical (or carveout) DRAM, so this is exactly the address G2D
     * needs when writing into it. With double-buffering each half is at
     * smem_start + n * (stride * yres). */
    if (finfo.smem_start) {
        p->fb_phys[0] = (uint32_t)finfo.smem_start;
        p->fb_phys[1] = (uint32_t)finfo.smem_start
                      + (p->double_buf ? p->stride * p->h : 0);
        LOG_I("backend_fb: smem_start=0x%lx (phys halves: 0x%x / 0x%x)",
              (unsigned long)finfo.smem_start, p->fb_phys[0], p->fb_phys[1]);
    } else {
        p->fb_phys[0] = 0;
        p->fb_phys[1] = 0;
        LOG_I("backend_fb: smem_start unavailable; HW accel will not "
              "directly target fb (CPU memcpy on present)");
    }

    *out_w      = p->w;
    *out_h      = p->h;
    *out_stride = p->stride;

    be->priv = p;
    LOG_I("backend_fb: %s %dx%d stride=%d bpp=%d double=%d",
          arg, p->w, p->h, p->stride, p->bpp, p->double_buf);
    return 0;
}

static uint32_t fb_get_buffer_phys(struct mc_backend *be)
{
    struct fb_priv *p = be->priv;
    return p ? p->fb_phys[p->back_idx] : 0;
}

static uint8_t *fb_get_buffer(struct mc_backend *be)
{
    struct fb_priv *p = be->priv;
    return p->fb_buf[p->back_idx];
}

static int fb_present(struct mc_backend *be)
{
    struct fb_priv *p = be->priv;
    if (!p->double_buf) {
        /* Single buffer: nothing to do, writes are already visible. */
        return 0;
    }
    /* Page flip via pan display */
    struct fb_var_screeninfo vinfo;
    if (ioctl(p->fd, FBIOGET_VSCREENINFO, &vinfo) < 0) return -errno;
    vinfo.yoffset = (p->back_idx == 1) ? p->h : 0;
    if (ioctl(p->fd, FBIOPAN_DISPLAY, &vinfo) < 0) {
        LOG_W("backend_fb: FBIOPAN_DISPLAY: %s", strerror(errno));
    }
    /* FBIO_WAITFORVSYNC: measured on T507 BSP as 2-6us, i.e. the driver
     * returns success without actually waiting -- it's a stub. Calling it
     * is harmless (just an ioctl round-trip) but provides no real sync.
     *
     * Default: disabled. Set MC_FB_VSYNC=1 to force-enable on hardware
     * where the ioctl might actually work (e.g. T113/RV1126 BSPs).
     *
     * If you observe tearing later, the right fix is to switch to
     * FBIOPUT_VSCREENINFO with vinfo.activate |= FB_ACTIVATE_VBL, which
     * many BSPs implement properly even when FBIO_WAITFORVSYNC doesn't. */
    static int vsync_enabled = -1;
    if (vsync_enabled < 0) {
        const char *e = getenv("MC_FB_VSYNC");
        vsync_enabled = (e && *e && *e != '0') ? 1 : 0;
        LOG_I("backend_fb: FBIO_WAITFORVSYNC %s",
              vsync_enabled ? "enabled (MC_FB_VSYNC=1)" : "disabled (default)");
    }
    if (vsync_enabled) {
        int zero = 0;
        (void)ioctl(p->fd, FBIO_WAITFORVSYNC, &zero);
    }
    p->back_idx ^= 1;
    return 0;
}

static void fb_close(struct mc_backend *be)
{
    struct fb_priv *p = be->priv;
    if (!p) return;
    if (p->map && p->map != MAP_FAILED) munmap(p->map, p->map_size);
    if (p->fd > 0) close(p->fd);
    free(p);
    be->priv = NULL;
}

struct mc_backend backend_fb = {
    .name            = "fb",
    .open            = fb_open,
    .get_buffer      = fb_get_buffer,
    .get_buffer_phys = fb_get_buffer_phys,
    .present         = fb_present,
    .close           = fb_close,
};

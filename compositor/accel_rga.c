/*
 * Rockchip RGA backend.
 *
 * Targets RV1126 / RV1106 / RK3036 / RK3399 etc. via /dev/rga ioctl.
 *
 * Same first-use-probe + mark-broken policy as the G2D backend: ABI
 * variance is real (rga_req fields differ across kernel versions), so
 * we keep the layout inline and degrade gracefully to CPU on failure.
 *
 * Memory model:
 *   We submit user-virtual buffers via RGA_USE_VIRT_ADDR. Most Rockchip
 *   kernels accept this for non-DMA-coherent paths and do the dma map
 *   internally. If the BSP requires dma-buf only, you'll see the broken
 *   flag flip; future surface allocator rework to dma-heap will unblock
 *   that.
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

/* ---- inline ABI ---- */

/* These format codes match Rockchip's drm/fourcc-ish enumeration for RGA.
 * BGRA8888 is the only one we need today. */
#define RK_FORMAT_BGRA_8888    0x00000004
#define RK_FORMAT_RGBA_8888    0x00000003
#define RK_FORMAT_ARGB_8888    0x00000016
#define RK_FORMAT_ABGR_8888    0x00000017

/* Source/dest descriptors. Pad to known size to keep layout stable across
 * BSPs that have added trailing fields. */
struct rga_img_info_t {
    uint32_t yrgb_addr;       /* phys (unused when virt is set) */
    uint32_t uv_addr;
    uint32_t v_addr;
    uint32_t format;
    uint16_t act_w;
    uint16_t act_h;
    uint16_t x_offset;
    uint16_t y_offset;
    uint16_t vir_w;
    uint16_t vir_h;
    uint16_t endian_mode;
    uint16_t alpha_swap;
};

struct mdp_img_act {
    uint16_t w;
    uint16_t h;
    int16_t  x_off;
    int16_t  y_off;
};

struct color_key_range {
    uint32_t max;
    uint32_t min;
};

struct rga_versions_t {
    uint32_t major;
    uint32_t minor;
    uint32_t revision;
    uint32_t reserved;
};

struct rga_req {
    uint8_t  render_mode;
    struct rga_img_info_t src;
    struct rga_img_info_t dst;
    struct rga_img_info_t pat;
    uint32_t rop_mask_addr;
    uint32_t LUT_addr;
    struct mdp_img_act rect_mode_ctrl;
    uint32_t alpha_rop_flag;        /* bit0=alpha en, bit1=rop en, etc. */
    uint8_t  scale_mode;
    uint32_t color_key_max;
    uint32_t color_key_min;
    uint32_t fg_color;
    uint32_t bg_color;
    struct color_key_range gr_color;
    struct mdp_img_act sg_global_alpha;
    uint16_t mmu_info;              /* bit0: src virt; bit1: dst virt; bit2: pat virt */
    uint8_t  alpha_global_value;
    uint32_t rop_code;
    uint8_t  bsfilter_flag;
    uint8_t  palette_mode;
    uint8_t  yuv2rgb_mode;
    uint8_t  endian_mode;
    uint8_t  rotate_mode;
    uint8_t  color_fill_mode;
    struct mdp_img_act gr_color_p;
    uint8_t  CMD_fin_int_enable;
    uint8_t  mmu_flag;              /* legacy; some kernels use this instead */
    uint8_t  alpha_rop_mode;
    uint8_t  src_trans_mode;
    uint32_t reserve[16];           /* slop to absorb BSP additions */
};

/* RGA ioctls (stable across BSPs). */
#define RGA_BLIT_SYNC      0x5004
#define RGA_BLIT_ASYNC     0x5005
#define RGA_FLUSH          0x5006
#define RGA_GET_RESULT     0x5007
#define RGA_GET_VERSION    0x5008

/* mmu_flag bits we set when buffers are user-virtual (the common path). */
#define RGA_MMU_FLAG_VIRT  0x1

#define DEV_PATH "/dev/rga"

static int g_fd     = -1;
static int g_broken = 0;

static void mark_broken(const char *why)
{
    if (!g_broken) {
        LOG_W("rga: disabling backend (%s). Falling back to CPU.", why);
        g_broken = 1;
    }
}

static void fill_side(struct rga_img_info_t *img,
                      const struct mc_accel_surface *s,
                      int x, int y, int w, int h)
{
    memset(img, 0, sizeof(*img));
    img->yrgb_addr  = (uintptr_t)s->virt;
    img->format     = RK_FORMAT_BGRA_8888;
    img->vir_w      = (uint16_t)(s->stride / 4);
    img->vir_h      = (uint16_t)s->h;
    img->x_offset   = (uint16_t)x;
    img->y_offset   = (uint16_t)y;
    img->act_w      = (uint16_t)w;
    img->act_h      = (uint16_t)h;
}

static int submit(struct rga_req *req, int alpha_en,
                  const struct mc_accel_surface *d, int dx, int dy,
                  const struct mc_accel_surface *s, int sx, int sy,
                  int w, int h)
{
    if (g_fd < 0 || g_broken) return -1;
    memset(req, 0, sizeof(*req));
    fill_side(&req->src, s, sx, sy, w, h);
    fill_side(&req->dst, d, dx, dy, w, h);
    req->mmu_info       = RGA_MMU_FLAG_VIRT;  /* both src and dst are virt */
    req->mmu_flag       = RGA_MMU_FLAG_VIRT;
    req->alpha_rop_flag = alpha_en ? 0x1 : 0x0;
    req->scale_mode     = 0;   /* nearest, not used anyway since w==w' */
    req->rotate_mode    = 0;

    if (ioctl(g_fd, RGA_BLIT_SYNC, req) < 0) {
        LOG_W("rga: ioctl BLIT_SYNC failed: %s", strerror(errno));
        mark_broken("ioctl failed");
        return -1;
    }
    return 0;
}

static int rga_init(void)
{
    int fd = open(DEV_PATH, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        LOG_I("rga: %s not openable (%s)", DEV_PATH, strerror(errno));
        return -1;
    }
    g_fd = fd;
    g_broken = 0;

    /* Optional: query version so it shows up in logs and acts as a
     * basic liveness probe (this ioctl is the same across most BSPs). */
    struct rga_versions_t v;
    memset(&v, 0, sizeof(v));
    if (ioctl(g_fd, RGA_GET_VERSION, &v) == 0) {
        LOG_I("rga: opened %s, version=%u.%u.%u",
              DEV_PATH, v.major, v.minor, v.revision);
    } else {
        LOG_I("rga: opened %s (version query unsupported)", DEV_PATH);
    }
    return 0;
}

static void rga_deinit(void)
{
    if (g_fd >= 0) { close(g_fd); g_fd = -1; }
    g_broken = 0;
}

static int rga_fill(const struct mc_accel_surface *d,
                    int x, int y, int w, int h, uint32_t bgra)
{
    /* RGA has color-fill render mode but its ABI for that varies even
     * more than blit. Skip until we have a verified-good codepath. */
    (void)d; (void)x; (void)y; (void)w; (void)h; (void)bgra;
    return -1;
}

static int rga_blit(const struct mc_accel_surface *d, int dx, int dy,
                    const struct mc_accel_surface *s, int sx, int sy,
                    int w, int h)
{
    struct rga_req req;
    return submit(&req, 0, d, dx, dy, s, sx, sy, w, h);
}

static int rga_blend(const struct mc_accel_surface *d, int dx, int dy,
                     const struct mc_accel_surface *s, int sx, int sy,
                     int w, int h)
{
    struct rga_req req;
    return submit(&req, 1, d, dx, dy, s, sx, sy, w, h);
}

static int rga_sync(void)
{
    /* BLIT_SYNC blocks; nothing extra to flush. */
    return 0;
}

const struct mc_accel_ops mc_accel_rga = {
    .name   = "rga",
    .init   = rga_init,
    .deinit = rga_deinit,
    .fill   = rga_fill,
    .blit   = rga_blit,
    .blend  = rga_blend,
    .sync   = rga_sync,
};

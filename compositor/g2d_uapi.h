/*
 * Allwinner G2D userspace ABI for T113 (sun8iw20) -- self-contained so we
 * don't depend on a vendor <linux/g2d_driver.h> in the sysroot.
 *
 * IMPORTANT (verified 2026-06-01 against the actual BSP, NOT the PDF):
 *   - docs/Linux G2D.pdf is the T5/T507 (sun50iw9) manual and is WRONG for
 *     T113 in places (e.g. it swaps FILLRECT_H/MASK_H).
 *   - T113 uses the *RCQ* G2D driver (deps_source/T113/sunxi_g2d-main/g2d_rcq/,
 *     selected by CONFIG_ARCH_SUN8IW20 in the BSP Makefile). That driver's
 *     ioctl table has NO 1.0 BITBLT(0x50)/FILLRECT(0x51) at all -- only the
 *     2.0 "_H" ops and the RCQ task ops. So the only usable path on T113 is
 *     the enhanced (g2d_image_enh) ABI below.
 *   - T113 has a G2D IOMMU (G2D_IOMMU_MASTER_ID 3), so buffers are passed as
 *     dma-buf fds (image.fd, use_phy_addr=0); the driver maps them via the
 *     IOMMU. A sunxi-ion physical address (use_phy_addr=1, laddr[0]=phys)
 *     also works but isn't required -- we use the fd path.
 *
 * Field order/types below are transcribed verbatim from the T113 vendor
 * header so the struct layout matches what the kernel copy_from_user()s.
 * Reference impl that this was cross-checked against:
 *   3rdLibrary/Awtk_g2d/awtk-tina-g2d/src/g2d_tina.c
 */
#ifndef MC_G2D_UAPI_H
#define MC_G2D_UAPI_H

#include <stdint.h>

#define MC_G2D_DEV_PATH "/dev/g2d"

/* ioctl numbers (bare ints, not _IOC-encoded for the _H series). */
enum {
    G2D_CMD_BITBLT_H   = 0x55,
    G2D_CMD_FILLRECT_H = 0x56,
    G2D_CMD_BLD_H      = 0x57,   /* Porter-Duff blend (per-pixel alpha) */
    G2D_CMD_MASK_H     = 0x58,
};

/* g2d_fmt_enh -- 32bpp packed formats. NB the names are 32-bit *word* order
 * (MSB..LSB), while buffers are little-endian *byte* order. So an mc surface
 * whose bytes are [B,G,R,A] in memory (mc's BGRA8888) is a 0xAARRGGBB word ==
 * G2D_FORMAT_ARGB8888. The verified Awtk_g2d reference maps BGRA8888->ARGB8888
 * for exactly this reason. Get this wrong and BITBLT (a pure copy) still looks
 * right, but BLD_H (which extracts channels for alpha math) shows wrong colors. */
typedef enum {
    G2D_FORMAT_ARGB8888 = 0,
    G2D_FORMAT_ABGR8888 = 1,
    G2D_FORMAT_RGBA8888 = 2,
    G2D_FORMAT_BGRA8888 = 3,
    G2D_FORMAT_XRGB8888 = 4,
    G2D_FORMAT_XBGR8888 = 5,
    G2D_FORMAT_RGBX8888 = 6,
    G2D_FORMAT_BGRX8888 = 7,
} g2d_fmt_enh;

/* g2d_blt_flags_h -- value of g2d_blt_h.flag_h.
 * NB: the RCQ driver routes (flag_h & 0xff00) to the rotate engine and the
 * rest to the mixer. G2D_BLT_NONE_H (0) => plain mixer blit. */
typedef enum {
    G2D_BLT_NONE_H = 0x0,
    G2D_ROT_90     = 0x00000100,
    G2D_ROT_180    = 0x00000200,
    G2D_ROT_270    = 0x00000300,
    G2D_ROT_0      = 0x00000400,
} g2d_blt_flags_h;

/* g2d_alpha_mode_enh -- g2d_image_enh.mode */
typedef enum {
    G2D_PIXEL_ALPHA  = 0,
    G2D_GLOBAL_ALPHA = 1,
    G2D_MIXER_ALPHA  = 2,
} g2d_alpha_mode_enh;

/* Porter-Duff command for BLD_H (g2d_bld.bld_cmd). SRCOVER = src over dst. */
typedef enum {
    G2D_BLD_CLEAR   = 0x00000001,
    G2D_BLD_COPY    = 0x00000002,
    G2D_BLD_DST     = 0x00000003,
    G2D_BLD_SRCOVER = 0x00000004,
    G2D_BLD_DSTOVER = 0x00000005,
} g2d_bld_cmd_flag;

typedef enum { G2D_BT601 = 0, G2D_BT709 = 1, G2D_BT2020 = 2 } g2d_color_gmt;
enum color_range { COLOR_RANGE_0_255 = 0, COLOR_RANGE_16_235 = 1 };

typedef struct { int32_t  x, y; uint32_t w, h; } g2d_rect;
typedef struct { uint32_t w, h; }                g2d_size;
typedef struct { uint32_t x, y; }                g2d_coor;

/* CK (colour-key) params -- unused by mc but part of g2d_bld's layout. */
typedef struct {
    int       match_rule;   /* `bool` in vendor hdr; int keeps size/align */
    uint32_t  max_color;
    uint32_t  min_color;
} g2d_ck;

/* g2d_image_enh -- field order MUST match the vendor header exactly. */
typedef struct {
    int                 bbuff;
    uint32_t            color;
    g2d_fmt_enh         format;
    uint32_t            laddr[3];
    uint32_t            haddr[3];
    uint32_t            width;       /* full buffer width in pixels  */
    uint32_t            height;      /* full buffer height in pixels */
    uint32_t            align[3];
    g2d_rect            clip_rect;   /* region to operate on         */
    g2d_size            resize;
    g2d_coor            coor;
    g2d_color_gmt       gamut;
    int                 bpremul;
    uint8_t             alpha;       /* plane alpha (GLOBAL_ALPHA)    */
    g2d_alpha_mode_enh  mode;
    int                 fd;          /* dma-buf fd (use_phy_addr=0)   */
    uint32_t            use_phy_addr;
    enum color_range    color_range;
} g2d_image_enh;

/* arg for G2D_CMD_FILLRECT_H */
typedef struct {
    g2d_image_enh dst_image_h;
} g2d_fillrect_h;

/* arg for G2D_CMD_BITBLT_H */
typedef struct {
    g2d_blt_flags_h flag_h;
    g2d_image_enh   src_image_h;
    g2d_image_enh   dst_image_h;
} g2d_blt_h;

/* arg for G2D_CMD_BLD_H. src_image[0]=top layer, src_image[1]=bottom;
 * dst_image=output. Only ch0/ch3 are used by the engine but the array is
 * 4 wide in the ABI. */
typedef struct {
    g2d_bld_cmd_flag bld_cmd;
    g2d_image_enh    dst_image;
    g2d_image_enh    src_image[4];
    g2d_ck           ck_para;
} g2d_bld;

#endif /* MC_G2D_UAPI_H */

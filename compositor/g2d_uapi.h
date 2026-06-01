/*
 * Allwinner G2D userspace ABI -- self-contained header so we don't depend
 * on a per-vendor <linux/g2d_driver.h> being present in the sysroot.
 *
 * Two API generations coexist on Allwinner BSPs:
 *
 *   1.0  -- T113 (sun8iw20), older T-series, sun8iw11. Selected at
 *           runtime by sending G2D_CMD_BITBLT (0x50). Surface buffers
 *           are described by `g2d_image` (no fd/use_phy_addr field);
 *           addr[0] is treated as a physical address on kernels <= 5.4,
 *           and as a dma-buf fd on kernels >= 5.10. The Allwinner
 *           "Linux G2D 开发指南" v2.2 (2022.7.11) section 3.1.6 spells
 *           this out explicitly.
 *
 *   2.0  -- T507 (sun50iw9), and several other sun8iw / sun50iw parts
 *           with V2X support. G2D_CMD_BITBLT_H (0x55) etc., surfaces
 *           described by `g2d_image_enh` (has explicit fd + use_phy_addr).
 *           This is what compositor/accel_g2d.c was originally written
 *           against (transcribed there before this header existed).
 *
 * The ioctl numbers are bare integers, NOT _IO/_IOC magic-encoded
 * (verified by reading the kernel driver source -- they're just enum
 * values starting at 0x50). 1.0 occupies 0x50..0x54, 2.0 occupies
 * 0x55..0x58, then MEM_* utilities follow at 0x59+.
 *
 * Reference: deps_source/T113/sunxi_g2d-main/ (Allwinner T113 BSP G2D
 * driver, GPL-2.0). Userspace UAPI <linux/g2d_driver.h> ships with the
 * vendor SDK and is what the values below were cross-checked against.
 */
#ifndef MC_G2D_UAPI_H
#define MC_G2D_UAPI_H

#include <stdint.h>

/* ============================================================ *
 * ioctl numbers (shared 1.0 + 2.0)                              *
 * ============================================================ */

enum {
    /* 1.0 (T113, sun8iw11, older parts) */
    G2D_CMD_BITBLT          = 0x50,
    G2D_CMD_FILLRECT        = 0x51,
    G2D_CMD_STRETCHBLT      = 0x52,
    G2D_CMD_PALETTE_TBL     = 0x53,   /* not used here */
    G2D_CMD_QUEUE           = 0x54,

    /* 2.0 (T507, V2X-capable parts) */
    G2D_CMD_BITBLT_H        = 0x55,
    G2D_CMD_MASK_H          = 0x56,
    G2D_CMD_BLD_H           = 0x57,
    G2D_CMD_FILLRECT_H      = 0x58,

    /* memory pool helpers (not used by mc) */
    G2D_CMD_MEM_REQUEST     = 0x59,
    G2D_CMD_MEM_RELEASE     = 0x5a,
    G2D_CMD_MEM_SELIDX      = 0x5b,
    G2D_CMD_MEM_GETADR      = 0x5c,
    G2D_CMD_INVERTED_ORDER  = 0x5d,
};

#define MC_G2D_DEV_PATH "/dev/g2d"

/* ============================================================ *
 * 1.0 ABI -- used on T113                                       *
 *                                                               *
 * Section refs are to "Linux G2D 开发指南 v2.2 (2022.7.11)" in   *
 * docs/Linux G2D.pdf.                                           *
 * ============================================================ */

/* 3.1.1 g2d_blt_flags -- bitblt / stretchblt operation flags. */
enum {
    G2D_BLT_NONE             = 0x00000000,
    G2D_BLT_PIXEL_ALPHA      = 0x00000001,  /* per-pixel source alpha */
    G2D_BLT_PLANE_ALPHA      = 0x00000002,  /* global plane alpha     */
    G2D_BLT_MULTI_ALPHA      = 0x00000004,  /* per-pixel * plane      */
    G2D_BLT_SRC_COLORKEY     = 0x00000008,
    G2D_BLT_DST_COLORKEY     = 0x00000010,
    G2D_BLT_FLIP_HORIZONTAL  = 0x00000020,
    G2D_BLT_FLIP_VERTICAL    = 0x00000040,
    G2D_BLT_ROTATE90         = 0x00000080,
    G2D_BLT_ROTATE180        = 0x00000100,
    G2D_BLT_ROTATE270        = 0x00000200,
    G2D_BLT_MIRROR45         = 0x00000400,
    G2D_BLT_MIRROR135        = 0x00000800,
};

/* 3.1.2 g2d_fillrect_flags. */
enum {
    G2D_FIL_NONE             = 0x00000000,
    G2D_FIL_PIXEL_ALPHA      = 0x00000001,
    G2D_FIL_PLANE_ALPHA      = 0x00000002,
    G2D_FIL_MULTI_ALPHA      = 0x00000004,
};

/* 3.1.3 g2d_data_fmt -- pixel formats supported by 1.0 ABI.
 * mc clients are BGRA8888 == G2D_FMT_BGRA8888 (0x01). */
enum {
    G2D_FMT_ARGB_AYUV8888  = 0x00,
    G2D_FMT_BGRA_VUYA8888  = 0x01,   /* this is what mc surfaces are */
    G2D_FMT_ABGR_AVUY8888  = 0x02,
    G2D_FMT_RGBA_YUVA8888  = 0x03,
    G2D_FMT_XRGB8888       = 0x04,
    G2D_FMT_BGRX8888       = 0x05,
    G2D_FMT_XBGR8888       = 0x06,
    G2D_FMT_RGBX8888       = 0x07,
};

/* 3.1.4 g2d_pixel_seq -- normal for our 32bpp BGRA surfaces. */
enum {
    G2D_SEQ_NORMAL         = 0x0,
};

/* 3.1.14 g2d_scan_order. */
enum {
    G2D_SM_TDLR = 0x00000000,  /* top->down, left->right (default) */
    G2D_SM_DTLR = 0x00000001,
    G2D_SM_TDRL = 0x00000002,
    G2D_SM_DTRL = 0x00000003,
};

/* 3.1.6 g2d_image (version 1.0).
 *
 * IMPORTANT: addr[0]'s meaning depends on the kernel:
 *   - linux <= 5.4: addr[0] is a 32-bit physical address (or DMA addr
 *     for an IOMMU-mapped buffer).
 *   - linux >= 5.10: addr[0] is a dma-buf fd allocated via dma_heap.
 *     addr[1] / addr[2] are reserved (kept 0).
 *
 * For BGRA8888 only addr[0] is meaningful regardless of kernel. */
typedef struct {
    uint32_t addr[3];
    uint32_t w;
    uint32_t h;
    uint32_t format;        /* G2D_FMT_* */
    uint32_t pixel_seq;     /* G2D_SEQ_* */
} g2d_image;

/* 3.1 g2d_rect / coor (shared by 1.0 and 2.0). */
typedef struct { int32_t  x, y; uint32_t w, h; } g2d_rect;
typedef struct { uint32_t x, y; }                g2d_coor;

/* 3.1.15 g2d_blt -- arg for G2D_CMD_BITBLT. */
typedef struct {
    uint32_t  flag;          /* g2d_blt_flags                */
    g2d_image src_image;
    g2d_rect  src_rect;
    g2d_image dst_image;
    int32_t   dst_x;         /* dst top-left x */
    int32_t   dst_y;         /* dst top-left y */
    uint32_t  color;         /* colorkey, ignored unless COLORKEY flags */
    uint32_t  alpha;         /* plane alpha when *_PLANE_ALPHA flag set */
} g2d_blt;

/* 3.1.16 g2d_fillrect -- arg for G2D_CMD_FILLRECT. */
typedef struct {
    uint32_t  flag;          /* g2d_fillrect_flags */
    g2d_image dst_image;
    g2d_rect  dst_rect;
    uint32_t  color;         /* ARGB: A[31:24] R[23:16] G[15:8] B[7:0] */
    uint32_t  alpha;         /* plane alpha when *_PLANE_ALPHA flag set */
} g2d_fillrect;

/* ============================================================ *
 * 2.0 ABI -- used on T507 (see compositor/accel_g2d.c)          *
 * NOT redefined here; accel_g2d.c keeps its own copy until that *
 * file is moved over.                                           *
 * ============================================================ */

#endif /* MC_G2D_UAPI_H */

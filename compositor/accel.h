/*
 * Accel abstraction: pluggable 2D blit / blend / fill backends.
 *
 * The compositor's composition pipeline only needs three operations:
 *   - clear a rectangle to a fixed RGBA color (frame baseline)
 *   - blit  src rect -> dst rect (opaque)
 *   - blend src rect -> dst rect (SRC_OVER alpha)
 *
 * Each backend implements those primitives behind a common ops table.
 * `mc_accel_select()` picks the best available one at startup based on
 * a) what was compiled in, b) what the MC_ACCEL env var requests,
 * c) whether the hardware device can be opened and self-tests OK.
 *
 * Surfaces are always BGRA8888 in this phase. Format is encoded for future
 * extension but currently asserted by the compositor.
 */
#ifndef MC_ACCEL_H
#define MC_ACCEL_H

#include <stdint.h>

enum {
    MC_ACCEL_FMT_BGRA8888 = 1,
};

/* Description of one rectangular surface (source or destination).
 *
 *   virt        always-valid mmap pointer (CPU backend uses this only)
 *   dmabuf_fd   valid dma-buf fd from dma-heap/ion, or -1 if not dma-buf
 *               (e.g. memfd, fb mmap'd file descriptor)
 *   phys        physical DRAM address, or 0 if unknown
 *   is_dmabuf   1 iff dmabuf_fd refers to a real dma-buf object; HW
 *               backends consult this rather than just "fd >= 0" so we
 *               don't accidentally pass a memfd to a HW driver that
 *               expects dma-buf semantics
 *
 * HW backends pick one path in priority order:
 *   - phys != 0     → use that (G2D's use_phy_addr=1, laddr[0]=phys)
 *   - is_dmabuf     → import via fd
 *   - else          → fail; per-op fallback to CPU
 */
struct mc_accel_surface {
    void    *virt;
    int      dmabuf_fd;
    uint32_t phys;
    int      is_dmabuf;

    int      w, h;
    int      stride;
    uint8_t  format;
    /* 1 if the pixel buffer is stored bottom-up (GL FBO origin in the
     * lower-left). Only meaningful on SRC surfaces; DST (the fb back
     * buffer) is always top-down. Honoured by the CPU blit/blend; HW
     * backends that can't express it fall back per-op to CPU. */
    uint8_t  flip_y;
};

/* Per-backend ops. NULL pointer for an op means "not supported" and the
 * caller must fall back. fill/blit/blend may queue async work; sync()
 * blocks until all in-flight ops are done. */
struct mc_accel_ops {
    const char *name;

    int  (*init)(void);
    void (*deinit)(void);

    int  (*fill) (const struct mc_accel_surface *dst,
                  int x, int y, int w, int h, uint32_t bgra);
    int  (*blit) (const struct mc_accel_surface *dst, int dx, int dy,
                  const struct mc_accel_surface *src, int sx, int sy,
                  int w, int h);
    int  (*blend)(const struct mc_accel_surface *dst, int dx, int dy,
                  const struct mc_accel_surface *src, int sx, int sy,
                  int w, int h);

    int  (*sync)(void);
};

/* Built-in backends; built-in conditionally per compile flag. The CPU
 * backend is always present. */
extern const struct mc_accel_ops mc_accel_cpu;

#ifdef MC_ENABLE_G2D
extern const struct mc_accel_ops mc_accel_g2d;
#endif
#ifdef MC_ENABLE_RGA
extern const struct mc_accel_ops mc_accel_rga;
#endif

/* Pick + initialize the best available backend. Honors MC_ACCEL env var
 * (values: "cpu" / "g2d" / "rga" / "auto", default "auto"). The returned
 * pointer is owned by the accel module; do not free. NULL means total
 * failure (which should not happen since CPU is always available). */
const struct mc_accel_ops *mc_accel_select(void);

#endif

/*
 * Output backend abstraction. Phase 0 supports:
 *   - fb : /dev/fb0 (production target, e.g. T507)
 *   - ppm: dumps composited frame to a .ppm file for visual verification
 *          on dev hosts that have no framebuffer hooked up.
 *
 * All backends present BGRA8888.
 */
#ifndef MC_BACKEND_H
#define MC_BACKEND_H

#include <stddef.h>
#include <stdint.h>

/* Forward declarations -- both types appear inside each other's
 * function-pointer prototypes; without these, GCC treats the
 * prototype-scope `struct X *` as a fresh local tag and emits
 * "incompatible pointer type" when the ops table is later filled in. */
struct mc_backend;
struct mc_surface;

/* When a backend can drive the whole compose loop itself (HW path:
 * Mali GPU on T507 via backend_egl, G2D blitter on T113 via backend_g2d),
 * it sets `hw_compose` on its mc_backend. compose.c walks visible
 * surfaces bottom-up and calls begin_frame / draw_surface(...) /
 * end_frame instead of doing per-pixel CPU blits, then calls
 * backend->present() to swap.
 *
 * Leaving `hw_compose` NULL keeps the backend on the CPU path (the
 * compositor reads back via get_buffer(), writes per-surface blits,
 * then present()). backend_fb and backend_ppm work this way. */
struct mc_backend_hw_compose_ops {
    void (*begin_frame)  (struct mc_backend *be);
    void (*draw_surface) (struct mc_backend *be, struct mc_surface *sf);
    void (*end_frame)    (struct mc_backend *be);
};

struct mc_backend {
    const char *name;

    /* Open the backend. arg is backend-specific (path, options, ...).
     * On success, *w, *h, *stride (bytes) and *bpp are filled.
     * The buffer pointer returned by get_buffer() is BGRA8888.
     * Returns 0 on success, -errno otherwise. */
    int (*open)(struct mc_backend *be, const char *arg,
                int w_hint, int h_hint,
                int *out_w, int *out_h, int *out_stride);

    /* Return current writable back-buffer (BGRA8888, full screen).
     * May return NULL on HW-compose backends that don't expose CPU
     * mapping. compose.c only calls this on the CPU path. */
    uint8_t *(*get_buffer)(struct mc_backend *be);

    /* Physical address of the current back-buffer in DRAM, or 0 if the
     * backend doesn't know one. Used by HW accel (G2D needs this to
     * write directly into fb without going through CPU mapping).
     * NULL function pointer means "not supported"; treat as 0. */
    uint32_t (*get_buffer_phys)(struct mc_backend *be);

    /* Present the back-buffer (swap, write file, ioctl pan, ...). */
    int (*present)(struct mc_backend *be);

    /* Close & release. */
    void (*close)(struct mc_backend *be);

    /* Optional HW compose hooks (see comment on struct above). NULL ->
     * CPU compose path. */
    const struct mc_backend_hw_compose_ops *hw_compose;

    void *priv;
};

extern struct mc_backend backend_fb;
extern struct mc_backend backend_ppm;

#ifdef MC_ENABLE_EGL
extern struct mc_backend backend_egl;
#endif

#ifdef MC_ENABLE_BACKEND_G2D
/* T113 / sun8iw path: blit each client dma-buf into the fb back-buffer
 * via /dev/g2d, then page-flip via the standard fb ioctl. Like
 * backend_egl, this is selected with --backend g2d, sets hw_compose,
 * and is used in place of the CPU per-pixel path. */
extern struct mc_backend backend_g2d;
#endif

#endif

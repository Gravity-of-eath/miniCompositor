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

struct mc_backend {
    const char *name;

    /* Open the backend. arg is backend-specific (path, options, ...).
     * On success, *w, *h, *stride (bytes) and *bpp are filled.
     * The buffer pointer returned by get_buffer() is BGRA8888.
     * Returns 0 on success, -errno otherwise. */
    int (*open)(struct mc_backend *be, const char *arg,
                int w_hint, int h_hint,
                int *out_w, int *out_h, int *out_stride);

    /* Return current writable back-buffer (BGRA8888, full screen). */
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

    void *priv;
};

extern struct mc_backend backend_fb;
extern struct mc_backend backend_ppm;

/* GPU compositor backend (Mali / EGL window on /dev/fb0). Doesn't expose
 * a CPU-writable buffer or use the per-surface CPU blit path; instead it
 * provides direct entry points that compose.c calls when
 * `s->gpu_compose` is set. The struct mc_backend interface only needs
 * `open`/`close`/`present` -- the rest are stubbed/unused.
 * Only compiled in when MC_ENABLE_EGL=1; otherwise main.c never selects
 * "egl" so this symbol won't be referenced. */
#ifdef MC_ENABLE_EGL
extern struct mc_backend backend_egl;
struct mc_server;
struct mc_surface;
void mc_backend_egl_begin_frame  (struct mc_backend *be);
void mc_backend_egl_draw_surface (struct mc_backend *be,
                                  struct mc_surface *sf);
void mc_backend_egl_end_frame    (struct mc_backend *be);
#endif

#endif

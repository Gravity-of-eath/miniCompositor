/**
 * File:   lcd_mc.c
 * Brief:  Software (CPU) mc-compositor client LCD device for T113.
 *
 * T113 has no GPU/EGL. Instead of rendering into a dma-buf via an FBO this
 * backend uses AWTK's software canvas (lcd_mem BGRA8888) pointed directly at
 * a CPU-mapped mc shared-memory surface buffer.
 *
 * Buffer ownership is identical to the EGL mc backend:
 *   - We hold ONE buffer at a time (the "render buffer", cur_idx).
 *   - After AWTK finishes a frame (flush hook) we commit cur_idx to the
 *     compositor, wait for the next buffer to be free, repoint the lcd_mem's
 *     offline/online_fb to that buffer, then advance cur_idx.
 *
 * NO cache flushing: the compositor backend (backend_g2d.c) already does the
 * cedar cache invalidation before G2D reads the surface. CPU writes via the
 * mmap'd pointer are visible to G2D without extra ioctl from the client.
 *
 * Globals exported for input_thread_mc.c:
 *   lcd_linux_mc_get_ctx()   -- the mc_ctx_t connection
 *   lcd_linux_mc_get_surf()  -- the fullscreen mc_surface_t
 *   lcd_linux_mc_set_hidden(h) / lcd_linux_mc_is_hidden()
 *
 * Public entrypoint:
 *   lcd_t* lcd_linux_mc_create(void)
 *     -- allocates, connects, creates surface, wraps lcd_mem, returns lcd_t*
 */

#ifdef WITH_LCD_LINUX_MC

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tkc/mem.h"
#include "base/lcd.h"
#include "lcd/lcd_mem.h"
#include "lcd/lcd_mem_bgra8888.h"

#include "mc.h"

/* -----------------------------------------------------------------------
 * Shared globals (same role as in egl_devices/mc/egl_devices.c).
 * input_thread_mc.c accesses these via the accessors below.
 * ---------------------------------------------------------------------- */
static mc_ctx_t     *g_shared_mc_ctx  = NULL;
static mc_surface_t *g_shared_mc_surf = NULL;

/* Hidden-state hint set by input_thread_mc when a MC_EV_LIFECYCLE arrives.
 * When hidden we skip the commit so we don't drown the compositor in
 * no-op recompose triggers. */
static volatile int g_hidden = 0;

/* -----------------------------------------------------------------------
 * Accessor symbols expected by input_thread_mc.c
 * ---------------------------------------------------------------------- */
mc_ctx_t *lcd_linux_mc_get_ctx(void)
{
    return g_shared_mc_ctx;
}

mc_surface_t *lcd_linux_mc_get_surf(void)
{
    return g_shared_mc_surf;
}

void lcd_linux_mc_set_hidden(int h)
{
    g_hidden = h ? 1 : 0;
}

int lcd_linux_mc_is_hidden(void)
{
    return g_hidden;
}

/* -----------------------------------------------------------------------
 * Internal context
 * ---------------------------------------------------------------------- */

typedef struct _mc_lcd_ctx_t {
    /* mc connection */
    mc_ctx_t     *mc;
    mc_surface_t *surf;
    int           w, h, stride;
    int           n_buf;
    int           cur_idx;          /* buffer we are currently drawing into */

    /* AWTK software lcd pointed at cur_idx's CPU-mapped buffer */
    lcd_t        *lcd;

    /* Saved original flush hook so we can chain if needed */
    ret_t       (*flush_default)(lcd_t *lcd);
} mc_lcd_ctx_t;

/* Single global instance (one AWTK process = one lcd). */
static mc_lcd_ctx_t *g_mc_lcd = NULL;

/* -----------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */
static const char *mc_socket_path(void)
{
    const char *s = getenv("MC_SOCKET");
    return s && *s ? s : "/tmp/mc.sock";
}

static const char *mc_app_name(void)
{
    const char *s = getenv("MC_APP_NAME");
    return s && *s ? s : "awtk";
}

static mc_role_t mc_role_from_env(void)
{
    const char *s = getenv("MC_ROLE");
    if (s) {
        if (strcmp(s, "popup")      == 0) return MC_ROLE_POPUP;
        if (strcmp(s, "background") == 0) return MC_ROLE_BG;
        if (strcmp(s, "bg")         == 0) return MC_ROLE_BG;
    }
    return MC_ROLE_FULLSCREEN;
}

/* -----------------------------------------------------------------------
 * Flush hook -- called by AWTK after every frame.
 *
 * Flow:
 *   1. If hidden: sleep briefly and return (no compositor traffic).
 *   2. Copy AWTK's private offline buffer into the current mc buffer.
 *   3. Commit cur_idx with the frame's damage rect.
 *   4. Wait for the next buffer to be released; advance cur_idx so the next
 *      frame copies into a buffer the compositor has finished with.
 * ---------------------------------------------------------------------- */
static ret_t lcd_mc_flush(lcd_t *lcd)
{
    mc_lcd_ctx_t *c = g_mc_lcd;
    if (!c) return RET_BAD_PARAMS;

    /* Throttle when hidden: AWTK's main loop still runs (cheap for a static
     * UI) but we don't commit frames the compositor can't display. */
    if (g_hidden) {
        struct timespec ts = { 0, 200 * 1000 * 1000 };
        nanosleep(&ts, NULL);
        return RET_OK;
    }

    /* Copy AWTK's freshly-rendered private buffer into the current mc buffer.
     * cur_idx was waited free at create (idx 0) or by the previous flush, so
     * the compositor is not reading it now.  Both buffers are tight BGRA8888
     * (line_length == w*4 on each side), so one linear copy is correct. */
    lcd_mem_t *mem = (lcd_mem_t *)c->lcd;
    uint8_t *src = (uint8_t *)lcd_mem_get_offline_fb(mem);
    uint8_t *dst = (uint8_t *)mc_surface_buf_at(c->surf, c->cur_idx, NULL);
    if (!src || !dst) {
        fprintf(stderr, "[mc-lcd] flush: null fb (src=%p dst=%p idx=%d)\n",
                (void *)src, (void *)dst, c->cur_idx);
        return RET_FAIL;
    }
    memcpy(dst, src, (size_t)c->stride * c->h);

    /* Always commit full-surface damage.  Per-rect dirty-rect optimisation is
     * a future follow-up. */
    mc_rect_t damage = { 0, 0, (int16_t)c->w, (int16_t)c->h };

    /* Ordering invariant (n_buf=2): commit the current buffer BEFORE waiting
     * for the next one.  Waiting first would deadlock — the compositor only
     * releases a buffer in response to a commit. */
    int rc = mc_surface_commit_idx(c->surf, c->cur_idx, &damage, 1);
    if (rc != 0) {
        fprintf(stderr, "[mc-lcd] mc_surface_commit_idx(%d) failed: %d\n",
                c->cur_idx, rc);
        return RET_FAIL;
    }

    /* Wait for the next buffer to be free, then make it the copy target for
     * the next frame.  AWTK's private offline buffer is never repointed, so
     * the lcd_mem fb_bitmaps[] registry stays valid (no "not found fb bitmap"). */
    int next = (c->cur_idx + 1) % c->n_buf;
    if (mc_surface_wait_buf_free(c->surf, next) != 0) {
        fprintf(stderr, "[mc-lcd] mc_surface_wait_buf_free(%d) failed\n", next);
        return RET_FAIL;
    }
    c->cur_idx = next;

    return RET_OK;
}

/* -----------------------------------------------------------------------
 * Dispose -- tear down surface, disconnect, clear globals.
 * ---------------------------------------------------------------------- */
static void lcd_mc_dispose(mc_lcd_ctx_t *c)
{
    if (!c) return;

    if (c->surf) {
        if (g_shared_mc_surf == c->surf) g_shared_mc_surf = NULL;
        mc_surface_destroy(c->surf);
        c->surf = NULL;
    }
    if (c->mc) {
        if (g_shared_mc_ctx == c->mc) g_shared_mc_ctx = NULL;
        mc_disconnect(c->mc);
        c->mc = NULL;
    }
    /* lcd_t* is owned by AWTK framework; do NOT destroy it here. */
    TKMEM_FREE(c);
    g_mc_lcd = NULL;
}

/* -----------------------------------------------------------------------
 * Public entrypoint: connect, create surface, wrap lcd_mem.
 *
 * Returns a fully initialised lcd_t* ready for AWTK to use, or NULL on
 * any error.
 * ---------------------------------------------------------------------- */
lcd_t *lcd_linux_mc_create(void)
{
    mc_lcd_ctx_t *c = TKMEM_ZALLOC(mc_lcd_ctx_t);
    if (!c) return NULL;
    c->cur_idx = -1;

    /* ---- mc connection ---- */
    mc_set_socket_path(mc_socket_path());
    c->mc = mc_connect(mc_app_name());
    if (!c->mc) {
        fprintf(stderr, "[mc-lcd] mc_connect('%s') failed: %s\n",
                mc_app_name(), mc_strerror(mc_last_error()));
        goto err;
    }

    mc_screen_info_t scr;
    if (mc_get_screen_info(c->mc, &scr) < 0) {
        fprintf(stderr, "[mc-lcd] mc_get_screen_info failed\n");
        goto err;
    }
    c->w = scr.screen_w;
    c->h = scr.screen_h;

    /* CPU client: no FLIP_Y flag (software canvas uses top-left origin
     * like every other surface; only GL FBO clients need the flip). */
    mc_role_t role = mc_role_from_env();
    c->surf = mc_surface_create_shm_ex(c->mc, c->w, c->h,
                                       MC_FMT_BGRA8888, role,
                                       2 /* n_buf */,
                                       0 /* flags: no FLIP_Y */);
    if (!c->surf) {
        fprintf(stderr, "[mc-lcd] mc_surface_create_shm_ex failed: %s\n",
                mc_strerror(mc_last_error()));
        goto err;
    }

    c->n_buf  = mc_surface_n_buf(c->surf);
    c->stride = mc_surface_buf_stride(c->surf);

    printf("[mc-lcd] surface %dx%d stride=%d n_buf=%d role=%d\n",
           c->w, c->h, c->stride, c->n_buf, (int)role);

    /* Acquire the starting buffer and wait until the compositor releases it. */
    c->cur_idx = 0;
    if (mc_surface_wait_buf_free(c->surf, c->cur_idx) != 0) {
        fprintf(stderr, "[mc-lcd] initial wait_buf_free(0) failed\n");
        goto err;
    }

    /* Create the AWTK software lcd with its OWN private offline buffer
     * (alloc=TRUE).  AWTK renders into this single, stable buffer for the
     * lifetime of the lcd; we copy it into the rotating mc buffer in flush.
     *
     * Why not map the mc buffer directly as the lcd_mem fb and rotate it?
     * lcd_mem keeps a fixed registry (fb_bitmaps[], built at create time) and
     * asserts "not found fb bitmap" if offline_fb is ever repointed to a
     * buffer that wasn't registered there.  Repointing to mc buffer[1] on the
     * 2nd frame tripped that assert (crash after one frame).  A stable private
     * buffer sidesteps the whole multi-buffer registry/dirty-rect problem; the
     * cost is one extra full-surface copy per frame (~1.5 MB at 800x480), the
     * same B1 trade-off the compositor already makes.  Zero-copy (register all
     * mc buffers via create_double_fb and rotate among them) is a future
     * optimization. */
    c->lcd = lcd_mem_bgra8888_create((wh_t)c->w, (wh_t)c->h, TRUE);
    if (!c->lcd) {
        fprintf(stderr, "[mc-lcd] lcd_mem_bgra8888_create failed\n");
        goto err;
    }

    /* Install our flush hook. AWTK calls lcd->flush at end_frame. */
    c->flush_default = c->lcd->flush;
    c->lcd->flush    = lcd_mc_flush;

    /* Publish globals for input_thread_mc. */
    g_shared_mc_ctx  = c->mc;
    g_shared_mc_surf = c->surf;
    g_mc_lcd         = c;

    return c->lcd;

err:
    lcd_mc_dispose(c);
    return NULL;
}

#endif /* WITH_LCD_LINUX_MC */

/*
 * LVGL 9.0 port for mc surfaces.
 *
 * Render mode: FULL with two buffers pointing straight at the mc surface's
 * two shm halves. LVGL repaints the whole screen into the active buffer each
 * frame, so every buffer we commit is fully painted (no stale/transparent
 * scanlines). We tried DIRECT (only redraw invalid areas, rely on LVGL's
 * cross-buffer dirty tracking) but our commit/rotate left never-painted
 * alpha=0 scanlines in one buffer that showed as transparent "white lines"
 * over an opaque base. FULL is still zero-copy (render straight into the mc
 * buffer); the only cost is a full redraw per frame, fine at 800x480.
 *
 * Surfaces with role=POPUP can have a transparent screen: simply call
 *   lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_TRANSP, 0);
 * before building the UI. LVGL 9 honors that at render time and writes
 * alpha=0 where no widget is drawn, so the compositor sees real transparency
 * outside rounded corners.
 *
 * Tick: install a callback that returns ms from CLOCK_MONOTONIC.
 */
#define _GNU_SOURCE
#include "lv_port_mc.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_FRAME_DMG 8
#define MAX_HOLES     4

typedef struct { int16_t x, y, w, h; } hole_rect_t;
static hole_rect_t g_holes[MAX_HOLES];
static int         g_n_holes;

static mc_surface_t  *g_surf;
static int            g_w, g_h, g_stride;
static lv_display_t  *g_disp;

/* Per-frame damage list. Each flush_cb invocation appends one rect; on
 * lv_display_flush_is_last() we commit with the accumulated list. */
static mc_rect_t      g_dmg[MAX_FRAME_DMG];
static int            g_dmg_n;

/* Which mc buffer LVGL is currently rendering into (0 or 1). LVGL gives
 * us px_map = the start of the active draw buffer, which matches one of
 * the mc buffer map pointers; we figure out which by pointer comparison. */
static uint8_t *g_buf_addr[2];

/* Tracks which mc buffer we last committed; -1 before first commit.
 * flush_wait_cb uses this to know which buffer it needs to wait on. */
static int g_last_committed_idx = -1;

static uint32_t mc_tick_cb(void)
{
    static struct timespec t0;
    static int inited;
    if (!inited) { clock_gettime(CLOCK_MONOTONIC, &t0); inited = 1; }
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    int64_t total_ns = (int64_t)(t.tv_sec - t0.tv_sec) * 1000000000LL
                     + (int64_t)(t.tv_nsec - t0.tv_nsec);
    return (uint32_t)(total_ns / 1000000);
}

static void dmg_append(int x, int y, int w, int h)
{
    if (g_dmg_n < MAX_FRAME_DMG) {
        g_dmg[g_dmg_n++] = (mc_rect_t){
            (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h
        };
        return;
    }
    /* Overflow: bbox-merge into slot 0. */
    int x0 = g_dmg[0].x < x ? g_dmg[0].x : x;
    int y0 = g_dmg[0].y < y ? g_dmg[0].y : y;
    int x1a = g_dmg[0].x + g_dmg[0].w;
    int x1b = x + w;
    int y1a = g_dmg[0].y + g_dmg[0].h;
    int y1b = y + h;
    int x1 = x1a > x1b ? x1a : x1b;
    int y1 = y1a > y1b ? y1a : y1b;
    g_dmg[0].x = (int16_t)x0; g_dmg[0].y = (int16_t)y0;
    g_dmg[0].w = (int16_t)(x1 - x0); g_dmg[0].h = (int16_t)(y1 - y0);
}

/* Force alpha=0 across a hole rect within the given buffer. Also clears
 * RGB to 0 (a precaution: even if the compositor only honored alpha, we
 * don't want stale-looking pixels showing through gradient-y blends). */
static void punch_hole_in_buf(uint8_t *buf, const hole_rect_t *r)
{
    int x0 = r->x, y0 = r->y, x1 = r->x + r->w, y1 = r->y + r->h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > g_w) x1 = g_w;
    if (y1 > g_h) y1 = g_h;
    if (x0 >= x1 || y0 >= y1) return;
    int line_bytes = (x1 - x0) * 4;
    for (int y = y0; y < y1; y++) {
        memset(buf + (size_t)y * g_stride + (size_t)x0 * 4, 0, line_bytes);
    }
}

static void mc_flush_cb(lv_display_t *disp,
                        const lv_area_t *area,
                        uint8_t *px_map)
{
    /* `px_map` is the start of the active draw buffer (i.e. one of our
     * mc shm pointers). In DIRECT mode it doesn't change per-area within
     * a frame, only between frames when LVGL swaps buffers. */
    int aw = area->x2 - area->x1 + 1;
    int ah = area->y2 - area->y1 + 1;
    dmg_append(area->x1, area->y1, aw, ah);

    if (lv_display_flush_is_last(disp)) {
        int idx = (px_map == g_buf_addr[0]) ? 0
                : (px_map == g_buf_addr[1]) ? 1 : -1;
        if (idx >= 0) {
            /* Punch any registered holes in the buffer LVGL just produced
             * and add them to the damage list so the compositor refreshes
             * those screen regions even if nothing else changed. */
            for (int i = 0; i < g_n_holes; i++) {
                punch_hole_in_buf(g_buf_addr[idx], &g_holes[i]);
                dmg_append(g_holes[i].x, g_holes[i].y,
                           g_holes[i].w, g_holes[i].h);
            }
            mc_surface_commit_idx(g_surf, idx, g_dmg, g_dmg_n);
            g_last_committed_idx = idx;
        }
        g_dmg_n = 0;
    }
    lv_display_flush_ready(disp);
}

void lv_port_mc_add_hole(int x, int y, int w, int h)
{
    if (g_n_holes >= MAX_HOLES) return;
    g_holes[g_n_holes++] = (hole_rect_t){
        (int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h
    };
}

void lv_port_mc_clear_holes(void) { g_n_holes = 0; }

/* ===== Input ===== */

static mc_ctx_t       *g_indev_ctx;
static int16_t         g_in_x, g_in_y;
static lv_indev_state_t g_in_state = LV_INDEV_STATE_RELEASED;

static lv_port_mc_event_cb_t g_user_event_cb;
static void                 *g_user_event_user;

void lv_port_mc_set_event_cb(lv_port_mc_event_cb_t cb, void *user)
{
    g_user_event_cb   = cb;
    g_user_event_user = user;
}

static void mc_indev_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    /* Drain everything mc has queued; keep the latest snapshot. */
    mc_event_t ev;
    while (mc_dispatch(g_indev_ctx, &ev, 0) > 0) {
        if (ev.kind == MC_EV_TOUCH) {
            g_in_x = ev.touch.x;
            g_in_y = ev.touch.y;
            switch (ev.touch.state) {
            case MC_TOUCH_STATE_DOWN:
            case MC_TOUCH_STATE_MOVE:
                g_in_state = LV_INDEV_STATE_PRESSED;
                break;
            case MC_TOUCH_STATE_UP:
            default:
                g_in_state = LV_INDEV_STATE_RELEASED;
                break;
            }
        } else if (g_user_event_cb) {
            /* Hand non-touch events (bus etc.) to the user. NB: ev.bus.*
             * pointers borrow into the ctx rx buf and become invalid on
             * the next dispatch -- so the callback must process them
             * synchronously (e.g. copy or update UI immediately). */
            g_user_event_cb(&ev, g_user_event_user);
        }
    }
    data->point.x = g_in_x;
    data->point.y = g_in_y;
    data->state   = g_in_state;
}

int lv_port_mc_input_init(mc_ctx_t *ctx)
{
    if (!ctx) return -1;
    g_indev_ctx = ctx;
    g_in_state  = LV_INDEV_STATE_RELEASED;
    g_in_x = g_in_y = 0;

    lv_indev_t *indev = lv_indev_create();
    if (!indev) return -1;
    lv_indev_set_type   (indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, mc_indev_read_cb);
    lv_indev_set_display(indev, g_disp);
    return 0;
}

/* Called by LVGL before it starts rendering into the "other" draw buffer.
 * We only need to ensure THAT buffer is FREE on the compositor's side.
 * Waiting on both buffers (the old code) would serialize the whole
 * pipeline -- LVGL would block until the just-committed buffer is also
 * released, killing throughput. */
static void mc_flush_wait_cb(lv_display_t *disp)
{
    (void)disp;
    if (g_last_committed_idx < 0) return;       /* nothing in flight yet */
    int next_idx = 1 - g_last_committed_idx;
    (void)mc_surface_wait_buf_free(g_surf, next_idx);
}

int lv_port_mc_init(mc_surface_t *surf, int w, int h, int draw_buf_lines)
{
    (void)draw_buf_lines;

    if (!surf || w <= 0 || h <= 0) return -1;
    if (mc_surface_n_buf(surf) != 2) return -1;

    g_surf = surf;
    g_w = w; g_h = h;
    g_dmg_n = 0;
    g_last_committed_idx = -1;

    int s;
    g_buf_addr[0] = (uint8_t *)mc_surface_buf_at(surf, 0, &s);
    g_buf_addr[1] = (uint8_t *)mc_surface_buf_at(surf, 1, NULL);
    if (!g_buf_addr[0] || !g_buf_addr[1]) return -1;
    g_stride = s;
    if (s != w * 4) {
        /* DIRECT mode assumes stride = w * bpp. Surface is allocated that
         * way by the compositor; abort if anything was unexpectedly padded. */
        return -1;
    }

    /* Zero both buffers up front so any pixels LVGL never touches (e.g.
     * outside a popup's rounded card) start out alpha=0. */
    size_t bytes = (size_t)g_stride * h;
    memset(g_buf_addr[0], 0, bytes);
    memset(g_buf_addr[1], 0, bytes);

    lv_tick_set_cb(mc_tick_cb);

    g_disp = lv_display_create(w, h);
    if (!g_disp) return -1;

    lv_display_set_color_format(g_disp, LV_COLOR_FORMAT_ARGB8888);
    /* FULL (not DIRECT) render mode: LVGL repaints the ENTIRE screen into the
     * active buffer every frame.  DIRECT only redraws each frame's invalid
     * areas, relying on LVGL's cross-buffer dirty tracking to keep BOTH mc
     * buffers consistent -- but with our commit/rotate that left stale (never-
     * painted, alpha=0) scanlines in one buffer, which showed as transparent
     * "white lines" over an opaque base (e.g. AWTK).  FULL guarantees every
     * committed buffer is wholly painted.  Still zero-copy (LVGL renders
     * straight into the mc buffer); cost is a full redraw per frame, fine at
     * 800x480.  Holes are still punched post-render in mc_flush_cb. */
    lv_display_set_buffers(g_disp,
                           g_buf_addr[0], g_buf_addr[1],
                           bytes,
                           LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(g_disp, mc_flush_cb);
    lv_display_set_flush_wait_cb(g_disp, mc_flush_wait_cb);
    return 0;
}

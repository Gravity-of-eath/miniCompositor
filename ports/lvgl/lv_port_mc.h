#ifndef LV_PORT_MC_H
#define LV_PORT_MC_H

#include "mc.h"
#include "lvgl.h"

/* Bind an LVGL display to a mc surface. Returns 0 on success.
 *
 * Uses LV_DISPLAY_RENDER_MODE_DIRECT with the surface's two shm buffers as
 * the LVGL draw buffers. LVGL 9 itself keeps the two buffers in sync
 * across frames, so we don't have to copy anything ourselves.
 *
 * `draw_buf_lines` is unused (kept for API stability with the old port).
 */
int lv_port_mc_init(mc_surface_t *surf, int w, int h, int draw_buf_lines);

/* Register an LVGL pointer input device that pulls events from mc_dispatch.
 * The mc context must be the one that owns `surf` from lv_port_mc_init. */
int lv_port_mc_input_init(mc_ctx_t *ctx);

/* Optional: install a callback invoked for any non-touch mc_event_t that
 * the input driver drains from mc_dispatch (e.g. BUS messages). Without
 * this, those events would be silently dropped because the indev read_cb
 * owns the dispatch loop. */
typedef void (*lv_port_mc_event_cb_t)(const mc_event_t *ev, void *user);
void lv_port_mc_set_event_cb(lv_port_mc_event_cb_t cb, void *user);

/* Register a rectangular "hole" in surface coords. After LVGL finishes
 * rendering each frame, the port forces alpha=0 across all registered
 * holes before committing to the compositor. Useful to punch a true
 * see-through window through e.g. a POPUP card, even though LVGL itself
 * has no "clear to transparent" blend mode.
 *
 * Holes are persistent across frames; call lv_port_mc_clear_holes() to
 * reset. Up to 4 holes supported. */
void lv_port_mc_add_hole(int x, int y, int w, int h);
void lv_port_mc_clear_holes(void);

#endif

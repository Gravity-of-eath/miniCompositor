/**
 * File:   input_thread_mc.c
 * Brief:  pumps mc-compositor touch events into AWTK's main loop.
 *
 * Used in place of input_thread/tslib_thread/mouse_thread when the LCD
 * backend is mc (egl_devices/mc). Touch routing, hit-testing and global
 * coordinate -> per-surface translation are all done by the compositor;
 * the events we receive already have surface-local x/y, so we just
 * forward them as AWTK pointer events.
 *
 * Threading: one tk_thread polls mc_dispatch on the shared mc_ctx_t
 * (provided by lcd_linux_mc_get_ctx()). Bus and lifecycle events are
 * drained but not yet routed -- AWTK currently has no consumer for
 * them; lifecycle pause/resume hooks could be added here later.
 */

#ifdef WITH_LCD_LINUX_MC

#include <stdlib.h>
#include <string.h>

#include "tkc/mem.h"
#include "tkc/thread.h"
#include "tkc/platform.h"
#include "base/event_queue.h"
#include "base/keys.h"

#include "input_thread_mc.h"

#include "mc.h"

/* Provided by awtk-port/egl_devices/mc/egl_devices.c — same compositor
 * connection the lcd backend is using, plus the fullscreen surface so we
 * can call mc_surface_request_focus() in response to bus messages, and
 * the hidden-state hint the lcd backend consults to throttle. */
extern mc_ctx_t     *lcd_linux_mc_get_ctx (void);
extern mc_surface_t *lcd_linux_mc_get_surf(void);
extern void          lcd_linux_mc_set_hidden(int h);

/* The "name" this app responds to on the "app/focus" bus topic. Matches
 * the MC_APP_NAME env var that egl_devices_create uses when connecting,
 * so the same identity that shows up in compositor logs is the one the
 * popup's app/focus payload should reference. */
static const char *focus_self_name(void)
{
    const char *s = getenv("MC_APP_NAME");
    return s && *s ? s : "awtk";
}

typedef struct _mc_input_run_t {
    input_dispatch_t dispatch;
    void            *ctx;
    int              w, h;
    volatile int     running;
} mc_input_run_t;

static void emit_pointer(mc_input_run_t *r, uint16_t evt_type,
                         int x, int y, int pressed)
{
    event_queue_req_t req;
    memset(&req, 0, sizeof(req));
    req.event.type             = evt_type;
    req.event.size             = sizeof(req.pointer_event);
    req.pointer_event.x        = x;
    req.pointer_event.y        = y;
    req.pointer_event.pressed  = pressed ? TRUE : FALSE;
    r->dispatch(r->ctx, &req, "mc-input");
}

static void *mc_input_thread_proc(void *arg)
{
    mc_input_run_t *r = arg;
    mc_ctx_t *mc = lcd_linux_mc_get_ctx();
    if (!mc) {
        log_warn("input_thread_mc: no mc ctx, exiting\n");
        return NULL;
    }

    /* Subscribe so the popup-OK app-switcher can bring us forward.
     * Matches what demo-fullscreen (LVGL) does -- both react to the same
     * topic so the popup can cycle between AWTK and any number of LVGL
     * fullscreens uniformly. */
    const char *self = focus_self_name();
    if (mc_bus_subscribe(mc, "app/focus") != 0) {
        log_warn("input_thread_mc: mc_bus_subscribe('app/focus') failed\n");
    } else {
        log_info("input_thread_mc: subscribed to 'app/focus' as '%s'\n", self);
    }

    while (r->running) {
        if (!mc_alive(mc)) {
            log_warn("input_thread_mc: compositor went away\n");
            break;
        }
        mc_event_t ev;
        int n = mc_dispatch(mc, &ev, 0);
        if (n <= 0) {
            /* No event right now. mc_dispatch with timeout_ms=0 is
             * effectively non-blocking; sleep briefly to avoid burning
             * CPU. ~5ms keeps p99 input latency well under one frame. */
            sleep_ms(5);
            continue;
        }
        if (ev.kind == MC_EV_LIFECYCLE) {
            int hide = (ev.lc.state == MC_LIFECYCLE_HIDDEN
                     || ev.lc.state == MC_LIFECYCLE_SUSPENDED);
            lcd_linux_mc_set_hidden(hide);
            log_info("input_thread_mc: lifecycle %s\n",
                     hide ? "HIDDEN (paused)" : "VISIBLE (resumed)");
            continue;
        }
        if (ev.kind == MC_EV_BUS) {
            if (ev.bus.topic && strcmp(ev.bus.topic, "app/focus") == 0) {
                /* Payload is the target app's name. NUL-terminated per
                 * libmc's contract, but only valid until the next
                 * mc_dispatch -- safe to use here in the same iteration. */
                if (ev.bus.data && ev.bus.len > 0
                        && (size_t)ev.bus.len == strlen(self)
                        && memcmp(ev.bus.data, self, ev.bus.len) == 0) {
                    mc_surface_t *surf = lcd_linux_mc_get_surf();
                    if (surf) {
                        int rc = mc_surface_request_focus(surf);
                        log_info("input_thread_mc: app/focus -> request_focus(%s) = %d\n",
                                 self, rc);
                    }
                }
            }
            continue;
        }
        if (ev.kind != MC_EV_TOUCH) {
            /* lifecycle, frame_done, buffer_free etc -- not yet handled */
            continue;
        }
        int x = ev.touch.x, y = ev.touch.y;
        if (x < 0) x = 0; if (x >= r->w) x = r->w - 1;
        if (y < 0) y = 0; if (y >= r->h) y = r->h - 1;
        switch (ev.touch.state) {
        case MC_TOUCH_STATE_DOWN:
            emit_pointer(r, EVT_POINTER_DOWN, x, y, 1); break;
        case MC_TOUCH_STATE_MOVE:
            emit_pointer(r, EVT_POINTER_MOVE, x, y, 1); break;
        case MC_TOUCH_STATE_UP:
            emit_pointer(r, EVT_POINTER_UP,   x, y, 0); break;
        default: break;
        }
    }
    return NULL;
}

tk_thread_t *input_thread_mc_run(input_dispatch_t dispatch, void *ctx,
                                 int32_t w, int32_t h)
{
    mc_input_run_t *r = TKMEM_ZALLOC(mc_input_run_t);
    if (!r) return NULL;
    r->dispatch = dispatch;
    r->ctx      = ctx;
    r->w        = w;
    r->h        = h;
    r->running  = 1;

    tk_thread_t *t = tk_thread_create(mc_input_thread_proc, r);
    if (!t) { TKMEM_FREE(r); return NULL; }
    tk_thread_start(t);
    return t;
}

#endif /* WITH_LCD_LINUX_MC */

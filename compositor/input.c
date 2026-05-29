#define _GNU_SOURCE
#include "input.h"
#include "transport.h"
#include "surface.h"
#include "proto.h"
#include "log.h"

#include <string.h>
#include <stdint.h>

struct mc_surface *mc_input_hit_test(struct mc_server *s, int x, int y)
{
    /* gather visible surfaces, sort z desc, pick first that contains pt */
    struct mc_surface *list[MC_MAX_SURFACES];
    int n = 0;
    for (int i = 0; i < MC_MAX_SURFACES; i++) {
        if (s->surfaces[i].sid && s->surfaces[i].visible) list[n++] = &s->surfaces[i];
    }
    /* top -> bottom: z desc, then focus_stamp desc (recent focus on top) */
    for (int i = 1; i < n; i++) {
        struct mc_surface *cur = list[i];
        int j = i;
        while (j > 0 &&
               (list[j-1]->z_order <  cur->z_order ||
                (list[j-1]->z_order == cur->z_order &&
                 list[j-1]->focus_stamp < cur->focus_stamp))) {
            list[j] = list[j - 1]; j--;
        }
        list[j] = cur;
    }
    for (int i = 0; i < n; i++) {
        struct mc_surface *sf = list[i];
        int sx0 = sf->x, sy0 = sf->y;
        int sx1 = sx0 + sf->w, sy1 = sy0 + sf->h;
        if (x >= sx0 && x < sx1 && y >= sy0 && y < sy1) return sf;
    }
    return NULL;
}

static struct mc_client *client_of(struct mc_server *s, struct mc_surface *sf)
{
    if (!sf) return NULL;
    if (sf->client_slot < 0 || sf->client_slot >= MC_MAX_CLIENTS) return NULL;
    struct mc_client *c = &s->clients[sf->client_slot];
    if (c->sock <= 0 || c->cid != sf->cid) return NULL;
    return c;
}

static int send_input(struct mc_client *c,
                      uint32_t sid, uint8_t type,
                      int16_t local_x, int16_t local_y, uint32_t t_ms)
{
    uint8_t buf[128];
    struct mc_builder b;
    mc_builder_init(&b, buf, sizeof(buf));
    mc_put_u32(&b, MC_T_SID,         sid);
    mc_put_u8 (&b, MC_T_INPUT_TYPE,  type);
    mc_put_i16(&b, MC_T_INPUT_X,     local_x);
    mc_put_i16(&b, MC_T_INPUT_Y,     local_y);
    mc_put_u8 (&b, MC_T_INPUT_SLOT,  0);
    mc_put_u32(&b, MC_T_INPUT_TIME,  t_ms);
    size_t flen = mc_builder_finalize(&b, MC_SV_INPUT, 0 /* async */);
    return mc_send_frame(c->sock, buf, flen, NULL, 0);
}

void mc_input_on_touch(mc_touch_state_t st, int sx, int sy, void *user)
{
    struct mc_server *s = user;

    /* On DOWN, lock target. On MOVE/UP, dispatch to grabbed target if any
     * (drag-outside semantics). */
    struct mc_surface *target = NULL;
    if (st == MC_TOUCH_DOWN) {
        target = mc_input_hit_test(s, sx, sy);
        if (target) {
            s->input.grabbed_sid = target->sid;
            /* INFO so users can see routing decisions in -v logs without
             * needing debug verbosity. Helpful when a touch event
             * "doesn't seem to do anything" -- you can confirm which
             * surface owned the touch (it may have hit a widget the demo
             * just didn't bind a handler to). */
            LOG_I("input: DOWN (%d,%d) -> sid=%u (role=%u z=%d)",
                  sx, sy, target->sid, target->role, target->z_order);
        } else {
            LOG_I("input: DOWN (%d,%d) -> no surface hit", sx, sy);
            return;
        }
    } else {
        if (s->input.grabbed_sid == 0) return;
        target = mc_surface_get(s, s->input.grabbed_sid);
        if (!target) { s->input.grabbed_sid = 0; return; }
    }

    struct mc_client *c = client_of(s, target);
    if (!c) {
        s->input.grabbed_sid = 0;
        return;
    }

    int16_t lx = (int16_t)(sx - target->x);
    int16_t ly = (int16_t)(sy - target->y);

    /* timestamp = ms since server start (cheap, monotonic enough) */
    static struct timespec t0;
    static int t0_init;
    if (!t0_init) { clock_gettime(CLOCK_MONOTONIC, &t0); t0_init = 1; }
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    uint32_t t_ms = (uint32_t)(
        ((int64_t)(t.tv_sec - t0.tv_sec) * 1000)
      + ((int64_t)(t.tv_nsec - t0.tv_nsec) / 1000000)
    );

    uint8_t type = (st == MC_TOUCH_DOWN) ? 1 : (st == MC_TOUCH_MOVE ? 2 : 3);
    (void)send_input(c, target->sid, type, lx, ly, t_ms);

    if (st == MC_TOUCH_UP) {
        LOG_I("input: UP   -> sid=%u local=(%d,%d)",
              target->sid, lx, ly);
        s->input.grabbed_sid = 0;
    }
}

#ifndef MC_INPUT_H
#define MC_INPUT_H

#include "ial_evdev.h"

struct mc_server;

/* Touch state shared with the input layer (managed by input.c). */
struct mc_input_state {
    uint32_t grabbed_sid;   /* surface that owns the current down-up sequence,
                             * 0 if no finger down. */
};

/* Hit-test (x,y) against visible surfaces top-down, returns matching surface
 * or NULL. Caller uses sf->x / sf->w / sf->h. */
struct mc_surface *mc_input_hit_test(struct mc_server *s, int x, int y);

/* Forwarder used as IAL callback. void *user must point to a mc_server. */
void mc_input_on_touch(mc_touch_state_t st, int sx, int sy, void *user);

#endif

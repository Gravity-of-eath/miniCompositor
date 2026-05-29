#define _GNU_SOURCE
#include "lifecycle.h"
#include "transport.h"
#include "surface.h"
#include "proto.h"
#include "log.h"

#include <string.h>

static int contains(const struct mc_surface *outer, const struct mc_surface *inner)
{
    int ox0 = outer->x, oy0 = outer->y;
    int ox1 = ox0 + outer->w, oy1 = oy0 + outer->h;
    int ix0 = inner->x, iy0 = inner->y;
    int ix1 = ix0 + inner->w, iy1 = iy0 + inner->h;
    return ox0 <= ix0 && oy0 <= iy0 && ox1 >= ix1 && oy1 >= iy1;
}

/* Does any other surface fully cover `s` with an opaque rect? */
static int is_occluded(const struct mc_server *srv, const struct mc_surface *s)
{
    for (int i = 0; i < MC_MAX_SURFACES; i++) {
        const struct mc_surface *t = &srv->surfaces[i];
        if (t == s) continue;
        if (t->sid == 0 || !t->visible) continue;
        /* A surface that hasn't committed even one frame yet is not a
         * useful occluder -- if we mark `s` HIDDEN on the strength of an
         * empty `t`, the compose loop will paint nothing on top of `s`
         * for ~50-100ms until `t`'s first commit arrives, leaving a
         * visible flash where the previous fullscreen briefly remains
         * before the new one takes over. Wait until `t` has real content. */
        if (t->cur_scanout < 0) continue;
        /* "Higher" matches the compose / hit-test ordering:
         * z_order asc, then focus_stamp asc means top of stack has the
         * largest (z, focus_stamp) tuple. So `t` occludes `s` iff
         * (t.z, t.stamp) > (s.z, s.stamp). */
        if (t->z_order < s->z_order) continue;
        if (t->z_order == s->z_order && t->focus_stamp <= s->focus_stamp) continue;
        /* Only opaque roles count as occluders. FULLSCREEN is treated
         * as opaque (it's our convention; nothing in the protocol forbids
         * a fullscreen surface from having alpha pixels, but in practice
         * dashboards don't). POPUP / BG don't occlude. */
        if (t->role != 1 /* FULLSCREEN */) continue;
        if (contains(t, s)) return 1;
    }
    return 0;
}

static struct mc_client *client_for(struct mc_server *srv, struct mc_surface *s)
{
    if (s->client_slot < 0 || s->client_slot >= MC_MAX_CLIENTS) return NULL;
    struct mc_client *c = &srv->clients[s->client_slot];
    if (c->sock <= 0 || c->cid != s->cid) return NULL;
    return c;
}

static void emit(struct mc_client *c, uint32_t sid, uint8_t state)
{
    if (!c) return;
    uint8_t buf[64];
    struct mc_builder b;
    mc_builder_init(&b, buf, sizeof(buf));
    mc_put_u32(&b, MC_T_SID,      sid);
    mc_put_u8 (&b, MC_T_LC_STATE, state);
    size_t flen = mc_builder_finalize(&b, MC_SV_LIFECYCLE, 0 /* async */);
    (void)mc_send_frame(c->sock, buf, flen, NULL, 0);
}

static const char *name_of(uint8_t st)
{
    switch (st) {
    case MC_LC_VISIBLE:   return "VISIBLE";
    case MC_LC_HIDDEN:    return "HIDDEN";
    case MC_LC_SUSPENDED: return "SUSPENDED";
    case MC_LC_RESUMED:   return "RESUMED";
    default:              return "?";
    }
}

void mc_lifecycle_recompute(struct mc_server *srv)
{
    for (int i = 0; i < MC_MAX_SURFACES; i++) {
        struct mc_surface *s = &srv->surfaces[i];
        if (s->sid == 0) continue;

        uint8_t want = is_occluded(srv, s) ? MC_LC_HIDDEN : MC_LC_VISIBLE;
        if (want == s->lc_state) continue;       /* no change, no spam */

        uint8_t prev = s->lc_state;
        s->lc_state = want;
        LOG_I("lifecycle: sid=%u %s -> %s",
              s->sid,
              prev ? name_of(prev) : "(init)",
              name_of(want));
        emit(client_for(srv, s), s->sid, want);
    }
}

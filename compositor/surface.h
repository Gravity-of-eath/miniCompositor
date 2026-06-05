#ifndef MC_SURFACE_H
#define MC_SURFACE_H

#include <stdint.h>
#include <sys/types.h>

#define MC_MAX_SURFACES    32
#define MC_BUFS_PER_SURF   2
#define MC_MAX_DAMAGE_RECTS 8

/* Buffer states (server-side authoritative). */
enum { MC_BUF_FREE = 0, MC_BUF_READY = 1, MC_BUF_SCANOUT = 2 };

/* Surface lifecycle state. Sent to clients as SV_LIFECYCLE.
 *   VISIBLE: surface is on-screen (possibly partly occluded — still VISIBLE)
 *   HIDDEN:  surface is fully occluded by an opaque surface above it;
 *            client can skip rendering to save CPU until VISIBLE again
 *   SUSPENDED / RESUMED: reserved for future resource-pressure policy,
 *            not emitted by the compositor today
 */
enum {
    MC_LC_VISIBLE   = 1,
    MC_LC_HIDDEN    = 2,
    MC_LC_SUSPENDED = 3,
    MC_LC_RESUMED   = 4,
};

struct mc_rect_i {
    int16_t x, y, w, h;
};

struct mc_buf {
    int      shm_fd;        /* dma-buf / ion / memfd / g2d fd; sent to
                             * client and also kept locally; closed (or
                             * sel-released, for g2d-mem) on destroy */
    uint8_t *map;
    size_t   size;
    uint32_t phys;          /* 0 if allocator didn't provide one */
    uint32_t mmap_offset;   /* offset client must pass to mmap() to reach
                             * this buffer through the SCM_RIGHTS-passed
                             * fd. memfd ⇒ 0; g2d-mem ⇒ phys. */
    int      state;
    /* book-keeping for the allocator's free path (e.g. g2d slot index) */
    int      alloc_handle;
};

struct mc_surface {
    uint32_t sid;           /* 0 = unused slot */
    uint32_t cid;           /* owning client id */
    int      client_slot;   /* index into server.clients */

    uint16_t w, h;
    uint32_t stride;        /* bytes */
    uint8_t  format;        /* MC_FMT_* */
    uint8_t  role;          /* 1=fullscreen 2=popup 3=bg */
    uint8_t  modal;
    int16_t  x, y;          /* popup position (fullscreen forced to 0,0) */
    int      z_order;
    /* Monotonic per-server counter; ties between surfaces of the same
     * z_order are broken by larger focus_stamp == higher. Every new
     * surface gets the next stamp (so it appears on top by default),
     * and CL_REQUEST_FOCUS bumps the stamp again so a previously-back
     * surface jumps back to the front. */
    uint32_t focus_stamp;
    int      visible;       /* legacy flag: 0 means do-not-render at all */
    uint8_t  flip_y;        /* 1: client buffers are bottom-up (GL FBO origin).
                             * Compositor inverts src_y in blit/blend. */
    uint8_t  lc_state;      /* current MC_LC_*, last value sent to client.
                             * 0 = no event sent yet (initial) */

    int      n_buf;
    struct mc_buf bufs[MC_BUFS_PER_SURF];
    int      event_fd;      /* notify client when a buffer becomes FREE */

    int      cur_scanout;   /* -1 if never committed */
    int      pending_idx;   /* -1 if no pending commit */

    /* Damage rects attached to the pending commit (surface-local coords).
     * Empty list means "no commit pending" or "no damage info, assume full". */
    int               pending_dmg_n;
    struct mc_rect_i  pending_dmg[MC_MAX_DAMAGE_RECTS];

    /* Bounding box of dmg from the last presented frame, in surface-local
     * coords. Used by compose to keep both fb halves consistent: with a
     * double-buffered fb, the back half last received content from the
     * frame BEFORE the previous one, so each compose must re-blit the
     * previous frame's damage as well. Tracking this PER surface (rather
     * than globally) avoids the bbox blow-up when two surfaces have
     * damage in opposite corners of the screen. */
    int               prev_dmg_valid;
    struct mc_rect_i  prev_dmg;
};

struct mc_server;  /* fwd */

void mc_surface_table_init(struct mc_server *s);

/* Create surface for client (allocates buffers + eventfd). */
struct mc_surface *mc_surface_create(struct mc_server *s,
                                     int client_slot, uint32_t cid,
                                     uint16_t w, uint16_t h,
                                     uint8_t format, uint8_t role,
                                     uint8_t n_buf, int *out_err);

/* Lookup. */
struct mc_surface *mc_surface_get(struct mc_server *s, uint32_t sid);

/* Default z-order for a role (FULLSCREEN=10, POPUP=100, BG=0, TOAST=200).
 * Shared by surface creation and SET_ROLE handling so the two never drift. */
int mc_surface_default_z_order(uint8_t role);

/* Destroy + release everything. */
void mc_surface_destroy(struct mc_server *s, uint32_t sid);

/* Release all surfaces owned by a given client (on disconnect). */
void mc_surface_destroy_for_client(struct mc_server *s, int client_slot);

/* On COMMIT: claim buffer idx, mark READY, set pending.
 * dmg/n_dmg may be NULL/0 ("no damage info, treat as full surface"). */
int mc_surface_commit(struct mc_surface *sf, int buf_idx,
                      const struct mc_rect_i *dmg, int n_dmg);

/* Walk visible surfaces in z-order ascending (bottom -> top). cb returns 0 to continue, !=0 to stop. */
void mc_surface_foreach_visible(struct mc_server *s,
                                int (*cb)(struct mc_surface *, void *),
                                void *user);

#endif

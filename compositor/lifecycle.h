/*
 * Surface lifecycle:
 *   recompute occlusion-based visibility for every surface in the server
 *   and emit SV_LIFECYCLE messages on transitions.
 *
 * Visibility rule (simple, good enough for current demos):
 *   A surface S is HIDDEN iff some other surface T has T.z_order > S.z_order
 *   AND T is OPAQUE (role == FULLSCREEN) AND T's screen rect fully covers
 *   S's screen rect. Otherwise S is VISIBLE.
 *
 *   This means:
 *     - A POPUP over a FULLSCREEN leaves the FULLSCREEN VISIBLE (popup is
 *       smaller / has alpha)
 *     - A new FULLSCREEN pushed to a higher z makes the old FULLSCREEN
 *       HIDDEN  (occlusion holds for the whole screen)
 *
 * Call this whenever surface set / z / role / position changes. Cheap
 * (O(n^2) on surface count; n is tiny). Doesn't need to be called every
 * frame.
 */
#ifndef MC_LIFECYCLE_H
#define MC_LIFECYCLE_H

struct mc_server;

void mc_lifecycle_recompute(struct mc_server *s);

#endif

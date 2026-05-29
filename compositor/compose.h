#ifndef MC_COMPOSE_H
#define MC_COMPOSE_H

struct mc_server;
struct mc_backend;

/* Recompose all visible surfaces into backend's back-buffer and present.
 * Also rotates each surface's scanout: previous scanout buffer is released
 * back to FREE and its eventfd is signaled.
 */
void mc_compose_frame(struct mc_server *s, struct mc_backend *be);

/* Force a recompose after a topology change (surface destroyed, role changed,
 * z-order changed). Runs compose twice so that both halves of a double-buffered
 * framebuffer get refreshed -- otherwise the previously-displayed half would
 * pop back on the next page flip and "ghost" the old content. */
void mc_request_recompose(struct mc_server *s);

#endif

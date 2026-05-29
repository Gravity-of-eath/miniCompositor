#ifndef MC_TRANSPORT_H
#define MC_TRANSPORT_H

#include <stdint.h>
#include <sys/types.h>
#include "proto.h"
#include "surface.h"
#include "ial_evdev.h"
#include "input.h"
#include "bus.h"

#define MC_MAX_CLIENTS 16

struct mc_client {
    int      sock;
    uint32_t cid;         /* assigned client id (1-based, 0=unused) */
    pid_t    pid;
    char     name[64];
    uint32_t caps;
    int      hello_done;

    struct mc_bus_subs bus;
};

struct mc_backend;

struct mc_server {
    int listen_fd;
    int epoll_fd;

    /* screen info reported in WELCOME */
    uint16_t screen_w;
    uint16_t screen_h;
    uint8_t  screen_format;
    uint32_t server_caps;
    int      fb_stride;            /* set by backend after open */

    struct mc_backend *backend;

    struct mc_client clients[MC_MAX_CLIENTS];
    uint32_t next_cid;

    struct mc_surface surfaces[MC_MAX_SURFACES];
    uint32_t next_sid;
    uint32_t next_focus_stamp;

    /* Input */
    struct mc_ial         ial;
    int                   ial_open;
    struct mc_input_state input;

    uint8_t *rxbuf;
    size_t   rxcap;

    /* Set by handle_commit when a non-first-commit lands; cleared by
     * mc_server_run after the per-batch deferred compose runs. Coalesces
     * the multi-client commit storm (popup blink + 2 fullscreens) into
     * one compose per epoll wakeup instead of one per commit. */
    int      compose_needed;

    /* When the active backend is the GPU (EGL on /dev/fb0), compose.c
     * skips all CPU blit/blend paths and calls the backend's
     * begin_frame/draw_surface/end_frame entry points directly. */
    int      gpu_compose;
};

int  mc_server_init(struct mc_server *s, const char *sock_path);
void mc_server_run (struct mc_server *s);
void mc_server_fini(struct mc_server *s);

#endif

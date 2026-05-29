#ifndef MC_INTERNAL_H
#define MC_INTERNAL_H

#include "mc.h"
#include "proto.h"

#define MC_CLI_MAX_BUFS 2

struct mc_cli_buf {
    void   *map;
    size_t  size;
    int     shm_fd;
    int     state;        /* 0=FREE 1=CLIENT_OWN 2=BUSY (committed) */
};

struct mc_surface {
    mc_ctx_t *ctx;
    uint32_t  sid;
    uint16_t  w, h;
    uint32_t  stride;
    uint8_t   format;
    uint8_t   role;

    int       n_buf;
    struct mc_cli_buf bufs[MC_CLI_MAX_BUFS];
    int       event_fd;
    int       cur_writing;   /* index acquired but not yet committed; -1 if none */
    int       next_acquire;  /* round-robin index for next acquire */
};

struct mc_ctx {
    int      sock;
    uint32_t next_serial;
    char     name[64];

    mc_screen_info_t scr;
    int      have_scr;

    uint8_t *rxbuf;
    size_t   rxcap;

    /* 1 while the socket is healthy; flipped to 0 by any I/O path that
     * observes the peer is gone. Once 0, blocking calls return immediately. */
    int      alive;

    /* For Phase 0: at most 1 surface per context. */
    mc_surface_t *surface;

    /* Stable scratch space for the NUL-terminated topic/sender returned in
     * mc_event_t.bus.*. Overwritten on each dispatch. */
    char ev_topic[64];
    char ev_sender[64];
};

void mc_internal_mark_dead(mc_ctx_t *c, const char *why);

extern int g_mc_last_error;
void mc_set_last_error(int err);
const char *mc_internal_socket_path(void);

/* receive helper: reads next frame, returns frame length. */
ssize_t mc_internal_recv(mc_ctx_t *c, int *fds, int *n_fds);

#endif

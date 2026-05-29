#define _GNU_SOURCE
#include "internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#define RX_CAP (MC_HDR_BYTES + MC_MAX_PAYLOAD)

static int connect_unix(const char *path)
{
    int s = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (s < 0) return -errno;

    struct sockaddr_un sa;
    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, path, sizeof(sa.sun_path) - 1);

    if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        int e = -errno;
        close(s);
        return e;
    }
    return s;
}

ssize_t mc_internal_recv(mc_ctx_t *c, int *fds, int *n_fds)
{
    return mc_recv_frame(c->sock, c->rxbuf, c->rxcap, fds, n_fds);
}

static int do_hello(mc_ctx_t *c)
{
    uint8_t buf[256];
    struct mc_builder b;
    mc_builder_init(&b, buf, sizeof(buf));
    mc_put_str(&b, MC_T_NAME, c->name);
    mc_put_u32(&b, MC_T_VERSION, MC_PROTO_VERSION);
    mc_put_u32(&b, MC_T_PID, (uint32_t)getpid());
    mc_put_u32(&b, MC_T_CAPS, 0);

    uint32_t serial = ++c->next_serial;
    size_t   flen   = mc_builder_finalize(&b, MC_CL_HELLO, serial);
    if (flen == 0) return MC_E_INTERNAL;

    if (mc_send_frame(c->sock, buf, flen, NULL, 0) < 0) return MC_E_PROTO;

    int fds[MC_MAX_FDS]; int n_fds = 0;
    ssize_t got = mc_internal_recv(c, fds, &n_fds);
    if (got <= 0) return MC_E_PROTO;
    for (int i = 0; i < n_fds; i++) close(fds[i]);

    uint16_t type = (uint16_t)c->rxbuf[2] | ((uint16_t)c->rxbuf[3] << 8);
    uint32_t plen = (uint32_t)c->rxbuf[4]
                  | ((uint32_t)c->rxbuf[5] << 8)
                  | ((uint32_t)c->rxbuf[6] << 16)
                  | ((uint32_t)c->rxbuf[7] << 24);
    const void *payload = c->rxbuf + MC_HDR_BYTES;

    if (type == MC_SV_ERROR) {
        uint32_t code = MC_E_INTERNAL;
        mc_get_u32(payload, plen, MC_T_CODE, &code);
        return (int)code;
    }
    if (type != MC_SV_WELCOME) return MC_E_PROTO;

    uint32_t cid = 0, scaps = 0;
    uint16_t w = 0, h = 0;
    uint8_t  fmt = 0;
    mc_get_u32(payload, plen, MC_T_CLIENT_ID,    &cid);
    mc_get_u16(payload, plen, MC_T_WIDTH,        &w);
    mc_get_u16(payload, plen, MC_T_HEIGHT,       &h);
    mc_get_u8 (payload, plen, MC_T_FORMAT,       &fmt);
    mc_get_u32(payload, plen, MC_T_SERVER_CAPS,  &scaps);

    c->scr.client_id     = cid;
    c->scr.screen_w      = w;
    c->scr.screen_h      = h;
    c->scr.screen_format = fmt;
    c->scr.server_caps   = scaps;
    c->have_scr = 1;
    return MC_OK;
}

mc_ctx_t *mc_connect(const char *app_name)
{
    mc_set_last_error(MC_OK);
    mc_ctx_t *c = calloc(1, sizeof(*c));
    if (!c) { mc_set_last_error(MC_E_NOMEM); return NULL; }

    c->rxcap = RX_CAP;
    c->rxbuf = malloc(c->rxcap);
    if (!c->rxbuf) { free(c); mc_set_last_error(MC_E_NOMEM); return NULL; }

    if (app_name) {
        size_t nl = strnlen(app_name, sizeof(c->name) - 1);
        memcpy(c->name, app_name, nl); c->name[nl] = '\0';
    } else {
        strcpy(c->name, "anon");
    }

    int s = connect_unix(mc_internal_socket_path());
    if (s < 0) {
        free(c->rxbuf); free(c);
        mc_set_last_error(MC_E_NOENT); return NULL;
    }
    c->sock = s;
    c->next_serial = 0;
    c->alive = 1;

    int r = do_hello(c);
    if (r != MC_OK) {
        close(c->sock); free(c->rxbuf); free(c);
        mc_set_last_error(r); return NULL;
    }
    return c;
}

void mc_disconnect(mc_ctx_t *c)
{
    if (!c) return;
    if (c->surface) mc_surface_destroy(c->surface);

    uint8_t buf[MC_HDR_BYTES];
    struct mc_builder b;
    mc_builder_init(&b, buf, sizeof(buf));
    size_t flen = mc_builder_finalize(&b, MC_CL_BYE, ++c->next_serial);
    if (flen > 0) (void)mc_send_frame(c->sock, buf, flen, NULL, 0);

    close(c->sock);
    free(c->rxbuf);
    free(c);
}

int mc_fd(mc_ctx_t *c) { return c ? c->sock : -1; }

int mc_get_screen_info(mc_ctx_t *c, mc_screen_info_t *out)
{
    if (!c || !out || !c->have_scr) return -1;
    *out = c->scr;
    return 0;
}

uint32_t mc_internal_next_serial(mc_ctx_t *c) { return ++c->next_serial; }

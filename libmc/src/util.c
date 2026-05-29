#define _GNU_SOURCE
#include "internal.h"

#include <stdio.h>
#include <string.h>

int g_mc_last_error = 0;

void mc_set_last_error(int err) { g_mc_last_error = err; }
int  mc_last_error(void) { return g_mc_last_error; }

const char *mc_strerror(int err)
{
    switch (err) {
    case MC_OK:         return "ok";
    case MC_E_PROTO:    return "protocol error";
    case MC_E_INVAL:    return "invalid argument";
    case MC_E_NOMEM:    return "out of memory";
    case MC_E_NOENT:    return "no such resource";
    case MC_E_BUSY:     return "resource busy";
    case MC_E_PERM:     return "permission denied";
    case MC_E_TOOLARGE: return "too large";
    case MC_E_NOTSUP:   return "not supported";
    case MC_E_INTERNAL: return "internal error";
    default:            return "unknown";
    }
}

/* socket path override */
static char g_sock_path[256] = "/var/run/mc.sock";

void mc_set_socket_path(const char *p)
{
    if (p) {
        strncpy(g_sock_path, p, sizeof(g_sock_path) - 1);
        g_sock_path[sizeof(g_sock_path) - 1] = '\0';
    }
}

const char *mc_internal_socket_path(void) { return g_sock_path; }

void mc_internal_mark_dead(mc_ctx_t *c, const char *why)
{
    if (!c) return;
    if (c->alive) {
        c->alive = 0;
        fprintf(stderr, "[mc] connection to compositor lost: %s\n",
                why ? why : "(unknown)");
    }
}

int mc_alive(mc_ctx_t *c) { return c ? c->alive : 0; }

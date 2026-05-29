/*
 * mc-test-client: Phase 0 demo.
 *   - Connect to compositor via libmc
 *   - Receive WELCOME with screen info
 *   - Print info and disconnect
 */
#define _GNU_SOURCE
#include "mc.h"

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [options]\n"
        "  -s, --socket PATH    socket path (default /var/run/mc.sock)\n"
        "  -n, --name NAME      app name reported in HELLO\n"
        "      --hold SEC       hold connection for N seconds before BYE\n"
        "      --help\n",
        prog);
}

int main(int argc, char **argv)
{
    const char *sock_path = NULL;
    const char *name      = "test-client";
    int hold = 0;

    static const struct option opts[] = {
        {"socket", required_argument, 0, 's'},
        {"name",   required_argument, 0, 'n'},
        {"hold",   required_argument, 0,  1 },
        {"help",   no_argument,       0,  0 },
        {0,0,0,0}
    };
    int c, idx;
    while ((c = getopt_long(argc, argv, "s:n:", opts, &idx)) != -1) {
        switch (c) {
        case 's': sock_path = optarg; break;
        case 'n': name = optarg; break;
        case 1:   hold = atoi(optarg); break;
        case 0:   usage(argv[0]); return 0;
        default:  usage(argv[0]); return 1;
        }
    }

    if (sock_path) mc_set_socket_path(sock_path);

    printf("[client] connecting as '%s'...\n", name);
    mc_ctx_t *ctx = mc_connect(name);
    if (!ctx) {
        int e = mc_last_error();
        fprintf(stderr, "[client] mc_connect failed: %s (%d)\n",
                mc_strerror(e), e);
        return 1;
    }

    mc_screen_info_t scr;
    if (mc_get_screen_info(ctx, &scr) != 0) {
        fprintf(stderr, "[client] no screen info??\n");
        mc_disconnect(ctx);
        return 1;
    }

    printf("[client] handshake OK:\n");
    printf("           client_id     = %u\n", scr.client_id);
    printf("           screen_w/h    = %u x %u\n", scr.screen_w, scr.screen_h);
    printf("           screen_format = %u\n", scr.screen_format);
    printf("           server_caps   = 0x%08x\n", scr.server_caps);

    if (hold > 0) {
        printf("[client] holding %d s...\n", hold);
        sleep((unsigned)hold);
    }

    printf("[client] disconnecting.\n");
    mc_disconnect(ctx);
    return 0;
}

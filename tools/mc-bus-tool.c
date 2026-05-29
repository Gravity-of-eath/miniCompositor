/*
 * mc-bus-tool: tiny pub/sub CLI for testing the bus.
 *
 *   mc-bus-tool sub TOPIC               subscribe and print received messages
 *   mc-bus-tool pub TOPIC PAYLOAD       publish then exit
 */
#define _GNU_SOURCE
#include "mc.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t s_running = 1;
static void on_sig(int sig) { (void)sig; s_running = 0; }

int main(int argc, char **argv)
{
    /* Allow MC_SOCKET env override (also accept '--socket' flag if present). */
    const char *sock_env = getenv("MC_SOCKET");
    if (sock_env && *sock_env) mc_set_socket_path(sock_env);

    int argi = 1;
    if (argi < argc && strcmp(argv[argi], "--socket") == 0 && argi + 1 < argc) {
        mc_set_socket_path(argv[argi + 1]);
        argi += 2;
    }
    if (argc - argi < 2) {
        fprintf(stderr, "Usage:\n"
                "  %s [--socket PATH] sub TOPIC\n"
                "  %s [--socket PATH] pub TOPIC PAYLOAD\n"
                "  MC_SOCKET env also accepted\n", argv[0], argv[0]);
        return 1;
    }
    const char *cmd   = argv[argi];
    const char *topic = argv[argi + 1];
    int payload_idx   = argi + 2;

    signal(SIGINT,  on_sig);
    signal(SIGTERM, on_sig);

    mc_ctx_t *ctx = mc_connect("bus-tool");
    if (!ctx) {
        fprintf(stderr, "connect: %s\n", mc_strerror(mc_last_error()));
        return 1;
    }

    if (strcmp(cmd, "pub") == 0) {
        const char *payload = (payload_idx < argc) ? argv[payload_idx] : "";
        int r = mc_bus_publish(ctx, topic, payload, (uint32_t)strlen(payload));
        if (r != 0) {
            fprintf(stderr, "publish failed: %s\n", mc_strerror(r));
            mc_disconnect(ctx);
            return 1;
        }
        printf("published topic='%s' payload='%s'\n", topic, payload);
        /* Give the kernel a moment to flush the socket before BYE. */
        usleep(50 * 1000);
        mc_disconnect(ctx);
        return 0;
    }

    if (strcmp(cmd, "sub") == 0) {
        if (mc_bus_subscribe(ctx, topic) != 0) {
            fprintf(stderr, "subscribe failed: %s\n",
                    mc_strerror(mc_last_error()));
            mc_disconnect(ctx);
            return 1;
        }
        printf("subscribed to '%s' (Ctrl-C to exit)\n", topic);

        while (s_running && mc_alive(ctx)) {
            mc_event_t ev;
            int r = mc_dispatch(ctx, &ev, 0);
            if (r < 0) break;
            if (r > 0 && ev.kind == MC_EV_BUS) {
                printf("[bus] topic='%s' sender='%s' payload(%u)=",
                       ev.bus.topic, ev.bus.sender, ev.bus.len);
                fwrite(ev.bus.data, 1, ev.bus.len, stdout);
                printf("\n");
                fflush(stdout);
            } else {
                usleep(20 * 1000);
            }
        }
        mc_disconnect(ctx);
        return 0;
    }

    fprintf(stderr, "unknown command: %s\n", cmd);
    mc_disconnect(ctx);
    return 1;
}

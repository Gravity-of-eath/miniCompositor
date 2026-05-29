#define _GNU_SOURCE
#include "transport.h"
#include "backend.h"
#include "log.h"
#include "mc_alloc.h"

#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

int g_log_level = 2;
volatile sig_atomic_t g_running = 1;

static void on_sig(int sig) { (void)sig; g_running = 0; }

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [options]\n"
        "  -s, --socket PATH      socket path (default /var/run/mc.sock)\n"
        "  -w, --width N          screen width  (default 800)\n"
        "  -h, --height N         screen height (default 480)\n"
        "  -b, --backend NAME     'fb' (CPU), 'egl' (Mali GPU on fb0), or 'ppm' (default: fb)\n"
        "  -o, --output PATH      backend arg (fb device or ppm path)\n"
        "  -i, --input PATH       input event device (default: autodetect)\n"
        "      --no-input         disable touch input\n"
        "      --swap-xy          swap touch X/Y axes\n"
        "      --invert-x         invert touch X axis\n"
        "      --invert-y         invert touch Y axis\n"
        "  -v, --verbose          increase log level\n"
        "  -q, --quiet            decrease log level\n"
        "      --help\n",
        prog);
}

int main(int argc, char **argv)
{
    const char *sock_path = "/var/run/mc.sock";
    const char *backend_name = "fb";
    const char *backend_arg  = NULL;
    const char *input_path = NULL;   /* NULL = autodetect, "" = disable */
    int w = 800, h = 480;

    static const struct option opts[] = {
        {"socket",   required_argument, 0, 's'},
        {"width",    required_argument, 0, 'w'},
        {"height",   required_argument, 0, 'h'},
        {"backend",  required_argument, 0, 'b'},
        {"output",   required_argument, 0, 'o'},
        {"input",    required_argument, 0, 'i'},
        {"no-input", no_argument,       0,  1 },
        {"swap-xy",  no_argument,       0,  2 },
        {"invert-x", no_argument,       0,  3 },
        {"invert-y", no_argument,       0,  4 },
        {"verbose",  no_argument,       0, 'v'},
        {"quiet",    no_argument,       0, 'q'},
        {"help",     no_argument,       0,  0 },
        {0,0,0,0}
    };

    int input_disabled = 0;
    struct mc_ial_cal cal = {0};
    int c, idx;
    while ((c = getopt_long(argc, argv, "s:w:h:b:o:i:vq", opts, &idx)) != -1) {
        switch (c) {
        case 's': sock_path = optarg; break;
        case 'w': w = atoi(optarg); break;
        case 'h': h = atoi(optarg); break;
        case 'b': backend_name = optarg; break;
        case 'o': backend_arg = optarg; break;
        case 'i': input_path = optarg; break;
        case 1:   input_disabled = 1; break;
        case 2:   cal.swap_xy = 1; break;
        case 3:   cal.invert_x = 1; break;
        case 4:   cal.invert_y = 1; break;
        case 'v': g_log_level++; break;
        case 'q': g_log_level--; break;
        case 0:   usage(argv[0]); return 0;
        default:  usage(argv[0]); return 1;
        }
    }

    struct mc_backend *be = NULL;
    int gpu_compose = 0;
    if (strcmp(backend_name, "fb")  == 0) be = &backend_fb;
    else if (strcmp(backend_name, "ppm") == 0) be = &backend_ppm;
#ifdef MC_ENABLE_EGL
    else if (strcmp(backend_name, "egl") == 0) { be = &backend_egl; gpu_compose = 1; }
#endif
    else { fprintf(stderr, "unknown backend: %s\n", backend_name); return 1; }

    signal(SIGINT,  on_sig);
    signal(SIGTERM, on_sig);
    signal(SIGPIPE, SIG_IGN);

    /* Surface buffer allocator: pick the best backend now so logs land
     * before we start accepting clients. */
    (void)mc_alloc_init();

    struct mc_server s;
    if (mc_server_init(&s, sock_path) < 0) {
        LOG_E("server init failed");
        return 1;
    }

    int out_w = w, out_h = h, out_stride = 0;
    if (be->open(be, backend_arg, w, h, &out_w, &out_h, &out_stride) < 0) {
        LOG_E("backend open failed");
        mc_server_fini(&s);
        unlink(sock_path);
        return 1;
    }
    s.screen_w   = (uint16_t)out_w;
    s.screen_h   = (uint16_t)out_h;
    s.fb_stride  = out_stride;
    s.backend    = be;
    s.gpu_compose = gpu_compose;

    LOG_I("mc-compositor Phase 0: backend=%s %dx%d stride=%d",
          be->name, out_w, out_h, out_stride);

    /* Input device (touch). */
    if (!input_disabled) {
        if (mc_ial_open(&s.ial, input_path, out_w, out_h, &cal) == 0) {
            struct epoll_event ev = { .events = EPOLLIN,
                                      .data.fd = mc_ial_fd(&s.ial) };
            if (epoll_ctl(s.epoll_fd, EPOLL_CTL_ADD,
                          mc_ial_fd(&s.ial), &ev) < 0) {
                LOG_E("epoll add ial fd: %s", strerror(errno));
                mc_ial_close(&s.ial);
            } else {
                s.ial_open = 1;
            }
        } else {
            LOG_W("no input device — touch routing disabled");
        }
    }
    LOG_I("build=%s %s, recompose-on-topology-change ENABLED",
          __DATE__, __TIME__);

    /* Make sure fb starts clean so stale content from a previous run isn't
     * visible until the first commit. Two passes cover double-buffered fb. */
    {
        uint8_t *fb = be->get_buffer(be);
        if (fb) {
            memset(fb, 0, (size_t)out_stride * out_h);
            be->present(be);
            fb = be->get_buffer(be);
            if (fb) memset(fb, 0, (size_t)out_stride * out_h);
            be->present(be);
        }
    }

    mc_server_run(&s);

    if (s.ial_open) mc_ial_close(&s.ial);
    be->close(be);
    mc_server_fini(&s);
    unlink(sock_path);
    LOG_I("bye");
    return 0;
}

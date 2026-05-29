/*
 * mc-fill-client: tiny non-LVGL client used to exercise the composition
 * pipeline on hosts where the cross-compiled LVGL lib can't run.
 *
 * Fills its surface with a solid BGRA color, optionally places it as a popup,
 * commits N times, then exits.
 */
#define _GNU_SOURCE
#include "mc.h"

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

static unsigned parse_color(const char *s)
{
    /* accept "#AARRGGBB" or "0xAARRGGBB" or hex digits */
    if (!s) return 0xFF000000u;
    if (*s == '#') s++;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    return (unsigned)strtoul(s, NULL, 16);
}

static void fill(uint8_t *buf, int stride, int w, int h, uint32_t argb)
{
    uint8_t a = (argb >> 24) & 0xff;
    uint8_t r = (argb >> 16) & 0xff;
    uint8_t g = (argb >>  8) & 0xff;
    uint8_t b =  argb        & 0xff;
    for (int y = 0; y < h; y++) {
        uint8_t *row = buf + (size_t)y * stride;
        for (int x = 0; x < w; x++) {
            row[x*4+0] = b;
            row[x*4+1] = g;
            row[x*4+2] = r;
            row[x*4+3] = a;
        }
    }
}

int main(int argc, char **argv)
{
    const char *sock = NULL;
    const char *role_s = "full";
    int w = 0, h = 0, x = 0, y = 0, n = 1;
    uint32_t color = 0xFF003366u;
    int hold = 1;
    int dmg_x = -1, dmg_y = 0, dmg_w = 0, dmg_h = 0;  /* -1 = use full surface */
    static const struct option opts[] = {
        {"socket", required_argument, 0, 's'},
        {"role",   required_argument, 0, 'r'},
        {"w",      required_argument, 0, 'w'},
        {"h",      required_argument, 0, 'h'},
        {"x",      required_argument, 0, 'x'},
        {"y",      required_argument, 0, 'y'},
        {"color",  required_argument, 0, 'c'},
        {"n",      required_argument, 0, 'n'},
        {"hold",   required_argument, 0, 'H'},
        {"dmg",    required_argument, 0,  1 },
        {0,0,0,0}
    };
    int c, idx;
    while ((c = getopt_long(argc, argv, "s:r:w:h:x:y:c:n:H:", opts, &idx)) != -1) {
        switch (c) {
        case 's': sock   = optarg; break;
        case 'r': role_s = optarg; break;
        case 'w': w = atoi(optarg); break;
        case 'h': h = atoi(optarg); break;
        case 'x': x = atoi(optarg); break;
        case 'y': y = atoi(optarg); break;
        case 'c': color = parse_color(optarg); break;
        case 'n': n = atoi(optarg); break;
        case 'H': hold = atoi(optarg); break;
        case 1:
            if (sscanf(optarg, "%d,%d,%d,%d", &dmg_x, &dmg_y, &dmg_w, &dmg_h) != 4) {
                fprintf(stderr, "bad --dmg, expected x,y,w,h\n"); return 1;
            }
            break;
        default: return 1;
        }
    }
    if (sock) mc_set_socket_path(sock);

    mc_ctx_t *ctx = mc_connect("fill");
    if (!ctx) {
        fprintf(stderr, "connect: %s\n", mc_strerror(mc_last_error()));
        return 1;
    }
    mc_screen_info_t scr; mc_get_screen_info(ctx, &scr);
    if (w == 0) w = scr.screen_w;
    if (h == 0) h = scr.screen_h;

    mc_role_t role = MC_ROLE_FULLSCREEN;
    if (strcmp(role_s, "popup") == 0) role = MC_ROLE_POPUP;

    mc_surface_t *surf = mc_surface_create_shm(
        ctx, w, h, MC_FMT_BGRA8888, role, 2);
    if (!surf) {
        fprintf(stderr, "create surface: %s\n", mc_strerror(mc_last_error()));
        mc_disconnect(ctx); return 1;
    }
    if (role == MC_ROLE_POPUP) mc_surface_set_popup_pos(surf, x, y);

    fprintf(stderr, "[fill] role=%s %dx%d @ (%d,%d) color=0x%08X commits=%d\n",
            role_s, w, h, x, y, color, n);

    /* Initial frame: full color, full damage. */
    {
        int stride;
        uint8_t *p = mc_surface_acquire(surf, &stride);
        if (!p) { fprintf(stderr, "acquire failed at frame 0\n"); goto cleanup; }
        fill(p, stride, w, h, color);
        mc_rect_t full = { 0, 0, (int16_t)w, (int16_t)h };
        mc_surface_commit(surf, &full, 1);
    }

    /* Subsequent frames: each commit paints a small box of a new color
     * AT THE BUFFER LEVEL, but reports only that box as damage. If damage
     * tracking works, only that box updates on screen; the rest stays the
     * color from the initial commit. */
    uint32_t palette[] = {
        0xFF00FF00, 0xFFFFFF00, 0xFF00FFFF, 0xFFFF00FF,
        0xFFFFA500, 0xFFFFFFFF,
    };
    int n_pal = sizeof(palette)/sizeof(palette[0]);

    for (int i = 1; i < n; i++) {
        int stride;
        uint8_t *p = mc_surface_acquire(surf, &stride);
        if (!p) {
            fprintf(stderr, "acquire returned NULL at frame %d\n", i);
            break;
        }

        /* Important: each acquire might give us a different buffer (double
         * buffered). Re-paint the WHOLE buffer with the BASE color so the
         * "unchanged" parts look right even on the other buffer. */
        fill(p, stride, w, h, color);

        /* Now paint a small box of palette[i % n_pal] at (dmg_x, dmg_y) */
        int bx = (dmg_x >= 0) ? dmg_x : 100;
        int by = dmg_y;
        int bw = (dmg_w > 0) ? dmg_w : 80;
        int bh = (dmg_h > 0) ? dmg_h : 40;
        uint32_t box_color = palette[(i - 1) % n_pal];
        uint8_t bb = box_color & 0xff;
        uint8_t bg = (box_color >> 8) & 0xff;
        uint8_t br = (box_color >> 16) & 0xff;
        uint8_t ba = (box_color >> 24) & 0xff;
        for (int yy = 0; yy < bh; yy++) {
            uint8_t *row = p + (by + yy) * stride + bx * 4;
            for (int xx = 0; xx < bw; xx++) {
                row[xx*4+0] = bb; row[xx*4+1] = bg;
                row[xx*4+2] = br; row[xx*4+3] = ba;
            }
        }

        mc_rect_t damage = { (int16_t)bx, (int16_t)by, (int16_t)bw, (int16_t)bh };
        mc_surface_commit(surf, &damage, 1);

        /* Pace so the recompose stats show separation. */
        struct timespec ts = { 0, 100 * 1000 * 1000 };
        nanosleep(&ts, NULL);
    }

    /* If MC_FILL_REFOCUS_S is set, request_focus after that many seconds
     * (and before exiting). Lets us script a "second app comes up, then
     * first one takes focus back" scenario without changing demo code. */
    const char *refocus_s = getenv("MC_FILL_REFOCUS_S");
    if (refocus_s && atoi(refocus_s) > 0 && hold > atoi(refocus_s)) {
        int t = atoi(refocus_s);
        sleep(t);
        fprintf(stderr, "[fill] requesting focus\n");
        mc_surface_request_focus(surf);
        sleep(hold - t);
    } else if (hold > 0) {
        fprintf(stderr, "[fill] holding %d s\n", hold);
        sleep((unsigned)hold);
    }
cleanup:
    mc_surface_destroy(surf);
    mc_disconnect(ctx);
    return 0;
}

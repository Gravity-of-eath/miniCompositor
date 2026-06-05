/*
 * toast-daemon: renders "ui/toast" bus messages as a top-most, untouchable,
 * auto-dismissing text card.
 *
 * - subscribes to "ui/toast" (payload = common/mc_toast_wire.h)
 * - on each toast: (re)creates a TOAST-role mc surface (screen_w x BAND_H),
 *   draws a centered translucent card+label with LVGL, positions the band by
 *   gravity, renders one frame, arms a timer.
 * - replace semantics: a new toast first dismisses the current one.
 * - the surface is destroyed on dismiss, so there is ZERO compose cost while
 *   no toast is showing.
 */
#define _GNU_SOURCE
#include "mc.h"
#include "mc_toast_wire.h"
#include "lv_port_mc.h"

#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define BAND_H        160     /* surface height; room for ~3 wrapped lines */
#define MARGIN        24      /* top/bottom gap from screen edge */
#define CARD_MAX_FRAC 80      /* card max width = 80% of screen width */

static volatile sig_atomic_t s_running = 1;
static void on_sig(int sig) { (void)sig; s_running = 0; }

static mc_ctx_t      *g_ctx;
static int            g_scr_w, g_scr_h;

static mc_surface_t  *g_surf;       /* NULL when nothing showing */
static int            g_visible;
static uint64_t       g_expire_ms;  /* monotonic ms when current toast ends */

static uint64_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000;
}

static int band_y_for(uint8_t pos)
{
    switch (pos) {
    case MC_TOAST_POS_TOP:    return MARGIN;
    case MC_TOAST_POS_CENTER: return (g_scr_h - BAND_H) / 2;
    case MC_TOAST_POS_BOTTOM:
    default:                  return g_scr_h - BAND_H - MARGIN;
    }
}

static void build_card(const char *text)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_opa(scr, LV_OPA_TRANSP, LV_PART_MAIN);  /* band is transparent */

    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_set_width(card, LV_SIZE_CONTENT);
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(card, (g_scr_w * CARD_MAX_FRAC) / 100, LV_PART_MAIN);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);                  /* centered in band */
    lv_obj_set_style_bg_color(card, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa  (card, LV_OPA_80,             LV_PART_MAIN);
    lv_obj_set_style_radius  (card, 12,                    LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 0,                 LV_PART_MAIN);
    lv_obj_set_style_pad_left (card, 18, LV_PART_MAIN);
    lv_obj_set_style_pad_right(card, 18, LV_PART_MAIN);
    lv_obj_set_style_pad_top  (card, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(card, 12, LV_PART_MAIN);

    lv_obj_t *lbl = lv_label_create(card);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl, (g_scr_w * CARD_MAX_FRAC) / 100 - 36); /* minus h-pad */
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
}

static void dismiss(void)
{
    if (!g_visible) return;
    lv_port_mc_deinit();
    if (g_surf) { mc_surface_destroy(g_surf); g_surf = NULL; }
    g_visible = 0;
}

static void show(const char *text, uint32_t dur_ms, uint8_t pos)
{
    dismiss();   /* replace semantics: drop any current toast first */

    g_surf = mc_surface_create_shm(g_ctx, g_scr_w, BAND_H,
                                   MC_FMT_BGRA8888, MC_ROLE_TOAST, 2);
    if (!g_surf) {
        fprintf(stderr, "[toast] create surface failed: %s\n",
                mc_strerror(mc_last_error()));
        return;
    }
    mc_surface_set_popup_pos(g_surf, 0, band_y_for(pos));

    if (lv_port_mc_init(g_surf, g_scr_w, BAND_H, 0) < 0) {
        fprintf(stderr, "[toast] lv_port_mc_init failed\n");
        mc_surface_destroy(g_surf); g_surf = NULL;
        return;
    }
    build_card(text);
    lv_refr_now(lv_display_get_default());  /* render+commit one frame now */

    g_visible   = 1;
    g_expire_ms = now_ms() + dur_ms;
    fprintf(stderr, "[toast] show pos=%u %ums: \"%s\"\n", pos, dur_ms, text);
}

static void handle_bus(const mc_event_t *ev)
{
    if (ev->kind != MC_EV_BUS) return;
    if (strcmp(ev->bus.topic, "ui/toast") != 0) return;
    uint32_t dur = MC_TOAST_DEFAULT_MS; uint8_t pos = MC_TOAST_POS_BOTTOM;
    char text[MC_TOAST_MAX_TEXT + 1];
    if (mc_toast_wire_decode(ev->bus.data, ev->bus.len, &dur, &pos,
                             text, sizeof(text)) != 0) {
        fprintf(stderr, "[toast] malformed ui/toast payload (len=%u)\n", ev->bus.len);
        return;
    }
    show(text, dur, pos);
}

int main(int argc, char **argv)
{
    const char *sock = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--socket") && i + 1 < argc) sock = argv[++i];
    }
    if (sock) mc_set_socket_path(sock);

    signal(SIGINT,  on_sig);
    signal(SIGTERM, on_sig);

    g_ctx = mc_connect("toast-daemon");
    if (!g_ctx) {
        fprintf(stderr, "[toast] connect failed: %s\n", mc_strerror(mc_last_error()));
        return 1;
    }
    mc_screen_info_t si;
    if (mc_get_screen_info(g_ctx, &si) != 0) {
        fprintf(stderr, "[toast] get_screen_info failed\n");
        return 1;
    }
    g_scr_w = si.screen_w; g_scr_h = si.screen_h;
    fprintf(stderr, "[toast] connected, screen %dx%d\n", g_scr_w, g_scr_h);

    lv_init();
    mc_bus_subscribe(g_ctx, "ui/toast");

    int fd = mc_fd(g_ctx);
    while (s_running) {
        if (!mc_alive(g_ctx)) { fprintf(stderr, "[toast] compositor gone\n"); break; }

        int timeout = -1;  /* block until a bus message arrives */
        if (g_visible) {
            uint64_t now = now_ms();
            if (now >= g_expire_ms) {
                timeout = 0;
            } else {
                uint64_t rem = g_expire_ms - now;
                timeout = (rem > (uint64_t)INT_MAX) ? INT_MAX : (int)rem;
            }
        }
        struct pollfd p = { .fd = fd, .events = POLLIN };
        int pr = poll(&p, 1, timeout);
        if (pr < 0 && errno == EINTR) continue;   /* signal wake-up: re-check s_running */

        mc_event_t ev;
        while (mc_dispatch(g_ctx, &ev, 0) > 0) handle_bus(&ev);

        if (g_visible && now_ms() >= g_expire_ms) {
            fprintf(stderr, "[toast] dismiss\n");
            dismiss();
        }
    }

    dismiss();
    mc_disconnect(g_ctx);
    return 0;
}

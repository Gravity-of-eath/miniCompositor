/*
 * demo-fullscreen (LVGL 9.0): full-screen UI with localized animations.
 *
 *   ┌──────────────────────────────────────────────┐
 *   │  Dashboard (mc)                ◉ tick        │  title bar (static)
 *   ├──────────────────────────────────────────────┤
 *   │                                              │
 *   │                12:34:56                      │  big clock, 1Hz update
 *   │                                              │
 *   ├──────────────────────────────────────────────┤
 *   │  ████████░░░░░░░░░░  42 / 100                │  progress bar, ~30ms
 *   └──────────────────────────────────────────────┘
 *
 * Only the tick dot / clock / progress bar regions change; mc damage
 * tracking means the compositor only repaints those small areas.
 */
#define _GNU_SOURCE
#include "mc.h"
#include "lv_port_mc.h"

#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t s_running = 1;
static void on_sig(int sig) { (void)sig; s_running = 0; }

/* Forward decls so on_bus() can touch globals defined further down. */
static lv_obj_t *g_bus_label;
static int       g_bus_recv_count;

/* Lifecycle: when HIDDEN we skip lv_timer_handler entirely to save CPU.
 * VISIBLE (default before any event arrives) means full speed. */
static int       g_hidden = 0;

/* App-switching support: a Bus message on topic "app/focus" with payload
 * == this instance's name triggers mc_surface_request_focus(). */
static char        g_app_name[32] = "dashboard";
static uint32_t    g_bg_color    = 0x102028;   /* default; configurable */
static mc_ctx_t     *g_ctx;
static mc_surface_t *g_surf;

/* Bus event handler: invoked by lv_port_mc on every non-touch event drained
 * from mc_dispatch. We expect topic 'popup/closed' carrying a short text
 * payload (the OK click count from the popup process). */
static void on_bus(const mc_event_t *ev, void *user)
{
    (void)user;
    if (ev->kind == MC_EV_LIFECYCLE) {
        int hide = (ev->lc.state == MC_LIFECYCLE_HIDDEN
                 || ev->lc.state == MC_LIFECYCLE_SUSPENDED);
        if (hide != g_hidden) {
            g_hidden = hide;
            fprintf(stderr, "[fs] lifecycle: %s\n",
                    g_hidden ? "HIDDEN (pausing ticks)" : "VISIBLE (resuming)");
        }
        return;
    }
    if (ev->kind != MC_EV_BUS) return;
    g_bus_recv_count++;
    /* Copy payload to a local nul-terminated buffer; ev->bus.data borrows
     * into the libmc rx buffer and dies on the next dispatch. */
    char payload[32];
    uint32_t plen = ev->bus.len;
    if (plen >= sizeof(payload)) plen = sizeof(payload) - 1;
    if (plen) memcpy(payload, ev->bus.data, plen);
    payload[plen] = 0;

    /* App-switcher: "app/focus" with payload == my name => bring me up. */
    if (strcmp(ev->bus.topic, "app/focus") == 0 &&
        strcmp(payload, g_app_name) == 0) {
        int r = mc_surface_request_focus(g_surf);
        fprintf(stderr, "[fs:%s] focus requested via bus: %s\n",
                g_app_name, r == 0 ? "ok" : mc_strerror(r));
        return;
    }
    char buf[80];
    snprintf(buf, sizeof(buf), "bus: %s #%d (%s)",
             ev->bus.topic, g_bus_recv_count, payload);
    if (g_bus_label) lv_label_set_text(g_bus_label, buf);
    fprintf(stderr, "[fs] bus rx '%s' sender='%s' payload='%s'\n",
            ev->bus.topic, ev->bus.sender, payload);
}

static lv_obj_t *g_clock_label;
static lv_obj_t *g_progress_bar;     /* outer (track) */
static lv_obj_t *g_progress_fill;    /* inner (filled portion) */
static lv_obj_t *g_progress_text;
static lv_obj_t *g_tick_dot;

/* Marker that demonstrates compositor see-through: it should be visible
 * inside the rectangular hole demo-popup punches through its card.
 * Pop default position+size:  hole_screen = (410, 170) 170x120.
 * We make the marker bigger than the hole so it's obviously rendered by
 * the fullscreen client (not by popup) — its edges are clipped by the
 * popup's opaque card around the hole. */
static lv_obj_t *g_marker_box;       /* static bg of marker area */
static lv_obj_t *g_marker_dot;       /* moving dot inside marker */
static lv_obj_t *g_marker_text;      /* "FULLSCREEN" label */

/* Touch-routing visualization. The whole screen background catches taps
 * that fell *outside* every other clickable area (and outside the popup's
 * rectangle, if a popup exists). Tap counter shows up in the title bar. */
static lv_obj_t *g_taparea;
static lv_obj_t *g_tap_label;
static int       g_tap_count = 0;

/* g_bus_label and g_bus_recv_count are forward-declared above. */
#define MARKER_X   380
#define MARKER_Y   140
#define MARKER_W   230
#define MARKER_H   180

static uint32_t now_ms(void)
{
    static struct timespec t0;
    static int inited;
    if (!inited) { clock_gettime(CLOCK_MONOTONIC, &t0); inited = 1; }
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    int64_t ns = (int64_t)(t.tv_sec - t0.tv_sec) * 1000000000LL
               + (int64_t)(t.tv_nsec - t0.tv_nsec);
    return (uint32_t)(ns / 1000000);
}

static void on_screen_tap(lv_event_t *e)
{
    (void)e;
    g_tap_count++;
    char buf[40];
    snprintf(buf, sizeof(buf), "fs taps: %d", g_tap_count);
    if (g_tap_label) lv_label_set_text(g_tap_label, buf);
    fprintf(stderr, "[fs:%s] screen tap #%d (= touch routed to fullscreen, "
                    "popup didn't catch it)\n", g_app_name, g_tap_count);
}

static void build_ui(int w, int h)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(g_bg_color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa  (scr, LV_OPA_COVER,           LV_PART_MAIN);

    /* Title bar (STATIC) */
    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, w, 48);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x00897B), LV_PART_MAIN);
    lv_obj_set_style_bg_opa  (bar, LV_OPA_COVER,           LV_PART_MAIN);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *title = lv_label_create(bar);
    char title_text[64];
    snprintf(title_text, sizeof(title_text), "[%s] mc 9.0 dashboard", g_app_name);
    lv_label_set_text(title, title_text);
    lv_obj_set_style_text_color(title, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 12, 0);

    /* Bus event counter (DYNAMIC: updates when popup publishes 'popup/closed'). */
    g_bus_label = lv_label_create(bar);
    lv_label_set_text(g_bus_label, "bus: -");
    lv_obj_set_style_text_color(g_bus_label, lv_color_hex(0xFFEB3B), LV_PART_MAIN);
    lv_obj_align(g_bus_label, LV_ALIGN_CENTER, 0, 0);

    /* Tick dot (DYNAMIC, 500ms blink) */
    g_tick_dot = lv_obj_create(bar);
    lv_obj_set_size(g_tick_dot, 16, 16);
    lv_obj_align(g_tick_dot, LV_ALIGN_RIGHT_MID, -14, 0);
    lv_obj_set_style_radius(g_tick_dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_tick_dot, lv_color_hex(0xFFEB3B), LV_PART_MAIN);
    lv_obj_set_style_bg_opa  (g_tick_dot, LV_OPA_COVER,           LV_PART_MAIN);
    lv_obj_set_style_border_width(g_tick_dot, 0, LV_PART_MAIN);

    /* Big clock (DYNAMIC, 1Hz) */
    g_clock_label = lv_label_create(scr);
    lv_label_set_text(g_clock_label, "00:00:00");
    lv_obj_set_style_text_color(g_clock_label, lv_color_hex(0xE0F7FA), LV_PART_MAIN);
    lv_obj_align(g_clock_label, LV_ALIGN_CENTER, 0, -20);

    /* Progress bar track (STATIC) */
    g_progress_bar = lv_obj_create(scr);
    lv_obj_set_size(g_progress_bar, w - 80, 18);
    lv_obj_align(g_progress_bar, LV_ALIGN_BOTTOM_MID, -30, -30);
    lv_obj_set_style_bg_color(g_progress_bar, lv_color_hex(0x263238), LV_PART_MAIN);
    lv_obj_set_style_bg_opa  (g_progress_bar, LV_OPA_COVER,           LV_PART_MAIN);
    lv_obj_set_style_border_width(g_progress_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius (g_progress_bar, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_all(g_progress_bar, 0, LV_PART_MAIN);

    /* Progress bar fill (DYNAMIC: width changes) */
    g_progress_fill = lv_obj_create(g_progress_bar);
    lv_obj_set_size(g_progress_fill, 0, 18);
    lv_obj_align(g_progress_fill, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(g_progress_fill, lv_color_hex(0x4DD0E1), LV_PART_MAIN);
    lv_obj_set_style_bg_opa  (g_progress_fill, LV_OPA_COVER,           LV_PART_MAIN);
    lv_obj_set_style_border_width(g_progress_fill, 0, LV_PART_MAIN);
    lv_obj_set_style_radius (g_progress_fill, 4, LV_PART_MAIN);

    g_progress_text = lv_label_create(scr);
    lv_label_set_text(g_progress_text, "  0 / 100");
    lv_obj_set_style_text_color(g_progress_text, lv_color_hex(0xB0BEC5), LV_PART_MAIN);
    lv_obj_align_to(g_progress_text, g_progress_bar, LV_ALIGN_OUT_RIGHT_MID, 8, 0);

    /* --- "see-through" marker, behind where popup will punch a hole --- */
    g_marker_box = lv_obj_create(scr);
    lv_obj_set_size(g_marker_box, MARKER_W, MARKER_H);
    lv_obj_set_pos (g_marker_box, MARKER_X, MARKER_Y);
    lv_obj_set_style_bg_color(g_marker_box, lv_color_hex(0xE91E63), LV_PART_MAIN);
    lv_obj_set_style_bg_opa  (g_marker_box, LV_OPA_COVER,           LV_PART_MAIN);
    lv_obj_set_style_border_color(g_marker_box, lv_color_hex(0xFFEB3B), LV_PART_MAIN);
    lv_obj_set_style_border_width(g_marker_box, 4,                      LV_PART_MAIN);
    lv_obj_set_style_radius (g_marker_box, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(g_marker_box, 0, LV_PART_MAIN);

    g_marker_text = lv_label_create(g_marker_box);
    lv_label_set_text(g_marker_text, "FULLSCREEN\n(seen via popup hole)");
    lv_obj_set_style_text_color(g_marker_text, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(g_marker_text, LV_ALIGN_TOP_LEFT, 8, 8);

    g_marker_dot = lv_obj_create(g_marker_box);
    lv_obj_set_size(g_marker_dot, 24, 24);
    lv_obj_set_style_radius(g_marker_dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_marker_dot, lv_color_hex(0x00E5FF), LV_PART_MAIN);
    lv_obj_set_style_bg_opa  (g_marker_dot, LV_OPA_COVER,           LV_PART_MAIN);
    lv_obj_set_style_border_width(g_marker_dot, 0, LV_PART_MAIN);
    lv_obj_set_pos(g_marker_dot, 8, MARKER_H - 32);

    /* --- Touch demo: full-screen click-catcher + tap label in title --- */
    g_taparea = lv_obj_create(scr);
    lv_obj_remove_style_all(g_taparea);
    lv_obj_set_size(g_taparea, w, h);
    lv_obj_set_pos(g_taparea, 0, 0);
    lv_obj_set_style_bg_opa(g_taparea, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_add_flag(g_taparea, LV_OBJ_FLAG_CLICKABLE);
    /* Send taparea to the bottom of z within the screen so it does NOT
     * eat events that should go to widgets drawn on top of it. */
    lv_obj_move_background(g_taparea);
    lv_obj_add_event_cb(g_taparea, on_screen_tap, LV_EVENT_CLICKED, NULL);

    g_tap_label = lv_label_create(bar);
    lv_label_set_text(g_tap_label, "fs taps: 0");
    lv_obj_set_style_text_color(g_tap_label, lv_color_hex(0xFFCDD2), LV_PART_MAIN);
    lv_obj_align(g_tap_label, LV_ALIGN_RIGHT_MID, -40, 0);
}

static void tick_anim(int bar_w)
{
    static uint32_t t0;
    if (!t0) t0 = now_ms();
    uint32_t t = now_ms() - t0;

    /* 1Hz clock */
    static uint32_t last_sec = (uint32_t)-1;
    uint32_t sec = t / 1000;
    if (sec != last_sec) {
        last_sec = sec;
        char buf[16];
        snprintf(buf, sizeof(buf), "%02u:%02u:%02u",
                 sec / 3600, (sec / 60) % 60, sec % 60);
        lv_label_set_text(g_clock_label, buf);
    }

    /* 500ms blink dot */
    static uint8_t dot_state = 0xFF;
    uint8_t want = ((t / 500) & 1) ? 0 : 0xFF;
    if (want != dot_state) {
        dot_state = want;
        lv_obj_set_style_bg_opa(g_tick_dot,
            want ? LV_OPA_COVER : LV_OPA_30, LV_PART_MAIN);
    }

    /* Progress bar (~3s cycle) */
    static int last_pct = -1;
    int pct = (int)((t / 30) % 101);
    if (pct != last_pct) {
        last_pct = pct;
        lv_obj_set_width(g_progress_fill, bar_w * pct / 100);
        char buf[24];
        snprintf(buf, sizeof(buf), "%3d / 100", pct);
        lv_label_set_text(g_progress_text, buf);
    }

    /* Marker dot: ping-pong inside marker_box on x axis (~3s round trip).
     * Movement proves the marker is being driven by the fullscreen client
     * even when popup's hole is what makes it visible. */
    static int last_mx = -1;
    int range = MARKER_W - 24 - 16;            /* leave 8px padding each side */
    int phase = (t / 12) % (2 * range);
    int mx = (phase < range) ? phase : (2 * range - phase);
    mx += 8;                                    /* left padding */
    if (mx != last_mx) {
        last_mx = mx;
        lv_obj_set_pos(g_marker_dot, mx, MARKER_H - 32);
    }
}

static void usage(const char *p)
{
    fprintf(stderr,
        "Usage: %s [options]\n"
        "  --socket PATH      mc socket path\n"
        "  --name NAME        app identity for the 'app/focus' bus topic\n"
        "                     (default 'dashboard')\n"
        "  --bg HEXRGB        screen background color (default 0x102028)\n",
        p);
}

int main(int argc, char **argv)
{
    const char *sock = NULL;
    static const struct option opts[] = {
        {"socket", required_argument, 0, 's'},
        {"name",   required_argument, 0, 'n'},
        {"bg",     required_argument, 0, 'b'},
        {"help",   no_argument,       0,  0 },
        {0,0,0,0}
    };
    int c, idx;
    while ((c = getopt_long(argc, argv, "s:n:b:", opts, &idx)) != -1) {
        switch (c) {
        case 's': sock = optarg; break;
        case 'n': snprintf(g_app_name, sizeof(g_app_name), "%s", optarg); break;
        case 'b': g_bg_color = (uint32_t)strtoul(optarg, NULL, 16); break;
        case 0:   usage(argv[0]); return 0;
        default:  usage(argv[0]); return 1;
        }
    }
    if (sock) mc_set_socket_path(sock);

    signal(SIGINT,  on_sig);
    signal(SIGTERM, on_sig);

    char conn_name[64];
    snprintf(conn_name, sizeof(conn_name), "demo-fs:%s", g_app_name);
    mc_ctx_t *ctx = mc_connect(conn_name);
    if (!ctx) {
        fprintf(stderr, "connect failed: %s\n", mc_strerror(mc_last_error()));
        return 1;
    }
    g_ctx = ctx;
    mc_screen_info_t scr;
    mc_get_screen_info(ctx, &scr);
    int W = scr.screen_w, H = scr.screen_h;
    fprintf(stderr, "[fs:%s] connected, screen=%dx%d\n", g_app_name, W, H);

    mc_surface_t *surf = mc_surface_create_shm(
        ctx, W, H, MC_FMT_BGRA8888, MC_ROLE_FULLSCREEN, 2);
    if (!surf) {
        fprintf(stderr, "create surface: %s\n", mc_strerror(mc_last_error()));
        mc_disconnect(ctx); return 1;
    }
    g_surf = surf;

    lv_init();
    if (lv_port_mc_init(surf, W, H, 0) < 0) {
        fprintf(stderr, "lv_port_mc_init failed\n");
        return 1;
    }
    if (lv_port_mc_input_init(ctx) < 0) {
        fprintf(stderr, "lv_port_mc_input_init failed\n");
        return 1;
    }
    lv_port_mc_set_event_cb(on_bus, NULL);
    /* Listen for popup acknowledgements and app-switch requests. */
    mc_bus_subscribe(ctx, "popup/closed");
    mc_bus_subscribe(ctx, "app/focus");
    build_ui(W, H);

    int bar_w = W - 80;
    while (s_running) {
        if (!mc_alive(ctx)) {
            fprintf(stderr, "[fs] compositor went away, exiting\n");
            break;
        }
        /* Even while hidden, we still need to drain mc_dispatch so we
         * see the next VISIBLE event. lv_indev's read_cb does that for
         * us, but only when we run the LVGL pipeline. Solution: when
         * hidden, just drain manually via the indev path (which calls
         * our event_cb) by running lv_timer_handler at low rate -- it's
         * almost free since nothing is invalidated. */
        if (!g_hidden) {
            tick_anim(bar_w);
            lv_timer_handler();
            struct timespec ts = { 0, 16 * 1000 * 1000 };  /* 60Hz */
            nanosleep(&ts, NULL);
        } else {
            lv_timer_handler();   /* still pumps indev → drains mc events */
            struct timespec ts = { 0, 200 * 1000 * 1000 }; /* 5Hz */
            nanosleep(&ts, NULL);
        }
    }

    fprintf(stderr, "[fs] exiting\n");
    mc_surface_destroy(surf);
    mc_disconnect(ctx);
    return 0;
}

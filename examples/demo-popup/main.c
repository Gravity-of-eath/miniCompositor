/*
 * demo-popup (LVGL 9.0): overlay dialog with TRUE transparent corners.
 *
 *   ┌────────────────────────────────┐
 *   │ Notice                ●        │  title + dot (300ms blink)
 *   │ This is a popup surface...     │  static body
 *   │ counter: 42                    │  1Hz counter
 *   │                       [  OK ]  │
 *   └────────────────────────────────┘
 *
 * Screen bg is set to LV_OPA_TRANSP, so the rounded corners of the card
 * end up as alpha=0 in the mc shm buffer and the compositor composites
 * the fullscreen surface visibly through them.
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

/* The ctx + dot pointer the click handler needs. Set in main() before
 * build_ui(). */
static mc_ctx_t  *g_ctx;
static lv_obj_t  *g_dot;
static int        g_ok_count = 0;

/* Lifecycle: pause LVGL ticks when HIDDEN to save CPU. */
static int        g_hidden  = 0;

static void on_lifecycle(const mc_event_t *ev, void *user)
{
    (void)user;
    if (ev->kind != MC_EV_LIFECYCLE) return;
    int hide = (ev->lc.state == MC_LIFECYCLE_HIDDEN
             || ev->lc.state == MC_LIFECYCLE_SUSPENDED);
    if (hide != g_hidden) {
        g_hidden = hide;
        fprintf(stderr, "[popup] lifecycle: %s\n",
                g_hidden ? "HIDDEN (pausing)" : "VISIBLE (resuming)");
    }
}

static void on_ok_clicked(lv_event_t *e)
{
    (void)e;
    g_ok_count++;

    /* Tell anyone listening that the user just clicked OK. */
    char msg[32];
    int n = snprintf(msg, sizeof(msg), "%d", g_ok_count);
    int r1 = mc_bus_publish(g_ctx, "popup/closed", msg, (uint32_t)n);

    /* Round-robin app-switch: each click cycles to the next fullscreen
     * name. Whoever subscribed to "app/focus" (LVGL demo-fullscreen via
     * --name=X, AWTK via MC_APP_NAME env var) will see this and call
     * mc_surface_request_focus() on itself. */
    static const char *targets[] = {
        "dashboard",     /* LVGL demo-fullscreen --name=dashboard   */
        "diagnostics",   /* LVGL demo-fullscreen --name=diagnostics */
        "awtk-demo1",    /* AWTK demo1 with MC_APP_NAME=awtk-demo1  */
    };
    int n_targets = (int)(sizeof(targets) / sizeof(targets[0]));
    const char *tgt = targets[g_ok_count % n_targets];
    int r2 = mc_bus_publish(g_ctx, "app/focus", tgt, (uint32_t)strlen(tgt));

    fprintf(stderr, "[popup] OK#%d -> popup/closed=%s, app/focus=%s (%s)\n",
            g_ok_count, r1 == 0 ? "ok" : "FAILED",
            tgt, r2 == 0 ? "ok" : "FAILED");

    /* Also fire a toast so the toast-daemon path is exercised on-device. */
    char tmsg[64];
    snprintf(tmsg, sizeof(tmsg), "OK pressed #%d", g_ok_count);
    mc_toast(g_ctx, tmsg, 2000,
             (mc_toast_pos_t)(g_ok_count % 3));   /* cycle bottom/center/top */

    /* Flash the dot green for visual feedback */
    lv_obj_set_style_bg_color(g_dot, lv_color_hex(0x66BB6A), LV_PART_MAIN);
}

static lv_obj_t *g_counter_label;
/* g_dot already declared above (used by on_ok_clicked); no duplicate here */

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

static void build_ui(int w, int h)
{
    lv_obj_t *scr = lv_screen_active();
    /* Transparent screen: corners outside the card surface stay alpha=0. */
    lv_obj_set_style_bg_opa(scr, LV_OPA_TRANSP, LV_PART_MAIN);

    /* Rounded card */
    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_set_size(card, w, h);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x37474F), LV_PART_MAIN);
    lv_obj_set_style_bg_opa  (card, LV_OPA_90,              LV_PART_MAIN);
    lv_obj_set_style_radius  (card, 16,                     LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(0x80DEEA), LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 2,                      LV_PART_MAIN);
    lv_obj_set_style_pad_all (card, 16,                     LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(card);
    lv_label_set_text(title, "Notice");
    lv_obj_set_style_text_color(title, lv_color_hex(0xE0F7FA), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    /* Blink dot (DYNAMIC, 300ms) */
    g_dot = lv_obj_create(card);
    lv_obj_set_size(g_dot, 14, 14);
    lv_obj_align(g_dot, LV_ALIGN_TOP_RIGHT, 0, 2);
    lv_obj_set_style_radius(g_dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_dot, lv_color_hex(0xFF7043), LV_PART_MAIN);
    lv_obj_set_style_bg_opa  (g_dot, LV_OPA_COVER,           LV_PART_MAIN);
    lv_obj_set_style_border_width(g_dot, 0, LV_PART_MAIN);

    lv_obj_t *msg = lv_label_create(card);
    lv_label_set_text(msg,
        "This is a popup surface rendered by\n"
        "demo-popup, composited on top of the\n"
        "fullscreen client by mc-compositor.");
    lv_obj_set_style_text_color(msg, lv_color_hex(0xCFD8DC), LV_PART_MAIN);
    lv_obj_align(msg, LV_ALIGN_TOP_LEFT, 0, 28);

    /* Counter (DYNAMIC, 1Hz) */
    g_counter_label = lv_label_create(card);
    lv_label_set_text(g_counter_label, "counter: 0");
    lv_obj_set_style_text_color(g_counter_label, lv_color_hex(0x80DEEA), LV_PART_MAIN);
    lv_obj_align(g_counter_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    /* OK button (real lv_button now that we have 9.0) */
    lv_obj_t *btn = lv_button_create(card);
    lv_obj_set_size(btn, 80, 32);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x00ACC1), LV_PART_MAIN);
    lv_obj_set_style_bg_opa  (btn, LV_OPA_COVER,           LV_PART_MAIN);
    lv_obj_set_style_radius  (btn, 6,                       LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 0,                   LV_PART_MAIN);
    lv_obj_set_style_pad_all (btn, 0,                       LV_PART_MAIN);
    lv_obj_t *btn_l = lv_label_create(btn);
    lv_label_set_text(btn_l, "OK");
    lv_obj_set_style_text_color(btn_l, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(btn_l);
    lv_obj_add_event_cb(btn, on_ok_clicked, LV_EVENT_CLICKED, NULL);
}

static void tick_anim(void)
{
    static uint32_t t0;
    if (!t0) t0 = now_ms();
    uint32_t t = now_ms() - t0;

    /* 300ms blink */
    static uint8_t dot_state = 0xFF;
    uint8_t want = ((t / 300) & 1) ? 0 : 0xFF;
    if (want != dot_state) {
        dot_state = want;
        lv_obj_set_style_bg_opa(g_dot,
            want ? LV_OPA_COVER : LV_OPA_30, LV_PART_MAIN);
    }

    /* 1Hz counter */
    static uint32_t last_sec = (uint32_t)-1;
    uint32_t sec = t / 1000;
    if (sec != last_sec) {
        last_sec = sec;
        char buf[32];
        snprintf(buf, sizeof(buf), "counter: %u", sec);
        lv_label_set_text(g_counter_label, buf);
    }
}

static void usage(const char *p)
{
    fprintf(stderr,
        "Usage: %s [options]\n"
        "  --socket PATH\n"
        "  --x N --y N  popup top-left (default 200 120)\n"
        "  --w N --h N  popup size      (default 400 240)\n", p);
}

int main(int argc, char **argv)
{
    const char *sock = NULL;
    int px = 200, py = 120, pw = 400, ph = 240;
    static const struct option opts[] = {
        {"socket", required_argument, 0, 's'},
        {"x",      required_argument, 0, 'x'},
        {"y",      required_argument, 0, 'y'},
        {"w",      required_argument, 0, 'w'},
        {"h",      required_argument, 0, 'h'},
        {"help",   no_argument,       0,  0 },
        {0,0,0,0}
    };
    int c, idx;
    while ((c = getopt_long(argc, argv, "s:x:y:w:h:", opts, &idx)) != -1) {
        switch (c) {
        case 's': sock = optarg; break;
        case 'x': px = atoi(optarg); break;
        case 'y': py = atoi(optarg); break;
        case 'w': pw = atoi(optarg); break;
        case 'h': ph = atoi(optarg); break;
        case 0:   usage(argv[0]); return 0;
        default:  usage(argv[0]); return 1;
        }
    }
    if (sock) mc_set_socket_path(sock);

    signal(SIGINT,  on_sig);
    signal(SIGTERM, on_sig);

    mc_ctx_t *ctx = mc_connect("demo-popup");
    if (!ctx) {
        fprintf(stderr, "connect failed: %s\n", mc_strerror(mc_last_error()));
        return 1;
    }
    fprintf(stderr, "[popup] connected\n");

    mc_surface_t *surf = mc_surface_create_shm(
        ctx, pw, ph, MC_FMT_BGRA8888, MC_ROLE_POPUP, 2);
    if (!surf) {
        fprintf(stderr, "create popup surface: %s\n",
                mc_strerror(mc_last_error()));
        mc_disconnect(ctx); return 1;
    }
    mc_surface_set_popup_pos(surf, px, py);

    lv_init();
    if (lv_port_mc_init(surf, pw, ph, 0) < 0) {
        fprintf(stderr, "lv_port_mc_init failed\n");
        return 1;
    }
    if (lv_port_mc_input_init(ctx) < 0) {
        fprintf(stderr, "lv_port_mc_input_init failed\n");
        return 1;
    }
    lv_port_mc_set_event_cb(on_lifecycle, NULL);
    g_ctx = ctx;
    build_ui(pw, ph);

    /* Punch a rectangular hole through the card (right half of popup) so
     * the fullscreen surface's marker shows through. The hole is in popup
     * local coords. Screen coords = (px + hole.x, py + hole.y). */
    int hx = 210, hy = 50, hw = 170, hh = 120;
    lv_port_mc_add_hole(hx, hy, hw, hh);
    fprintf(stderr, "[popup] hole at popup-local (%d,%d) %dx%d "
                    "= screen (%d,%d) %dx%d\n",
            hx, hy, hw, hh, px + hx, py + hy, hw, hh);

    while (s_running) {
        if (!mc_alive(ctx)) {
            fprintf(stderr, "[popup] compositor went away, exiting\n");
            break;
        }
        if (!g_hidden) {
            tick_anim();
            lv_timer_handler();
            struct timespec ts = { 0, 16 * 1000 * 1000 };
            nanosleep(&ts, NULL);
        } else {
            lv_timer_handler();   /* still drain mc events via indev path */
            struct timespec ts = { 0, 200 * 1000 * 1000 };
            nanosleep(&ts, NULL);
        }
    }

    fprintf(stderr, "[popup] exiting\n");
    mc_surface_destroy(surf);
    mc_disconnect(ctx);
    return 0;
}

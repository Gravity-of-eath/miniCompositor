/*
 * Input Abstraction Layer (IAL) — evdev backend.
 *
 * Opens a Linux input device (auto-detected or user-supplied), grabs it
 * exclusively, decodes touch events (MT-B preferred, falls back to single
 * touch), and produces one logical pointer stream in screen coordinates.
 *
 * Multi-finger: only slot 0 is reported up. Extra slots are ignored.
 * Subsequent phases can extend this.
 */
#ifndef MC_IAL_EVDEV_H
#define MC_IAL_EVDEV_H

#include <stdint.h>

typedef enum {
    MC_TOUCH_NONE = 0,
    MC_TOUCH_DOWN = 1,
    MC_TOUCH_MOVE = 2,
    MC_TOUCH_UP   = 3,
} mc_touch_state_t;

struct mc_ial_cal {
    int swap_xy;
    int invert_x;
    int invert_y;
};

struct mc_ial {
    int fd;
    char path[64];

    /* abs ranges, populated at open() */
    int abs_x_min, abs_x_max;
    int abs_y_min, abs_y_max;

    int screen_w, screen_h;

    struct mc_ial_cal cal;

    /* MT-B slot 0 state */
    int slot_id;            /* tracking id; -1 = released */
    int slot_x, slot_y;     /* device coords */
    int cur_slot;           /* which slot the next ABS_MT_* describes */

    /* ST fallback */
    int st_pressed;
    int st_x, st_y;

    /* Latched state across frames */
    int     was_pressed;
    int16_t last_screen_x, last_screen_y;
    int     last_dev_x, last_dev_y;   /* for diagnostic log */
};

/* Open input device.
 *   - If `dev_path` is non-NULL, open that exact path.
 *   - Otherwise auto-detect by scanning /dev/input/event0..15 for the first
 *     device reporting touch (ABS_MT_POSITION_X or ABS_X+BTN_TOUCH).
 * Returns 0 on success, -errno otherwise.
 */
int  mc_ial_open(struct mc_ial *ial,
                 const char *dev_path,
                 int screen_w, int screen_h,
                 const struct mc_ial_cal *cal);

int  mc_ial_fd(const struct mc_ial *ial);

/* Drain pending events from kernel buffer. On each SYN_REPORT that produced
 * an observable change, invoke cb(state, screen_x, screen_y, user) once.
 * Returns the number of events delivered to cb, or -errno on read failure
 * (EAGAIN is mapped to 0). */
typedef void (*mc_ial_cb_t)(mc_touch_state_t st, int sx, int sy, void *user);
int  mc_ial_pump(struct mc_ial *ial, mc_ial_cb_t cb, void *user);

void mc_ial_close(struct mc_ial *ial);

#endif

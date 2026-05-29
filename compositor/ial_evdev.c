#define _GNU_SOURCE
#include "ial_evdev.h"
#include "log.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* ----- helpers ----- */

static int test_bit(const unsigned long *bits, int bit)
{
    return (bits[bit / (8 * sizeof(long))] >> (bit % (8 * sizeof(long)))) & 1u;
}

static int looks_like_touch(int fd)
{
    unsigned long evbits[(EV_MAX + 8 * sizeof(long) - 1) / (8 * sizeof(long))] = {0};
    unsigned long absbits[(ABS_MAX + 8 * sizeof(long) - 1) / (8 * sizeof(long))] = {0};
    unsigned long keybits[(KEY_MAX + 8 * sizeof(long) - 1) / (8 * sizeof(long))] = {0};

    if (ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits) < 0) return 0;
    if (!test_bit(evbits, EV_ABS)) return 0;
    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absbits)), absbits) < 0) return 0;
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits) < 0) {
        /* no EV_KEY: only acceptable if MT */
    }

    if (test_bit(absbits, ABS_MT_POSITION_X) && test_bit(absbits, ABS_MT_POSITION_Y))
        return 1;
    if (test_bit(absbits, ABS_X) && test_bit(absbits, ABS_Y)
        && test_bit(keybits, BTN_TOUCH))
        return 1;
    return 0;
}

static int autodetect(char *out, size_t out_cap)
{
    for (int i = 0; i < 16; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        int ok = looks_like_touch(fd);
        close(fd);
        if (ok) {
            snprintf(out, out_cap, "%s", path);
            return 0;
        }
    }
    return -ENOENT;
}

/* ----- open / close ----- */

int mc_ial_open(struct mc_ial *ial, const char *dev_path,
                int screen_w, int screen_h,
                const struct mc_ial_cal *cal)
{
    memset(ial, 0, sizeof(*ial));
    ial->fd        = -1;
    ial->screen_w  = screen_w;
    ial->screen_h  = screen_h;
    ial->slot_id   = -1;
    ial->cur_slot  = 0;
    ial->was_pressed = 0;
    if (cal) ial->cal = *cal;

    char picked[64];
    if (dev_path) {
        snprintf(picked, sizeof(picked), "%s", dev_path);
    } else {
        if (autodetect(picked, sizeof(picked)) < 0) {
            LOG_W("ial: no touch input device found in /dev/input/event*");
            return -ENOENT;
        }
        LOG_I("ial: auto-detected touch device: %s", picked);
    }

    int fd = open(picked, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        LOG_E("ial: open %s: %s", picked, strerror(errno));
        return -errno;
    }

    /* Refuse non-touch devices, even if the user forced a path with -i.
     * If this fires you picked the wrong /dev/input/eventN; check
     * cat /proc/bus/input/devices to find the touchscreen node. */
    if (!looks_like_touch(fd)) {
        LOG_E("ial: %s is NOT a touch device "
              "(no ABS_X/Y or ABS_MT_POSITION_X/Y reported). "
              "Check /proc/bus/input/devices and pick the right eventN.",
              picked);
        close(fd);
        return -ENOTSUP;
    }

    /* abs ranges: prefer MT then ST */
    struct input_absinfo ai;
    int got_range = 0;
    if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &ai) == 0 && ai.maximum > ai.minimum) {
        ial->abs_x_min = ai.minimum; ial->abs_x_max = ai.maximum;
        got_range = 1;
    } else if (ioctl(fd, EVIOCGABS(ABS_X), &ai) == 0 && ai.maximum > ai.minimum) {
        ial->abs_x_min = ai.minimum; ial->abs_x_max = ai.maximum;
        got_range = 1;
    }
    if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &ai) == 0 && ai.maximum > ai.minimum) {
        ial->abs_y_min = ai.minimum; ial->abs_y_max = ai.maximum;
    } else if (ioctl(fd, EVIOCGABS(ABS_Y), &ai) == 0 && ai.maximum > ai.minimum) {
        ial->abs_y_min = ai.minimum; ial->abs_y_max = ai.maximum;
    }
    if (!got_range) {
        /* worst case: assume coords already in screen pixels */
        ial->abs_x_min = 0; ial->abs_x_max = screen_w - 1;
        ial->abs_y_min = 0; ial->abs_y_max = screen_h - 1;
        LOG_W("ial: no ABS range, assuming raw screen pixels");
    } else {
        LOG_I("ial: abs range X=[%d..%d] Y=[%d..%d]",
              ial->abs_x_min, ial->abs_x_max,
              ial->abs_y_min, ial->abs_y_max);
    }

    /* Get exclusive ownership so nobody else reads these events. */
    if (ioctl(fd, EVIOCGRAB, 1) < 0) {
        LOG_W("ial: EVIOCGRAB failed (%s) — other consumers may interfere",
              strerror(errno));
    }

    ial->fd = fd;
    snprintf(ial->path, sizeof(ial->path), "%s", picked);
    LOG_I("ial: opened %s", picked);
    return 0;
}

int mc_ial_fd(const struct mc_ial *ial) { return ial->fd; }

void mc_ial_close(struct mc_ial *ial)
{
    if (ial->fd >= 0) {
        (void)ioctl(ial->fd, EVIOCGRAB, 0);
        close(ial->fd);
        ial->fd = -1;
    }
}

/* ----- decode loop ----- */

/* Map device value in [dev_min..dev_max] linearly to [0..out_max]. */
static int16_t map_axis(int dev_v, int dev_min, int dev_max, int out_max)
{
    int span = dev_max - dev_min;
    if (span <= 0 || out_max <= 0) return 0;
    long v = ((long)(dev_v - dev_min) * out_max + span / 2) / span;
    if (v < 0) v = 0;
    if (v > out_max) v = out_max;
    return (int16_t)v;
}

/* Resolve the current "pressed + coords" view from MT-B / ST state.
 *
 * Calibration applied in this order, after picking the active device coords:
 *   1. swap_xy   — device X is reported on what is physically the screen Y
 *                  axis (and vice versa). When set, device X maps into the
 *                  screen Y direction (using screen height) and vice versa.
 *   2. invert_*  — flip the mapped value within the screen dimension.
 *
 * Each device axis has its OWN range (abs_x_min..max / abs_y_min..max).
 * When we cross-map, we still use the device axis's own range to do the
 * linear map; only the target range (screen W or H) changes.
 */
static int snapshot(struct mc_ial *ial, int *sx, int *sy, int *dx, int *dy)
{
    int rx = 0, ry = 0;
    int pressed = 0;
    if (ial->slot_id >= 0) {
        rx = ial->slot_x; ry = ial->slot_y; pressed = 1;
    } else if (ial->st_pressed) {
        rx = ial->st_x; ry = ial->st_y; pressed = 1;
    }
    if (!pressed) return 0;

    if (dx) *dx = rx;
    if (dy) *dy = ry;

    int sx_v, sy_v;
    if (ial->cal.swap_xy) {
        /* device-X -> screen-Y direction, device-Y -> screen-X direction */
        sx_v = map_axis(ry, ial->abs_y_min, ial->abs_y_max, ial->screen_w - 1);
        sy_v = map_axis(rx, ial->abs_x_min, ial->abs_x_max, ial->screen_h - 1);
    } else {
        sx_v = map_axis(rx, ial->abs_x_min, ial->abs_x_max, ial->screen_w - 1);
        sy_v = map_axis(ry, ial->abs_y_min, ial->abs_y_max, ial->screen_h - 1);
    }
    if (ial->cal.invert_x) sx_v = ial->screen_w - 1 - sx_v;
    if (ial->cal.invert_y) sy_v = ial->screen_h - 1 - sy_v;
    *sx = sx_v;
    *sy = sy_v;
    return 1;
}

int mc_ial_pump(struct mc_ial *ial, mc_ial_cb_t cb, void *user)
{
    if (ial->fd < 0) return -EBADF;

    int delivered = 0;
    struct input_event ev;

    for (;;) {
        ssize_t n = read(ial->fd, &ev, sizeof(ev));
        if (n < 0) {
            if (errno == EAGAIN) return delivered;
            if (errno == EINTR)  continue;
            LOG_W("ial: read: %s", strerror(errno));
            return -errno;
        }
        if (n != sizeof(ev)) continue;   /* partial; shouldn't happen */

        switch (ev.type) {
        case EV_SYN:
            if (ev.code == SYN_REPORT) {
                int sx = 0, sy = 0, dx = 0, dy = 0;
                int pressed = snapshot(ial, &sx, &sy, &dx, &dy);
                mc_touch_state_t st = MC_TOUCH_NONE;
                if (pressed && !ial->was_pressed)      st = MC_TOUCH_DOWN;
                else if (pressed &&  ial->was_pressed) {
                    if (sx != ial->last_screen_x || sy != ial->last_screen_y)
                        st = MC_TOUCH_MOVE;
                } else if (!pressed && ial->was_pressed) st = MC_TOUCH_UP;

                if (st != MC_TOUCH_NONE) {
                    int rx = (st == MC_TOUCH_UP) ? ial->last_screen_x : sx;
                    int ry = (st == MC_TOUCH_UP) ? ial->last_screen_y : sy;
                    /* DOWN/UP are rare and useful diagnostics: log at INFO.
                     * MOVE stays at DEBUG to avoid flooding. */
                    if (st == MC_TOUCH_DOWN) {
                        LOG_I("ial: DOWN dev=(%d,%d) -> screen=(%d,%d)",
                              dx, dy, rx, ry);
                    } else if (st == MC_TOUCH_UP) {
                        LOG_I("ial: UP   dev=(%d,%d) -> screen=(%d,%d)",
                              ial->last_dev_x, ial->last_dev_y, rx, ry);
                    } else {
                        LOG_D("ial: MOVE dev=(%d,%d) -> screen=(%d,%d)",
                              dx, dy, rx, ry);
                    }
                    if (cb) {
                        cb(st, rx, ry, user);
                        delivered++;
                    }
                }
                if (pressed) {
                    ial->last_screen_x = (int16_t)sx;
                    ial->last_screen_y = (int16_t)sy;
                    ial->last_dev_x = dx;
                    ial->last_dev_y = dy;
                }
                ial->was_pressed = pressed;
            }
            break;

        case EV_ABS:
            switch (ev.code) {
            case ABS_MT_SLOT:
                ial->cur_slot = ev.value;
                break;
            case ABS_MT_TRACKING_ID:
                if (ial->cur_slot == 0) ial->slot_id = ev.value;  /* -1 = release */
                break;
            case ABS_MT_POSITION_X:
                if (ial->cur_slot == 0) ial->slot_x = ev.value;
                break;
            case ABS_MT_POSITION_Y:
                if (ial->cur_slot == 0) ial->slot_y = ev.value;
                break;
            case ABS_X: ial->st_x = ev.value; break;
            case ABS_Y: ial->st_y = ev.value; break;
            default: break;
            }
            break;

        case EV_KEY:
            if (ev.code == BTN_TOUCH) ial->st_pressed = ev.value;
            break;

        default: break;
        }
    }
}

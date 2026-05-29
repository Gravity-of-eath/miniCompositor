/*
 * CPU accel backend.
 *
 * Same primitives that used to live inline in compose.c. Kept in its own
 * file so the HW backends are drop-in alternatives and the CPU path
 * remains available as a verified fallback regardless of platform.
 *
 * All ops assume:
 *   - format BGRA8888
 *   - source/dest rects already clipped to surface bounds by caller
 *   - non-overlapping rects (overlapping src/dst on the same buffer
 *     would need a temp copy; we don't do that today)
 */
#define _GNU_SOURCE
#include "accel.h"
#include "log.h"

#include <stdint.h>
#include <string.h>

static int cpu_init(void)   { return 0; }
static void cpu_deinit(void) { }
static int cpu_sync(void)    { return 0; }

static int cpu_fill(const struct mc_accel_surface *d,
                    int x, int y, int w, int h, uint32_t bgra)
{
    uint8_t *base = (uint8_t *)d->virt;
    if (!base) return -1;
    for (int row = 0; row < h; row++) {
        uint32_t *p = (uint32_t *)(base + (y + row) * d->stride + x * 4);
        for (int col = 0; col < w; col++) p[col] = bgra;
    }
    return 0;
}

/* For a Y-flipped source (GL FBO bottom-up), row 0 in the dst corresponds
 * to row (src_h - 1 - sy) in the src buffer. We adjust src_y for the row
 * starting point and step backwards through the source. */
static int cpu_blit(const struct mc_accel_surface *d, int dx, int dy,
                    const struct mc_accel_surface *s, int sx, int sy,
                    int w, int h)
{
    uint8_t       *dp = (uint8_t *)d->virt;
    const uint8_t *sp = (const uint8_t *)s->virt;
    if (!dp || !sp) return -1;
    if (s->flip_y) {
        for (int row = 0; row < h; row++) {
            int src_row = s->h - 1 - (sy + row);
            memcpy(dp + (dy + row) * d->stride + dx * 4,
                   sp + src_row     * s->stride + sx * 4,
                   (size_t)w * 4);
        }
    } else {
        for (int row = 0; row < h; row++) {
            memcpy(dp + (dy + row) * d->stride + dx * 4,
                   sp + (sy + row) * s->stride + sx * 4,
                   (size_t)w * 4);
        }
    }
    return 0;
}

static int cpu_blend(const struct mc_accel_surface *d, int dx, int dy,
                     const struct mc_accel_surface *s, int sx, int sy,
                     int w, int h)
{
    uint8_t       *dp = (uint8_t *)d->virt;
    const uint8_t *sp = (const uint8_t *)s->virt;
    if (!dp || !sp) return -1;

    for (int row = 0; row < h; row++) {
        int src_row = s->flip_y ? (s->h - 1 - (sy + row)) : (sy + row);
        uint8_t       *dl = dp + (dy + row) * d->stride + dx * 4;
        const uint8_t *sl = sp + src_row     * s->stride + sx * 4;
        for (int col = 0; col < w; col++, dl += 4, sl += 4) {
            uint32_t a = sl[3];
            if (a == 0)   continue;
            if (a == 255) { dl[0] = sl[0]; dl[1] = sl[1]; dl[2] = sl[2]; dl[3] = 255; continue; }
            uint32_t ia = 255 - a;
            dl[0] = (uint8_t)((sl[0] * a + dl[0] * ia + 127) / 255);
            dl[1] = (uint8_t)((sl[1] * a + dl[1] * ia + 127) / 255);
            dl[2] = (uint8_t)((sl[2] * a + dl[2] * ia + 127) / 255);
            dl[3] = (uint8_t)(a + ((dl[3] * ia + 127) / 255));
        }
    }
    return 0;
}

const struct mc_accel_ops mc_accel_cpu = {
    .name   = "cpu",
    .init   = cpu_init,
    .deinit = cpu_deinit,
    .fill   = cpu_fill,
    .blit   = cpu_blit,
    .blend  = cpu_blend,
    .sync   = cpu_sync,
};

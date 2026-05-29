/*
 * PPM dump backend: writes the composited frame to a P6 PPM file.
 * Used on dev machines without a usable framebuffer to inspect output.
 *
 * Atomic write: writes to "<path>.tmp" then renames to "<path>".
 */
#define _GNU_SOURCE
#include "backend.h"
#include "log.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct ppm_priv {
    int       w, h;
    int       stride;       /* bytes */
    uint8_t  *buf;          /* w*h*4 BGRA8888 */
    char     *path;         /* output path */
    char     *tmppath;
    unsigned  frame_seq;
};

static int ppm_open(struct mc_backend *be, const char *arg,
                    int w_hint, int h_hint,
                    int *out_w, int *out_h, int *out_stride)
{
    if (w_hint <= 0 || h_hint <= 0) return -EINVAL;
    if (!arg) arg = "/tmp/mc-screen.ppm";

    struct ppm_priv *p = calloc(1, sizeof(*p));
    if (!p) return -ENOMEM;

    p->w      = w_hint;
    p->h      = h_hint;
    p->stride = w_hint * 4;
    p->buf    = aligned_alloc(64, (size_t)p->stride * p->h);
    if (!p->buf) { free(p); return -ENOMEM; }
    memset(p->buf, 0, (size_t)p->stride * p->h);

    p->path    = strdup(arg);
    size_t plen = strlen(arg);
    p->tmppath = malloc(plen + 5);
    if (!p->path || !p->tmppath) {
        free(p->buf); free(p->path); free(p->tmppath); free(p);
        return -ENOMEM;
    }
    snprintf(p->tmppath, plen + 5, "%s.tmp", arg);

    *out_w      = p->w;
    *out_h      = p->h;
    *out_stride = p->stride;

    be->priv = p;
    LOG_I("backend_ppm: %s %dx%d", arg, p->w, p->h);
    return 0;
}

static uint8_t *ppm_get_buffer(struct mc_backend *be)
{
    return ((struct ppm_priv *)be->priv)->buf;
}

static int ppm_present(struct mc_backend *be)
{
    struct ppm_priv *p = be->priv;
    p->frame_seq++;

    int fd = open(p->tmppath, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) { LOG_W("ppm open %s: %s", p->tmppath, strerror(errno)); return -errno; }

    char hdr[64];
    int hl = snprintf(hdr, sizeof(hdr), "P6\n%d %d\n255\n", p->w, p->h);
    if (write(fd, hdr, hl) != hl) { close(fd); return -EIO; }

    /* Convert BGRA8888 -> RGB triplets in chunks. */
    enum { CHUNK = 4096 };
    uint8_t out[CHUNK * 3];
    int total_px = p->w * p->h;
    for (int i = 0; i < total_px; i += CHUNK) {
        int n = (total_px - i < CHUNK) ? (total_px - i) : CHUNK;
        const uint8_t *src = p->buf + i * 4;
        for (int j = 0; j < n; j++) {
            out[j*3 + 0] = src[j*4 + 2]; /* R */
            out[j*3 + 1] = src[j*4 + 1]; /* G */
            out[j*3 + 2] = src[j*4 + 0]; /* B */
        }
        if (write(fd, out, n * 3) != n * 3) {
            close(fd); return -EIO;
        }
    }
    close(fd);

    if (rename(p->tmppath, p->path) < 0) {
        LOG_W("ppm rename: %s", strerror(errno));
        return -errno;
    }
    LOG_D("ppm frame %u -> %s", p->frame_seq, p->path);
    return 0;
}

static void ppm_close(struct mc_backend *be)
{
    struct ppm_priv *p = be->priv;
    if (!p) return;
    free(p->buf);
    free(p->path);
    free(p->tmppath);
    free(p);
    be->priv = NULL;
}

struct mc_backend backend_ppm = {
    .name       = "ppm",
    .open       = ppm_open,
    .get_buffer = ppm_get_buffer,
    .present    = ppm_present,
    .close      = ppm_close,
};

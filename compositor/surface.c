#define _GNU_SOURCE
#include "surface.h"
#include "transport.h"
#include "log.h"
#include "proto.h"
#include "mc_alloc.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <unistd.h>

/* Bytes per pixel for a format. */
static int format_bpp(uint8_t fmt)
{
    switch (fmt) {
    case 1: /* ARGB8888 */
    case 2: /* BGRA8888 */
        return 4;
    case 3: /* RGB565 */
        return 2;
    default:
        return 0;
    }
}

int mc_surface_default_z_order(uint8_t role)
{
    switch (role) {
    case 4: return 200;   /* TOAST — above everything */
    case 2: return 100;   /* POPUP */
    case 3: return 0;     /* BG */
    case 1: default:
        return 10;        /* FULLSCREEN */
    }
}

void mc_surface_table_init(struct mc_server *s)
{
    /* server.surfaces is part of mc_server struct (see transport.h). */
    for (int i = 0; i < MC_MAX_SURFACES; i++) {
        s->surfaces[i].sid = 0;
    }
    s->next_sid = 0;
}

static struct mc_surface *find_slot(struct mc_server *s)
{
    for (int i = 0; i < MC_MAX_SURFACES; i++)
        if (s->surfaces[i].sid == 0) return &s->surfaces[i];
    return NULL;
}

struct mc_surface *mc_surface_get(struct mc_server *s, uint32_t sid)
{
    if (sid == 0) return NULL;
    for (int i = 0; i < MC_MAX_SURFACES; i++)
        if (s->surfaces[i].sid == sid) return &s->surfaces[i];
    return NULL;
}

struct mc_surface *mc_surface_create(struct mc_server *s,
                                     int client_slot, uint32_t cid,
                                     uint16_t w, uint16_t h,
                                     uint8_t format, uint8_t role,
                                     uint8_t n_buf, int *out_err)
{
    *out_err = MC_OK;
    int bpp = format_bpp(format);
    if (!bpp) { *out_err = MC_E_INVAL; return NULL; }
    if (n_buf < 1 || n_buf > MC_BUFS_PER_SURF) { *out_err = MC_E_INVAL; return NULL; }
    if (w == 0 || h == 0)                       { *out_err = MC_E_INVAL; return NULL; }

    struct mc_surface *sf = find_slot(s);
    if (!sf) { *out_err = MC_E_NOMEM; return NULL; }

    memset(sf, 0, sizeof(*sf));
    sf->sid          = ++s->next_sid;
    sf->cid          = cid;
    sf->client_slot  = client_slot;
    sf->w            = w;
    sf->h            = h;
    sf->format       = format;
    sf->role         = role ? role : 1;
    sf->n_buf        = n_buf;
    /* stride = w * bpp (no padding). LVGL's direct_mode assumes stride
     * matches hor_res exactly. For 4-byte BGRA this is also naturally 4-byte
     * aligned. When hardware accelerators are added later we can introduce
     * a separate "alloc_stride" while keeping "logical stride" == w*bpp. */
    sf->stride       = (uint32_t)w * bpp;
    sf->z_order      = mc_surface_default_z_order(sf->role);
    sf->focus_stamp  = ++s->next_focus_stamp;
    sf->visible      = 1;
    sf->cur_scanout  = -1;
    sf->pending_idx  = -1;

    size_t buf_size = (size_t)sf->stride * h;

    /* Allocate buffers via mc_alloc: picks dma-heap / ion / memfd based on
     * what the host supports. Each buffer's fd is mmap-able + SCM_RIGHTS
     * passable (client API unchanged); the (fd, phys) pair lets us hand
     * the buffer to G2D/RGA later. */
    int ok = 1;
    int phys_count = 0;
    for (int i = 0; i < sf->n_buf; i++) {
        struct mc_alloc_buf ab;
        if (mc_alloc_create(&ab, buf_size) < 0) {
            LOG_E("mc_alloc_create failed for buf %d", i);
            ok = 0; break;
        }
        sf->bufs[i].shm_fd       = ab.fd;
        sf->bufs[i].map          = (uint8_t *)ab.map;
        sf->bufs[i].size         = ab.size;
        sf->bufs[i].phys         = ab.phys;
        sf->bufs[i].mmap_offset  = ab.mmap_offset;
        sf->bufs[i].alloc_handle = ab._g2d_sel;
        sf->bufs[i].state        = MC_BUF_FREE;
        if (ab.phys) phys_count++;
    }

    int efd = -1;
    if (ok) {
        efd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (efd < 0) { LOG_E("eventfd: %s", strerror(errno)); ok = 0; }
    }
    sf->event_fd = efd;

    if (!ok) {
        for (int i = 0; i < MC_BUFS_PER_SURF; i++) {
            if (sf->bufs[i].map || sf->bufs[i].shm_fd > 0) {
                struct mc_alloc_buf tmp = {
                    .fd       = sf->bufs[i].shm_fd,
                    .map      = sf->bufs[i].map,
                    .size     = sf->bufs[i].size,
                    .phys     = sf->bufs[i].phys,
                    ._g2d_sel = sf->bufs[i].alloc_handle,
                };
                /* Heuristic: backend matches what we just used. */
                mc_alloc_destroy(&tmp);
            }
        }
        if (efd > 0) close(efd);
        sf->sid = 0;
        *out_err = MC_E_NOMEM;
        return NULL;
    }

    LOG_I("surface sid=%u cid=%u %ux%u stride=%u role=%u n_buf=%u "
          "alloc=%s phys=%d/%d",
          sf->sid, sf->cid, sf->w, sf->h, sf->stride, sf->role, sf->n_buf,
          mc_alloc_backend_name(), phys_count, sf->n_buf);
    return sf;
}

void mc_surface_destroy(struct mc_server *s, uint32_t sid)
{
    struct mc_surface *sf = mc_surface_get(s, sid);
    if (!sf) return;
    LOG_I("destroy surface sid=%u", sf->sid);
    for (int i = 0; i < sf->n_buf; i++) {
        if (sf->bufs[i].map || sf->bufs[i].shm_fd > 0) {
            struct mc_alloc_buf tmp = {
                .fd          = sf->bufs[i].shm_fd,
                .map         = sf->bufs[i].map,
                .size        = sf->bufs[i].size,
                .phys        = sf->bufs[i].phys,
                .mmap_offset = sf->bufs[i].mmap_offset,
                ._g2d_sel    = sf->bufs[i].alloc_handle,
            };
            mc_alloc_destroy(&tmp);
        }
    }
    if (sf->event_fd > 0) close(sf->event_fd);
    memset(sf, 0, sizeof(*sf));
}

void mc_surface_destroy_for_client(struct mc_server *s, int client_slot)
{
    for (int i = 0; i < MC_MAX_SURFACES; i++) {
        if (s->surfaces[i].sid != 0 && s->surfaces[i].client_slot == client_slot)
            mc_surface_destroy(s, s->surfaces[i].sid);
    }
}

int mc_surface_commit(struct mc_surface *sf, int buf_idx,
                      const struct mc_rect_i *dmg, int n_dmg)
{
    if (buf_idx < 0 || buf_idx >= sf->n_buf) return MC_E_INVAL;
    sf->bufs[buf_idx].state = MC_BUF_READY;
    sf->pending_idx = buf_idx;

    sf->pending_dmg_n = 0;
    if (dmg && n_dmg > 0) {
        int n = n_dmg > MC_MAX_DAMAGE_RECTS ? MC_MAX_DAMAGE_RECTS : n_dmg;
        for (int i = 0; i < n; i++) sf->pending_dmg[i] = dmg[i];
        sf->pending_dmg_n = n;
    }
    return MC_OK;
}

/* Bubble sort by z_order asc; few surfaces so O(n^2) is fine. */
static int compare_z(const void *a, const void *b)
{
    const struct mc_surface * const *pa = a;
    const struct mc_surface * const *pb = b;
    return (*pa)->z_order - (*pb)->z_order;
}

void mc_surface_foreach_visible(struct mc_server *s,
                                int (*cb)(struct mc_surface *, void *),
                                void *user)
{
    struct mc_surface *list[MC_MAX_SURFACES];
    int n = 0;
    for (int i = 0; i < MC_MAX_SURFACES; i++) {
        if (s->surfaces[i].sid && s->surfaces[i].visible)
            list[n++] = &s->surfaces[i];
    }
    qsort(list, n, sizeof(list[0]), compare_z);
    for (int i = 0; i < n; i++) {
        if (cb(list[i], user) != 0) break;
    }
}

#define _GNU_SOURCE
#include "transport.h"
#include "backend.h"
#include "compose.h"
#include "lifecycle.h"
#include "log.h"
#include "surface.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#define RX_CAP (MC_HDR_BYTES + MC_MAX_PAYLOAD)

extern volatile sig_atomic_t g_running;

static struct mc_client *find_slot(struct mc_server *s)
{
    for (int i = 0; i < MC_MAX_CLIENTS; i++)
        if (s->clients[i].sock <= 0) return &s->clients[i];
    return NULL;
}

static struct mc_client *find_by_fd(struct mc_server *s, int fd)
{
    for (int i = 0; i < MC_MAX_CLIENTS; i++)
        if (s->clients[i].sock == fd) return &s->clients[i];
    return NULL;
}

static int client_slot_index(struct mc_server *s, struct mc_client *c)
{
    return (int)(c - s->clients);
}

static int send_error(int sock, uint32_t serial, uint32_t code, const char *msg)
{
    uint8_t buf[256];
    struct mc_builder b;
    mc_builder_init(&b, buf, sizeof(buf));
    mc_put_u32(&b, MC_T_CODE, code);
    if (msg) mc_put_str(&b, MC_T_MSG, msg);
    size_t flen = mc_builder_finalize(&b, MC_SV_ERROR, serial);
    return mc_send_frame(sock, buf, flen, NULL, 0);
}

static int send_welcome(struct mc_server *s, struct mc_client *c, uint32_t serial)
{
    uint8_t buf[128];
    struct mc_builder b;
    mc_builder_init(&b, buf, sizeof(buf));
    mc_put_u32(&b, MC_T_CLIENT_ID,   c->cid);
    mc_put_u16(&b, MC_T_WIDTH,       s->screen_w);
    mc_put_u16(&b, MC_T_HEIGHT,      s->screen_h);
    mc_put_u8 (&b, MC_T_FORMAT,      s->screen_format);
    mc_put_u32(&b, MC_T_SERVER_CAPS, s->server_caps);
    size_t flen = mc_builder_finalize(&b, MC_SV_WELCOME, serial);
    if (flen == 0) return -EINVAL;
    return mc_send_frame(c->sock, buf, flen, NULL, 0);
}

static int send_surface_ok(struct mc_client *c, struct mc_surface *sf,
                           uint32_t serial)
{
    uint8_t buf[256];
    struct mc_builder b;
    mc_builder_init(&b, buf, sizeof(buf));
    mc_put_u32(&b, MC_T_SID,    sf->sid);
    mc_put_u32(&b, MC_T_STRIDE, sf->stride);
    mc_put_u32(&b, MC_T_SIZE,   (uint32_t)sf->bufs[0].size);
    mc_put_u8 (&b, MC_T_N_BUF,  (uint8_t)sf->n_buf);

    /* Pass per-buffer mmap offset to the client. Memfd path always uses
     * 0; g2d-mem stores phys here so the client's mmap(fd, ..., offset)
     * reaches the same physical pages we wrote. We always send this tag
     * (just zeros for memfd) so the client doesn't need to special-case
     * "tag missing". */
    uint32_t offs[MC_BUFS_PER_SURF];
    for (int i = 0; i < sf->n_buf; i++) offs[i] = sf->bufs[i].mmap_offset;
    mc_put_bin(&b, MC_T_PHYS_LIST, offs, (size_t)sf->n_buf * sizeof(uint32_t));

    size_t flen = mc_builder_finalize(&b, MC_SV_SURFACE_OK, serial);

    int fds[MC_BUFS_PER_SURF + 1];
    int n_fds = 0;
    for (int i = 0; i < sf->n_buf; i++) fds[n_fds++] = sf->bufs[i].shm_fd;
    fds[n_fds++] = sf->event_fd;
    return mc_send_frame(c->sock, buf, flen, fds, n_fds);
}

static void close_client(struct mc_server *s, struct mc_client *c)
{
    if (c->sock <= 0) return;
    LOG_I("client cid=%u '%s' pid=%d disconnected",
          c->cid, c->name, (int)c->pid);
    int slot = client_slot_index(s, c);
    int had_surfaces = 0;
    for (int i = 0; i < MC_MAX_SURFACES; i++) {
        if (s->surfaces[i].sid && s->surfaces[i].client_slot == slot) {
            had_surfaces = 1; break;
        }
    }
    mc_surface_destroy_for_client(s, slot);
    epoll_ctl(s->epoll_fd, EPOLL_CTL_DEL, c->sock, NULL);
    close(c->sock);
    memset(c, 0, sizeof(*c));

    /* Topology changed: refresh display so the gone surface stops "ghosting". */
    if (had_surfaces) {
        LOG_I("topology change (client gone) -> recompose");
        mc_request_recompose(s);
        mc_lifecycle_recompute(s);
    }
}

static int handle_hello(struct mc_server *s, struct mc_client *c,
                        const void *payload, size_t plen, uint32_t serial)
{
    if (c->hello_done) {
        send_error(c->sock, serial, MC_E_PROTO, "duplicate HELLO");
        return -1;
    }
    char     name[64] = {0};
    uint32_t version = 0, pid = 0, caps = 0;
    mc_get_str(payload, plen, MC_T_NAME, name, sizeof(name));
    mc_get_u32(payload, plen, MC_T_VERSION, &version);
    mc_get_u32(payload, plen, MC_T_PID,     &pid);
    mc_get_u32(payload, plen, MC_T_CAPS,    &caps);

    uint16_t cli_major = (version >> 16) & 0xffff;
    if (cli_major != MC_VERSION_MAJOR) {
        send_error(c->sock, serial, MC_E_PROTO, "version mismatch");
        return -1;
    }

    {
        size_t nl = strnlen(name, sizeof(c->name) - 1);
        memcpy(c->name, name, nl);
        c->name[nl] = '\0';
    }
    c->pid  = (pid_t)pid;
    c->caps = caps;
    c->cid  = ++s->next_cid;
    c->hello_done = 1;

    LOG_I("client cid=%u '%s' pid=%d ver=%u.%u caps=0x%x",
          c->cid, c->name, (int)c->pid,
          cli_major, (uint16_t)(version & 0xffff), caps);

    return send_welcome(s, c, serial) < 0 ? -1 : 0;
}

static int handle_create_surface(struct mc_server *s, struct mc_client *c,
                                 const void *payload, size_t plen,
                                 uint32_t serial)
{
    uint16_t w = 0, h = 0;
    uint8_t  fmt = 0, role = 0, n_buf = 0, modal = 0, buf_type = 1;
    uint8_t  flip_y = 0;
    int16_t  px = 0, py = 0;
    mc_get_u16(payload, plen, MC_T_WIDTH,    &w);
    mc_get_u16(payload, plen, MC_T_HEIGHT,   &h);
    mc_get_u8 (payload, plen, MC_T_FORMAT,   &fmt);
    mc_get_u8 (payload, plen, MC_T_ROLE,     &role);
    mc_get_u8 (payload, plen, MC_T_N_BUF,    &n_buf);
    mc_get_u8 (payload, plen, MC_T_MODAL,    &modal);
    mc_get_u8 (payload, plen, MC_T_BUF_TYPE, &buf_type);
    mc_get_u8 (payload, plen, MC_T_FLIP_Y,   &flip_y);
    mc_get_i16(payload, plen, MC_T_POPUP_X,  &px);
    mc_get_i16(payload, plen, MC_T_POPUP_Y,  &py);

    if (buf_type != 1) {  /* Phase 0 supports only SHM */
        send_error(c->sock, serial, MC_E_NOTSUP, "dmabuf not yet supported");
        return 0;
    }
    if (n_buf == 0) n_buf = 2;

    int err;
    struct mc_surface *sf = mc_surface_create(
        s, client_slot_index(s, c), c->cid, w, h, fmt, role, n_buf, &err);
    if (!sf) {
        send_error(c->sock, serial, (uint32_t)err, "create_surface failed");
        return 0;
    }
    sf->x = px; sf->y = py; sf->modal = modal;
    sf->flip_y = flip_y;
    if (flip_y) LOG_I("surface sid=%u flip_y=1 (GL FBO bottom-up source)",
                      sf->sid);

    if (send_surface_ok(c, sf, serial) < 0) {
        mc_surface_destroy(s, sf->sid);
        return -1;
    }
    mc_lifecycle_recompute(s);
    return 0;
}

static int handle_commit(struct mc_server *s, struct mc_client *c,
                         const void *payload, size_t plen, uint32_t serial)
{
    (void)c;
    uint32_t sid = 0;
    uint8_t  idx = 0;
    mc_get_u32(payload, plen, MC_T_SID,     &sid);
    mc_get_u8 (payload, plen, MC_T_BUF_IDX, &idx);

    /* Decode damage rects. Each rect is 8 bytes (i16 x,y,w,h LE). */
    struct mc_rect_i dmg[MC_MAX_DAMAGE_RECTS];
    int n_dmg = 0;
    const void *dmg_bytes = NULL;
    size_t      dmg_len = 0;
    if (mc_get_bin(payload, plen, MC_T_DAMAGE, &dmg_bytes, &dmg_len) == 0) {
        int n = (int)(dmg_len / 8);
        if (n > MC_MAX_DAMAGE_RECTS) n = MC_MAX_DAMAGE_RECTS;
        const uint8_t *p = (const uint8_t *)dmg_bytes;
        for (int i = 0; i < n; i++) {
            int16_t x = (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
            int16_t y = (int16_t)((uint16_t)p[2] | ((uint16_t)p[3] << 8));
            int16_t w = (int16_t)((uint16_t)p[4] | ((uint16_t)p[5] << 8));
            int16_t h = (int16_t)((uint16_t)p[6] | ((uint16_t)p[7] << 8));
            dmg[i] = (struct mc_rect_i){ x, y, w, h };
            p += 8;
        }
        n_dmg = n;
    }

    struct mc_surface *sf = mc_surface_get(s, sid);
    if (!sf || sf->client_slot != client_slot_index(s, c)) {
        send_error(c->sock, serial, MC_E_NOENT, "no such surface");
        return 0;
    }

    /* First-commit detection: before mc_surface_commit applies the new
     * buffer, cur_scanout is -1 if and only if this is the surface's
     * very first commit. A first commit may unblock a deferred occlusion
     * decision (we treat surfaces that have never committed as
     * "not-an-occluder" in lifecycle.c so a brand-new fullscreen
     * doesn't kick the current one off-screen before it has anything
     * to show). After the commit lands we re-evaluate lifecycle AND
     * force a full re-paint so both fb halves pick up the new content
     * atomically -- otherwise damage-only compose can leave one half
     * with stale "previous fullscreen alone" pixels visible after PAN. */
    int first_commit = (sf->cur_scanout < 0);

    int r = mc_surface_commit(sf, idx, dmg, n_dmg);
    if (r != MC_OK) {
        send_error(c->sock, serial, (uint32_t)r, "bad buf_idx");
        return 0;
    }

    if (first_commit) {
        /* Order matters here. cur_scanout transitions from -1 to a real
         * buffer index inside mc_compose_frame (the "rotate" step). So we
         * MUST run a compose first (any compose; we use the full-sync one),
         * then call lifecycle recompute -- only then will is_occluded see
         * this surface as having real content and use it as an occluder. */
        LOG_I("first commit sid=%u -> full re-sync", sid);
        mc_request_recompose(s);
        mc_lifecycle_recompute(s);
        return 0;
    }

    /* Defer the actual compose to end of this epoll batch. The reason:
     * three clients (AWTK + 2 LVGL fullscreens + popup) commonly all have
     * COMMITs ready in the SAME epoll_wait wakeup. If we compose
     * synchronously per commit, we do 3 full-screen composes back-to-back
     * (~15ms each on T507 CPU path), so the batch takes ~45ms and the
     * compositor's event loop is starved -- popup OK clicks (input events)
     * stack up behind compose work. By flagging instead, all 3 commits
     * are absorbed and a SINGLE compose runs after the batch, picking up
     * every surface's latest pending_idx. Net effect: each client's
     * wait_buf_free still unblocks promptly (compose still runs every
     * batch), but compose count drops from N-per-batch to 1-per-batch.
     * Worst case (no other commits in batch) we still compose once. */
    s->compose_needed = 1;
    return 0;
}

static int handle_destroy_surface(struct mc_server *s, struct mc_client *c,
                                  const void *payload, size_t plen,
                                  uint32_t serial)
{
    (void)c; (void)serial;
    uint32_t sid = 0;
    mc_get_u32(payload, plen, MC_T_SID, &sid);
    struct mc_surface *sf = mc_surface_get(s, sid);
    if (sf && sf->client_slot == client_slot_index(s, c)) {
        mc_surface_destroy(s, sid);
        LOG_I("topology change (destroy sid=%u) -> recompose", sid);
        mc_request_recompose(s);
        mc_lifecycle_recompute(s);
    }
    return 0;
}

static int handle_set_role(struct mc_server *s, struct mc_client *c,
                           const void *payload, size_t plen, uint32_t serial)
{
    (void)serial;
    uint32_t sid = 0;
    uint8_t  role = 0, modal = 0;
    int16_t  px = 0, py = 0;
    mc_get_u32(payload, plen, MC_T_SID,     &sid);
    mc_get_u8 (payload, plen, MC_T_ROLE,    &role);
    mc_get_u8 (payload, plen, MC_T_MODAL,   &modal);
    mc_get_i16(payload, plen, MC_T_POPUP_X, &px);
    mc_get_i16(payload, plen, MC_T_POPUP_Y, &py);

    struct mc_surface *sf = mc_surface_get(s, sid);
    if (!sf || sf->client_slot != client_slot_index(s, c)) return 0;
    int z_changed = 0, geom_changed = (sf->x != px || sf->y != py);
    if (role) {
        if (sf->role != role) z_changed = 1;
        sf->role = role;
    }
    sf->modal = modal;
    sf->x = px; sf->y = py;
    sf->z_order = mc_surface_default_z_order(sf->role);
    if (z_changed || geom_changed) {
        mc_request_recompose(s);
        mc_lifecycle_recompute(s);
    }
    return 0;
}

static int handle_frame(struct mc_server *s, struct mc_client *c,
                        const uint8_t *frame, size_t flen)
{
    uint16_t type   = (uint16_t)frame[2] | ((uint16_t)frame[3] << 8);
    uint32_t plen   = (uint32_t)frame[4]
                    | ((uint32_t)frame[5] << 8)
                    | ((uint32_t)frame[6] << 16)
                    | ((uint32_t)frame[7] << 24);
    uint32_t serial = (uint32_t)frame[8]
                    | ((uint32_t)frame[9] << 8)
                    | ((uint32_t)frame[10] << 16)
                    | ((uint32_t)frame[11] << 24);
    const void *payload = frame + MC_HDR_BYTES;
    (void)flen;

    LOG_D("rx type=0x%02x plen=%u serial=%u from fd=%d",
          type, plen, serial, c->sock);

    if (!c->hello_done && type != MC_CL_HELLO) {
        send_error(c->sock, serial, MC_E_PROTO, "HELLO required first");
        return -1;
    }

    switch (type) {
    case MC_CL_HELLO:           return handle_hello(s, c, payload, plen, serial);
    case MC_CL_CREATE_SURFACE:  return handle_create_surface(s, c, payload, plen, serial);
    case MC_CL_DESTROY_SURFACE: return handle_destroy_surface(s, c, payload, plen, serial);
    case MC_CL_COMMIT:          return handle_commit(s, c, payload, plen, serial);
    case MC_CL_SET_ROLE:        return handle_set_role(s, c, payload, plen, serial);

    case MC_CL_BYE:
        LOG_I("client cid=%u sent BYE", c->cid);
        return -1;

    case MC_CL_BUS_SUB: {
        char topic[MC_BUS_MAX_TOPIC_LEN];
        if (mc_get_str(payload, plen, MC_T_BUS_TOPIC, topic, sizeof(topic)) < 0) {
            send_error(c->sock, serial, MC_E_INVAL, "BUS_SUB missing topic");
            return 0;
        }
        int r = mc_bus_sub_add(c, topic);
        if (r < 0) {
            send_error(c->sock, serial, (uint32_t)(-r), "BUS_SUB rejected");
        } else {
            LOG_I("bus: cid=%u subscribed to '%s'", c->cid, topic);
        }
        return 0;
    }
    case MC_CL_BUS_UNSUB: {
        char topic[MC_BUS_MAX_TOPIC_LEN];
        if (mc_get_str(payload, plen, MC_T_BUS_TOPIC, topic, sizeof(topic)) < 0)
            return 0;
        mc_bus_sub_remove(c, topic);
        return 0;
    }
    case MC_CL_BUS_PUB: {
        char topic[MC_BUS_MAX_TOPIC_LEN];
        if (mc_get_str(payload, plen, MC_T_BUS_TOPIC, topic, sizeof(topic)) < 0) {
            send_error(c->sock, serial, MC_E_INVAL, "BUS_PUB missing topic");
            return 0;
        }
        const void *pl = NULL; size_t pl_len = 0;
        mc_get_bin(payload, plen, MC_T_BUS_PAYLOAD, &pl, &pl_len);
        mc_bus_publish(s, c, topic, pl, pl_len);
        return 0;
    }

    case MC_CL_REQUEST_FOCUS: {
        uint32_t sid = 0;
        mc_get_u32(payload, plen, MC_T_SID, &sid);
        struct mc_surface *sf = mc_surface_get(s, sid);
        if (!sf) {
            send_error(c->sock, serial, MC_E_NOENT, "no such surface");
            return 0;
        }
        /* Permission check: only the owner can refocus its own surface.
         * Any client requesting focus on someone else's surface would be
         * a UX violation as much as a security one. */
        if (sf->client_slot != client_slot_index(s, c)) {
            send_error(c->sock, serial, MC_E_PERM, "not the surface owner");
            return 0;
        }
        uint32_t old = sf->focus_stamp;
        sf->focus_stamp = ++s->next_focus_stamp;
        LOG_I("focus: sid=%u stamp %u -> %u (z=%d role=%u)",
              sf->sid, old, sf->focus_stamp, sf->z_order, sf->role);
        /* Repaint + re-evaluate occlusion so old foreground apps go HIDDEN
         * and the requesting one (if it was HIDDEN) goes VISIBLE. */
        mc_request_recompose(s);
        mc_lifecycle_recompute(s);
        return 0;
    }

    default:
        LOG_W("unknown msg type 0x%02x from cid=%u", type, c->cid);
        send_error(c->sock, serial, MC_E_PROTO, "unknown type");
        return 0;
    }
}

static int on_client_readable(struct mc_server *s, struct mc_client *c)
{
    int fds[MC_MAX_FDS];
    int n_fds = 0;
    ssize_t r = mc_recv_frame(c->sock, s->rxbuf, s->rxcap, fds, &n_fds);
    if (r == 0) return -1;
    if (r < 0) {
        LOG_W("recv error: %s", strerror((int)-r));
        return -1;
    }
    /* Phase 0: clients don't send fds (no dmabuf). Close any unexpected. */
    for (int i = 0; i < n_fds; i++) close(fds[i]);

    return handle_frame(s, c, s->rxbuf, (size_t)r);
}

static int on_accept(struct mc_server *s)
{
    struct sockaddr_un sa;
    socklen_t sl = sizeof(sa);
    int cs = accept4(s->listen_fd, (struct sockaddr *)&sa, &sl,
                     SOCK_CLOEXEC | SOCK_NONBLOCK);
    if (cs < 0) { LOG_W("accept: %s", strerror(errno)); return -1; }
    int flags = fcntl(cs, F_GETFL, 0);
    fcntl(cs, F_SETFL, flags & ~O_NONBLOCK);

    struct mc_client *c = find_slot(s);
    if (!c) { LOG_W("too many clients"); close(cs); return -1; }
    c->sock = cs;
    c->cid  = 0;
    c->hello_done = 0;

    struct epoll_event ev = { .events = EPOLLIN | EPOLLRDHUP, .data.fd = cs };
    if (epoll_ctl(s->epoll_fd, EPOLL_CTL_ADD, cs, &ev) < 0) {
        LOG_E("epoll_ctl add: %s", strerror(errno));
        close(cs); c->sock = 0; return -1;
    }
    LOG_I("new client fd=%d (slot %ld)", cs, (long)client_slot_index(s, c));
    return 0;
}

int mc_server_init(struct mc_server *s, const char *sock_path)
{
    memset(s, 0, sizeof(*s));
    s->screen_w      = 800;
    s->screen_h      = 480;
    s->screen_format = 2;
    s->server_caps   = MC_CAP_BUS | MC_CAP_MULTI_SURFACE;
    s->next_cid      = 0;
    mc_surface_table_init(s);

    s->rxcap = RX_CAP;
    s->rxbuf = malloc(s->rxcap);
    if (!s->rxbuf) return -ENOMEM;

    unlink(sock_path);

    s->listen_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (s->listen_fd < 0) return -errno;

    struct sockaddr_un sa;
    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, sock_path, sizeof(sa.sun_path) - 1);

    if (bind(s->listen_fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        LOG_E("bind %s: %s", sock_path, strerror(errno));
        return -errno;
    }
    chmod(sock_path, 0660);

    if (listen(s->listen_fd, 8) < 0) return -errno;

    s->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (s->epoll_fd < 0) return -errno;

    struct epoll_event ev = { .events = EPOLLIN, .data.fd = s->listen_fd };
    if (epoll_ctl(s->epoll_fd, EPOLL_CTL_ADD, s->listen_fd, &ev) < 0)
        return -errno;

    LOG_I("listening on %s", sock_path);
    return 0;
}

void mc_server_run(struct mc_server *s)
{
    struct epoll_event evs[MC_MAX_CLIENTS + 4];
    /* Compose rate limit. CPU compose on T507 is ~100ms for a full-screen
     * fullscreen + popup (uncached fb DRAM write at ~50MB/s). If we
     * composed on every batch, popup's 60Hz blink alone would peg the
     * compositor at 100% CPU and starve every client's wait_buf_free,
     * which makes seekbar drags lag by hundreds of ms. We cap at ~60Hz
     * which is faster than the display can show new frames anyway and
     * leaves the compositor headroom to drain epoll batches promptly so
     * input events (which share the same epoll loop) get to clients
     * with low latency. Override with MC_COMPOSE_HZ=N. */
    long compose_hz = 60;
    {
        const char *e = getenv("MC_COMPOSE_HZ");
        if (e && atoi(e) > 0) compose_hz = atoi(e);
    }
    const long min_compose_us = 1000000 / compose_hz;
    struct timespec last_compose = { 0, 0 };
    while (g_running) {
        int wait_ms = -1;
        /* If a compose is pending but throttled, wait only until the
         * throttle window expires so we can compose then. */
        if (s->compose_needed) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            long elapsed_us = (long)(
                (now.tv_sec  - last_compose.tv_sec ) * 1000000 +
                (now.tv_nsec - last_compose.tv_nsec) / 1000);
            long remaining_us = min_compose_us - elapsed_us;
            wait_ms = remaining_us > 0 ? (int)(remaining_us / 1000) + 1 : 0;
        }
        int n = epoll_wait(s->epoll_fd, evs,
                           (int)(sizeof(evs)/sizeof(evs[0])), wait_ms);
        if (n < 0) {
            if (errno == EINTR) continue;
            LOG_E("epoll_wait: %s", strerror(errno));
            break;
        }
        for (int i = 0; i < n; i++) {
            int fd = evs[i].data.fd;

            if (fd == s->listen_fd) { on_accept(s); continue; }

            if (s->ial_open && fd == mc_ial_fd(&s->ial)) {
                /* Touch event(s) waiting; decode and dispatch. */
                (void)mc_ial_pump(&s->ial, mc_input_on_touch, s);
                continue;
            }

            struct mc_client *c = find_by_fd(s, fd);
            if (!c) {
                LOG_W("event for unknown fd %d", fd);
                epoll_ctl(s->epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                close(fd);
                continue;
            }
            int has_in  = evs[i].events & EPOLLIN;
            int has_hup = evs[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP);

            if (has_in && has_hup) {
                while (on_client_readable(s, c) >= 0)
                    ;
                close_client(s, c);
            } else if (has_in) {
                if (on_client_readable(s, c) < 0)
                    close_client(s, c);
            } else if (has_hup) {
                close_client(s, c);
            }
        }
        /* After consuming the whole epoll batch, compose if needed AND
         * the throttle window has elapsed. */
        if (s->compose_needed) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            long elapsed_us = (long)(
                (now.tv_sec  - last_compose.tv_sec ) * 1000000 +
                (now.tv_nsec - last_compose.tv_nsec) / 1000);
            if (last_compose.tv_sec == 0 || elapsed_us >= min_compose_us) {
                s->compose_needed = 0;
                mc_compose_frame(s, s->backend);
                last_compose = now;
            }
            /* else: throttled. Next epoll_wait uses a short timeout so
             * we come back and compose as soon as the window opens. */
        }
    }
}

void mc_server_fini(struct mc_server *s)
{
    for (int i = 0; i < MC_MAX_SURFACES; i++) {
        if (s->surfaces[i].sid)
            mc_surface_destroy(s, s->surfaces[i].sid);
    }
    for (int i = 0; i < MC_MAX_CLIENTS; i++)
        if (s->clients[i].sock > 0) close(s->clients[i].sock);
    if (s->epoll_fd > 0) close(s->epoll_fd);
    if (s->listen_fd > 0) close(s->listen_fd);
    free(s->rxbuf);
}

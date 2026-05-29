/*
 * libmc public API.
 * Phase 0 subset: connect/hello, single SHM surface, commit, basic dispatch.
 */
#ifndef MC_H
#define MC_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mc_ctx     mc_ctx_t;
typedef struct mc_surface mc_surface_t;

typedef enum {
    MC_FMT_ARGB8888 = 1,
    MC_FMT_BGRA8888 = 2,
    MC_FMT_RGB565   = 3,
} mc_format_t;

typedef enum {
    MC_ROLE_FULLSCREEN = 1,
    MC_ROLE_POPUP      = 2,
    MC_ROLE_BG         = 3,
} mc_role_t;

typedef struct { int16_t x, y, w, h; } mc_rect_t;

typedef struct {
    uint32_t client_id;
    uint16_t screen_w;
    uint16_t screen_h;
    uint8_t  screen_format;
    uint32_t server_caps;
} mc_screen_info_t;

/* ---- connection ---- */
mc_ctx_t *mc_connect(const char *app_name);
void      mc_disconnect(mc_ctx_t *);
int       mc_fd(mc_ctx_t *);
int       mc_get_screen_info(mc_ctx_t *, mc_screen_info_t *out);
void      mc_set_socket_path(const char *path);

/* Returns 1 while the connection to the compositor is healthy, 0 once any
 * I/O has observed the peer going away. Once it goes to 0, all blocking
 * API calls fail fast (no long timeouts); the caller should exit and let
 * a launcher restart it. We deliberately don't auto-reconnect: surfaces,
 * sids, popup positions and LVGL state would all need to be rebuilt and
 * that's better solved at the supervisor layer. */
int       mc_alive(mc_ctx_t *);

int         mc_last_error(void);
const char *mc_strerror(int err);

/* Surface create flags. Bit 0: client buffers are stored bottom-up
 * (GL FBO origin). Set by GL/EGL clients (AWTK over FBO); CPU clients
 * leave it cleared. The compositor inverts src_y when blitting flipped
 * surfaces so the GL coordinate convention reaches the screen with
 * top-left origin like every other surface. */
#define MC_SURF_FLAG_FLIP_Y    (1u << 0)

/* ---- surface (SHM path) ---- */
mc_surface_t *mc_surface_create_shm(mc_ctx_t *,
                                    int w, int h,
                                    mc_format_t fmt,
                                    mc_role_t role,
                                    int n_buf);

/* Same as mc_surface_create_shm() but takes an extra `flags` bitmask
 * (MC_SURF_FLAG_*). New clients can use this directly; callers of the
 * simpler form behave as if flags=0. */
mc_surface_t *mc_surface_create_shm_ex(mc_ctx_t *,
                                       int w, int h,
                                       mc_format_t fmt,
                                       mc_role_t role,
                                       int n_buf,
                                       uint32_t flags);

/* Set popup position before first commit. */
int mc_surface_set_popup_pos(mc_surface_t *, int x, int y);

/* Ask the compositor to bring this surface to the front of its z-class.
 * For a FULLSCREEN surface this is the "switch app to foreground" gesture.
 * If the surface was HIDDEN it will be made VISIBLE; whichever other
 * fullscreen used to be on top will be HIDDEN.
 *
 * Returns 0 on success, negative MC_E_* on failure.
 */
int mc_surface_request_focus(mc_surface_t *);

/* Acquire next writable buffer. Blocks until one is FREE.
 * Returns pointer to pixel memory (size = stride * h). */
void *mc_surface_acquire(mc_surface_t *, int *out_stride);

/* Commit current acquired buffer. damage may be NULL/0 (full surface). */
int   mc_surface_commit (mc_surface_t *, const mc_rect_t *damage, int n_rect);

void  mc_surface_destroy(mc_surface_t *);

/* Direct-buffer access (for LVGL direct_mode or any client that wants to
 * own buffer rotation itself). Both buffers are mapped at surface creation,
 * so these calls don't allocate or block. */
void *mc_surface_buf_at(mc_surface_t *, int idx, int *out_stride);
int   mc_surface_n_buf (mc_surface_t *);
int   mc_surface_size_bytes(mc_surface_t *);   /* stride * height for one buf */

/* GPU-side access: returns the underlying dma-buf / shm fd for buffer `idx`.
 * Pass this to EGL via EGL_LINUX_DMA_BUF_EXT so a GLES context can bind it
 * as an FBO color attachment and render directly into the shared buffer.
 * Returns -1 if `idx` is out of range. The fd is owned by libmc — do not
 * close it. */
int   mc_surface_buf_fd    (mc_surface_t *, int idx);
int   mc_surface_buf_stride(mc_surface_t *);

/* Block until buf `idx` is FREE on the client side (compositor has released it).
 * Returns 0 ok, negative MC_E_* on failure (signal, stalled, etc). */
int mc_surface_wait_buf_free(mc_surface_t *, int idx);

/* Commit a specific buffer index. Caller must have previously waited for it
 * to be FREE (via mc_surface_wait_buf_free). Marks it BUSY locally and sends
 * CL_COMMIT to the compositor. */
int mc_surface_commit_idx(mc_surface_t *, int idx,
                          const mc_rect_t *dmg, int n_dmg);

/* ---- dispatch ----
 * Returns >0 if an event was returned, 0 if no event ready, <0 on error. */
typedef enum {
    MC_EV_NONE = 0,
    MC_EV_FRAME_DONE,
    MC_EV_BUFFER_FREE,    /* eventfd ticked; mostly internal */
    MC_EV_TOUCH,          /* touch.* populated */
    MC_EV_BUS,            /* bus.* populated */
    MC_EV_LIFECYCLE,      /* lc.* populated */
} mc_event_kind_t;

/* Surface lifecycle states (mc_event_t.lc.state values). */
typedef enum {
    MC_LIFECYCLE_VISIBLE   = 1,
    MC_LIFECYCLE_HIDDEN    = 2,
    MC_LIFECYCLE_SUSPENDED = 3,
    MC_LIFECYCLE_RESUMED   = 4,
} mc_lifecycle_state_e;

typedef enum {
    MC_TOUCH_STATE_DOWN = 1,
    MC_TOUCH_STATE_MOVE = 2,
    MC_TOUCH_STATE_UP   = 3,
} mc_touch_state_e;

typedef struct {
    mc_event_kind_t kind;
    uint32_t        sid;
    struct {
        int16_t x, y;        /* surface-local coords */
        uint8_t slot;
        uint8_t state;       /* mc_touch_state_e */
        uint32_t t_ms;
    } touch;
    struct {
        /* Pointers borrow into the mc_ctx_t's rx buffer; valid only until
         * the NEXT call to mc_dispatch(). Copy if you need to keep them.
         * topic/sender are NUL-terminated; data may be binary. */
        const char *topic;
        const char *sender;
        const void *data;
        uint32_t    len;
    } bus;
    struct {
        uint8_t state;     /* mc_lifecycle_state_e */
    } lc;
} mc_event_t;

/* Drain one event from the compositor socket. timeout_ms is currently
 * a hint (0 = nonblocking, >0 not yet implemented; treated as 0). */
int mc_dispatch(mc_ctx_t *, mc_event_t *out, int timeout_ms);

/*
 * Bus (pub/sub).
 *
 * Topics use '/'-separated segments. Subscriptions support a single
 * trailing '/' + '*' wildcard, or bare '*' meaning all topics.
 * Examples:
 *   mc_bus_subscribe(ctx, "vehicle/speed");
 *   mc_bus_subscribe(ctx, "ui/...");        // pass "ui/" + "*"
 *   mc_bus_subscribe(ctx, "...");           // pass "*"
 *
 * Publishers don't receive their own messages. Payload max 4 KB.
 * Returns 0 on success, negative MC_E_* on failure.
 */
int mc_bus_subscribe  (mc_ctx_t *, const char *topic);
int mc_bus_unsubscribe(mc_ctx_t *, const char *topic);
int mc_bus_publish    (mc_ctx_t *, const char *topic,
                       const void *payload, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* MC_H */

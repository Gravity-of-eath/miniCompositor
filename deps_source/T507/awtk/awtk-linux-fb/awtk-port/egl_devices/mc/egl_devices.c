/**
 * File:   egl_devices.c  (mc backend)
 * Brief:  zero-copy EGL → mc-compositor surface.
 *
 * Slots into AWTK's existing egl_devices abstraction (used by
 * lcd_linux_egl.c). Instead of opening /dev/fb0 as an EGL window
 * surface, this backend:
 *
 *   1. connects to mc-compositor over the unix socket
 *   2. creates a FULLSCREEN mc surface (compositor allocates ion dma-bufs)
 *   3. imports each dma-buf as an EGLImageKHR + GL_TEXTURE_2D + FBO
 *   4. on make_current: rebinds the FBO matching the next-to-write buf
 *   5. on swap_buffers: glFinish, mc_surface_commit_idx, rotate
 *
 * AWTK / nanovg-plus / GLES2 / 3D code is unchanged. The GPU writes
 * straight into the shared dma-buf the compositor then composites onto
 * the framebuffer. No glReadPixels, no extra copy.
 *
 * Multi-buffer ownership:
 *   We hold ONE buffer at a time (the "render buffer"); the compositor
 *   may hold any number of previously committed buffers until its
 *   eventfd ticks (mc_surface_wait_buf_free).
 *
 *   acquire_next_idx() picks the next FREE buffer (waits if needed),
 *   binds its FBO, returns its idx. swap_buffers commits the current
 *   idx and then acquires the next one so the *next* draw cycle finds
 *   the right FBO bound on make_current.
 */

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <drm/drm_fourcc.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../egl_devices.h"
#include "tkc/mem.h"

#include "mc.h"

#define MC_MAX_BUFS 4

/* Single globals so input_thread_mc can reach the compositor connection
 * and the lcd's fullscreen surface (for request_focus on bus messages).
 * Set by egl_devices_create, cleared by egl_devices_dispose. */
static mc_ctx_t     *g_shared_mc_ctx  = NULL;
static mc_surface_t *g_shared_mc_surf = NULL;

/* HIDDEN-state hint, set by input_thread_mc when it receives MC_EV_LIFECYCLE.
 * When set, swap_buffers throttles itself instead of committing, so a hidden
 * AWTK doesn't drown the compositor in full-screen recompose triggers and
 * doesn't burn GPU for frames nobody can see. */
static volatile int g_hidden = 0;

/* Frame's bounding-box dirty rect, populated by lcd_egl_on_dirty_rect()
 * (which lcd_linux_egl.c calls from its begin_frame hook). swap_buffers
 * reads this and sends it as the mc damage rect so the compositor only
 * blits the changed area instead of the whole 1024x600 frame. The flag
 * `g_dirty_valid` distinguishes "no dirty rect this frame" (e.g. very
 * first commit before AWTK has begun a frame) from a zero-area rect. */
static int g_dirty_x = 0, g_dirty_y = 0, g_dirty_w = 0, g_dirty_h = 0;
static int g_dirty_valid = 0;

/* Strong override of the weak symbol in lcd_linux_egl.c. AWTK calls this
 * for every frame just before nanovg starts drawing. */
void lcd_egl_on_dirty_rect(int x, int y, int w, int h)
{
    g_dirty_x = x;
    g_dirty_y = y;
    g_dirty_w = w;
    g_dirty_h = h;
    g_dirty_valid = 1;
}

mc_ctx_t *lcd_linux_mc_get_ctx(void)
{
    return g_shared_mc_ctx;
}

mc_surface_t *lcd_linux_mc_get_surf(void)
{
    return g_shared_mc_surf;
}

void lcd_linux_mc_set_hidden(int h)
{
    g_hidden = h ? 1 : 0;
}

int lcd_linux_mc_is_hidden(void)
{
    return g_hidden;
}

typedef struct _mc_egl_ctx_t {
    /* ---- mc connection ---- */
    mc_ctx_t      *mc;
    mc_surface_t  *surf;
    int            w, h, stride;
    int            n_buf;
    int            cur_idx;             /* buf we are currently drawing into */

    /* ---- EGL ---- */
    EGLDisplay     dpy;
    EGLConfig      cfg;
    EGLSurface     pbuf;                /* 1x1 dummy, just for binding ctx */
    EGLContext     ctx;

    /* ---- per-buffer GL objects ---- */
    EGLImageKHR    eglimg[MC_MAX_BUFS];
    GLuint         tex   [MC_MAX_BUFS];
    GLuint         fbo   [MC_MAX_BUFS];

    /* ---- extension proc pointers ---- */
    PFNEGLCREATEIMAGEKHRPROC                fpCreateImage;
    PFNEGLDESTROYIMAGEKHRPROC               fpDestroyImage;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC     fpImg2Tex;
    PFNEGLCREATESYNCKHRPROC                 fpCreateSync;
    PFNEGLDESTROYSYNCKHRPROC                fpDestroySync;
    PFNEGLCLIENTWAITSYNCKHRPROC             fpClientWaitSync;
    int                                     have_fence_sync;
} mc_egl_ctx_t;

/* Compositor address; overridable via MC_SOCKET env. */
static const char *mc_socket_path(void)
{
    const char *s = getenv("MC_SOCKET");
    return s && *s ? s : "/tmp/mc.sock";
}
static const char *mc_app_name(void)
{
    const char *s = getenv("MC_APP_NAME");
    return s && *s ? s : "awtk";
}

static int import_buffer_into_fbo(mc_egl_ctx_t *c, int idx)
{
    int fd = mc_surface_buf_fd(c->surf, idx);
    if (fd < 0) {
        fprintf(stderr, "[mc-egl] mc_surface_buf_fd(%d) failed\n", idx);
        return -1;
    }
    EGLint attrs[] = {
        EGL_WIDTH,                     c->w,
        EGL_HEIGHT,                    c->h,
        /* mc surface fmt was BGRA8888; in DRM that is ARGB8888 fourcc
         * (little-endian byte order [B,G,R,A]). Mali importer treats the
         * memory the same way -- it's the byte layout that matters. */
        EGL_LINUX_DRM_FOURCC_EXT,      DRM_FORMAT_ARGB8888,
        EGL_DMA_BUF_PLANE0_FD_EXT,     fd,
        EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
        EGL_DMA_BUF_PLANE0_PITCH_EXT,  c->stride,
        EGL_NONE
    };
    c->eglimg[idx] = c->fpCreateImage(c->dpy, EGL_NO_CONTEXT,
                                      EGL_LINUX_DMA_BUF_EXT,
                                      (EGLClientBuffer)NULL, attrs);
    if (c->eglimg[idx] == EGL_NO_IMAGE_KHR) {
        fprintf(stderr, "[mc-egl] eglCreateImageKHR(idx=%d): 0x%x\n",
                idx, eglGetError());
        return -1;
    }

    glGenTextures(1, &c->tex[idx]);
    glBindTexture(GL_TEXTURE_2D, c->tex[idx]);
    c->fpImg2Tex(GL_TEXTURE_2D, (GLeglImageOES)c->eglimg[idx]);
    GLenum e = glGetError();
    if (e != GL_NO_ERROR) {
        fprintf(stderr, "[mc-egl] glEGLImageTargetTexture2DOES(idx=%d): 0x%x\n",
                idx, e);
        return -1;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glGenFramebuffers(1, &c->fbo[idx]);
    glBindFramebuffer(GL_FRAMEBUFFER, c->fbo[idx]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, c->tex[idx], 0);
    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (st != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "[mc-egl] FBO[%d] incomplete: 0x%x\n", idx, st);
        return -1;
    }
    return 0;
}

void *egl_devices_create(const char *filename)
{
    (void)filename;
    mc_egl_ctx_t *c = TKMEM_ZALLOC(mc_egl_ctx_t);
    if (!c) return NULL;
    c->cur_idx = -1;
    for (int i = 0; i < MC_MAX_BUFS; i++) {
        c->eglimg[i] = EGL_NO_IMAGE_KHR;
    }

    /* ---- mc connection ---- */
    mc_set_socket_path(mc_socket_path());
    c->mc = mc_connect(mc_app_name());
    if (!c->mc) {
        fprintf(stderr, "[mc-egl] mc_connect(%s) failed: %s\n",
                mc_socket_path(), mc_strerror(mc_last_error()));
        goto err;
    }
    mc_screen_info_t scr;
    if (mc_get_screen_info(c->mc, &scr) < 0) {
        fprintf(stderr, "[mc-egl] get_screen_info failed\n");
        goto err;
    }
    c->w = scr.screen_w;
    c->h = scr.screen_h;

    /* MC_SURF_FLAG_FLIP_Y: we render into the dma-buf via a GL FBO, whose
     * origin is lower-left (GL convention). AWTK / nanovg-plus assume
     * top-left like the rest of the screen, so the compositor must invert
     * src_y when blitting our surface or the UI lands on screen
     * upside-down. The original EGL-window path on T507 got this flip
     * implicitly from the fb driver; FBOs don't, so we declare it here. */
    c->surf = mc_surface_create_shm_ex(c->mc, c->w, c->h, MC_FMT_BGRA8888,
                                       MC_ROLE_FULLSCREEN, 2,
                                       MC_SURF_FLAG_FLIP_Y);
    if (!c->surf) {
        fprintf(stderr, "[mc-egl] create_shm failed: %s\n",
                mc_strerror(mc_last_error()));
        goto err;
    }
    c->n_buf  = mc_surface_n_buf(c->surf);
    c->stride = mc_surface_buf_stride(c->surf);
    if (c->n_buf > MC_MAX_BUFS) c->n_buf = MC_MAX_BUFS;
    printf("[mc-egl] mc surface %dx%d stride=%d n_buf=%d\n",
           c->w, c->h, c->stride, c->n_buf);

    /* ---- EGL bring-up (pbuffer is just a current-target dummy) ---- */
    c->dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (c->dpy == EGL_NO_DISPLAY) goto err;
    if (!eglInitialize(c->dpy, NULL, NULL)) goto err;
    eglBindAPI(EGL_OPENGL_ES_API);

    const char *exts = eglQueryString(c->dpy, EGL_EXTENSIONS);
    if (!exts || !strstr(exts, "EGL_EXT_image_dma_buf_import")) {
        fprintf(stderr, "[mc-egl] missing EGL_EXT_image_dma_buf_import\n");
        goto err;
    }

    EGLint cfg_attrs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,   8, EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE,  8, EGL_ALPHA_SIZE, 8,
        EGL_STENCIL_SIZE, 8,
        EGL_NONE
    };
    EGLint ncfg = 0;
    if (!eglChooseConfig(c->dpy, cfg_attrs, &c->cfg, 1, &ncfg) || ncfg < 1) {
        fprintf(stderr, "[mc-egl] eglChooseConfig: 0x%x\n", eglGetError());
        goto err;
    }
    EGLint pb_attrs[] = { EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE };
    c->pbuf = eglCreatePbufferSurface(c->dpy, c->cfg, pb_attrs);
    if (c->pbuf == EGL_NO_SURFACE) goto err;
    EGLint ctx_attrs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    c->ctx = eglCreateContext(c->dpy, c->cfg, EGL_NO_CONTEXT, ctx_attrs);
    if (c->ctx == EGL_NO_CONTEXT) goto err;
    if (!eglMakeCurrent(c->dpy, c->pbuf, c->pbuf, c->ctx)) goto err;
    printf("[mc-egl] GL_RENDERER='%s'\n", glGetString(GL_RENDERER));

    /* ---- bind extension entrypoints AFTER context is current ---- */
    c->fpCreateImage  = (PFNEGLCREATEIMAGEKHRPROC)
        eglGetProcAddress("eglCreateImageKHR");
    c->fpDestroyImage = (PFNEGLDESTROYIMAGEKHRPROC)
        eglGetProcAddress("eglDestroyImageKHR");
    c->fpImg2Tex      = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)
        eglGetProcAddress("glEGLImageTargetTexture2DOES");
    if (!c->fpCreateImage || !c->fpImg2Tex) {
        fprintf(stderr, "[mc-egl] missing required ext proc addrs\n");
        goto err;
    }

    /* EGL_KHR_fence_sync: optional. If present we use per-buffer fences
     * to overlap GPU finish with mc_surface_wait_buf_free; if absent
     * (no extension or missing proc addrs) swap_buffers falls back to
     * the simple glFinish path. */
    if (exts && strstr(exts, "EGL_KHR_fence_sync")) {
        c->fpCreateSync     = (PFNEGLCREATESYNCKHRPROC)
            eglGetProcAddress("eglCreateSyncKHR");
        c->fpDestroySync    = (PFNEGLDESTROYSYNCKHRPROC)
            eglGetProcAddress("eglDestroySyncKHR");
        c->fpClientWaitSync = (PFNEGLCLIENTWAITSYNCKHRPROC)
            eglGetProcAddress("eglClientWaitSyncKHR");
        c->have_fence_sync = (c->fpCreateSync && c->fpDestroySync
                              && c->fpClientWaitSync);
        printf("[mc-egl] EGL_KHR_fence_sync = %s\n",
               c->have_fence_sync ? "yes (pipelined)" : "no (glFinish fallback)");
    }

    /* ---- import every dma-buf into its own FBO once up front ---- */
    for (int i = 0; i < c->n_buf; i++) {
        if (import_buffer_into_fbo(c, i) != 0) goto err;
    }

    /* ---- pick a starting buffer; make_current/swap will rotate ---- */
    c->cur_idx = 0;
    (void)mc_surface_wait_buf_free(c->surf, c->cur_idx);
    glBindFramebuffer(GL_FRAMEBUFFER, c->fbo[c->cur_idx]);
    glViewport(0, 0, c->w, c->h);

    g_shared_mc_ctx  = c->mc;
    g_shared_mc_surf = c->surf;
    return c;

err:
    if (c) {
        if (c->ctx)  eglDestroyContext(c->dpy, c->ctx);
        if (c->pbuf) eglDestroySurface(c->dpy, c->pbuf);
        if (c->dpy)  eglTerminate(c->dpy);
        if (c->surf) mc_surface_destroy(c->surf);
        if (c->mc)   mc_disconnect(c->mc);
        TKMEM_FREE(c);
    }
    return NULL;
}

ret_t egl_devices_dispose(void *opaque)
{
    mc_egl_ctx_t *c = opaque;
    if (!c) return RET_BAD_PARAMS;
    eglMakeCurrent(c->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    for (int i = 0; i < c->n_buf; i++) {
        if (c->fbo[i])    glDeleteFramebuffers(1, &c->fbo[i]);
        if (c->tex[i])    glDeleteTextures(1, &c->tex[i]);
        if (c->eglimg[i] != EGL_NO_IMAGE_KHR && c->fpDestroyImage)
            c->fpDestroyImage(c->dpy, c->eglimg[i]);
    }
    if (c->ctx)  eglDestroyContext(c->dpy, c->ctx);
    if (c->pbuf) eglDestroySurface(c->dpy, c->pbuf);
    if (c->dpy)  eglTerminate(c->dpy);
    if (c->surf) mc_surface_destroy(c->surf);
    if (c->surf && g_shared_mc_surf == c->surf) g_shared_mc_surf = NULL;
    if (c->mc) {
        if (g_shared_mc_ctx == c->mc) g_shared_mc_ctx = NULL;
        mc_disconnect(c->mc);
    }
    TKMEM_FREE(c);
    return RET_OK;
}

float_t egl_devices_get_ratio (void *c) { (void)c; return 1.0f; }
int32_t egl_devices_get_width (void *c) { return c ? ((mc_egl_ctx_t*)c)->w : 0; }
int32_t egl_devices_get_height(void *c) { return c ? ((mc_egl_ctx_t*)c)->h : 0; }

/* Called by AWTK before each frame's render. We make our EGL context
 * current AND bind the FBO matching the buffer we're about to draw. */
ret_t egl_devices_make_current(void *opaque)
{
    mc_egl_ctx_t *c = opaque;
    if (!c) return RET_BAD_PARAMS;
    if (!eglMakeCurrent(c->dpy, c->pbuf, c->pbuf, c->ctx)) {
        fprintf(stderr, "[mc-egl] eglMakeCurrent: 0x%x\n", eglGetError());
        return RET_FAIL;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, c->fbo[c->cur_idx]);
    return RET_OK;
}

/* Called by AWTK after each frame is rendered.
 *
 * Two waits happen on every swap, in this order:
 *   (A) GPU done writing fbo[cur_idx] -- required before the compositor
 *       is allowed to consume the buffer (no half-frame composites).
 *   (B) Compositor has released the OTHER buffer -- required before we
 *       can bind it as the next FBO and draw into it.
 *
 * We can't reorder these: with n_buf=2, the commit between A and B is
 * what triggers the compositor's buffer rotate that frees 'next'. If we
 * waited on 'next' before commit, the protocol would deadlock. True
 * cross-frame pipelining would need n_buf >= 3 -- the compositor's
 * surface protocol supports that but AWTK's main loop doesn't have
 * deeper-than-two pipelining anyway.
 *
 * What EGL_KHR_fence_sync still buys us over glFinish:
 *   - eglClientWaitSync waits on the SPECIFIC fence we created after
 *     this frame's draws, not the entire GL pipeline. Driver can use a
 *     more efficient primitive than the blanket pipeline drain that
 *     glFinish forces, and any GL work the driver lazily defers (e.g.
 *     texture uploads from the next frame's setup that AWTK might have
 *     queued via lv_timer_handler -> nanovg) is not part of the wait.
 *   - It's the right foundation if we later want to add a 3-buffer mode
 *     for true GPU/compositor overlap. */
ret_t egl_devices_swap_buffers(void *opaque)
{
    mc_egl_ctx_t *c = opaque;
    if (!c) return RET_BAD_PARAMS;

    /* Hidden? Skip the commit + buffer-rotate entirely and pace AWTK at
     * ~5 Hz so its main loop doesn't busy-render frames that nobody can
     * see, AND doesn't flood the compositor with full-screen damage
     * commits that force costly CPU composites. AWTK's GL rendering
     * itself still happens (cheap; mostly no-op for a static UI) but we
     * neither glFinish nor talk to the compositor. */
    if (g_hidden) {
        struct timespec ts = { 0, 200 * 1000 * 1000 };
        nanosleep(&ts, NULL);
        return RET_OK;
    }

    /* Phase A: wait for the GPU to finish writing the current buffer
     * before handing it to the compositor. Prefer EGL_KHR_fence_sync
     * (waits on a specific fence, not the entire pipeline) and fall
     * back to glFinish if the driver lacks it. */
    if (c->have_fence_sync) {
        EGLSyncKHR fence = c->fpCreateSync(c->dpy, EGL_SYNC_FENCE_KHR, NULL);
        if (fence != EGL_NO_SYNC_KHR) {
            /* EGL_SYNC_FLUSH_COMMANDS_BIT_KHR makes the wait flush
             * pending GL commands first -- otherwise it could deadlock
             * on a fence the driver hasn't yet submitted. */
            (void)c->fpClientWaitSync(c->dpy, fence,
                EGL_SYNC_FLUSH_COMMANDS_BIT_KHR, EGL_FOREVER_KHR);
            c->fpDestroySync(c->dpy, fence);
        } else {
            glFinish();
        }
    } else {
        glFinish();
    }

    /* Damage rect: tells the compositor which area changed this frame
     * (small rect is enough -- LVGL widgets typically dirty 100-300 px
     * regions). For the GPU compositor (backend_egl) damage is ignored
     * and the whole texture is drawn each frame; for the CPU compositor
     * (backend_fb) it determines the fb blit cost. */
    mc_rect_t damage;
    if (g_dirty_valid && g_dirty_w > 0 && g_dirty_h > 0) {
        damage.x = (int16_t)g_dirty_x;
        damage.y = (int16_t)g_dirty_y;
        damage.w = (int16_t)g_dirty_w;
        damage.h = (int16_t)g_dirty_h;
        g_dirty_valid = 0;
    } else {
        damage.x = 0; damage.y = 0;
        damage.w = (int16_t)c->w;
        damage.h = (int16_t)c->h;
    }
    int rc = mc_surface_commit_idx(c->surf, c->cur_idx, &damage, 1);
    if (rc != 0) {
        fprintf(stderr, "[mc-egl] mc_surface_commit_idx(%d) failed: %d\n",
                c->cur_idx, rc);
        return RET_FAIL;
    }

    /* Phase B: wait for the OTHER buffer to be released by the
     * compositor so we can start drawing into it next frame. */
    int next = (c->cur_idx + 1) % c->n_buf;
    if (mc_surface_wait_buf_free(c->surf, next) != 0) {
        return RET_FAIL;
    }
    c->cur_idx = next;
    glBindFramebuffer(GL_FRAMEBUFFER, c->fbo[c->cur_idx]);
    return RET_OK;
}

ret_t egl_devices_resize(void *opaque, uint32_t w, uint32_t h)
{
    /* mc surfaces are created at compositor screen size; AWTK calling resize
     * with a different size is unusual on an embedded full-screen client.
     * Reject for now -- if a real need shows up we can recreate the mc
     * surface + all FBOs here. */
    (void)opaque; (void)w; (void)h;
    return RET_NOT_IMPL;
}

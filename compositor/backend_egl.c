/*
 * GPU compositor backend.
 *
 * Mali-G31 / EGL window surface on /dev/fb0. Each visible client surface
 * becomes a GL texture (imported from its mc dma-buf via EGLImage if
 * possible, falling back to glTexSubImage2D for non-dma-buf surfaces).
 * Per frame: clear, draw one textured quad per surface in z-order,
 * eglSwapBuffers replaces FBIOPAN_DISPLAY.
 *
 * The Mali write speed to fb memory is ~10x the CPU memcpy path on T507
 * (uncached fb DRAM). With a typical fullscreen + popup the per-frame
 * compose drops from ~120ms (CPU) to ~5-10ms (GPU), and the cost is
 * largely independent of how many surfaces stack on top because each
 * is just one quad.
 */
#define _GNU_SOURCE
#include "backend.h"
#include "surface.h"
#include "transport.h"
#include "log.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <drm/drm_fourcc.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Per-buffer GL texture cache attached to each surface. We can't put it
 * on struct mc_surface (that's in surface.h and shouldn't pull in GL
 * types), so we keep a parallel sparse table keyed by (sid, buf_idx).
 *
 * For dma-buf surfaces: eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT, fd, ...)
 * + glEGLImageTargetTexture2DOES. No per-frame upload -- the GPU reads
 * the client's dma-buf directly.
 *
 * For memfd / phys-only surfaces (none in our current setup): would
 * need glTexSubImage2D per commit. Not implemented yet; LOG_W and skip. */
#define EGL_TEX_CACHE_MAX 32
struct egl_tex_entry {
    uint32_t      sid;
    int           buf_idx;
    int           fd;            /* dma-buf fd we imported */
    int           w, h, stride;
    EGLImageKHR   image;
    GLuint        tex;
};

struct egl_priv {
    EGLDisplay   dpy;
    EGLConfig    cfg;
    EGLSurface   surf;
    EGLContext   ctx;

    int          w, h;

    /* Shader program: textured quad with flip_y + alpha. */
    GLuint       prog;
    GLint        u_screen_size;
    GLint        u_rect;          /* (x, y, w, h) in pixels */
    GLint        u_flip_y;
    GLint        u_opaque;
    GLint        a_pos;
    GLint        u_tex;

    GLuint       quad_vbo;

    struct egl_tex_entry cache[EGL_TEX_CACHE_MAX];
    int                  cache_n;

    PFNEGLCREATEIMAGEKHRPROC            fpCreateImage;
    PFNEGLDESTROYIMAGEKHRPROC           fpDestroyImage;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC fpImg2Tex;
};

/* ------------------------------------------------------------------ */
/* Shader                                                             */
/* ------------------------------------------------------------------ */

/* The quad is fixed in NDC; we compute UV and final position in shader
 * from u_rect (pixels) and u_screen_size. u_flip_y inverts texcoord.y
 * for GL FBO sources (AWTK). u_opaque skips the alpha blend. */
static const char *VS_SRC =
    "attribute vec2 a_pos;\n"               /* [0,1] x [0,1] */
    "uniform vec2 u_screen_size;\n"
    "uniform vec4 u_rect;\n"                /* x, y, w, h pixels */
    "uniform float u_flip_y;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "    vec2 px = u_rect.xy + a_pos * u_rect.zw;\n"
    "    vec2 ndc = vec2((px.x / u_screen_size.x) * 2.0 - 1.0,\n"
    "                    1.0 - (px.y / u_screen_size.y) * 2.0);\n"
    "    gl_Position = vec4(ndc, 0.0, 1.0);\n"
    "    v_uv = vec2(a_pos.x,\n"
    "                mix(a_pos.y, 1.0 - a_pos.y, u_flip_y));\n"
    "}\n";

static const char *FS_SRC =
    "precision mediump float;\n"
    "varying vec2 v_uv;\n"
    "uniform sampler2D u_tex;\n"
    "uniform float u_opaque;\n"               /* 1.0 = force alpha=1 */
    "void main() {\n"
    "    vec4 c = texture2D(u_tex, v_uv);\n"
    "    if (u_opaque > 0.5) c.a = 1.0;\n"
    "    gl_FragColor = c;\n"
    "}\n";

static GLuint compile_shader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[512]; GLsizei n = 0;
        glGetShaderInfoLog(s, sizeof(buf), &n, buf);
        LOG_E("backend_egl: shader compile: %.*s", (int)n, buf);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static GLuint link_program(GLuint vs, GLuint fs)
{
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[512]; GLsizei n = 0;
        glGetProgramInfoLog(p, sizeof(buf), &n, buf);
        LOG_E("backend_egl: program link: %.*s", (int)n, buf);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

/* ------------------------------------------------------------------ */
/* Texture cache                                                      */
/* ------------------------------------------------------------------ */

static struct egl_tex_entry *find_or_create_tex(struct egl_priv *p,
                                                struct mc_surface *sf, int idx)
{
    int fd = sf->bufs[idx].shm_fd;
    /* Lookup existing entry by (sid, buf_idx). The fd should be stable
     * across the surface's lifetime so we don't have to re-import on
     * every commit. */
    for (int i = 0; i < p->cache_n; i++) {
        if (p->cache[i].sid == sf->sid && p->cache[i].buf_idx == idx) {
            if (p->cache[i].fd == fd) return &p->cache[i];
            /* fd changed (surface re-created?) -- drop and re-import. */
            if (p->cache[i].image != EGL_NO_IMAGE_KHR && p->fpDestroyImage) {
                p->fpDestroyImage(p->dpy, p->cache[i].image);
            }
            if (p->cache[i].tex) glDeleteTextures(1, &p->cache[i].tex);
            /* shift down */
            for (int j = i; j < p->cache_n - 1; j++) p->cache[j] = p->cache[j+1];
            p->cache_n--;
            break;
        }
    }
    if (p->cache_n >= EGL_TEX_CACHE_MAX) {
        /* drop oldest */
        if (p->cache[0].image != EGL_NO_IMAGE_KHR && p->fpDestroyImage)
            p->fpDestroyImage(p->dpy, p->cache[0].image);
        if (p->cache[0].tex) glDeleteTextures(1, &p->cache[0].tex);
        for (int j = 0; j < EGL_TEX_CACHE_MAX - 1; j++)
            p->cache[j] = p->cache[j+1];
        p->cache_n--;
    }

    /* Create new entry: import dma-buf as EGLImage, bind to texture. */
    EGLint img_attrs[] = {
        EGL_WIDTH,                     sf->w,
        EGL_HEIGHT,                    sf->h,
        EGL_LINUX_DRM_FOURCC_EXT,      DRM_FORMAT_ARGB8888,
        EGL_DMA_BUF_PLANE0_FD_EXT,     fd,
        EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
        EGL_DMA_BUF_PLANE0_PITCH_EXT,  (EGLint)sf->stride,
        EGL_NONE
    };
    EGLImageKHR img = p->fpCreateImage(p->dpy, EGL_NO_CONTEXT,
                                       EGL_LINUX_DMA_BUF_EXT,
                                       (EGLClientBuffer)NULL, img_attrs);
    if (img == EGL_NO_IMAGE_KHR) {
        LOG_W("backend_egl: eglCreateImage(sid=%u buf=%d): 0x%x",
              sf->sid, idx, eglGetError());
        return NULL;
    }
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    p->fpImg2Tex(GL_TEXTURE_2D, (GLeglImageOES)img);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    struct egl_tex_entry *e = &p->cache[p->cache_n++];
    e->sid = sf->sid;
    e->buf_idx = idx;
    e->fd = fd;
    e->w = sf->w; e->h = sf->h; e->stride = (int)sf->stride;
    e->image = img;
    e->tex = tex;
    return e;
}

/* ------------------------------------------------------------------ */
/* mc_backend ops                                                     */
/* ------------------------------------------------------------------ */

static int egl_open(struct mc_backend *be, const char *arg,
                    int w_hint, int h_hint,
                    int *out_w, int *out_h, int *out_stride)
{
    (void)arg; (void)w_hint; (void)h_hint;
    struct egl_priv *p = calloc(1, sizeof(*p));
    if (!p) return -ENOMEM;

    p->dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (p->dpy == EGL_NO_DISPLAY) { LOG_E("eglGetDisplay"); goto err; }
    if (!eglInitialize(p->dpy, NULL, NULL)) { LOG_E("eglInitialize"); goto err; }
    eglBindAPI(EGL_OPENGL_ES_API);

    const char *exts = eglQueryString(p->dpy, EGL_EXTENSIONS);
    if (!exts || !strstr(exts, "EGL_EXT_image_dma_buf_import")) {
        LOG_E("backend_egl: missing EGL_EXT_image_dma_buf_import");
        goto err;
    }

    EGLint cfg_attrs[] = {
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    EGLint nc = 0;
    if (!eglChooseConfig(p->dpy, cfg_attrs, &p->cfg, 1, &nc) || nc < 1) {
        LOG_E("eglChooseConfig"); goto err;
    }
    p->surf = eglCreateWindowSurface(p->dpy, p->cfg,
                                     (EGLNativeWindowType)0, NULL);
    if (p->surf == EGL_NO_SURFACE) {
        LOG_E("eglCreateWindowSurface(fb0): 0x%x", eglGetError());
        goto err;
    }
    EGLint w = 0, h = 0;
    eglQuerySurface(p->dpy, p->surf, EGL_WIDTH,  &w);
    eglQuerySurface(p->dpy, p->surf, EGL_HEIGHT, &h);
    p->w = w; p->h = h;

    EGLint ctx_attrs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    p->ctx = eglCreateContext(p->dpy, p->cfg, EGL_NO_CONTEXT, ctx_attrs);
    if (p->ctx == EGL_NO_CONTEXT) { LOG_E("eglCreateContext"); goto err; }
    if (!eglMakeCurrent(p->dpy, p->surf, p->surf, p->ctx)) {
        LOG_E("eglMakeCurrent"); goto err;
    }

    p->fpCreateImage  = (PFNEGLCREATEIMAGEKHRPROC)
        eglGetProcAddress("eglCreateImageKHR");
    p->fpDestroyImage = (PFNEGLDESTROYIMAGEKHRPROC)
        eglGetProcAddress("eglDestroyImageKHR");
    p->fpImg2Tex      = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)
        eglGetProcAddress("glEGLImageTargetTexture2DOES");
    if (!p->fpCreateImage || !p->fpImg2Tex) {
        LOG_E("backend_egl: missing required extension procs"); goto err;
    }

    GLuint vs = compile_shader(GL_VERTEX_SHADER, VS_SRC);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, FS_SRC);
    if (!vs || !fs) goto err;
    p->prog = link_program(vs, fs);
    glDeleteShader(vs); glDeleteShader(fs);
    if (!p->prog) goto err;

    p->a_pos          = glGetAttribLocation (p->prog, "a_pos");
    p->u_screen_size  = glGetUniformLocation(p->prog, "u_screen_size");
    p->u_rect         = glGetUniformLocation(p->prog, "u_rect");
    p->u_flip_y       = glGetUniformLocation(p->prog, "u_flip_y");
    p->u_opaque       = glGetUniformLocation(p->prog, "u_opaque");
    p->u_tex          = glGetUniformLocation(p->prog, "u_tex");

    /* Quad in [0,1] x [0,1]; shader transforms to NDC. */
    static const float quad[] = {
        0,0,  1,0,  0,1,
        0,1,  1,0,  1,1,
    };
    glGenBuffers(1, &p->quad_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, p->quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);

    be->priv = p;
    *out_w      = p->w;
    *out_h      = p->h;
    *out_stride = p->w * 4;    /* not really used for GPU path */

    LOG_I("backend_egl: %dx%d on Mali (renderer='%s')",
          p->w, p->h, glGetString(GL_RENDERER));
    return 0;

err:
    if (p) {
        if (p->ctx)  eglDestroyContext(p->dpy, p->ctx);
        if (p->surf) eglDestroySurface(p->dpy, p->surf);
        if (p->dpy)  eglTerminate(p->dpy);
        free(p);
    }
    return -EIO;
}

static uint8_t *egl_get_buffer(struct mc_backend *be)
{
    (void)be;
    /* GPU backend doesn't expose a CPU-writable back buffer. compose.c
     * detects this by `gpu_compose` flag and uses the begin/draw/end
     * entry points instead. */
    return NULL;
}

static int egl_present(struct mc_backend *be)
{
    struct egl_priv *p = be->priv;
    if (!p) return -EINVAL;
    eglSwapBuffers(p->dpy, p->surf);
    return 0;
}

static void egl_close(struct mc_backend *be)
{
    struct egl_priv *p = be->priv;
    if (!p) return;
    for (int i = 0; i < p->cache_n; i++) {
        if (p->cache[i].image != EGL_NO_IMAGE_KHR && p->fpDestroyImage)
            p->fpDestroyImage(p->dpy, p->cache[i].image);
        if (p->cache[i].tex) glDeleteTextures(1, &p->cache[i].tex);
    }
    if (p->quad_vbo) glDeleteBuffers(1, &p->quad_vbo);
    if (p->prog)     glDeleteProgram(p->prog);
    eglMakeCurrent(p->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (p->ctx)  eglDestroyContext(p->dpy, p->ctx);
    if (p->surf) eglDestroySurface(p->dpy, p->surf);
    if (p->dpy)  eglTerminate(p->dpy);
    free(p);
    be->priv = NULL;
}

/* ------------------------------------------------------------------ */
/* Direct GPU compose entry points                                    */
/* ------------------------------------------------------------------ */

static void egl_begin_frame(struct mc_backend *be)
{
    struct egl_priv *p = be->priv;
    if (!p) return;
    glViewport(0, 0, p->w, p->h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(p->prog);
    glUniform2f(p->u_screen_size, (float)p->w, (float)p->h);
    glUniform1i(p->u_tex, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindBuffer(GL_ARRAY_BUFFER, p->quad_vbo);
    glEnableVertexAttribArray(p->a_pos);
    glVertexAttribPointer(p->a_pos, 2, GL_FLOAT, GL_FALSE, 0, 0);
}

static void egl_draw_surface(struct mc_backend *be, struct mc_surface *sf)
{
    struct egl_priv *p = be->priv;
    if (!p || !sf) return;
    int idx = (sf->pending_idx >= 0) ? sf->pending_idx : sf->cur_scanout;
    if (idx < 0) return;
    struct egl_tex_entry *e = find_or_create_tex(p, sf, idx);
    if (!e) return;

    /* FULLSCREEN role: assume opaque, skip alpha blend (one GPU
     * fragment less per pixel). POPUP / BG: alpha-blend over current
     * fb contents. */
    int opaque = (sf->role == 1 /* FULLSCREEN */);
    if (opaque) {
        glDisable(GL_BLEND);
        glUniform1f(p->u_opaque, 1.0f);
    } else {
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                            GL_ONE,       GL_ONE_MINUS_SRC_ALPHA);
        glUniform1f(p->u_opaque, 0.0f);
    }
    glUniform4f(p->u_rect, (float)sf->x, (float)sf->y,
                (float)sf->w, (float)sf->h);
    glUniform1f(p->u_flip_y, sf->flip_y ? 1.0f : 0.0f);
    glBindTexture(GL_TEXTURE_2D, e->tex);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

static void egl_end_frame(struct mc_backend *be)
{
    /* Nothing per-frame here; mc_backend.present() does eglSwapBuffers. */
    (void)be;
}

static const struct mc_backend_hw_compose_ops egl_hw_compose_ops = {
    .begin_frame  = egl_begin_frame,
    .draw_surface = egl_draw_surface,
    .end_frame    = egl_end_frame,
};

struct mc_backend backend_egl = {
    .name            = "egl",
    .open            = egl_open,
    .get_buffer      = egl_get_buffer,
    .get_buffer_phys = NULL,
    .present         = egl_present,
    .close           = egl_close,
    .hw_compose      = &egl_hw_compose_ops,
};

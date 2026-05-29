/*
 * egl_dmabuf_probe: T507 plan-A feasibility check.
 *
 *   1. ION allocates a dma-buf (heap_mask=1).
 *   2. EGL imports it as EGLImageKHR (EGL_LINUX_DMA_BUF_EXT).
 *   3. Bind to GL_TEXTURE_2D via glEGLImageTargetTexture2DOES.
 *   4. Attach as FBO color attachment, glClear with a specific colour.
 *   5. glFinish, then mmap the same dma-buf and verify the bytes.
 *
 * If step 5 sees the expected pixels, plan A is fully unblocked:
 *   - mc-compositor allocates ion dma-buf
 *   - AWTK imports it as FBO and renders with full GPU/3D
 *   - mc-compositor reads (or G2D-blits) the result onto fb
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <drm/drm_fourcc.h>

/* --- ION old ABI (matches libaw_opengles + our mc_alloc fix) --- */
struct ion_alloc_old {
    size_t   len;
    size_t   align;
    uint32_t heap_id_mask;
    uint32_t flags;
    void    *handle;
};
struct ion_fd_data_old { int handle; int fd; };
struct ion_handle_data_old { int handle; };
#define ION_IOC_ALLOC _IOWR('I', 0, struct ion_alloc_old)
#define ION_IOC_FREE  _IOWR('I', 1, struct ion_handle_data_old)
#define ION_IOC_SHARE _IOWR('I', 4, struct ion_fd_data_old)

static int ion_alloc_dmabuf(size_t size)
{
    int ion_fd = open("/dev/ion", O_RDWR);
    if (ion_fd < 0) { perror("open /dev/ion"); return -1; }

    struct ion_alloc_old a;
    memset(&a, 0, sizeof(a));
    a.len = size; a.align = 4096; a.heap_id_mask = 1;
    if (ioctl(ion_fd, ION_IOC_ALLOC, &a) < 0) {
        perror("ION_IOC_ALLOC"); close(ion_fd); return -1;
    }
    int handle = (int)(uintptr_t)a.handle;
    struct ion_fd_data_old s = { .handle = handle, .fd = -1 };
    if (ioctl(ion_fd, ION_IOC_SHARE, &s) < 0) {
        perror("ION_IOC_SHARE"); close(ion_fd); return -1;
    }
    struct ion_handle_data_old h = { .handle = handle };
    (void)ioctl(ion_fd, ION_IOC_FREE, &h);
    close(ion_fd);
    return s.fd;
}

#define CHECK_EGL(call) do { \
    EGLBoolean _r = (call); \
    if (!_r) { fprintf(stderr, "%s failed: 0x%x\n", #call, eglGetError()); \
               return 1; } \
} while (0)

static const char *gl_err_str(GLenum e) {
    switch (e) {
    case GL_NO_ERROR: return "GL_NO_ERROR";
    case GL_INVALID_ENUM: return "GL_INVALID_ENUM";
    case GL_INVALID_VALUE: return "GL_INVALID_VALUE";
    case GL_INVALID_OPERATION: return "GL_INVALID_OPERATION";
    case GL_OUT_OF_MEMORY: return "GL_OUT_OF_MEMORY";
    default: return "GL_?";
    }
}

int main(void)
{
    const int W = 256, H = 256, BPP = 4;
    const int stride = W * BPP;
    const size_t buf_size = (size_t)stride * H;

    /* ---------------- step 1: ion dma-buf ---------------- */
    int dmabuf_fd = ion_alloc_dmabuf(buf_size);
    if (dmabuf_fd < 0) return 1;
    printf("step1: ion dma-buf fd=%d size=%zu  OK\n", dmabuf_fd, buf_size);

    /* Pre-fill the buffer with a magic byte so we know the clear actually
     * wrote to THIS memory (not some other GPU scratch). */
    void *vptr = mmap(NULL, buf_size, PROT_READ|PROT_WRITE,
                      MAP_SHARED, dmabuf_fd, 0);
    if (vptr == MAP_FAILED) { perror("mmap"); return 1; }
    memset(vptr, 0xCC, buf_size);
    uint8_t before[4];
    memcpy(before, vptr, 4);
    munmap(vptr, buf_size);
    printf("step1: prefilled with 0xCC: [%02X %02X %02X %02X]\n",
           before[0], before[1], before[2], before[3]);

    /* ---------------- step 2: EGL bring-up ---------------- */
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (dpy == EGL_NO_DISPLAY) { fprintf(stderr, "eglGetDisplay failed\n"); return 1; }

    EGLint maj, min;
    CHECK_EGL(eglInitialize(dpy, &maj, &min));
    printf("step2: EGL %d.%d, vendor='%s'\n",
           maj, min, eglQueryString(dpy, EGL_VENDOR));

    const char *exts = eglQueryString(dpy, EGL_EXTENSIONS);
    int have_dmabuf_import = exts && strstr(exts, "EGL_EXT_image_dma_buf_import");
    printf("step2: EGL_EXT_image_dma_buf_import = %s\n",
           have_dmabuf_import ? "YES" : "NO");
    if (!have_dmabuf_import) return 1;

    CHECK_EGL(eglBindAPI(EGL_OPENGL_ES_API));

    EGLint cfg_attrs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,   8, EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE,  8, EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    EGLConfig cfg;
    EGLint nc;
    CHECK_EGL(eglChooseConfig(dpy, cfg_attrs, &cfg, 1, &nc));
    if (nc < 1) { fprintf(stderr, "no EGL config\n"); return 1; }

    EGLint pb_attrs[] = { EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE };
    EGLSurface surf = eglCreatePbufferSurface(dpy, cfg, pb_attrs);
    if (surf == EGL_NO_SURFACE) {
        fprintf(stderr, "pbuffer surface: 0x%x\n", eglGetError()); return 1;
    }

    EGLint ctx_attrs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attrs);
    if (ctx == EGL_NO_CONTEXT) {
        fprintf(stderr, "create context: 0x%x\n", eglGetError()); return 1;
    }
    CHECK_EGL(eglMakeCurrent(dpy, surf, surf, ctx));
    printf("step2: GL_VENDOR='%s' GL_RENDERER='%s'\n",
           (const char*)glGetString(GL_VENDOR),
           (const char*)glGetString(GL_RENDERER));

    /* ---------------- step 3: import dma-buf as EGLImage ---------------- */
    PFNEGLCREATEIMAGEKHRPROC  fpCreateImage =
        (PFNEGLCREATEIMAGEKHRPROC) eglGetProcAddress("eglCreateImageKHR");
    PFNEGLDESTROYIMAGEKHRPROC fpDestroyImage =
        (PFNEGLDESTROYIMAGEKHRPROC) eglGetProcAddress("eglDestroyImageKHR");
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC fpImg2Tex =
        (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)
        eglGetProcAddress("glEGLImageTargetTexture2DOES");
    if (!fpCreateImage || !fpImg2Tex) {
        fprintf(stderr, "missing required EGL/GL ext entry points\n");
        return 1;
    }

    /* Format note: DRM_FORMAT_ARGB8888 = bytes [B,G,R,A] in little-endian
     * memory. So glClearColor(1,0,0,1) -> R=255 -> memory bytes
     * 00 00 FF FF. */
    EGLint img_attrs[] = {
        EGL_WIDTH,                     W,
        EGL_HEIGHT,                    H,
        EGL_LINUX_DRM_FOURCC_EXT,      DRM_FORMAT_ARGB8888,
        EGL_DMA_BUF_PLANE0_FD_EXT,     dmabuf_fd,
        EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
        EGL_DMA_BUF_PLANE0_PITCH_EXT,  stride,
        EGL_NONE
    };
    EGLImageKHR img = fpCreateImage(dpy, EGL_NO_CONTEXT,
                                    EGL_LINUX_DMA_BUF_EXT,
                                    (EGLClientBuffer)NULL, img_attrs);
    if (img == EGL_NO_IMAGE_KHR) {
        fprintf(stderr, "eglCreateImageKHR(dma_buf): 0x%x\n", eglGetError());
        return 1;
    }
    printf("step3: EGLImage imported  OK\n");

    /* ---------------- step 4: tex+fbo, glClear ---------------- */
    GLuint tex; glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    fpImg2Tex(GL_TEXTURE_2D, (GLeglImageOES)img);
    GLenum e = glGetError();
    if (e != GL_NO_ERROR) {
        fprintf(stderr, "glEGLImageTargetTexture2DOES failed: %s\n",
                gl_err_str(e));
        return 1;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    GLuint fbo; glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, tex, 0);
    GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (st != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "FBO incomplete: 0x%x\n", st);
        return 1;
    }
    printf("step4: FBO complete  OK\n");

    glViewport(0, 0, W, H);
    /* RED -> memory 00 00 FF FF (for DRM_FORMAT_ARGB8888 little-endian) */
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();
    e = glGetError();
    if (e != GL_NO_ERROR) {
        fprintf(stderr, "glClear/glFinish err: %s\n", gl_err_str(e));
        return 1;
    }
    printf("step4: glClear(red) + glFinish  OK\n");

    /* ---------------- step 5: mmap & verify bytes ---------------- */
    vptr = mmap(NULL, buf_size, PROT_READ|PROT_WRITE, MAP_SHARED, dmabuf_fd, 0);
    if (vptr == MAP_FAILED) { perror("mmap-verify"); return 1; }

    uint8_t *p = (uint8_t*)vptr;
    /* sample 4 corners + center */
    int xs[5] = { 0, W-1, 0, W-1, W/2 };
    int ys[5] = { 0,   0, H-1, H-1, H/2 };
    int all_ok = 1;
    for (int i = 0; i < 5; i++) {
        uint8_t *px = p + ys[i]*stride + xs[i]*BPP;
        printf("  (%3d,%3d) = %02X %02X %02X %02X %s\n",
               xs[i], ys[i], px[0], px[1], px[2], px[3],
               (px[0]==0x00 && px[1]==0x00 && px[2]==0xFF && px[3]==0xFF)
                 ? "" : "<-- WRONG");
        if (!(px[0]==0x00 && px[1]==0x00 && px[2]==0xFF && px[3]==0xFF))
            all_ok = 0;
    }
    munmap(vptr, buf_size);

    /* ---------------- cleanup ---------------- */
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &tex);
    fpDestroyImage(dpy, img);
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(dpy, ctx);
    eglDestroySurface(dpy, surf);
    eglTerminate(dpy);
    close(dmabuf_fd);

    if (all_ok) {
        printf("\nPLAN A FEASIBLE: GPU rendered into ion dma-buf, "
               "CPU read it back through mmap.\n");
        return 0;
    } else {
        printf("\nROUND-TRIP FAILED: pixels read back are not the cleared "
               "red. Plan A blocked.\n");
        return 2;
    }
}

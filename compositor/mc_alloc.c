#define _GNU_SOURCE
#include "mc_alloc.h"
#include "log.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

/* ============================================================ *
 * dma-heap                                                     *
 * ============================================================ */

/* Try these in order. cma/reserved tend to be contiguous (good for G2D);
 * system is paged but always a dma-buf so HW can still import it. */
static const char *const DMA_HEAP_PATHS[] = {
    "/dev/dma_heap/cma",
    "/dev/dma_heap/reserved",
    "/dev/dma_heap/linux,cma",
    "/dev/dma_heap/system",
    NULL,
};
#define DMA_HEAP_IOC_MAGIC 'H'

struct dma_heap_alloc_data_v {
    uint64_t len;
    uint32_t fd;
    uint32_t fd_flags;
    uint64_t heap_flags;
};

#define DMA_HEAP_IOCTL_ALLOC \
    _IOWR(DMA_HEAP_IOC_MAGIC, 0x0, struct dma_heap_alloc_data_v)

static int  g_dh_fd = -1;

static int dh_open(void)
{
    for (int i = 0; DMA_HEAP_PATHS[i]; i++) {
        int fd = open(DMA_HEAP_PATHS[i], O_RDWR | O_CLOEXEC);
        if (fd >= 0) {
            g_dh_fd = fd;
            LOG_I("mc_alloc: dma-heap opened %s", DMA_HEAP_PATHS[i]);
            return 0;
        }
    }
    return -ENOENT;
}

static int dh_alloc(struct mc_alloc_buf *out, size_t size)
{
    struct dma_heap_alloc_data_v req;
    memset(&req, 0, sizeof(req));
    req.len      = size;
    req.fd_flags = O_RDWR | O_CLOEXEC;
    if (ioctl(g_dh_fd, DMA_HEAP_IOCTL_ALLOC, &req) < 0) {
        LOG_W("mc_alloc: dma-heap alloc failed: %s", strerror(errno));
        return -errno;
    }
    out->fd = (int)req.fd;
    out->size = size;
    out->phys = 0;
    return 0;
}

/* ============================================================ *
 * sunxi-ion                                                    *
 *                                                              *
 * Two ABIs coexist on Allwinner BSPs (kernel 4.9 / 5.4):       *
 *                                                              *
 *   OLD: struct ion_allocation_info_old { 32 bytes }           *
 *        ALLOC returns a handle (int), then ION_IOC_MAP turns  *
 *        it into a dma-buf fd. T113 / T507 4.9-class kernels.  *
 *                                                              *
 *   NEW: struct ion_allocation_data_new { 24 bytes }           *
 *        ALLOC returns a dma-buf fd directly. More recent      *
 *        kernels with the upstream-style ION uapi.             *
 *                                                              *
 * Both use _IOWR('I', 0, struct), but because the struct sizes *
 * differ the encoded ioctl numbers differ too. The driver only *
 * matches its own size; the other one fails with ENOTTY.       *
 *                                                              *
 * We detect at init: try the OLD allocator with a tiny test    *
 * size; if it works, use OLD for the session. Otherwise try    *
 * NEW. Either way, phys is fetched via the sunxi-extension     *
 * PHYS_ADDR custom ioctl.                                      *
 * ============================================================ */

#define ION_PATH        "/dev/ion"
#define ION_IOC_MAGIC   'I'

/* === OLD ABI (T113/T507 4.9-class) === */
typedef int ion_handle_t;

struct ion_alloc_old_v {
    size_t       len;
    size_t       align;
    unsigned int heap_id_mask;
    unsigned int flags;
    ion_handle_t handle;   /* OUT */
};

struct ion_fd_data_v {
    ion_handle_t handle;   /* IN */
    int          fd;       /* OUT */
};

struct ion_handle_data_v {
    ion_handle_t handle;
};

#define ION_IOC_ALLOC_OLD  _IOWR(ION_IOC_MAGIC, 0, struct ion_alloc_old_v)
#define ION_IOC_FREE       _IOWR(ION_IOC_MAGIC, 1, struct ion_handle_data_v)
#define ION_IOC_MAP        _IOWR(ION_IOC_MAGIC, 2, struct ion_fd_data_v)

/* === NEW ABI (upstream-style) === */
struct ion_alloc_new_v {
    uint64_t len;
    uint32_t heap_id_mask;
    uint32_t flags;
    uint32_t fd;           /* OUT */
    uint32_t unused;
};

#define ION_IOC_ALLOC_NEW  _IOWR(ION_IOC_MAGIC, 0, struct ion_alloc_new_v)

/* === sunxi extension: physical address query === */
struct ion_custom_data_v {
    uint32_t      cmd;
    unsigned long arg;
};

struct sunxi_phys_data_v {
    ion_handle_t handle;       /* IN, used by OLD ABI */
    uint32_t     phys_addr;    /* OUT */
    uint32_t     size;         /* OUT */
};

#define ION_IOC_CUSTOM           _IOWR(ION_IOC_MAGIC, 6, struct ion_custom_data_v)
#define ION_IOC_SUNXI_PHYS_ADDR  7    /* subcommand passed via CUSTOM.cmd */

/* heap masks: bit 0 = system (paged, no phys), bit 1 = DMA/CMA.
 * Probe order is "best for HW first": try DMA/CMA so we get phys for G2D,
 * then fall back to SYS which is what some BSPs (T507 carbit) actually
 * expose. The first mask that allocates is remembered as g_ion_heap_pref
 * so the runtime alloc path tries it first. */
#define ION_DMA_HEAP_MASK   (1u << 1)
#define ION_SYS_HEAP_MASK   (1u << 0)
static const uint32_t ION_PROBE_MASKS[] = {
    ION_DMA_HEAP_MASK,
    ION_SYS_HEAP_MASK,
    (1u << 2),
    (1u << 3),
    0xffffffffu,
};

static int      g_ion_fd       = -1;
static int      g_ion_abi_old  = 0;        /* set after probe */
static uint32_t g_ion_heap_pref = 0;       /* the mask that worked in probe */

static int ion_try_old(uint32_t mask)
{
    struct ion_alloc_old_v test;
    memset(&test, 0, sizeof(test));
    test.len          = 4096;
    test.align        = 0x1000;
    test.heap_id_mask = mask;
    if (ioctl(g_ion_fd, ION_IOC_ALLOC_OLD, &test) != 0) return -errno;
    struct ion_handle_data_v fr = { .handle = test.handle };
    (void)ioctl(g_ion_fd, ION_IOC_FREE, &fr);
    return 0;
}

static int ion_try_new(uint32_t mask)
{
    struct ion_alloc_new_v test;
    memset(&test, 0, sizeof(test));
    test.len          = 4096;
    test.heap_id_mask = mask;
    if (ioctl(g_ion_fd, ION_IOC_ALLOC_NEW, &test) != 0) return -errno;
    if ((int)test.fd > 0) close((int)test.fd);
    return 0;
}

static int ion_open_probe(void)
{
    int fd = open(ION_PATH, O_RDWR | O_CLOEXEC);
    if (fd < 0) return -errno;
    g_ion_fd = fd;

    /* Detect which ABI the kernel supports AND which heap mask it actually
     * has populated. T113/T507 4.9 BSPs are OLD ABI; whether they expose
     * the DMA/CMA heap depends on dts -- carbit T507 only has the system
     * heap (mask=1), so a DMA-only probe (the old code) would give up
     * here even though ion is perfectly functional. */
    int last_err_old = 0, last_err_new = 0;
    for (size_t i = 0; i < sizeof(ION_PROBE_MASKS)/sizeof(ION_PROBE_MASKS[0]); i++) {
        uint32_t m = ION_PROBE_MASKS[i];
        int r = ion_try_old(m);
        if (r == 0) {
            g_ion_abi_old   = 1;
            g_ion_heap_pref = m;
            LOG_I("mc_alloc: ion ABI=OLD, heap_mask=0x%x", m);
            return 0;
        }
        last_err_old = r;
    }
    for (size_t i = 0; i < sizeof(ION_PROBE_MASKS)/sizeof(ION_PROBE_MASKS[0]); i++) {
        uint32_t m = ION_PROBE_MASKS[i];
        int r = ion_try_new(m);
        if (r == 0) {
            g_ion_abi_old   = 0;
            g_ion_heap_pref = m;
            LOG_I("mc_alloc: ion ABI=NEW, heap_mask=0x%x", m);
            return 0;
        }
        last_err_new = r;
    }
    LOG_W("mc_alloc: /dev/ion opens but no heap mask works "
          "(OLD last=%s, NEW last=%s)",
          strerror(-last_err_old), strerror(-last_err_new));
    close(g_ion_fd); g_ion_fd = -1;
    return -ENOTSUP;
}

/* Fetch phys via sunxi CUSTOM ioctl. handle = OLD ABI handle, OR (NEW)
 * the fd cast to int (the driver overloads this for NEW). Returns phys
 * or 0 on failure. */
static uint32_t ion_query_phys(ion_handle_t h)
{
    struct sunxi_phys_data_v pd;
    memset(&pd, 0, sizeof(pd));
    pd.handle = h;
    struct ion_custom_data_v cd;
    cd.cmd = ION_IOC_SUNXI_PHYS_ADDR;
    cd.arg = (unsigned long)(uintptr_t)&pd;
    if (ioctl(g_ion_fd, ION_IOC_CUSTOM, &cd) == 0) return pd.phys_addr;
    return 0;
}

static int ion_alloc(struct mc_alloc_buf *out, size_t size)
{
    /* Try the probe-validated heap first (1 ioctl in the common path), then
     * the other well-known masks as fallback. CMA is preferred when it
     * works because it gives a stable phys for G2D direct-phys ops; SYS
     * is paged but still produces a real dma-buf that HW can import. */
    uint32_t heaps[4]; int nh = 0;
    if (g_ion_heap_pref) heaps[nh++] = g_ion_heap_pref;
    if (g_ion_heap_pref != ION_DMA_HEAP_MASK) heaps[nh++] = ION_DMA_HEAP_MASK;
    if (g_ion_heap_pref != ION_SYS_HEAP_MASK) heaps[nh++] = ION_SYS_HEAP_MASK;

    if (g_ion_abi_old) {
        for (int i = 0; i < nh; i++) {
            struct ion_alloc_old_v req;
            memset(&req, 0, sizeof(req));
            req.len          = size;
            req.align        = 0x1000;
            req.heap_id_mask = heaps[i];
            if (ioctl(g_ion_fd, ION_IOC_ALLOC_OLD, &req) < 0) {
                if (i == nh - 1) {
                    LOG_W("mc_alloc: ion OLD ALLOC failed: %s",
                          strerror(errno));
                    return -errno;
                }
                continue;
            }
            /* phys best-effort */
            out->phys = ion_query_phys(req.handle);

            struct ion_fd_data_v fdd = { .handle = req.handle };
            if (ioctl(g_ion_fd, ION_IOC_MAP, &fdd) < 0) {
                LOG_W("mc_alloc: ion MAP failed: %s", strerror(errno));
                struct ion_handle_data_v fr = { .handle = req.handle };
                (void)ioctl(g_ion_fd, ION_IOC_FREE, &fr);
                return -errno;
            }
            out->fd   = fdd.fd;
            out->size = size;
            return 0;
        }
    } else {
        for (int i = 0; i < nh; i++) {
            struct ion_alloc_new_v req;
            memset(&req, 0, sizeof(req));
            req.len          = size;
            req.heap_id_mask = heaps[i];
            if (ioctl(g_ion_fd, ION_IOC_ALLOC_NEW, &req) < 0) {
                if (i == nh - 1) {
                    LOG_W("mc_alloc: ion NEW ALLOC failed: %s",
                          strerror(errno));
                    return -errno;
                }
                continue;
            }
            out->phys = ion_query_phys((ion_handle_t)req.fd);
            out->fd   = (int)req.fd;
            out->size = size;
            return 0;
        }
    }
    return -EIO;
}

/* ============================================================ *
 * g2d-mem (Allwinner /dev/g2d built-in CMA allocator)          *
 *                                                              *
 * The sunxi g2d driver itself provides a small CMA-backed pool *
 * and exposes it as ioctls on /dev/g2d:                        *
 *                                                              *
 *   G2D_CMD_MEM_REQUEST (size)   → slot index 0..9 (or <0)     *
 *   G2D_CMD_MEM_GETADR  (sel)    → return value = phys addr    *
 *   G2D_CMD_MEM_RELEASE (sel)                                  *
 *                                                              *
 * The driver also implements .mmap with remap_pfn_range using  *
 * the user's mmap offset as the PFN. So a single open /dev/g2d *
 * gives us BOTH a phys addr (for G2D ioctls) AND a way for any *
 * process holding that fd to mmap the same physical memory.    *
 *                                                              *
 * Cross-process sharing: SCM_RIGHTS dup of the /dev/g2d fd     *
 * lets the client mmap (offset = phys) into the same memory.   *
 * We pass the phys list in SURFACE_OK via MC_T_PHYS_LIST.      *
 *                                                              *
 * Lifecycle: each sel is owned by the file (g2d_fd). When the  *
 * file fully closes (all refs released), the driver frees its  *
 * sels. We explicitly MEM_RELEASE on destroy.                  *
 *                                                              *
 * Pool size: small (12 MB CMA total on this T507 board); we    *
 * are budgeted for ~5 MB of surfaces (fullscreen + popup).     *
 * ============================================================ */

#define G2D_PATH             "/dev/g2d"
/* These values come from sunxi g2d_driver.h: */
#define G2D_CMD_MEM_REQUEST  0x59
#define G2D_CMD_MEM_RELEASE  0x5A
#define G2D_CMD_MEM_GETADR   0x5B

static int g_g2d_fd = -1;

static int g2d_open_probe(void)
{
    int fd = open(G2D_PATH, O_RDWR | O_CLOEXEC);
    if (fd < 0) return -errno;

    /* Smoke test: small allocation. Two failure modes worth distinguishing
     * in logs:
     *   ENOTTY (Inappropriate ioctl)  -> driver doesn't expose MEM_REQUEST
     *   ENOMEM (Cannot allocate)      -> driver responds but its internal
     *                                    allocator (dma_alloc_coherent
     *                                    against the g2d node) has no
     *                                    backing memory -- BSP / dts is
     *                                    missing a memory-region for g2d
     * Either way we move on to the next backend. */
    int sel = ioctl(fd, G2D_CMD_MEM_REQUEST, 4096);
    if (sel < 0) {
        const char *hint = "";
        if (errno == ENOMEM)
            hint = " (BSP needs reserved-memory area for g2d in dts)";
        LOG_I("mc_alloc: /dev/g2d MEM_REQUEST unavailable: %s%s",
              strerror(errno), hint);
        close(fd);
        return -ENOTSUP;
    }
    (void)ioctl(fd, G2D_CMD_MEM_RELEASE, sel);
    g_g2d_fd = fd;
    LOG_I("mc_alloc: /dev/g2d built-in CMA allocator detected");
    return 0;
}

static int g2dmem_alloc(struct mc_alloc_buf *out, size_t size)
{
    int sel = ioctl(g_g2d_fd, G2D_CMD_MEM_REQUEST, size);
    if (sel < 0) {
        LOG_W("mc_alloc: g2d MEM_REQUEST(%zu) failed: %s",
              size, strerror(errno));
        return -errno;
    }
    long phys = ioctl(g_g2d_fd, G2D_CMD_MEM_GETADR, sel);
    if (phys <= 0) {
        LOG_W("mc_alloc: g2d MEM_GETADR(sel=%d) failed: %s",
              sel, strerror(errno));
        (void)ioctl(g_g2d_fd, G2D_CMD_MEM_RELEASE, sel);
        return -EIO;
    }
    out->fd          = g_g2d_fd;     /* same fd shared across all bufs;
                                      * SCM_RIGHTS dup gives each client a
                                      * private fd into the same file */
    out->size        = size;
    out->phys        = (uint32_t)phys;
    out->mmap_offset = (uint32_t)phys;
    out->_g2d_sel    = sel;
    return 0;
}

static void g2dmem_free(struct mc_alloc_buf *b)
{
    if (g_g2d_fd >= 0 && b->_g2d_sel >= 0) {
        (void)ioctl(g_g2d_fd, G2D_CMD_MEM_RELEASE, b->_g2d_sel);
    }
}

/* ============================================================ *
 * memfd (fallback)                                             *
 * ============================================================ */

static int memfd_alloc(struct mc_alloc_buf *out, size_t size)
{
#ifdef SYS_memfd_create
    int fd = (int)syscall(SYS_memfd_create, "mc-buf", 1 /* MFD_CLOEXEC */);
#else
    int fd = -1; errno = ENOSYS;
#endif
    if (fd < 0) {
        LOG_W("mc_alloc: memfd_create failed: %s", strerror(errno));
        return -errno;
    }
    if (ftruncate(fd, size) < 0) {
        LOG_W("mc_alloc: ftruncate failed: %s", strerror(errno));
        close(fd);
        return -errno;
    }
    out->fd   = fd;
    out->size = size;
    out->phys = 0;
    return 0;
}

/* ============================================================ *
 * selection                                                    *
 * ============================================================ */

enum { B_NONE = 0, B_DMA_HEAP, B_G2D_MEM, B_ION, B_MEMFD };
static int g_backend = B_NONE;

static const char *bname(int b)
{
    switch (b) {
    case B_DMA_HEAP: return "dma-heap";
    case B_G2D_MEM:  return "g2d-mem";
    case B_ION:      return "ion";
    case B_MEMFD:    return "memfd";
    default:         return "(none)";
    }
}

const char *mc_alloc_backend_name(void) { return bname(g_backend); }

int mc_alloc_is_dmabuf(void)
{
    return (g_backend == B_DMA_HEAP) || (g_backend == B_ION);
}

int mc_alloc_init(void)
{
    const char *want = getenv("MC_ALLOC");
    if (!want || !*want) want = "auto";

    int try_dh    = (strcmp(want, "dma-heap") == 0 || strcmp(want, "auto") == 0);
    int try_g2d   = (strcmp(want, "g2d-mem")  == 0 || strcmp(want, "auto") == 0);
    int try_ion   = (strcmp(want, "ion")      == 0 || strcmp(want, "auto") == 0);
    int try_memfd = (strcmp(want, "memfd")    == 0 || strcmp(want, "auto") == 0);

    if (try_dh && dh_open() == 0) {
        g_backend = B_DMA_HEAP;
        LOG_I("mc_alloc: using dma-heap");
        return 0;
    }
    /* g2d-mem is preferred over ion on Allwinner BSPs that lack a
     * working ion ABI: the g2d driver carries its own CMA allocator and
     * its mmap() honors physical-offset, so we get one fd that both
     * sides (compositor & client via SCM_RIGHTS) can mmap into the same
     * physical memory, AND we have phys for G2D ioctls. */
    if (try_g2d && g2d_open_probe() == 0) {
        g_backend = B_G2D_MEM;
        LOG_I("mc_alloc: using g2d-mem (/dev/g2d built-in CMA)");
        return 0;
    }
    if (try_ion && ion_open_probe() == 0) {
        g_backend = B_ION;
        LOG_I("mc_alloc: using sunxi-ion (%s)", ION_PATH);
        return 0;
    }
    if (try_memfd) {
        g_backend = B_MEMFD;
        LOG_I("mc_alloc: using memfd (cross-process shareable, but no "
              "phys addr -- HW accel can't engage). Composition runs on "
              "CPU; functionality is unaffected.");
        return 0;
    }
    LOG_E("mc_alloc: no backend available (MC_ALLOC=%s)", want);
    return -ENODEV;
}

int mc_alloc_create(struct mc_alloc_buf *out, size_t size)
{
    if (!out || size == 0) return -EINVAL;
    memset(out, 0, sizeof(*out));
    out->fd = -1;
    out->_g2d_sel = -1;
    out->_backend = g_backend;

    int rc;
    switch (g_backend) {
    case B_DMA_HEAP: rc = dh_alloc    (out, size); break;
    case B_G2D_MEM:  rc = g2dmem_alloc(out, size); break;
    case B_ION:      rc = ion_alloc   (out, size); break;
    case B_MEMFD:    rc = memfd_alloc (out, size); break;
    default:         return -ENODEV;
    }
    if (rc < 0) return rc;

    /* mmap_offset is 0 for memfd/dma-heap/ion (one fd per buffer); for
     * g2d-mem it equals phys because g2d_mmap uses vm_pgoff as PFN. */
    out->map = mmap(NULL, size, PROT_READ | PROT_WRITE,
                    MAP_SHARED, out->fd, out->mmap_offset);
    if (out->map == MAP_FAILED) {
        LOG_W("mc_alloc: mmap (%s) %zu bytes off=0x%x failed: %s",
              bname(g_backend), size, out->mmap_offset, strerror(errno));
        /* For g2d-mem, the shared fd stays open; just free the slot. */
        if (g_backend == B_G2D_MEM) {
            g2dmem_free(out);
        } else if (out->fd > 0) {
            close(out->fd);
        }
        memset(out, 0, sizeof(*out));
        out->fd = -1; out->_g2d_sel = -1;
        return -errno;
    }
    memset(out->map, 0, size);
    return 0;
}

void mc_alloc_destroy(struct mc_alloc_buf *buf)
{
    if (!buf) return;
    if (buf->map && buf->map != MAP_FAILED) munmap(buf->map, buf->size);
    /* g2d-mem: many bufs share the SAME global g2d fd; never close
     * here, only release the slot. Other backends: each fd is unique. */
    if (g_backend == B_G2D_MEM) {
        if (buf->_g2d_sel >= 0)
            (void)ioctl(g_g2d_fd, G2D_CMD_MEM_RELEASE, buf->_g2d_sel);
    } else if (buf->fd > 0) {
        close(buf->fd);
    }
    memset(buf, 0, sizeof(*buf));
    buf->fd = -1; buf->_g2d_sel = -1;
}

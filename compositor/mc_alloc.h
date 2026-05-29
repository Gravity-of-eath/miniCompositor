/*
 * Surface buffer allocator.
 *
 * The compositor's per-surface shared memory used to come from
 * memfd_create. That works for cross-process mmap but offers no physical
 * address and no dma-buf semantics, so HW accelerators (G2D, RGA) can't
 * consume the buffer directly and end up rejecting the ioctl.
 *
 * This module probes the system for the best available allocator at
 * startup and exposes a uniform interface. The fd it returns is always
 * mmap-able and SCM_RIGHTS-passable; that means clients see no change.
 * The compositor additionally learns the buffer's physical address (or
 * dma-buf identity) and can hand that to G2D/RGA via accel ops.
 *
 * Backends, in priority order:
 *
 *   1. dma-heap   ("/dev/dma_heap/system")
 *      Upstream Linux ≥ 5.6. Returns a dma-buf fd. Most modern.
 *      No phys addr exposed -- HW backends use the fd path.
 *
 *   2. sunxi-ion  ("/dev/ion")
 *      Allwinner BSPs (T113 / T507 / etc.) up through 4.9-class kernels.
 *      Returns a dma-buf fd via ION_IOC_ALLOC; additionally lets us pull
 *      a physical address via the sunxi-specific custom ioctl. So we
 *      fill BOTH fd and phys on this path -- G2D can use whichever it
 *      prefers.
 *
 *   3. memfd_create
 *      Last-resort fallback. fd works, mmap works, but no phys/dma-buf.
 *      HW accel will not engage; CPU compositing only.
 *
 * Selection respects env override:
 *   MC_ALLOC=auto|dma-heap|ion|memfd   (default auto)
 */
#ifndef MC_ALLOC_H
#define MC_ALLOC_H

#include <stddef.h>
#include <stdint.h>

struct mc_alloc_buf {
    int       fd;          /* always > 0; mmap-able; SCM_RIGHTS-passable */
    void     *map;          /* mmap'd in the compositor; NULL after init */
    size_t    size;
    uint32_t  phys;         /* 0 if not available; 32-bit because that's
                             * what sunxi PHYS_ADDR returns and what G2D
                             * laddr[0] accepts */
    uint32_t  mmap_offset;  /* offset to pass to mmap() to reach this
                             * specific buffer through `fd`. memfd / dma-
                             * buf use 0; for the g2d allocator this is
                             * equal to phys (driver remap_pfn_range path).
                             * Compositor passes this to client so the
                             * client uses the same offset. */
    int       _backend;     /* internal: which backend allocated this */
    int       _g2d_sel;     /* internal: g2d allocator slot for RELEASE */
};

/* One-time process-wide init. Probes available backends, picks one,
 * logs the result. Subsequent allocs use the chosen backend. Returns 0
 * on success (memfd always succeeds, so this should never fail). */
int  mc_alloc_init(void);

/* Allocate `size` bytes; on success fills `out` and mmaps it for our use. */
int  mc_alloc_create(struct mc_alloc_buf *out, size_t size);

/* Unmap + close. Safe with all-zero `buf`. */
void mc_alloc_destroy(struct mc_alloc_buf *buf);

/* For logs / diagnostics. */
const char *mc_alloc_backend_name(void);

/* 1 iff the fd returned by mc_alloc_create() is a real dma-buf object
 * (i.e. importable by HW drivers that expect dma-buf semantics). False
 * for memfd_create. */
int mc_alloc_is_dmabuf(void);

#endif

# mc — design as implemented

Status: production-ready on T507. Replaces the original v1.1 plan
(kept as `DESIGN-v1.1-original-plan.md` for history). The big
architectural change from that plan is that the **compositor itself
went GPU** (Mali via EGL/GLES2 on `/dev/fb0`) instead of trying to
drive G2D, and the LVGL clients stayed CPU-side. AWTK remained GPU.

---

## 1. Architecture at a glance

```
+----------- client process (e.g. AWTK) -----------+
| Mali EGL context                                  |
|   eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT, fd)    |
|   glEGLImageTargetTexture2DOES + FBO              |
|   nanovg / GLES draw -> FBO                       |
|   glFinish or eglClientWaitSyncKHR                |
|   mc_surface_commit_idx(buf, damage)              |
+--------------------|-----------------------------+
                     | unix socket
                     | TLV (proto.h) + SCM_RIGHTS for fd
                     v
+----------- mc-compositor process ----------------+
| transport.c     epoll loop, COMMIT batching       |
| surface.c       mc_surface table + buf states     |
| mc_alloc.c      dma-heap / ion / g2d-mem / memfd  |
| input.c         /dev/input/eventN -> hit-test ->  |
|                 SCM_RIGHTS-routed touch frames    |
| bus.c           pub/sub, topic match              |
| lifecycle.c     VISIBLE/HIDDEN occlusion calc     |
| compose.c       z-sort, cull, dispatch to backend |
| backend_egl.c   Mali EGL window on /dev/fb0       |
|                   per-surface EGLImage cache      |
|                   shader: textured quad + blend   |
|                   eglSwapBuffers replaces PAN     |
+--------------------|-----------------------------+
                     | eglSwapBuffers
                     v
                  /dev/fb0
```

Two compose backends coexist:

* `--backend egl` — Mali GPU, ~5-10 ms/frame on T507; default when
  EGL/GLES2 sysroot is found at build time (`MC_ENABLE_EGL=1`).
* `--backend fb` — CPU memcpy/blend into fb DRAM; ~50-120 ms/frame at
  1024×600 on T507 because fb DRAM is uncached. Kept as the fallback
  for platforms without an EGL fbdev winsys.


## 2. Protocol

TLV over `SOCK_STREAM` unix socket. fds (shm/dma-buf for buffers,
eventfd for buffer release notification) travel via `SCM_RIGHTS`.

Frame layout (`common/proto.h`):

```
struct mc_hdr {
    uint16_t magic;        // 0xMC42
    uint16_t type;          // MC_CL_* / MC_SV_*
    uint32_t plen;          // payload length in bytes
    uint32_t serial;        // request/reply correlation
};
```

The payload is a list of `(tag, len, value)` triples; tags are 16-bit
(see `MC_T_*` in `common/proto.h`). Adding a new field is backward-
compatible — readers ignore unknown tags.

Notable client → server messages:

| type | name | purpose |
|---|---|---|
| 0x01 | `MC_CL_HELLO` | app name, pid, version, caps |
| 0x02 | `MC_CL_CREATE_SURFACE` | w/h/format/role/n_buf/flags |
| 0x04 | `MC_CL_COMMIT` | buf_idx + damage rects |
| 0x05 | `MC_CL_SET_ROLE` | popup pos, modal |
| 0x06 | `MC_CL_REQUEST_FOCUS` | bring this surface to top of z-class |
| 0x10 | `MC_CL_BUS_SUB` | subscribe to topic (pattern) |
| 0x11 | `MC_CL_BUS_PUB` | publish on topic |

Notable server → client messages:

| type | name | purpose |
|---|---|---|
| 0x81 | `MC_SV_WELCOME` | client_id, screen size, server caps |
| 0x82 | `MC_SV_SURFACE_OK` | sid, stride, phys list (if g2d-mem), buf fds + eventfd |
| 0x84 | `MC_SV_LIFECYCLE` | VISIBLE / HIDDEN / SUSPENDED |
| 0x85 | `MC_SV_INPUT` | touch event with surface-local x/y |
| 0x90 | `MC_SV_BUS_MSG` | matched bus message delivery |


## 3. Buffer model

`mc-compositor` allocates buffers, hands the fds to the client via
SCM_RIGHTS. Clients never allocate display buffers themselves — they
just mmap (CPU path) or `eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT)`
(GPU path) the fds they receive.

Allocator selection happens at compositor startup, prioritised:

```
mc_alloc_init()
   try /dev/dma_heap/{cma, reserved, linux,cma, system}      [linux >= 5.6]
   try /dev/ion (probes heap masks 0x02 / 0x01 / 0x04 / 0x08 / all,
                  remembers the one that worked)              [T113/T507 BSP]
   try /dev/g2d MEM_REQUEST                                   [some sun50i]
   fall back to memfd_create()                                [host x86_64 dev]
```

On T507 carbit, ION old-ABI (heap_mask=1, system heap, paged) is the
working path. The fds returned look like `/dmabuf (deleted)` in
`/proc/$pid/fd/` and Mali importing them via `EGL_LINUX_DMA_BUF_EXT`
just works (verified by `tools/egl_dmabuf_probe.c`).

Per-surface buffer ownership:

* Server side: `MC_BUF_FREE` ↔ `MC_BUF_READY` ↔ `MC_BUF_SCANOUT`.
* Client side: round-robin, never tries to discover which buf the
  server just freed — relies on the protocol invariant that
  N-buffer rotation is symmetric.
* Release path: server writes to the surface's eventfd whenever a buf
  goes from SCANOUT back to FREE.


## 4. Surface flags

```c
#define MC_SURF_FLAG_FLIP_Y    (1u << 0)   // dma-buf is GL FBO origin
                                            // (bottom-up); compositor
                                            // inverts UV when drawing.
```

AWTK sets it because GL FBO renders bottom-up. LVGL (CPU AGGE) leaves
it cleared.


## 5. Composition

### GPU backend (backend_egl.c)

Per frame:

1. `glClear(0,0,0)` (full screen)
2. z-sort visible surfaces bottom-up (`z_order, focus_stamp` tiebreaker)
3. For each surface:
   * lookup or create `EGLImageKHR` from `bufs[idx].shm_fd`
   * bind as `GL_TEXTURE_2D` via `glEGLImageTargetTexture2DOES`
   * `glDrawArrays(GL_TRIANGLES, 0, 6)` of a textured quad
   * shader handles UV-flip-y, opaque (fullscreen) vs alpha (popup)
4. `eglSwapBuffers` (Mali handles fb page flip + maybe vsync)

No damage tracking — Mali draws the whole screen each frame in a few ms.

### CPU backend (backend_fb.c)

Falls back to per-surface dmg bbox + history (two-frame catch-up for
the double-buffered fb), throttled to `MC_COMPOSE_HZ` (default 60).
Compose is coalesced: an epoll batch with N commits triggers exactly
one compose at the end.


## 6. Lifecycle

`lifecycle.c::is_occluded` recomputes VISIBLE/HIDDEN whenever a
surface is created, destroyed, focus_stamp changes, or a first commit
lands. Only opaque fullscreen surfaces occlude. A surface that hasn't
yet committed (`cur_scanout < 0`) is treated as non-occluding so a
newly-created fullscreen doesn't kick the current one off-screen
before it has content to show.


## 7. Touch routing

* All `/dev/input/eventN` MT-B events pass through `ial_evdev.c`.
* DOWN: hit-test by (z, focus_stamp, role) descending, **rectangular**
  (popups capture taps in their alpha=0 corners). The hit grabs all
  MOVE/UP until the next UP — drag-outside semantics.
* The compositor sends surface-local `(x, y)` to the owning client.


## 8. AWTK integration

Three files, with `LCD_DEVICES = "mc"` selected in
`awtk_config_define.py`:

| file | role |
|---|---|
| `awtk-port/egl_devices/mc/egl_devices.c` | mc EGL backend: connect, create surface, import dma-buf as FBO, swap on each frame |
| `awtk-port/input_thread/input_thread_mc.{c,h}` | pumps `mc_dispatch` touch into AWTK's main_loop, subscribes to `app/focus` |
| `awtk-port/lcd_linux/lcd_linux_egl.c` | wraps lcd's begin_frame so the mc backend can read AWTK's per-frame dirty rect bbox |

AWTK's own widget tree and GL renderer (nanovg-plus) are unchanged.


## 9. LVGL integration

`ports/lvgl/lv_port_mc.{c,h}` is a single-file display + indev driver
for LVGL 9.0 (DIRECT mode, double-buffered). Clients call
`lv_port_mc_init()` once with a surface they obtained from `mc_connect`
+ `mc_surface_create_shm`, then drive LVGL normally.


## 10. What was tried and rejected

* **G2D HW accel**: bsp dma_buf_get returned fd=0 on T507 4.9 kernel.
  Struct layout in BSP source ≠ struct layout in the BSP doc. Not
  worth chasing per-board. Kept `accel_g2d.c` as a reference impl for
  other platforms.
* **LVGL OpenGL ES backend (9.2+)**: confirmed it's just texture-cache
  for software-rendered widgets. Same effect as our compositor-side
  GPU upload would have. Not worth the LVGL version upgrade given the
  compositor-side path delivered.
* **Compose throttle**: helped CPU backend (popup blink no longer
  saturated compose) but became irrelevant once GPU backend landed
  (compose so cheap that throttling actively hurts responsiveness).
  Code stays, default 60 Hz when CPU backend is active.


## 11. File layout (where to read what)

| path | content |
|---|---|
| `common/proto.{h,c}` | TLV protocol + SCM_RIGHTS helpers |
| `compositor/main.c` | argv, backend select, startup |
| `compositor/transport.c` | epoll loop, message dispatch, COMMIT batching |
| `compositor/surface.c` | surface/buf table, commit semantics |
| `compositor/compose.c` | z-sort + GPU/CPU compose dispatch |
| `compositor/backend_egl.c` | Mali EGL backend + texture cache + shader |
| `compositor/backend_fb.c` | CPU fb backend with FBIOPAN_DISPLAY |
| `compositor/mc_alloc.c` | dma-heap / ion / g2d-mem / memfd |
| `compositor/lifecycle.c` | occlusion-based VISIBLE/HIDDEN |
| `compositor/input.c` + `ial_evdev.c` | touch hit-test + evdev decoder |
| `compositor/bus.c` | pub/sub with prefix wildcard |
| `libmc/src/connect.c` | mc_connect, fd plumbing |
| `libmc/src/surface.c` | mc_surface_*, buf round-robin |
| `libmc/src/bus.c` | client-side pub/sub |
| `ports/lvgl/lv_port_mc.c` | LVGL display driver |
| `awtk/.../awtk-port/egl_devices/mc/egl_devices.c` | AWTK lcd backend |
| `examples/demo-fullscreen` | LVGL fullscreen demo with `app/focus` subscription |
| `examples/demo-popup` | LVGL popup with OK-cycles-focus button |
| `tools/mc-fill-client.c` | minimal non-LVGL client for pipeline tests |
| `tools/egl_dmabuf_probe.c` | end-to-end EGL+dma-buf round-trip check |

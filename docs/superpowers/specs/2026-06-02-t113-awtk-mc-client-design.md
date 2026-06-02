# T113 AWTK as an mc client (software path) + mixed AWTK/LVGL overlay test

**Date:** 2026-06-02
**Branch:** `t113-awtk`
**Status:** design approved, pending spec review

## Goal

Make AWTK run on T113 as an **mc compositor client** (rendering into a
compositor-allocated dma-buf surface, composited by the G2D backend), and
prove heterogeneous compositing by overlaying an AWTK demo and an LVGL demo
on screen at the same time.

Production-quality port (input, lifecycle, multi-buffer all wired), modeled
on the **already-working T507 scheme**. T113 has no Mali/EGL, so the only
thing that changes vs T507 is the render target: a CPU-mapped dma-buf +
AWTK software canvas, instead of an EGLImage/FBO. G2D-accelerated AWTK
rendering is explicitly out of scope for this round (see Non-goals).

## Background: the proven T507 scheme

On T507 AWTK becomes an mc client via a custom egl device
(`awtk-port/egl_devices/mc/egl_devices.c`):

1. `mc_connect()` to the compositor.
2. `mc_surface_create_shm_ex(w,h, MC_FMT_BGRA8888, MC_ROLE_FULLSCREEN, n_buf=2)`
   — the compositor allocates ion dma-bufs.
3. imports each dma-buf as `EGLImageKHR` → `GL_TEXTURE_2D` → FBO.
4. AWTK renders into the FBO (Mali).
5. `swap_buffers`: `glFinish`, `mc_surface_commit_idx(cur, damage)`, rotate
   to next, `mc_surface_wait_buf_free(next)`.

Companion pieces, already mc-aware and **reused unchanged** on T113:
`awtk-port/input_thread/input_thread_mc.c` (input comes from the
compositor, so AWTK needs no tslib), `main_loop_linux.c`, the lifecycle
HIDDEN-state throttle, and the dirty-rect → damage hook.

## Architecture

```
 AWTK app (demo)
   │  draws with software canvas (VGCANVAS=NANOVG, lcd_mem BGRA8888)
   ▼
 lcd_devices/mc  (NEW)  ── mc client ──►  mc-compositor (--backend g2d)
   - mc_connect                              - G2D composites all clients
   - mc_surface_create_shm_ex(BGRA8888,2)    - AWTK(role) blended/copied with
   - lcd_mem points at mc_surface_buf_at(i)    LVGL(role) into dma-buf back-buf
   - flush: commit_idx + rotate + wait_free  - cedar cache-flush + ARGB8888 fmt
   ▲                                          - memcpy → /dev/fb0 + page-flip
   │ input + lifecycle
 input_thread_mc (reused)
```

## Components

### 1. `lcd_devices/mc` — software mc-client LCD (the only substantial new code)

Direct analog of `egl_devices/mc`, FBO replaced by a CPU bitmap. Lives in
the T113 copy of `awtk-port/` (see Source layout); platform-neutral C.

**Create:**
- `mc_connect(mc_app_name())`.
- `mc_surface_create_shm_ex(mc, w, h, MC_FMT_BGRA8888, role, n_buf=2)`.
  `role` from config/env (default `MC_ROLE_FULLSCREEN`; the AWTK demo in the
  overlay test runs as `MC_ROLE_POPUP`).
- For each buffer `i`: `mc_surface_buf_at(surf, i, &stride)` already returns
  the libmc-mmap'd CPU pointer — no manual mmap needed.
- Wrap the **current** buffer as an AWTK `lcd_mem_bgra8888` (online fb =
  the mc buffer pointer, with the reported stride).
- Stash shared `mc_ctx_t*` / `mc_surface_t*` in globals
  (`lcd_linux_mc_get_ctx/_surf`) for `input_thread_mc` — same hooks the
  T507 backend exposes.

**Flush / swap (per frame):**
1. AWTK has finished drawing into the current mc buffer (CPU writes).
2. `mc_surface_commit_idx(surf, cur_idx, &damage, n)` with the frame's
   dirty rect(s).
3. `next = (cur_idx + 1) % n_buf`; `mc_surface_wait_buf_free(surf, next)`.
4. Re-point the `lcd_mem` online fb at `mc_surface_buf_at(surf, next, …)`;
   `cur_idx = next`.

No cache calls needed on the client side: the compositor already flushes
each source surface (cedar `AW_MEM_FLUSH_CACHE_RANGE`) before G2D reads it,
which covers AWTK's CPU writes.

**Lifecycle / dirty rect:** reuse the T507 pattern — a HIDDEN hint set by
`input_thread_mc` on `MC_EV_LIFECYCLE` that throttles commits when hidden,
and `lcd_*_on_dirty_rect()` feeding the commit damage.

### 2. Reused unchanged
`input_thread/input_thread_mc.{c,h}`, `main_loop_linux.c`, the dirty-rect
and lifecycle hooks. Verify they don't hard-depend on EGL symbols; if they
do, the dependency is moved behind the shared `lcd_linux_mc_*` accessors.

### 3. Device selection / config
- New config value selecting the software mc LCD (e.g. `LCD_DEVICES=mc`),
  wired in the T113 copy's `awtk-port/SConscript` + `devices.c` so the new
  device source is compiled and chosen. `VGCANVAS=NANOVG`, EGL off,
  `WITH_G2D=False`.
- New `t113_mc_awtk_config_define.py` (or adapt the existing
  `t113_awtk_config_define.py`, which currently selects `LCD_DEVICES=fb`).

## Source layout (isolated, no T507 interference)

The repo keeps third-party source per platform under `deps_source/<P>/`.
AWTK core (1.2 G, unmodified) is shared by symlink; the port layer we edit
is a real copy so all T113 changes are isolated:

```
deps_source/T113/awtk/
  awtk          -> symlink ../../T507/awtk/awtk      # 1.2G core, read-only, shared
  awtk-linux-fb/                                     # COPY of T507's (source only)
      awtk-port/lcd_devices/mc/lcd_mc.c              # NEW device
      awtk-port/{devices.c,SConscript}               # T113-local edits
      <t113 mc config>
```

`awtk-linux-fb/awtk_config.py` resolves the core via `TK_ROOT=../awtk`,
which lands on the symlink → shared core. Build outputs (`*.o`, `bin/`,
`lib/`) are git-ignored, so the committed copy is source only.

All work happens on the **`t113-awtk` branch**; merge to `main` after the
overlay test passes on hardware. `main` keeps the `t113-g2d-working` tag.

## Build & packaging (`build.sh`)

- T113: drop the unconditional `SKIP_AWTK=1`; build `libawtk.so` + the AWTK
  demo via the awtk-linux-fb scons flow with the T113 mc config + musl
  toolchain.
- Package `lib/libawtk.so` and `bin/awtk-demo` into staging.
- **tslib is no longer required** for the mc path (input via
  `input_thread_mc`); the empty `deps_libs/T113/tslib` stops mattering.
- Keep CPU/LVGL fallbacks intact; AWTK build failure must not break the
  rest of the T113 package (guard like `SKIP_LVGL`).

## Mixed overlay test (the validation)

`start.sh` on the device:
1. `mc-compositor -b g2d`.
2. LVGL `demo-fullscreen` as **`MC_ROLE_FULLSCREEN`** (bottom).
3. AWTK demo as **`MC_ROLE_POPUP`** (top) — composited over LVGL via the
   G2D `BLD_H` SRCOVER path.

This exercises, in one shot: heterogeneous clients (LVGL + AWTK) sharing the
compositor; G2D opaque copy (LVGL) + alpha blend (AWTK popup); and AWTK's
CPU-written dma-buf going through the source cache-flush. Validated by
screenshot: AWTK card blends correctly over the LVGL dashboard, correct
colors, no white lines.

## Cache & pixel format (already solved upstream)
AWTK writes BGRA8888 into the mc dma-buf with the CPU. The compositor's
existing `backend_g2d` handling — cedar source-flush before BITBLT_H/BLD_H
and declaring surfaces as `G2D_FORMAT_ARGB8888` — applies to AWTK surfaces
unchanged, so neither white lines nor wrong colors should recur.

## Success criteria
1. `./build.sh T113` produces `libawtk.so` + an AWTK mc-client demo (ARM/musl),
   AWTK build cleanly skippable on failure.
2. On hardware: AWTK demo alone renders correctly as an mc client (`-b g2d`),
   touch input works via `input_thread_mc`.
3. Mixed test: AWTK (popup) correctly alpha-composited over LVGL (fullscreen),
   right colors, no artifacts (screenshot).
4. T507 build/behavior unchanged (shared core untouched; port layer copied).

## Non-goals (this round)
- G2D-accelerated AWTK rendering (`WITH_G2D`, the awtk-tina-g2d path) — a
  later optimization (approach "B") if software rendering is too slow.
- disp2 zero-copy scanout (tracked separately; revisit on tearing).
- Multi-window AWTK, resize, rotation.

## Risks
- `input_thread_mc` / `main_loop_linux` may carry EGL-specific assumptions;
  may need small refactors to share cleanly with a non-EGL LCD.
- AWTK `lcd_mem` double-buffering vs mc's n_buf rotation: ensure AWTK isn't
  also keeping its own back buffer that desyncs from the mc buffer we swap.
- Software NANOVG fill-rate on T113 (Cortex-A7) at 800×480 — acceptable for
  the test; revisit with approach B if too slow.

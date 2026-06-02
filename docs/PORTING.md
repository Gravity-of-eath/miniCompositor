# Porting mc to a new platform

This guide describes how to bring up `mc` on a new SoC / BSP. The current
reference is **T507** (Allwinner sun50iw, Mali-G31). Planned next:
**T113** (Allwinner sun8iw, no GPU, has G2D).

The goal of this document is to keep mc as **one source tree** while
making per-platform code easy to slot in.


## Design rules

1. **One tree, no per-platform branches.** Branches diverge and every
   fix has to be cherry-picked. Don't go there.
2. **Platform differences live behind backends**, not behind `#ifdef`s
   scattered through business logic. Compose, surface, bus, lifecycle,
   focus, and the wire protocol are platform-agnostic and stay shared.
3. **`#ifdef` is the bottom-layer fallback** — used only for kernel/driver
   ABI differences (e.g. ION old/new struct, header path), and confined to
   one source file per concern (`mc_alloc.c`, `accel_*.c`).
4. **Selection is build-time, not runtime**, for things that can't coexist
   (toolchain, sysroot, library set). Runtime selection (`--backend`,
   `MC_ALLOC=…`) is for things that *can* coexist on the same build.
5. **Per-platform artifacts live under `deps_libs/<P>/` and
   `deps_source/<P>/`.** mc's own source never moves.


## What's platform-dependent

| Concern | Layer | Mechanism |
|---|---|---|
| Toolchain + sysroot | build | `T5SDK_ENV` (or analog) sourced in `build.sh` |
| LVGL / AWTK headers + prebuilt libs | build | `deps_libs/<P>/{lvgl,mali,…}`, `deps_source/<P>/…` |
| LVGL build (it's CPU rendering) | build | `LVGL_PLATFORM=<P>` → `deps_libs/<P>/lvgl/Makefile` |
| Output backend (CPU fb / GPU EGL / G2D blit / ...) | runtime | `--backend` flag, `compositor/backend_*.c` |
| 2D HW accel (G2D, RGA) for client→fb blits | build + runtime | `MC_ENABLE_G2D` / `MC_ENABLE_RGA` flags + `accel_*.c` |
| dma-buf allocator (dma-heap / ION old / ION new / memfd) | runtime | `MC_ALLOC=…`, kernel ABI variants live in `mc_alloc.c` |
| AWTK lcd backend (GL FBO vs. plain framebuffer) | build | `LCD_DEVICES=mc` (GL) or new `LCD_DEVICES=mc_fb` |
| Input device path + axis calibration | runtime | `--input`, `--swap-xy`, `--invert-x/y` |

The right column is the contract. If something doesn't fit any of these
slots, it usually means a new backend or accel module is the right home,
not a new `#ifdef`.


## Directory layout per platform

```
deps_source/<P>/                          # third-party sources
  lvgl-release-v9.0/                      # LVGL src (currently same across platforms)
  awtk/                                   # AWTK + awtk-linux-fb (+ mc integration)

deps_libs/<P>/                            # prebuilt third-party libs
  lvgl/
    liblvgl9.a + lv_conf.h + Makefile     # rebuilt from deps_source/<P>/lvgl-release-v9.0
    build/                                # intermediate (gitignored)
  <gpu>/                                  # e.g. mali/  (T507 only — T113 has none)
  <accel>/                                # e.g. g2d-headers/ if vendor split
  tslib/                                  # touchscreen runtime + plugins
```

`lv_conf.h` may need to differ across platforms (e.g. cache sizes, AGGE
flags). That's why it lives under `deps_libs/<P>/lvgl/` rather than
being shared.


## How the compositor selects HW

### Output backend (`compositor/backend_*.c`)

| Backend | When | Hardware required | Compose path |
|---|---|---|---|
| `backend_fb`  | always built | `/dev/fb0` | CPU (compose.c per-pixel) |
| `backend_ppm` | always built | none (dev host) | CPU |
| `backend_egl` | `MC_ENABLE_EGL=1` | Mali + EGL fbdev (libmali) | HW (sets `hw_compose`) |
| `backend_g2d` | `MC_ENABLE_BACKEND_G2D=1` | Allwinner `/dev/g2d` | HW (sets `hw_compose`) |

**HW vs CPU compose** is determined by whether the backend populates
`struct mc_backend::hw_compose` with begin_frame / draw_surface /
end_frame ops. compose.c checks that pointer per frame -- no per-backend
`if (strcmp(...))` anywhere in the main loop.

Adding a new output backend (e.g. RGA-blit, DRM/KMS):
1. Create `compositor/backend_<name>.c` implementing `struct mc_backend`
   (`compositor/backend.h`). If the device can compose whole frames in
   HW, also fill `.hw_compose` with a `struct mc_backend_hw_compose_ops`.
2. Declare `extern struct mc_backend backend_<name>;` in `backend.h`
   under an `#ifdef MC_ENABLE_<NAME>` guard.
3. Register it in `main.c` next to the existing
   `if (strcmp(backend_name, "egl") == 0)` cases. No `gpu_compose` flag
   to thread through -- it's derived from `be->hw_compose`.
4. Add a build flag block in the top Makefile mirroring `MC_ENABLE_EGL`
   or `MC_ENABLE_BACKEND_G2D`.

### Client→fb blit accel (`compositor/accel_*.c`)

Selected per-blit via `accel_select.c`. CPU is the fallback. Adding one
(e.g. RGA already exists, you'd add another the same way):
1. `compositor/accel_<name>.c` implementing the `struct mc_accel` ops.
2. Wire it into `accel_select.c`.
3. Gate the source in the Makefile with `MC_ENABLE_<NAME>`.

### dma-buf allocator (`compositor/mc_alloc.c`)

This is the one place where vendor ABI differences are unavoidable.
Pattern: define one `static int alloc_<scheme>(...)` per ABI in the same
file, dispatch by `MC_ALLOC` env or auto-probe order. New SoC with a new
ION struct layout → add a new variant beside `ion_old` / `ion_new`,
don't fork the file.


## Build flags surface

| Flag | Purpose | Default |
|---|---|---|
| `CROSS=1` | use `aarch64-linux-gnu-gcc` | host gcc |
| `LVGL_PLATFORM=<P>` | which `deps_*/<P>/lvgl/` to use | `T507` |
| `MC_ENABLE_EGL=1` | build `backend_egl` (Mali GPU compose) | auto if T507 sysroot |
| `MC_ENABLE_G2D=1` | build `accel_g2d` | auto if Allwinner sysroot |
| `MC_ENABLE_RGA=1` | build `accel_rga` | auto if Rockchip sysroot |

Auto-detection sniffs `$SDKTARGETSYSROOT` for vendor markers (see
`Makefile` lines 33-66). When adding a new SoC, add a marker test (e.g.
`findstring sun8iw,$(SDKTARGETSYSROOT)`) rather than hard-coding `1`,
so CPU-only builds still work without the SDK.


## Walk-through: adding T113

T113 has no Mali GPU but does have a 2D engine (G2D). The plan:

| Concern | T113 choice |
|---|---|
| Compose | `--backend g2d` (`compositor/backend_g2d.c`). G2D composites each client dma-buf into a private dma-buf back-buffer (`FILLRECT_H` clears, `BITBLT_H` draws), then present memcpy's that into `/dev/fb0` + page-flips ("B1" path, since fb0 has no smem_start so G2D can't target it directly). **ABI: the enhanced `_H` family only** (`g2d_image_enh`, `BITBLT_H=0x55`/`FILLRECT_H=0x56`/`BLD_H=0x57`) — T113 uses the *RCQ* driver (`deps_source/T113/sunxi_g2d-main/g2d_rcq/`, `CONFIG_ARCH_SUN8IW20`) which has NO 1.0 `BITBLT(0x50)`/`FILLRECT(0x51)`. ⚠️ Ignore `docs/Linux G2D.pdf` — it's the T5 manual and gets details (e.g. FILLRECT_H/MASK_H numbers) wrong for T113. Cross-checked against the verified reference `3rdLibrary/Awtk_g2d/awtk-tina-g2d/`. |
| Client→fb accel | `accel_g2d.c` is the T507 `_H` per-surface accelerator. **It must NOT be built for T113** (`MC_ENABLE_G2D=0` in `build.sh`): its struct layout / call flow differs from T113's RCQ driver and HANGS the compositor. Use `--backend g2d` instead. |
| Surface backing | T113 has a **G2D IOMMU** (`G2D_IOMMU_MASTER_ID 3`), so G2D consumes **dma-buf fds** (`g2d_image_enh.fd`, `use_phy_addr=0`) — no physical address needed (`phys=0` from ion is fine). Use `MC_ALLOC=ion` or `dma-heap` so surfaces are real dma-bufs; `MC_ALLOC=memfd` forces the whole-frame CPU fallback. |
| Cache | The dma-buf back-buffer is CPU-read on present. The standard `DMA_BUF_IOCTL_SYNC` is a no-op for sunxi-ion on this kernel, so `backend_g2d.c` flushes via `/dev/cedar_dev` `AW_MEM_FLUSH_CACHE_RANGE` (clean+invalidate over the buffer's CPU VA) — both the source surfaces before BITBLT_H/BLD_H (else random horizontal white lines) and the back-buffer before the present memcpy. |
| Pixel format | mc surfaces are BGRA8888 *in memory*; G2D format names are 32-bit *word* order, so that's `G2D_FORMAT_ARGB8888` (not BGRA8888). Wrong here → BITBLT (copy) still looks right but BLD_H (blend) shows wrong colors. |

**Future — zero-copy scanout (B2):** present currently memcpy's the composited dma-buf into `/dev/fb0` (B1). To drop that copy, hand the ion buffer straight to a sunxi **disp2** layer: `<video/sunxi_display2.h>` ships in the T113 toolchain, and `DISP_LAYER_SET_CONFIG2` (0x49) + `disp_fb_info2.fd` take a dma-buf fd directly (IOMMU, no phys). Cost is medium (double-buffer + vsync + coexisting with the fb0 layer); main risk is there's no verified reference for the disp layer config in-tree, and a bad config blanks the panel. Only worth it if tearing/memcpy becomes a measured problem at higher resolution — at 800×480 the copy is ~1–2 ms/frame. Prototype the disp layer config standalone before wiring it into present(), keep B1 as fallback.
| LVGL | Same source. Has its own `lv_conf.h` under `deps_libs/T113/lvgl/` (start from T507's, adjust LCD/cache). |
| AWTK | No GPU → no `egl_devices/mc`. Need a `LCD_DEVICES=mc_fb` backend that maps a mc dma-buf as a CPU bitmap and uses AWTK's software canvas (NANOVG software backend or AGGE). Build skipped by default (`SKIP_AWTK=1` in `build.sh T113`). |
| tslib | T113 build of tslib + plugins, dropped under `deps_libs/T113/tslib/`. |
| Toolchain | `arm-openwrt-linux-muslgnueabi-gcc` — armv7, 32-bit. `build.sh T113` reads `T113_TOOLCHAIN` (default `/develop/toolchain_t113_musl/bin/arm-openwrt-linux-muslgnueabi-`). |

### Concrete steps

1. **Stage deps.**
   ```
   deps_source/T113/
     lvgl-release-v9.0/    # copy from T507 if no changes, or pin a tag
     awtk/                 # copy of awtk-linux-fb + a new mc_fb lcd backend
   deps_libs/T113/
     lvgl/                 # Makefile pointing at deps_source/T113/lvgl-release-v9.0
                           # + a T113-specific lv_conf.h
     tslib/                # armv7 tslib runtime + plugins
   ```
   Mali blobs are *not* needed.

2. **Toolchain hook in `build.sh`.** Add a `case T113)` branch that sets
   `T113_ENV` (or just exports `CROSS_COMPILE=arm-openwrt-linux-...-`).

3. **Top Makefile.** Add `findstring sun8iw,$(SDKTARGETSYSROOT)` to the
   `MC_ENABLE_G2D` auto-detect block. Verify `MC_ENABLE_EGL` stays *off*
   for T113.

4. **Verify allocator.** Run a small dma-buf alloc test on the device.
   If `ion_old` succeeds, no code change. If the struct layout is
   different, add `ion_t113` next to `ion_old` in `mc_alloc.c`.

5. **Verify G2D.** Same: try a small blit. If the kernel module's struct
   layout differs from T507, add a variant in `accel_g2d.c` selected by
   compile flag or runtime probe — don't fork the file.

6. **AWTK `mc_fb` lcd backend.** New file under
   `deps_source/T113/awtk/awtk-linux-fb/awtk-port/fb_devices/mc/`
   modeled on `egl_devices/mc/` but using a CPU surface instead of GL
   FBO. Wire it in `awtk_config_define.py` with `LCD_DEVICES = "mc_fb"`.

7. **Run.** `./build.sh T113` should produce
   `output/T113/mc-T113-<stamp>.tar.gz` with the same layout as T507.

### What should *not* happen

- No `#ifdef T113` in `compose.c` / `surface.c` / `bus.c` / `lifecycle.c`
  / `transport.c` / `input.c`. If you find yourself wanting one, the
  thing belongs in a backend or accel module.
- No `if (strcmp(platform, "T113") == 0)` runtime branching in the
  business logic. Use the existing backend dispatch.
- No second copy of mc source under `platforms/T113/`. The whole point
  of `deps_*/<P>/` is to keep mc itself single-source.


## Checklist when adding any new platform

- [ ] `deps_source/<P>/` populated (LVGL src, AWTK src + awtk-linux-fb)
- [ ] `deps_libs/<P>/` populated (lvgl prebuilt, GPU blobs if any, tslib)
- [ ] `deps_libs/<P>/lvgl/Makefile` builds `liblvgl9.a` from
      `deps_source/<P>/lvgl-release-v9.0/`
- [ ] `lv_conf.h` reviewed for screen size / cache / feature flags
- [ ] `Makefile` auto-detect blocks updated (`MC_ENABLE_G2D`, `_RGA`,
      `_EGL`) — add sysroot markers, never hard-code `1`
- [ ] `compositor/mc_alloc.c` verified or extended for the kernel ABI
- [ ] `compositor/accel_*.c` verified or extended for vendor 2D engine
- [ ] Output backend chosen / written: `backend_fb` (default),
      `backend_egl` (if Mali), or a new `backend_<vendor>`
- [ ] AWTK port written under `deps_source/<P>/awtk/awtk-linux-fb/
      awtk-port/<dev>_devices/mc/` — GL FBO if GPU, CPU bitmap if not
- [ ] `awtk_config_define.py` (and any `<P>_awtk_config_define.py`)
      updated with toolchain prefix, `MC_ROOT` walk-up, lib/include
      paths under `deps_libs/<P>/`
- [ ] `build.sh` extended with a `case <P>)` branch (toolchain env)
- [ ] `output/<P>/staging/scripts/{start,stop,deploy}.sh` reviewed for
      device paths (`/dev/input/event…`, fb device)
- [ ] Smoke test on device: compositor + `demo-fullscreen` + `demo-popup`
      + AWTK `demo1`, with touch
- [ ] `.gitignore` whitelists for any new committed binary blobs under
      `deps_libs/<P>/`
- [ ] README.md updated to mention the new platform in the Layout +
      Build sections


## When to break the rules

- **`#ifdef <PLATFORM>` is OK** when isolating a kernel/driver ABI
  difference inside one file (`mc_alloc.c`, `accel_g2d.c`). Keep the
  ifdef narrow — a struct definition or a single ioctl call, not whole
  functions or files.
- **A second backend file is the right answer** when the platform needs
  a different *strategy* (GPU compose vs. CPU compose vs. direct G2D
  blit). Don't try to unify these with conditional code.
- **A platform branch is *not* OK.** If you're tempted, write down what
  exactly is platform-specific and put each item through the table at
  the top of this doc — almost always it fits a backend or accel slot.

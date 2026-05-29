# deps_source/T113

Third-party source trees for the **T113** target.

Mirror of `deps_source/T507/`.

## What to populate

| Directory | Source | Notes |
|---|---|---|
| `lvgl-release-v9.0/` | LVGL 9.0 source tarball | Currently identical to T507's tree. If T113 ever needs LVGL patches (e.g. cache size, AGGE flags) keep them in this copy, not as ifdefs in the shared T507 tree. Could symlink to T507 to start. |
| `awtk/` | AWTK + `awtk-linux-fb` + mc integration | T113 has no GPU, so the AWTK port lives under `awtk/awtk-linux-fb/awtk-port/fb_devices/mc/` (CPU bitmap into a mc dma-buf), NOT `egl_devices/mc/`. See `docs/PORTING.md`. |

## Quick start (when T113 toolchain is available)

```sh
export CROSS_COMPILE=/develop/toolchain_t113_musl/bin/arm-openwrt-linux-muslgnueabi-

# 1. populate sources first (lvgl + awtk)
# 2. build the platform LVGL static lib
make -C deps_libs/T113/lvgl CROSS=1

# 3. cross-build mc + demos for T113
make CROSS=1 LVGL_PLATFORM=T113 MC_ENABLE_BACKEND_G2D=1

# 4. or use the one-shot script (once toolchain env hook is set up)
./build.sh T113
```

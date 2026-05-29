# deps_libs/T113

Prebuilt third-party libraries for the **T113** target (Allwinner sun8iw,
Cortex-A7, no GPU, has G2D 2D engine, sunxi-ION old ABI).

Mirror of `deps_libs/T507/` minus `mali/` (T113 has no Mali GPU).

## What to populate

| Directory | Source | Notes |
|---|---|---|
| `lvgl/liblvgl9.a` | `make -C deps_libs/T113/lvgl CROSS=1` | Rebuilds from `deps_source/T113/lvgl-release-v9.0/`. Needs `deps_source/T113/lvgl-release-v9.0/` and `deps_libs/T113/lvgl/lv_conf.h` first. |
| `lvgl/lv_conf.h` | hand-tuned for T113 LCD size + cache budget | Start by copying from `deps_libs/T507/lvgl/lv_conf.h` then adjust `LV_COLOR_DEPTH`, `LV_MEM_SIZE`, `LV_LAYER_SIMPLE_BUF_SIZE` for the actual T113 screen. |
| `tslib/lib/`, `tslib/include/` | cross-built tslib for armv7-musl | Copy `libts-1.3.so.0.1.3`, `ts/<plugins>.so`, plus headers. |

`build/` is intermediate output and is gitignored.

## What is NOT here vs. T507

- **`mali/`** — T113 has no Mali GPU. No EGL/GLESv2 blobs needed.
  Mc's GPU compose backend (`backend_egl`) is not built for T113.

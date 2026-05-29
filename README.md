# mc — multi-process compositor for embedded Linux

A lightweight Unix-socket-based display compositor that lets several UI
processes (LVGL, AWTK, custom) share a single framebuffer with z-order,
popups, touch routing, lifecycle messaging and a pub/sub bus. Designed
for Allwinner T113/T507 and Rockchip RV1126/RV1106/RK3036 BSPs which
historically allow only one process to own `/dev/fb0`.

The current implementation targets **T507 (Allwinner sun50iw, Mali-G31)**
end-to-end; other platforms reuse the framework with CPU compositing
until they get their own HW accel backend wired in.

See `docs/DESIGN.md` for the architecture in detail.


## What runs today

```
+------------------+    +------------------+    +------------------+
|  AWTK app        |    |  LVGL fullscreen |    |  LVGL popup      |
|  Mali GPU +      |    |  CPU AGGE        |    |  CPU AGGE        |
|  nanovg-plus     |    |  -> dma-buf SHM  |    |  -> dma-buf SHM  |
|  -> dma-buf FBO  |    |                  |    |                  |
+--------|---------+    +--------|---------+    +--------|---------+
         |                       |                       |
         +-----------+-----------+-----------+-----------+
                                 |
                       unix socket + SCM_RIGHTS
                                 |
                  +--------------v--------------+
                  |  mc-compositor              |
                  |  - z-order, hit-test        |
                  |  - lifecycle, bus, focus    |
                  |  - ion dma-buf allocator    |
                  |  - GPU compose (Mali EGL)   |
                  |    or CPU compose (fb)      |
                  +--------------|--------------+
                                 |
                         eglSwapBuffers
                                 |
                            /dev/fb0
```

| Component                        | Status         |
| -------------------------------- | -------------- |
| TLV protocol + fd-passing        | done           |
| Unix socket server / epoll loop  | done           |
| ion dma-buf allocator (T507/T113)| done           |
| Multi-surface z-order            | done           |
| Touch routing (hit-test + grab)  | done           |
| Lifecycle (VISIBLE/HIDDEN)       | done           |
| Pub/sub bus                      | done           |
| REQUEST_FOCUS + focus_stamp      | done           |
| CPU compositor (`--backend fb`)  | done           |
| **GPU compositor (`--backend egl`, Mali)** | **done** |
| LVGL 9.0 port (`ports/lvgl/`)    | done           |
| AWTK port (`awtk/awtk-linux-fb/awtk-port/egl_devices/mc/`) | done |
| `demo-fullscreen` + `demo-popup` | done           |
| AWTK `demo1` running on mc       | done           |
| 4-app concurrent test on T507    | done, smooth   |


## Performance (T507, Mali-G31, 1024×600 fb)

| Scenario | CPU compose (`--backend fb`) | **GPU compose (`--backend egl`)** |
|---|---|---|
| AWTK + 2 LVGL fullscreens + popup | ~120 ms / frame, ~8 Hz | **~5-10 ms / frame, ~30+ Hz** |
| seekbar drag latency | sluggish | tracks finger |

GPU compose works because each client surface is a dma-buf fd that can be
imported as `EGLImage` and bound as a GL texture; the compositor then
issues one textured quad per surface per frame with alpha blend for
popups. Mali draws the full screen in a few ms vs. CPU writing
1024×600×4 = 2.4 MB to uncached fb DRAM in ~50 ms per pass.


## Build

### Native (x86_64, smoke test only)

```sh
make            # core compositor + libmc + tools
```

Produces `build/{mc-compositor, mc-fill-client, mc-test-client, mc-bus-tool, libmc.a}`.

### Cross-compile for T507

```sh
source /develop/toolchain_t5sdk/environment-carbit.sh
make CROSS=1                              # core + GPU backend auto-enabled
make CROSS=1 demos                        # LVGL demo-fullscreen, demo-popup
# AWTK build is a separate flow under awtk/awtk-linux-fb/, see below.
```

Outputs in `build/`. All ELFs aarch64.

#### AWTK (separate scons build)

```sh
cd awtk/awtk-linux-fb
./build.sh T507    # selects t5_awtk_config_define.py (LCD_DEVICES=mc)
```

Produces `awtk/awtk-linux-fb/bin/{libawtk.so, demo1, ...}`. The AWTK lcd
backend lives in `awtk-port/egl_devices/mc/egl_devices.c` and renders
through GL FBO bound to a mc dma-buf.


## Run on T507

After pushing the binaries:

```sh
# mc binaries
adb push build/mc-compositor build/demo-fullscreen build/demo-popup /data/mc_demo/

# AWTK
adb push awtk/awtk-linux-fb/bin/libawtk.so /data/awtk_mc/bin/
adb push awtk/awtk-linux-fb/bin/demo1     /data/awtk_mc/bin/
adb push awtk/awtk-linux-fb/release/assets /data/awtk_mc/res/
```

Start them (use `setsid sh -c "... &" </dev/null >/dev/null 2>&1 &` to
detach from the shell session):

```sh
mc-compositor -v --backend egl --socket /tmp/mc.sock --input /dev/input/event1 &

# AWTK
MC_SOCKET=/tmp/mc.sock MC_APP_NAME=awtk-demo1 /data/awtk_mc/bin/demo1 &

# LVGL
demo-fullscreen --socket /tmp/mc.sock --name dashboard    --bg 0xFF205080 &
demo-fullscreen --socket /tmp/mc.sock --name diagnostics  --bg 0xFFB02040 &
demo-popup      --socket /tmp/mc.sock --x 300 --y 150 --w 400 --h 240 &
```

Popup OK cycles app/focus between `dashboard`, `diagnostics`, `awtk-demo1`.
Tap anywhere outside the popup card to interact with the visible
fullscreen app.


## Diagnostic env vars

These are off by default; set them to 1 to enable per-frame logging.

| env | what it shows |
|---|---|
| `MC_COMPOSE_TRACE`  | `LOG_I` line per CPU compose with painted px and µs |
| `MC_FB_VSYNC`       | enable `FBIO_WAITFORVSYNC` on CPU backend (off by default; T507 BSP stub) |
| `MC_COMPOSE_HZ`     | rate-limit compose (default 60) for CPU backend |
| `MC_ALLOC`          | force surface allocator: `dma-heap` / `ion` / `g2d` / `memfd` |
| `MC_SOCKET`         | client-side: socket path (default `/tmp/mc.sock`) |
| `MC_APP_NAME`       | AWTK side: identity used on `app/focus` bus topic |


## Layout

```
common/                 shared TLV + fd-passing
compositor/             mc-compositor (proto, surface, compose, backends, accel)
libmc/                  client SDK (libmc.a + mc.h)
ports/lvgl/             LVGL display driver wrapper (lv_port_mc.{c,h})
examples/               demo-fullscreen, demo-popup
tools/                  mc-fill-client, mc-bus-tool, egl_dmabuf_probe
launcher/               mc-launcher (auto-restart supervisor)
docs/                   architecture, Allwinner G2D reference, screenshots

awtk/                   AWTK source + awtk-linux-fb port
  awtk-linux-fb/
    awtk-port/
      egl_devices/mc/        # mc lcd backend (GL FBO over mc dma-buf)
      input_thread/input_thread_mc.{c,h}
      lcd_linux/lcd_linux_egl.c   # we hook begin_frame for dirty rect

lvgl/                   LVGL 9.0 sources (pre-built liblvgl.a + headers)
lvgl9/                  LVGL 9.0 build artefacts (liblvgl9.a)
lvgl-release-v9.0/      LVGL 9.0 source distribution
```


## Repo notes

- AWTK source tree under `awtk/` is large; if you only need to read the
  mc integration, look at `awtk/awtk-linux-fb/awtk-port/egl_devices/mc/`
  and `awtk/awtk-linux-fb/awtk-port/input_thread/input_thread_mc.{c,h}`.
- `awtk/awtk-linux-fb/bin/`, `awtk/awtk-linux-fb/build/`, and
  `awtk/awtk-linux-fb/lib/` are build artifacts and gitignored.
- `build/` and `lvgl9/build/` are gitignored.
- Mali userspace blobs (`awtk/awtk-linux-fb/lib_t5/libmali.so` etc.) are
  committed because they're not redistributable from anywhere else
  reachable to the toolchain.

# T113 AWTK mc-client (software) + AWTK/LVGL overlay — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Run AWTK on T113 as an mc compositor client (software canvas → CPU-mapped dma-buf surface) and prove it composites with an LVGL client via the G2D backend.

**Architecture:** Mirror the proven T507 `egl_devices/mc` scheme but replace EGLImage/FBO with `mc_surface_buf_at()` + AWTK `lcd_mem`. New device `lcd_devices/mc`; reuse `input_thread_mc`, `main_loop_linux`, lifecycle/dirty hooks. Compositor side already handles cache (cedar flush) and color (ARGB8888) — no client changes there.

**Tech Stack:** C, AWTK (awtk-linux-fb + NANOVG software canvas), libmc client API, scons, T113 musl cross toolchain, `mc-compositor --backend g2d`.

**Branch:** `t113-awtk` (already created). Spec: `docs/superpowers/specs/2026-06-02-t113-awtk-mc-client-design.md`.

**Testing note:** No unit-test harness exists for this layer and the host can't run ARM/G2D code. "Verify" steps are: (a) cross-compile/link success, (b) symbol/arch checks on artifacts, (c) on-device runs + screenshots performed by the user. On-device steps are explicitly marked **[ON-DEVICE]**.

---

## File structure

```
deps_source/T113/awtk/
  awtk                         -> symlink ../../T507/awtk/awtk     (NEW symlink; shared core)
  awtk-linux-fb/               (NEW copy, source only)
    awtk-port/
      lcd_devices/mc/lcd_mc.c  (NEW — the software mc-client LCD device)
      devices.c                (MODIFY — dispatch LCD_DEVICES=mc)
      SConscript               (MODIFY — compile lcd_mc.c under LCD_DEVICES=mc)
      input_thread/input_thread_mc.c (REUSE; minor de-EGL only if needed)
    t113_mc_awtk_config_define.py (NEW — LCD_DEVICES=mc, NANOVG, no EGL)
build.sh                       (MODIFY — build+package AWTK on T113, guarded)
docs/PORTING.md                (MODIFY — record AWTK T113 mc-client path)
```

Reference template (read-only): `deps_source/T507/awtk/awtk-linux-fb/awtk-port/egl_devices/mc/egl_devices.c`.

---

## Task 1: Source tree — symlink core, copy port layer

**Files:**
- Create symlink: `deps_source/T113/awtk/awtk`
- Create copy: `deps_source/T113/awtk/awtk-linux-fb/` (source only)

- [ ] **Step 1: Create the T113 awtk dir + core symlink**

```bash
cd /mnt/a57a7843-7ae2-415a-9125-4c61fa4163d9/claud_prj/shared_fb_fwk
mkdir -p deps_source/T113/awtk
ln -sfn ../../T507/awtk/awtk deps_source/T113/awtk/awtk
```

- [ ] **Step 2: Copy the port layer, source only (no build artifacts)**

```bash
rsync -a --exclude='*.o' --exclude='*.a' --exclude='*.so' --exclude='bin/' \
      --exclude='.sconsign.dblite' --exclude='.sconf_temp/' \
      deps_source/T507/awtk/awtk-linux-fb/ \
      deps_source/T113/awtk/awtk-linux-fb/
```

- [ ] **Step 3: Verify core path resolution**

Run:
```bash
readlink deps_source/T113/awtk/awtk
ls deps_source/T113/awtk/awtk-linux-fb/awtk-port/egl_devices/mc/egl_devices.c
test -e deps_source/T113/awtk/awtk-linux-fb/../awtk/src/awtk.h && echo "core reachable via ../awtk"
```
Expected: symlink prints `../../T507/awtk/awtk`; both files exist; prints "core reachable via ../awtk".

- [ ] **Step 4: Commit**

```bash
git add deps_source/T113/awtk
git commit -m "T113 awtk: symlink shared core + copy awtk-linux-fb port layer"
```

---

## Task 2: `lcd_devices/mc/lcd_mc.c` — software mc-client LCD

Adapt from the template `egl_devices/mc/egl_devices.c`. **Keep verbatim:** the
shared globals + accessors (`g_shared_mc_ctx`, `g_shared_mc_surf`,
`lcd_linux_mc_get_ctx/_surf`, `lcd_linux_mc_set_hidden/_is_hidden`), the
`lcd_egl_on_dirty_rect` dirty-rect capture, the mc connect/create/commit/
wait_buf_free flow. **Replace:** all EGL/GLES/EGLImage/FBO code with an AWTK
`lcd_mem` pointed at `mc_surface_buf_at()`. **Remove:** `#include <EGL/*>`,
`<GLES2/*>`, `<drm/drm_fourcc.h>`.

**Files:**
- Create: `deps_source/T113/awtk/awtk-linux-fb/awtk-port/lcd_devices/mc/lcd_mc.c`
- Read first: `.../egl_devices/mc/egl_devices.c` (full, 475 lines) and AWTK
  `awtk/src/lcd/lcd_mem_bgra8888.h` + `awtk/src/base/lcd.h` for the exact
  `lcd_mem_bgra8888_create_single_fb` / online-fb-repoint signatures.

- [ ] **Step 1: Read the template + AWTK lcd_mem API**

```bash
sed -n '1,475p' deps_source/T113/awtk/awtk-linux-fb/awtk-port/egl_devices/mc/egl_devices.c
grep -rn "lcd_mem_bgra8888_create\|lcd_mem_set_line_length\|->offline_fb\|->online_fb\|lcd_mem_create" deps_source/T113/awtk/awtk/src/lcd/ | head
```
Expected: confirms the mc flow to copy and the `lcd_mem_bgra8888_create*` signature + how to repoint the online fb between buffers.

- [ ] **Step 2: Write `lcd_mc.c` — create path**

Core of the create function (BGRA8888, n_buf=2, role from env/param):

```c
#include "../../awtk-port/lcd_linux/lcd_mem_others.h" /* or lcd_mem_bgra8888.h per Step 1 */
#include "base/lcd.h"
#include "tkc/mem.h"
#include "mc.h"

static mc_ctx_t     *g_shared_mc_ctx  = NULL;   /* verbatim from template */
static mc_surface_t *g_shared_mc_surf = NULL;
/* lcd_linux_mc_get_ctx/_surf/_set_hidden/_is_hidden: copy verbatim. */

typedef struct _lcd_mc_t {
  lcd_t        base_unused;     /* we wrap an lcd_mem; see below */
  mc_ctx_t     *mc;
  mc_surface_t *surf;
  int           w, h, stride, n_buf, cur_idx;
  lcd_t        *lcd;            /* the AWTK lcd_mem */
} lcd_mc_t;

lcd_t *lcd_mc_create(uint32_t w, uint32_t h) {
  lcd_mc_t *c = TKMEM_ZALLOC(lcd_mc_t);
  int role = getenv("MC_ROLE") ? atoi(getenv("MC_ROLE")) : MC_ROLE_FULLSCREEN;

  c->mc = mc_connect(mc_app_name());
  return_value_if_fail(c->mc != NULL, NULL);
  c->surf = mc_surface_create_shm_ex(c->mc, w, h, MC_FMT_BGRA8888, role, 2);
  return_value_if_fail(c->surf != NULL, NULL);
  c->w = w; c->h = h; c->cur_idx = 0;
  c->n_buf  = mc_surface_n_buf(c->surf);

  uint8_t *fb0 = mc_surface_buf_at(c->surf, 0, &c->stride);
  /* online fb = mc buffer 0; offline fb = AWTK-managed scratch (NULL lets
     lcd_mem alloc one). Use the single-fb-online variant and repoint per swap. */
  c->lcd = lcd_mem_bgra8888_create_single_fb(w, h, fb0); /* confirm exact name in Step 1 */
  c->lcd->impl_data = c;

  g_shared_mc_ctx = c->mc; g_shared_mc_surf = c->surf;
  printf("[mc-lcd] %dx%d stride=%d n_buf=%d role=%d\n", w, h, c->stride, c->n_buf, role);
  return c->lcd;
}
```

- [ ] **Step 3: Write `lcd_mc.c` — flush/swap path**

Hook AWTK's lcd `flush`/`swap` (the lcd_mem online path calls a swap when
double-buffered). Implement the mc commit + rotate, repointing the online fb:

```c
static ret_t lcd_mc_flush(lcd_t *lcd) {
  lcd_mc_t *c = (lcd_mc_t *)lcd->impl_data;
  if (lcd_linux_mc_is_hidden()) return RET_OK;     /* throttle when hidden */

  mc_rect_t damage = lcd_egl_take_dirty_rect();    /* same helper as template */
  mc_surface_commit_idx(c->surf, c->cur_idx, &damage, 1);

  int next = (c->cur_idx + 1) % c->n_buf;
  mc_surface_wait_buf_free(c->surf, next);
  int stride = 0;
  uint8_t *nb = mc_surface_buf_at(c->surf, next, &stride);
  lcd_mem_set_online_fb(c->lcd, nb);               /* confirm repoint API in Step 1 */
  c->cur_idx = next;
  return RET_OK;
}
```
Wire `lcd_mc_flush` as the lcd's `swap`/`flush` callback (per the lcd_mem
create variant chosen in Step 1). Add a `dispose` that `mc_surface_destroy` +
`mc_disconnect` + clears the globals.

- [ ] **Step 4: Cross-compile just this TU as a sanity check**

Run (object-only, include paths from awtk_config):
```bash
/develop/toolchain_t113_musl/bin/arm-openwrt-linux-muslgnueabi-gcc -fsyntax-only \
  -I deps_source/T113/awtk/awtk/src -I libmc/include \
  -I deps_source/T113/awtk/awtk-linux-fb/awtk-port \
  deps_source/T113/awtk/awtk-linux-fb/awtk-port/lcd_devices/mc/lcd_mc.c
```
Expected: no errors (fix include paths/signatures against Step 1 findings until clean). This is a syntax gate; full build is Task 5.

- [ ] **Step 5: Commit**

```bash
git add deps_source/T113/awtk/awtk-linux-fb/awtk-port/lcd_devices/mc/lcd_mc.c
git commit -m "T113 awtk: software mc-client LCD device (lcd_devices/mc)"
```

---

## Task 3: Wire device selection + T113 config

**Files:**
- Modify: `.../awtk-port/SConscript`
- Modify: `.../awtk-port/devices.c`
- Create: `.../t113_mc_awtk_config_define.py`

- [ ] **Step 1: Inspect how LCD_DEVICES selects a device**

```bash
sed -n '1,120p' deps_source/T113/awtk/awtk-linux-fb/awtk-port/SConscript
grep -n "LCD_DEVICES\|lcd_linux_create\|egl_devices\|lcd_mem\|tk_lcd_linux" deps_source/T113/awtk/awtk-linux-fb/awtk-port/devices.c
```
Expected: shows the `LCD_DEVICES` branch (string compare or scons var) and the
function `devices.c` calls to create the platform lcd. Note the exact symbol.

- [ ] **Step 2: SConscript — compile lcd_mc.c when LCD_DEVICES=mc**

Add a branch mirroring the existing `fb`/`egl` ones (exact syntax per Step 1):
```python
if LCD_DEVICES == 'mc':
    SOURCES += ['lcd_devices/mc/lcd_mc.c']
```

- [ ] **Step 3: devices.c — dispatch LCD_DEVICES=mc to lcd_mc_create**

In the platform-lcd creation path add (guarded by the same mechanism the
file already uses, e.g. `#ifdef LCD_DEVICES_MC` or the runtime string):
```c
/* extern lcd_t* lcd_mc_create(uint32_t w, uint32_t h); */
#if defined(WITH_LCD_MC)
  return lcd_mc_create(w, h);
#endif
```
Match the file's existing convention exactly (Step 1).

- [ ] **Step 4: Create the T113 mc config**

```bash
cp deps_source/T113/awtk/awtk-linux-fb/t113_awtk_config_define.py \
   deps_source/T113/awtk/awtk-linux-fb/t113_mc_awtk_config_define.py
```
Then edit `t113_mc_awtk_config_define.py`: set `LCD_DEVICES = "mc"`, keep
`VGCANVAS = "NANOVG"`, keep EGL off, `WITH_G2D = False`. Add any define the
SConscript/devices.c branch needs (e.g. `-DWITH_LCD_MC`).

- [ ] **Step 5: Commit**

```bash
git add deps_source/T113/awtk/awtk-linux-fb/awtk-port/SConscript \
        deps_source/T113/awtk/awtk-linux-fb/awtk-port/devices.c \
        deps_source/T113/awtk/awtk-linux-fb/t113_mc_awtk_config_define.py
git commit -m "T113 awtk: select software mc LCD via LCD_DEVICES=mc + config"
```

---

## Task 4: build.sh — build & package AWTK on T113 (guarded)

**Files:**
- Modify: `build.sh`

- [ ] **Step 1: Inspect the existing AWTK build invocation**

```bash
grep -n "AWTK\|awtk\|SKIP_AWTK\|build.sh\|scons" build.sh
```
Expected: shows the T507 AWTK section (the `(cd "$AWTK_LINUX_FB" && ./build.sh "$PLATFORM")` block) and the `SKIP_AWTK` default for T113.

- [ ] **Step 2: Make T113 build AWTK with the mc config; guard failure**

In `build.sh`, for T113: stop forcing `SKIP_AWTK=1`; instead default it off
but make the AWTK build non-fatal (mirror the `SKIP_LVGL` guard). Point the
awtk build at the T113 mc config and musl toolchain. Concretely, change the
T113 branch so:
```bash
T113)
    ...
    SKIP_AWTK="${SKIP_AWTK:-0}"   # was 1
    AWTK_CONFIG="t113_mc"          # selects t113_mc_awtk_config_define.py
    ;;
```
and in the AWTK section, wrap the scons build so a failure warns + skips
packaging AWTK rather than aborting (`set +e` around it, check `$?`).

- [ ] **Step 3: Package libawtk.so + the AWTK demo**

Copy `libawtk.so` and the built demo into staging (mirror T507's cp lines),
under `lib/` and `bin/awtk-demo`. Skip the tslib copy requirement for T113
(input is via mc).

- [ ] **Step 4: Commit**

```bash
git add build.sh
git commit -m "build.sh: build+package AWTK as mc client on T113 (guarded)"
```

---

## Task 5: Cross-build the full T113 package + standalone AWTK bring-up

**Files:** none new (build/fix loop).

- [ ] **Step 1: Run the T113 build**

```bash
export STAGING_DIR=/tmp/stub_staging
./build.sh T113 2>&1 | tee /tmp/t113-awtk-build.log | tail -40
```
Expected: scons builds `libawtk.so`; staging gets `bin/awtk-demo` + `lib/libawtk.so`.
Fix compile/link errors against Task 2/3 (most likely: lcd_mem API name, missing
include dirs, the `WITH_LCD_MC` define not threaded through). Iterate until clean.

- [ ] **Step 2: Verify artifacts are ARM/musl**

```bash
file output/T113/staging/lib/libawtk.so output/T113/staging/bin/awtk-demo
```
Expected: both `ELF 32-bit ... ARM ... interpreter /lib/ld-musl-armhf.so.1`.

- [ ] **Step 3: [ON-DEVICE] AWTK alone as an mc client**

Hand to user. Deploy the package, then:
```sh
killall mc-compositor 2>/dev/null; rm -f /tmp/mc.sock
./bin/mc-compositor -s /tmp/mc.sock -b g2d -v &
MC_SOCKET=/tmp/mc.sock MC_APP_NAME=awtk LD_LIBRARY_PATH=./lib ./bin/awtk-demo &
```
Expected: AWTK UI renders correctly (colors right, no white lines), touch works.
Capture a screenshot. If colors/lines wrong, it's a client-format/role bug — fix
in Task 2 (format must be `MC_FMT_BGRA8888`).

- [ ] **Step 4: Commit any fixes**

```bash
git add -A && git commit -m "T113 awtk: fixes from standalone mc-client bring-up"
```

---

## Task 6: Mixed AWTK/LVGL overlay test + validation

**Files:**
- Modify: `build.sh` (the generated `start.sh` heredoc)

- [ ] **Step 1: Add the overlay launch to start.sh**

In the `start.sh` heredoc emitted by `build.sh`, after the compositor start,
launch LVGL fullscreen (bottom) + AWTK as popup (top):
```sh
# LVGL fullscreen (role defaults to fullscreen in the demo)
setsid sh -c "$HERE/bin/demo-fullscreen --socket /tmp/mc.sock --name dashboard \
    > /tmp/mc-lvgl.log 2>&1" </dev/null >/dev/null 2>&1 &
sleep 2
# AWTK as a popup overlay (MC_ROLE_POPUP = 2)
if [ -x "$HERE/bin/awtk-demo" ]; then
  setsid sh -c "cd $HERE && LD_LIBRARY_PATH=$HERE/lib MC_SOCKET=/tmp/mc.sock \
      MC_APP_NAME=awtk-popup MC_ROLE=2 $HERE/bin/awtk-demo \
      > /tmp/mc-awtk.log 2>&1" </dev/null >/dev/null 2>&1 &
fi
```

- [ ] **Step 2: Rebuild the package**

```bash
export STAGING_DIR=/tmp/stub_staging
./build.sh T113 2>&1 | tail -8
```
Expected: package built; `start.sh` contains the AWTK+LVGL launches.

- [ ] **Step 3: [ON-DEVICE] Run the overlay test**

Hand to user. Deploy + `start.sh`. Expected: LVGL dashboard fullscreen with the
AWTK demo composited on top as a popup; AWTK's transparent/rounded areas show the
LVGL content through (BLD_H SRCOVER); correct colors; no white lines. Capture a
screenshot.

- [ ] **Step 4: Commit + update PORTING.md**

Update `docs/PORTING.md` AWTK row: T113 AWTK runs as an mc client via the
software `lcd_devices/mc` device (not LCD_DEVICES=fb); input via mc (no tslib);
overlay with LVGL verified.
```bash
git add build.sh docs/PORTING.md
git commit -m "T113 awtk: mixed AWTK/LVGL overlay in start.sh + PORTING update"
```

- [ ] **Step 5: Finish the branch**

After on-device validation passes, use the finishing-a-development-branch skill
to merge `t113-awtk` to `main` and tag (e.g. `t113-awtk-working`).

---

## Self-review notes
- **Spec coverage:** lcd_devices/mc (Task 2), reuse input/lifecycle (Task 2 globals + existing input_thread_mc), source layout symlink+copy (Task 1), config/selection (Task 3), build.sh + packaging + tslib-not-needed (Task 4), overlay test (Task 6), cache/format reuse (no task — handled compositor-side already, noted), T507 isolation (Task 1 copy). All covered.
- **Investigation steps** (read template / API / SConscript) are included where exact AWTK signatures aren't yet known; they precede the code that depends on them. The `lcd_mem_bgra8888_create_single_fb` / `lcd_mem_set_online_fb` names MUST be confirmed in Task 2 Step 1 before use.
- **No unit tests** by design; verification is cross-build + on-device, marked **[ON-DEVICE]**.

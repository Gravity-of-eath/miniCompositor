#!/bin/bash
#
# build.sh — cross-compile the mc framework + clients for a target platform
# and package the binaries into output/<PLATFORM>/.
#
# Usage:  ./build.sh [PLATFORM]
#
# Supported PLATFORM:
#   T507   Allwinner T507 (aarch64, Mali-G31). Default.
#   T113   Allwinner T113 (armv7-musl, no GPU, G2D 2D engine via backend_g2d).
#          LVGL/AWTK not ported yet -> packages compositor + tools only.
#
# Outputs:
#   output/<PLATFORM>/staging/        full unpacked tree, ready to scp/adb push
#     bin/                            mc-compositor, demos, tools
#     lib/                            libts (tslib runtime) + plugins
#     res/                            AWTK demo assets
#     scripts/start.sh, stop.sh       deploy/run helpers
#   output/<PLATFORM>/mc-<PLATFORM>-<DATE>.tar.gz   archive of the above
#
# Honoured env vars:
#   T5SDK_ENV    path to the T507 toolchain env script
#                (default: /develop/toolchain_t5sdk/environment-carbit.sh)
#   T113_TOOLCHAIN  T113 toolchain bin prefix (default:
#                /develop/toolchain_t113_musl/bin/arm-openwrt-linux-muslgnueabi-)
#   JOBS         make -j level (default: nproc)
#   SKIP_AWTK=1  skip the AWTK scons build (much slower than mc itself).
#                For T113 this defaults to 1 until an AWTK fb_devices/mc
#                port exists under deps_source/T113/awtk/.
#   SKIP_LVGL=1  skip the LVGL demos. Auto-on when the platform has no
#                prebuilt deps_libs/<PLATFORM>/lvgl/liblvgl9.a (e.g. T113,
#                whose LVGL port isn't in the tree yet). Override =0 once
#                that lib is built.

set -e
set -o pipefail

PLATFORM="${1:-T507}"
JOBS="${JOBS:-$(nproc)}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

case "$PLATFORM" in
T507)
    T5SDK_ENV="${T5SDK_ENV:-/develop/toolchain_t5sdk/environment-carbit.sh}"
    ;;
T113)
    # T113 is a buildroot-style toolchain, not a yocto SDK env script.
    # We just need CROSS_COMPILE to point at the gcc/ar binaries; the
    # Makefile picks it up via CROSS=1.
    T113_TOOLCHAIN="${T113_TOOLCHAIN:-/develop/toolchain_t113_musl/bin/arm-openwrt-linux-muslgnueabi-}"
    # AWTK on T113 needs a separate fb_devices/mc port (no GPU); skip
    # by default until that lands.
    SKIP_AWTK="${SKIP_AWTK:-1}"
    ;;
*)
    echo "ERROR: unsupported platform '$PLATFORM'."
    echo "       Supported: T507, T113"
    exit 1
    ;;
esac

# Per-platform toolchain sanity check.
case "$PLATFORM" in
T507)
    if [[ ! -f "$T5SDK_ENV" ]]; then
        echo "ERROR: toolchain env script not found: $T5SDK_ENV"
        echo "       Override with: T5SDK_ENV=/path/to/env-script $0 $PLATFORM"
        exit 1
    fi
    ;;
T113)
    if [[ ! -x "${T113_TOOLCHAIN}gcc" ]]; then
        echo "ERROR: T113 cross-gcc not found: ${T113_TOOLCHAIN}gcc"
        echo "       Override with: T113_TOOLCHAIN=/path/to/prefix- $0 $PLATFORM"
        exit 1
    fi
    ;;
esac

# LVGL demos need liblvgl9.a for the platform. T507 ships a prebuilt one;
# T113 builds it on demand (below) from the shared v9.0 source + its own
# lv_conf.h. Only skip the demos when we have neither the lib nor the means
# to build it -- otherwise `make demos` would abort the package under `set -e`.
# Honour an explicit SKIP_LVGL if given.
if [[ -z "${SKIP_LVGL:-}" ]]; then
    if [[ -f "deps_libs/$PLATFORM/lvgl/liblvgl9.a" ]] || \
       { [[ -d "deps_source/$PLATFORM/lvgl-release-v9.0/src" ]] && \
         [[ -f "deps_libs/$PLATFORM/lvgl/lv_conf.h" ]]; }; then
        SKIP_LVGL=0
    else
        SKIP_LVGL=1
    fi
fi

# ---------- helpers ----------------------------------------------------

log()  { printf '\033[1;34m[build]\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m[build]\033[0m %s\n' "$*" >&2; }
die()  { printf '\033[1;31m[build]\033[0m %s\n' "$*" >&2; exit 1; }

run() {
    printf '\033[2m  + %s\033[0m\n' "$*"
    "$@"
}

# ---------- prepare output tree ----------------------------------------

OUT_ROOT="output/$PLATFORM"
STAGE="$OUT_ROOT/staging"

log "platform: $PLATFORM  (output: $OUT_ROOT)"
rm -rf "$STAGE"
mkdir -p "$STAGE"/{bin,lib,res,scripts}

# ---------- 1. mc compositor + LVGL demos + tools ----------------------

case "$PLATFORM" in
T507)
    log "sourcing $PLATFORM toolchain env: $T5SDK_ENV"
    # shellcheck disable=SC1090
    source "$T5SDK_ENV"
    MAKE_PLATFORM_FLAGS=( CROSS=1 MC_ENABLE_EGL=1 LVGL_PLATFORM=T507 )
    ;;
T113)
    log "using T113 cross prefix: ${T113_TOOLCHAIN}"
    export CC="${T113_TOOLCHAIN}gcc"
    export AR="${T113_TOOLCHAIN}ar"
    # T113: no Mali (EGL=0). NOTE: MC_ENABLE_G2D builds accel_g2d.c, which
    # is the 2.0 "_H" ABI accelerator (T507). T113 (sun8iw20) only has the
    # G2D 1.0 ABI, so issuing a 2.0 ioctl there HANGS the compositor
    # (client then dies with "wait_buf_free stalled"). Keep it OFF and use
    # the 1.0 path in backend_g2d.c (MC_ENABLE_BACKEND_G2D) via `-b g2d`;
    # the default fb backend falls back to CPU accel.
    MAKE_PLATFORM_FLAGS=( CROSS=1
                          MC_ENABLE_EGL=0
                          MC_ENABLE_G2D=0
                          MC_ENABLE_BACKEND_G2D=1
                          LVGL_PLATFORM=T113 )
    ;;
esac

log "building mc compositor + tools (Makefile)"
run make "${MAKE_PLATFORM_FLAGS[@]}" -j"$JOBS"

if [[ "$SKIP_LVGL" == "1" ]]; then
    warn "SKIP_LVGL=1 -- no LVGL lib or source for $PLATFORM, skipping LVGL demos"
else
    LVGL_LIB="deps_libs/$PLATFORM/lvgl/liblvgl9.a"
    if [[ ! -f "$LVGL_LIB" ]]; then
        # T507 commits a prebuilt lib; T113 builds it here from the shared
        # v9.0 source. Pass CC/AR on the command line (command-line origin
        # beats the Makefile's default) so the cross compiler is really used
        # -- a plain `?=` would silently fall back to the host gcc.
        log "building LVGL static lib for $PLATFORM (one-time, ~280 files)"
        run make -C "deps_libs/$PLATFORM/lvgl" CC="$CC" AR="${AR:-${CROSS_COMPILE:-}ar}" -j"$JOBS"
    fi
    log "building LVGL demos"
    run make "${MAKE_PLATFORM_FLAGS[@]}" demos -j"$JOBS"
fi

cp -v build/mc-compositor   "$STAGE/bin/"
cp -v build/mc-fill-client  "$STAGE/bin/"
cp -v build/mc-bus-tool     "$STAGE/bin/"
cp -v build/mc-launcher     "$STAGE/bin/"
if [[ "$SKIP_LVGL" != "1" ]]; then
    cp -v build/demo-fullscreen "$STAGE/bin/"
    cp -v build/demo-popup      "$STAGE/bin/"
fi

# ---------- 2. AWTK (optional) -----------------------------------------

AWTK_LINUX_FB="deps_source/$PLATFORM/awtk/awtk-linux-fb"

if [[ "${SKIP_AWTK:-0}" == "1" ]]; then
    log "SKIP_AWTK=1 set, skipping AWTK build"
else
    log "building AWTK + demo1 (scons)"
    (
        cd "$AWTK_LINUX_FB"
        # build.sh T507 picks the t5 config + runs scons cleanly
        ./build.sh "$PLATFORM"
    )
    cp -v "$AWTK_LINUX_FB/bin/libawtk.so" "$STAGE/lib/"
    cp -v "$AWTK_LINUX_FB/bin/demo1"      "$STAGE/bin/awtk-demo1"
fi

# ---------- 3. tslib runtime (needed by AWTK demo1) --------------------

log "copying tslib runtime (libts + plugins)"
TSLIB_SRC="deps_libs/$PLATFORM/tslib/lib"
if [[ -f "$TSLIB_SRC/libts-1.3.so.0.1.3" ]]; then
    cp -v "$TSLIB_SRC"/libts-1.3.so.0.1.3 "$STAGE/lib/"
    (cd "$STAGE/lib" && ln -sf libts-1.3.so.0.1.3 libts-1.3.so.0)
    mkdir -p "$STAGE/lib/ts"
    cp -v "$TSLIB_SRC"/ts/*.so "$STAGE/lib/ts/" 2>/dev/null || true
else
    warn "tslib for $PLATFORM not found at $TSLIB_SRC -- AWTK demo will fail to load"
fi

# ---------- 4. AWTK demo resources -------------------------------------

if [[ -d "$AWTK_LINUX_FB/release/assets" ]]; then
    log "copying AWTK demo assets"
    cp -r "$AWTK_LINUX_FB/release/assets" "$STAGE/res/"
else
    warn "AWTK release/assets not found -- AWTK demo will run without UI assets"
fi

# ---------- 5. runtime scripts -----------------------------------------

log "writing deploy/start/stop scripts"

# deploy.sh: push staging tree to the device (assumes adb).
cat > "$STAGE/scripts/deploy.sh" <<'EOF'
#!/bin/sh
# Push mc + AWTK to /data/mc_stack/ via adb. Run from anywhere; the
# script resolves paths relative to itself.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
STAGE="$(cd "$HERE/.." && pwd)"
REMOTE=/data/mc_stack
adb shell "rm -rf $REMOTE && mkdir -p $REMOTE"
adb push "$STAGE/bin" "$REMOTE/bin"
adb push "$STAGE/lib" "$REMOTE/lib"
adb push "$STAGE/res" "$REMOTE/res"
adb push "$STAGE/scripts/start.sh" "$REMOTE/start.sh"
adb push "$STAGE/scripts/stop.sh"  "$REMOTE/stop.sh"
adb shell "chmod +x $REMOTE/*.sh $REMOTE/bin/*"
echo
echo "deployed to $REMOTE/"
echo "run:   adb shell $REMOTE/start.sh"
echo "stop:  adb shell $REMOTE/stop.sh"
EOF
chmod +x "$STAGE/scripts/deploy.sh"

# start.sh: bring up compositor + demos on device.
# Backend per-platform: T507=egl (Mali GPU), T113=g2d (Allwinner G2D blitter).
case "$PLATFORM" in
T507) START_BACKEND=egl ;;
T113) START_BACKEND=g2d ;;
*)    START_BACKEND=fb  ;;
esac
cat > "$STAGE/scripts/start.sh" <<EOF
#!/bin/sh
# Start mc-compositor + LVGL demos + AWTK demo on the device.
# Assumes /data/mc_stack/ layout from deploy.sh.
HERE=/data/mc_stack
LIB="\$HERE/lib"

# kill any previous instances
for p in \$(ps | grep -E "mc-compositor|demo-fullscreen|demo-popup|awtk-demo1" | grep -v grep | awk '{print \$1}'); do
    kill -9 \$p 2>/dev/null
done
sleep 1
rm -f /tmp/mc.sock /tmp/mc-*.log

INPUT="\${MC_INPUT:-/dev/input/event1}"  # touchscreen

# 1. compositor ($PLATFORM compose backend: $START_BACKEND)
setsid sh -c "\$HERE/bin/mc-compositor -v \\
    --backend $START_BACKEND \\
    --socket /tmp/mc.sock \\
    --input \$INPUT \\
    > /tmp/mc-compositor.log 2>&1" </dev/null >/dev/null 2>&1 &
sleep 1

# 2. AWTK demo1
if [ -x "\$HERE/bin/awtk-demo1" ]; then
    setsid sh -c "cd \$HERE && \\
        LD_LIBRARY_PATH=\$LIB:/usr/lib \\
        TSLIB_PLUGINDIR=\$LIB/ts \\
        TSLIB_CONFFILE=\$HERE/lib/ts/ts.conf \\
        MC_SOCKET=/tmp/mc.sock \\
        MC_APP_NAME=awtk-demo1 \\
        \$HERE/bin/awtk-demo1 \\
        > /tmp/mc-awtk.log 2>&1" </dev/null >/dev/null 2>&1 &
    sleep 3
fi

# 3. LVGL fullscreen demos (only if packaged -- not present on T113 yet)
if [ -x "\$HERE/bin/demo-fullscreen" ]; then
    setsid sh -c "\$HERE/bin/demo-fullscreen --socket /tmp/mc.sock \\
        --name dashboard --bg 0xFF205080 \\
        > /tmp/mc-dashboard.log 2>&1" </dev/null >/dev/null 2>&1 &
    sleep 2
    setsid sh -c "\$HERE/bin/demo-fullscreen --socket /tmp/mc.sock \\
        --name diagnostics --bg 0xFFB02040 \\
        > /tmp/mc-diagnostics.log 2>&1" </dev/null >/dev/null 2>&1 &
    sleep 2
fi

# 4. LVGL popup overlay
if [ -x "\$HERE/bin/demo-popup" ]; then
    setsid sh -c "\$HERE/bin/demo-popup --socket /tmp/mc.sock \\
        --x 300 --y 150 --w 400 --h 240 \\
        > /tmp/mc-popup.log 2>&1" </dev/null >/dev/null 2>&1 &
    sleep 2
fi

echo "started. logs in /tmp/mc-*.log"
ps | grep -E "mc-comp|demo-|awtk-demo1" | grep -v grep | grep -v "sh -c"
EOF
chmod +x "$STAGE/scripts/start.sh"

cat > "$STAGE/scripts/stop.sh" <<'EOF'
#!/bin/sh
# Stop the entire mc stack.
for p in $(ps | grep -E "mc-compositor|demo-fullscreen|demo-popup|awtk-demo1" | grep -v grep | awk '{print $1}'); do
    kill -9 $p 2>/dev/null
done
sleep 1
rm -f /tmp/mc.sock
echo "stopped"
EOF
chmod +x "$STAGE/scripts/stop.sh"

# A README in the staging tree so what's in the tarball is obvious.
cat > "$STAGE/README.txt" <<EOF
mc framework build for $PLATFORM
Built: $(date -u +%Y-%m-%dT%H:%M:%SZ)

Layout:
  bin/           mc-compositor + demos + helper tools
  lib/           tslib runtime + plugins (libts-1.3.so.0)
  lib/libawtk.so AWTK shared library (used by awtk-demo1)
  res/assets/    AWTK demo UI assets
  scripts/       deploy.sh, start.sh, stop.sh

Deploy (uses adb):
  ./scripts/deploy.sh

Run on device:
  adb shell /data/mc_stack/start.sh
  adb shell /data/mc_stack/stop.sh

Per-program docs:
  bin/mc-compositor --help
  bin/demo-fullscreen --help
EOF

# ---------- 6. tarball -------------------------------------------------

STAMP=$(date -u +%Y%m%d-%H%M%S)
ARCHIVE="$OUT_ROOT/mc-$PLATFORM-$STAMP.tar.gz"
log "packaging $ARCHIVE"
tar -czf "$ARCHIVE" -C "$OUT_ROOT" --transform "s,^staging,mc-$PLATFORM," staging

# Update a "latest" symlink for convenience.
ln -sfn "mc-$PLATFORM-$STAMP.tar.gz" "$OUT_ROOT/latest.tar.gz"

# ---------- 7. summary -------------------------------------------------

log "done."
echo
echo "  staging tree : $STAGE/"
echo "  archive      : $ARCHIVE"
echo "  size         : $(du -h "$ARCHIVE" | cut -f1)"
echo
echo "Next:"
echo "  $STAGE/scripts/deploy.sh        # push to device via adb"
echo "  adb shell /data/mc_stack/start.sh"

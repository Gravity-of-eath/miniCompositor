#!/bin/sh
# Build for T507, strip, and (optionally) scp to a board.
#
# Usage:
#   tools/deploy-t507.sh                # just build + strip into build/deploy/
#   tools/deploy-t507.sh <user>@<host>  # also scp to /tmp on the board
#
# Requires:
#   - environment-carbit.sh has been sourced (or we source it ourselves below)

set -e

SDK_ENV=/develop/toolchain_t5sdk/environment-carbit.sh
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/deploy"
TARGET="${1:-}"

if ! command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
    if [ -r "$SDK_ENV" ]; then
        # shellcheck disable=SC1090
        . "$SDK_ENV"
    else
        echo "ERROR: aarch64-linux-gnu-gcc not found and $SDK_ENV missing" >&2
        exit 1
    fi
fi

echo "== build =="
make -C "$ROOT" CROSS=1 all demos

echo "== strip =="
mkdir -p "$OUT"
for f in mc-compositor mc-launcher demo-fullscreen demo-popup mc-fill-client mc-test-client; do
    aarch64-linux-gnu-strip --strip-unneeded \
        -o "$OUT/$f" "$ROOT/build/$f"
done

echo "== deploy bundle =="
ls -la "$OUT"
du -sh "$OUT"

if [ -n "$TARGET" ]; then
    echo "== scp -> $TARGET:/tmp/ =="
    scp "$OUT"/mc-compositor \
        "$OUT"/mc-launcher \
        "$OUT"/demo-fullscreen \
        "$OUT"/demo-popup \
        "$OUT"/mc-fill-client \
        "$OUT"/mc-test-client \
        "$ROOT"/launcher/examples/launcher.conf \
        "$TARGET":/tmp/
    echo
    echo "== next: ssh $TARGET and run =="
    echo "  fbset -i | grep -E 'mode|geometry'      # confirm fb resolution"
    echo "  /tmp/mc-compositor -s /tmp/mc.sock -b fb -o /dev/fb0 \\"
    echo "                     -w <W> -h <H> -v &"
    echo "  /tmp/demo-fullscreen -s /tmp/mc.sock &"
    echo "  sleep 1"
    echo "  /tmp/demo-popup      -s /tmp/mc.sock --x 200 --y 120 --w 400 --h 240 &"
fi

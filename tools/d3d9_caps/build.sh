#!/bin/sh
# Build and run probe.c on DXVK Native + KosmicKrisp (notes/dxvk_triangle.md).
# Both come from HPRECOMP_DEPS_DIR (default build.nosync/deps), as in tests/run_all.sh.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
DEPS=${HPRECOMP_DEPS_DIR:-$ROOT/build.nosync/deps}
DXVK=$DEPS/dxvk
OUT=${OUT:-${TMPDIR:-/tmp}/hp-d3d9_caps}
mkdir -p "$OUT"
clang -std=c11 -Wall -I$DXVK/include/dxvk -I/opt/homebrew/include/SDL2 "$(dirname "$0")/probe.c" -o "$OUT/probe" \
    -L$DXVK/lib -ldxvk_d3d9 -Wl,-rpath,$DXVK/lib -L/opt/homebrew/lib -lSDL2
[ "$1" = run ] || exit 0
VK_DRIVER_FILES=$DEPS/kosmickrisp/share/vulkan/icd.d/kosmickrisp_mesa_icd.aarch64.json DXVK_WSI_DRIVER=SDL2 \
DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib DXVK_LOG_LEVEL=${DXVK_LOG_LEVEL:-warn} \
    perl -e 'alarm 120; exec @ARGV' "$OUT/probe"

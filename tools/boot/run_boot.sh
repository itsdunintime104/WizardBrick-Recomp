#!/bin/bash
# One boot run with the standard run settings: 120 s limit (BOOT_SECS overrides), KosmicKrisp, DXVK on
# SDL2, no alert UI. Log: $TMPDIR/hp-boot/run<N>.log; the game's PID in run<N>.pid
# (env and perl exec, so it is the lhp process). Usage: run_boot.sh N [extra env...]
# The game is BIN (default build.nosync/lhp) with orig.nosync/lhp_win32.exe and
# $HPRECOMP_GAME_DIR (required: the LEGO Harry Potter folder); KosmicKrisp comes from
# HPRECOMP_DEPS_DIR (default build.nosync/deps), as in engine/windows/tests/run_all.sh.
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BIN=${BIN:-$ROOT/build.nosync/lhp}
DEPS=${HPRECOMP_DEPS_DIR:-$ROOT/build.nosync/deps}
[ -n "${HPRECOMP_GAME_DIR:-}" ] || { echo "run_boot.sh: set HPRECOMP_GAME_DIR to the LEGO Harry Potter folder" >&2; exit 2; }
N=$1; shift
LOG=${TMPDIR:-/tmp}/hp-boot/run$N.log
mkdir -p "${LOG%/*}"
env RECOMP_ASSUME_FOCUS=1 RECOMP_NO_ALERT_UI=1 \
    VK_DRIVER_FILES="$DEPS/kosmickrisp/share/vulkan/icd.d/kosmickrisp_mesa_icd.aarch64.json" \
    DXVK_WSI_DRIVER=SDL2 DXVK_LOG_LEVEL=info SDL_AUDIO_DRIVER=dummy DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib \
    "$@" perl -e 'alarm($ENV{BOOT_SECS} || 120); exec {$ARGV[0]} @ARGV' \
    "$BIN" "$ROOT/orig.nosync/lhp_win32.exe" "$HPRECOMP_GAME_DIR" > "$LOG" 2>&1 &
echo $! > "${LOG%.log}.pid"
wait $!
echo "exit=$? log=$LOG lines=$(wc -l < "$LOG")"

#!/bin/bash
# soak.sh RUN STOP INPUT DUMPLIST: shoot.sh run of STOP s with HP_TRACE_D3D9, the
# frame dump at DUMPLIST (n1,n2,...) into $LOGS/frames<RUN>, the shader dump into
# $LOGS/shaders<RUN>, scripted INPUT, and RSS samples every 60 s in $LOGS/rss<RUN>.txt.
# Needs HPRECOMP_GAME_DIR (run_boot.sh).
T=$(cd "$(dirname "$0")" && pwd); R=$1; STOP=$2; IN=$3; LIST=$4
[ -n "${HPRECOMP_GAME_DIR:-}" ] || { echo "soak.sh: set HPRECOMP_GAME_DIR to the LEGO Harry Potter folder" >&2; exit 2; }
LOGS=${TMPDIR:-/tmp}/hp-boot
mkdir -p "$LOGS/frames$R" "$LOGS/shaders$R"
STOP=$STOP SHOTS="$((STOP - 10))" "$T/shoot.sh" "$R" HP_TRACE_D3D9=1 HPRECOMP_DUMP_FRAMES="$LIST" \
    HPRECOMP_DUMP_DIR="$LOGS/frames$R" HPRECOMP_SHADER_DUMP="$LOGS/shaders$R" HPRECOMP_INPUT_SCRIPT="$IN" &
: > "$LOGS/rss$R.txt"
start=$(date +%s)
while [ $(( $(date +%s) - start )) -lt $((STOP + 5)) ]; do
    sleep 60
    p=$(cat "$LOGS/run$R.pid" 2>/dev/null)
    [ -n "$p" ] && kill -0 "$p" 2>/dev/null && echo "$(( $(date +%s) - start )) $(ps -o rss= -p "$p")" >> "$LOGS/rss$R.txt"
done
wait
echo done > "$LOGS/soak$R.done"

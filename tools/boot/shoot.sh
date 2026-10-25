#!/bin/bash
# shoot.sh RUN [ENV...]: boot run RUN (run_boot.sh) with window screenshots at
# SHOTS seconds (default 20 45 80) into $LOGS/shots, stopped at STOP (default 90) s.
# Keeps the display awake (caffeinate -u -d); a sleeping display captures black.
# Needs HPRECOMP_GAME_DIR (run_boot.sh); BIN and HPRECOMP_DEPS_DIR pass through to it.
T=$(cd "$(dirname "$0")" && pwd); R=$1; shift
[ -n "${HPRECOMP_GAME_DIR:-}" ] || { echo "shoot.sh: set HPRECOMP_GAME_DIR to the LEGO Harry Potter folder" >&2; exit 2; }
LOGS=${TMPDIR:-/tmp}/hp-boot
mkdir -p "$LOGS/shots"
[ "$LOGS/shots/winid" -nt "$T/winid.swift" ] || swiftc -O -o "$LOGS/shots/winid" "$T/winid.swift"
STOP=${STOP:-90}
caffeinate -u -d -t $((STOP + 10)) &
rm -f "$LOGS/run$R.pid"
BOOT_SECS=$((STOP + 30)) "$T/run_boot.sh" "$R" "$@" > /dev/null 2>&1 &
start=$(date +%s)
until [ -s "$LOGS/run$R.pid" ]; do sleep 0.2; done
pid=$(cat "$LOGS/run$R.pid")
for t in ${SHOTS:-20 45 80}; do
    while [ $(( $(date +%s) - start )) -lt $t ]; do sleep 1; done
    w=$("$LOGS/shots/winid" "$pid")
    echo "t=$t window: ${w:-none}"
    if [ -n "$w" ]; then
        set -- $w
        screencapture -x -l "$1" "$LOGS/shots/run${R}_${t}s.png" || screencapture -x -R "$2,$3,$4,$5" "$LOGS/shots/run${R}_${t}s.png"
    else
        screencapture -x "$LOGS/shots/run${R}_${t}s_screen.png"
    fi
done
while [ $(( $(date +%s) - start )) -lt $STOP ]; do sleep 1; done
kill -TERM "$pid" 2>/dev/null; sleep 5; kill -KILL "$pid" 2>/dev/null
wait

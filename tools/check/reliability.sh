#!/bin/bash
# One-command reliability verdict for an HP build (notes/reliability.md).
# Usage: tools/check/reliability.sh [--short] [--skip-tests] [--boot-limit S] [--out DIR] BUILD [SECONDS]
#        tools/check/reliability.sh [--skip-tests] [--out DIR] --baseline APP_A --candidate APP_B [SECONDS]
#   BUILD    a game binary (build.nosync/lhp; needs HPRECOMP_GAME_DIR) or "Harry Potter Recompiled.app"
#   SECONDS  run length (default 1200, the run-24 soak; compare 600); --short = 600
# Waits while another game runs or the screen is locked, runs tests/run_all.sh, then
# tools/check/reliability.py run (compare: A, then B, each after an unjudged warm-up run, same
# input and empty private HOME, then reliability.py compare). Exit 0 only when all pass (1 on a failed check or a worse
# candidate, 2 on a usage error).
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
PY=$ROOT/.venv/bin/python   # as tests/run_all.sh
SECS=1200; TESTS=1; BOOT=(); OUT=; BASE=; CAND=
while [ $# -gt 0 ]; do
    case $1 in
        --short) SECS=600; shift ;;
        --baseline) BASE=$2; SECS=600; shift 2 ;;
        --candidate) CAND=$2; shift 2 ;;
        --skip-tests) TESTS=0; shift ;;
        --boot-limit) BOOT=(--boot-limit "$2"); shift 2 ;;
        --out) OUT=$2; shift 2 ;;
        -*) echo "unknown option $1" >&2; exit 2 ;;
        *) break ;;
    esac
done
if [ -n "$BASE$CAND" ]; then
    [ -e "$BASE" ] && [ -e "$CAND" ] || { echo "--baseline and --candidate need two builds" >&2; exit 2; }
    [ $# -ge 1 ] && SECS=$1
else
    [ $# -ge 1 ] || { sed -n 3,7p "$0" >&2; exit 2; }
    BUILD=$1; [ $# -ge 2 ] && SECS=$2
    [ -e "$BUILD" ] || { echo "no such build: $BUILD" >&2; exit 2; }
    case $BUILD in *.app|*.app/) ;; *) [ -n "${HPRECOMP_GAME_DIR:-}" ] ||
        { echo "a bare binary needs HPRECOMP_GAME_DIR (the LEGO Harry Potter folder)" >&2; exit 2; } ;; esac
fi
[ -n "$OUT" ] || [ -n "${TMPDIR:-}" ] || { echo "set TMPDIR or pass --out DIR" >&2; exit 2; }
OUT=${OUT:-$TMPDIR/hp_reliability/$(date +%Y%m%d-%H%M%S)}
mkdir -p "$OUT" || exit 2

busy() {
    # Process names only: a pgrep -f pattern also matches other lanes' waiting shells.
    # Called only before this script starts its own game, so it never sees that PID.
    pgrep -x lhp_boot || pgrep -x lhp || pgrep -x hobbit || pgrep -x marvel
}
locked() {
    /usr/local/bin/python3.11 -I -c "import Quartz,sys;sys.exit(0 if Quartz.CGSessionCopyCurrentDictionary().get('CGSSessionScreenIsLocked') else 1)"
}
wait_free() {
    while busy > /dev/null || locked; do echo "waiting: game running ($(busy | tr '\n' ' ')) or screen locked"; sleep 30; done
}
wait_free

RC=()
if [ $TESTS = 1 ]; then
    "$ROOT/tests/run_all.sh" --out "$OUT/tests" > "$OUT/tests.log" 2>&1
    RC=(--tests-rc $?)
    tail -n 3 "$OUT/tests.log"
fi
run_one() {  # run_one BUILD DIR: one judged run; prints its verdict table
    mkdir -p "$2" || exit 2
    wait_free
    "$PY" "$ROOT/tools/check/reliability.py" run --exe "$1" --seconds "$SECS" --out "$2" ${WARM[@]+"${WARM[@]}"} \
        ${RC[@]+"${RC[@]}"} ${BOOT[@]+"${BOOT[@]}"} | tee "$2/verdict.txt"
    return "${PIPESTATUS[0]}"
}
if [ -z "$BASE" ]; then
    run_one "$BUILD" "$OUT"
    exit $?
fi
WARM=(--warm)  # each build runs once unjudged first: both judged runs start with warm shader caches
run_one "$BASE" "$OUT/baseline"
run_one "$CAND" "$OUT/candidate"
"$PY" "$ROOT/tools/check/reliability.py" compare --baseline "$OUT/baseline" --candidate "$OUT/candidate" \
    | tee "$OUT/compare.txt"
exit "${PIPESTATUS[0]}"

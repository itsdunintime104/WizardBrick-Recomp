"""HP reliability check: run one build for a fixed time, sample it, and judge the run.

  reliability.py run --exe PATH --seconds N --out DIR [--warm] [--tests-rc RC] [--boot-limit S]
  reliability.py judge --out DIR --seconds N [--tests-rc RC] [--boot-limit S]   (re-judge a run)
  reliability.py compare --baseline DIR --candidate DIR   (two judged runs; FAIL when worse)

`run` launches the game with a private HOME, the frame trace, frame dumps, the shader dump and a
generated keyboard input script (menus, then from PLAY_AT walking, three pause-menu opens and a
cover of the window); samples RSS every 60 s and `vmmap --summary` plus `heap` at EARLY_AT and
at N-60 s; stops the game at N s; then judges and writes metrics.json. Exit 0 when every check
passes.
The log rules (alive, RECOMP:, UNSUPPORTED, guest heap, assertions) and the stop sequence are
imported from the engine's tools/checks/boot_check.py (ENGINE, default the engine/ submodule).
A bare binary (build.nosync/lhp) runs with orig.nosync/lhp_win32.exe and $HPRECOMP_GAME_DIR as its
arguments and the KosmicKrisp driver from HPRECOMP_DEPS_DIR (default build.nosync/deps).
"""
from __future__ import annotations

import argparse
import bisect
import json
import math
import os
import re
import statistics
import subprocess
import sys
import time
from collections.abc import Callable
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
ENGINE = Path(os.environ.get("ENGINE", ROOT / "engine"))
sys.path.insert(0, str(ENGINE))
from tools.checks import boot_check  # noqa: E402
from tools.checks.recomp_check import CheckResult  # noqa: E402

DEPS = Path(os.environ.get("HPRECOMP_DEPS_DIR", ROOT / "build.nosync" / "deps"))
IMAGE = ROOT / "orig.nosync" / "lhp_win32.exe"
KNOWN_UNSUPPORTED = HERE / "known_unsupported.txt"
CRASH_DIR = Path.home() / "Library" / "Logs" / "DiagnosticReports"
CRASH_NAMES = ("lhp", "HarryPotter")

BOOT_LIMIT_S = 390        # 60 fps cap (job relck/final): HUD by Present #21000, t=358 s. The first
                          # story cutscene runs per frame: at ~115 fps (relck/val3) it was 282 s
GAP_MS = 1000.0           # owner brief: no Present gap over 1 s
FPS_TARGET = 60           # owner target; compare counts fps above it as 60
FPS_MEDIAN_MIN = 55       # owner target 60 fps, less 8 % for the per-second count
FPS_MIN = 30              # Hobbit/engine plot_frames.py SLOW_MS 33.3 ms = 30 fps
FOOTPRINT_GROWTH = 1.25   # run 24: 816 -> 858 MB peak in 20 min (+5 %)
OBJ_GROWTH_COUNT = 1000   # a class that gains this many live objects ...
OBJ_GROWTH_RATIO = 2.0    # ... and at least doubles between the two samples is a leak
SAMPLE_PAD_S = 5          # frames this close to a vmmap/heap sample are not judged
SETTLE_S = 10             # the load dip after gameplay starts (run 23: 59 fps at 245 s)
DUMP_STEP = 300           # frame dump every 300 Presents (5 s at 60 fps; cap 1024, d3d9 DUMP_MAX):
                          # one lands in every pause hold; dump Presents (~85 ms) are not judged
DUMP_MAX = 1024
STALL_MS = 50.0           # a Present interval over 3 vblanks at 60 Hz is a stall
LOAD_MERGE_MS = 500.0     # stalls closer than this are one load
DISPLAY_HZ = 120          # this Mac's panel; the log line "hp: <N> Hz display" overrides it
HIST_MAX = 4              # interval histogram buckets 1, 2, 3, 4+ vblanks

# Input-script schedule in seconds. Every build must be in gameplay by PLAY_AT
# (BOOT_LIMIT_S plus a settle); boot_t is checked against it.
EARLY_AT = 395            # first vmmap/heap sample: after the level load, so growth is not the load
PLAY_AT = 405             # first 30 s walk block
PAUSE_AT = (440, 452, 464)  # PAUSE_OPEN_KEY opens the pause menu, PAUSE_CLOSE_KEY PAUSE_HOLD_S later
PAUSE_OPEN_KEY = "ESCAPE"   # game 0x96E2D0 (lhp_0076.c): ESC sets the pause bit 0x20; RETURN is confirm
PAUSE_CLOSE_KEY = "RETURN"  # confirm on Resume, the menu's default item
PAUSE_HOLD_S = 6          # > DUMP_STEP at 60 fps + MENU_DELAY_S: a dump shows each hold
MENU_DELAY_S = 0.5        # a dump this long after the key shows the menu if it opened
PAUSE_WINDOW_S = 4        # stalls this soon after the key belong to the open
COVER_AT = 480            # tools/check/cover.py covers the screens for COVER_S
COVER_S = 5
AFTER_COVER_S = 3         # frames this long after the uncover belong to the cover phase
PLAY2_AT = 495            # walk blocks again until SECONDS-20
MIN_SECONDS = 600         # the late sample (SECONDS-60) follows every phase
COVER_PY = "/usr/local/bin/python3.11"  # has PyObjC; the venv and miniforge pythons do not

# compare: the candidate fails a row when it is worse than the baseline by more than this.
MS_RATIO, MS_ABS = 1.25, 100.0   # ms rows: worse than baseline x1.25 + 100 ms
FPS_DROP = 5.0                   # fps rows (capped at FPS_TARGET): 5 fps below the baseline
JUDDER_PTS = 5.0                 # judder rows: 5 percentage points above the baseline
FOOTPRINT_PTS = 0.10             # footprint growth ratio: +0.10 over the baseline

# Gameplay HUD signature (calibrated on runs 23, 24, 26 at 960x600): the player portrait (blue)
# and the heart row (red). Gameplay frames score red >= 1012, blue >= 581; title, menus,
# cutscenes and loads score red <= 299, blue <= 18.
HUD_RED_BOX, HUD_BLUE_BOX = (140, 80, 250, 115), (90, 40, 150, 110)
HUD_RED_MIN, HUD_BLUE_MIN = 500, 300
REF_W, REF_H = 960, 600
# Pause-menu signature (calibrated on 800 dumps of runs pause/esc1, pause/probe1, gpu1/run1,
# fifo/rel and relck/val3 at 2056x1290, per 960x600 area): the gold "Paused" label under the
# player name and the light-blue Options/Extras/Quit Game column. 84 menu dumps score label
# 239-289, column 1112-1908; the other 716 score label <= 44, column up to 8205 and 1064 (title,
# boot) and 888 (a blue spell in gameplay). The label limit sits in the 44-239 gap; the column
# alone does not separate, so it only backs the label up.
MENU_LABEL_BOX, MENU_COL_BOX = (60, 155, 145, 190), (400, 260, 560, 370)
MENU_LABEL_MIN, MENU_COL_MIN = 120, 500

GAME_ENV = {
    "RECOMP_ASSUME_FOCUS": "1", "RECOMP_NO_ALERT_UI": "1", "SDL_AUDIO_DRIVER": "dummy",
    "HP_TRACE_D3D9": "1", "HPRECOMP_FRAME_TRACE": "1", "DXVK_LOG_LEVEL": "info",
}
BARE_ENV = {  # tools/boot/run_boot.sh; the .app launcher sets its own
    "VK_DRIVER_FILES": str(DEPS / "kosmickrisp/share/vulkan/icd.d/kosmickrisp_mesa_icd.aarch64.json"),
    "DXVK_WSI_DRIVER": "SDL2", "DYLD_FALLBACK_LIBRARY_PATH": "/opt/homebrew/lib",
}


# ---- input script ---------------------------------------------------------------------------
def walk(t_s: int) -> list[str]:
    """One 30 s block of tools/boot/load_kb.input keys: W/D/S/A 2.5 s each, J held 2 s, K tap."""
    t = t_s * 1000
    ev = []
    for i, k in enumerate(("W", "D", "S", "A")):
        ev += [f"{t + i * 3000} down {k}", f"{t + i * 3000 + 2500} up {k}"]
    return ev + [f"{t + 12000} down J", f"{t + 14000} up J", f"{t + 14500} tap K"]


def input_script(seconds: int) -> str:
    """Wall-clock schedule (input_script.c counts ms on CLOCK_MONOTONIC): RETURN every 3 s to
    60 s (boot_log runs 24/28: taps must run to 60 s); a walk block at PLAY_AT; at each PAUSE_AT
    PAUSE_OPEN_KEY opens the pause menu and PAUSE_CLOSE_KEY closes it PAUSE_HOLD_S later; walk
    blocks from PLAY2_AT. The cover is done by run()."""
    ev = [f"{t * 1000} tap RETURN" for t in range(3, 61, 3)] + walk(PLAY_AT)
    for t in PAUSE_AT:
        ev += [f"{t * 1000} tap {PAUSE_OPEN_KEY}", f"{(t + PAUSE_HOLD_S) * 1000} tap {PAUSE_CLOSE_KEY}"]
    t = PLAY2_AT
    while t + 30 <= seconds - 20:
        ev += walk(t)
        t += 30
    return "# generated by tools/check/reliability.py\n" + "\n".join(ev) + "\n"


# ---- run ------------------------------------------------------------------------------------
def resolve_exe(path: Path) -> tuple[list[str], dict[str, str]]:
    if path.suffix == ".app":
        return [str(path / "Contents" / "MacOS" / "HarryPotterRecompiled")], {}
    game_dir = os.environ.get("HPRECOMP_GAME_DIR")
    if not game_dir:
        sys.exit("reliability.py: a bare binary needs HPRECOMP_GAME_DIR (the LEGO Harry Potter folder)")
    return [str(path), str(IMAGE), game_dir], BARE_ENV


def game_pid(launcher: int) -> int:
    """The .app launcher may spawn the game; sample the child when there is one."""
    out = subprocess.run(["pgrep", "-P", str(launcher)], capture_output=True, text=True).stdout.split()
    return int(out[0]) if out else launcher


def crash_reports() -> set[str]:
    if not CRASH_DIR.is_dir():
        return set()
    return {p.name for p in CRASH_DIR.iterdir() if p.name.startswith(CRASH_NAMES)}


def sample(pid: int, out: Path, label: str, t0: float) -> list[float]:
    start = time.monotonic() - t0
    for tool, args in (("vmmap", ["vmmap", "--summary", str(pid)]), ("heap", ["heap", str(pid)])):
        with (out / f"{tool}_{label}.txt").open("w") as f:
            try:
                subprocess.run(args, stdout=f, stderr=subprocess.STDOUT, timeout=240, check=False)
            except subprocess.TimeoutExpired:
                f.write(f"\n{tool}: timed out\n")
    return [start, time.monotonic() - t0]


def ft_last(ft: Path) -> tuple[int, float] | None:
    """(n, t_ms) of the last complete F line; the trace is flushed about once a second."""
    try:
        with ft.open("rb") as f:
            f.seek(0, 2)
            f.seek(max(0, f.tell() - 4096))
            lines = f.read().decode(errors="replace").split("\n")[1:-1]
    except FileNotFoundError:
        return None
    for line in reversed(lines):
        p = line.split()
        if len(p) == 9 and p[0] == "F":
            return int(p[1]), float(p[2])
    return None


def cover_game(t0: float) -> list[float] | None:
    """Runs cover.py; returns the run times (s) of its "covered" and "uncovered" lines."""
    proc = subprocess.Popen([COVER_PY, "-I", str(HERE / "cover.py"), str(COVER_S)],
                            stdout=subprocess.PIPE, text=True)
    marks = [time.monotonic() - t0 for line in proc.stdout or [] if line.strip() in ("covered", "uncovered")]
    try:
        proc.wait(timeout=30)
    except subprocess.TimeoutExpired:
        proc.kill()
    return marks if len(marks) == 2 else None


def base_env(defaults: dict[str, str]) -> dict[str, str]:
    """defaults (BARE_ENV) yield to the caller's environment; GAME_ENV is what the check needs."""
    return {**defaults, **os.environ, **GAME_ENV}


def run(args: argparse.Namespace) -> None:
    out: Path = args.out
    for d in ("home/Library/Logs", "home/Library/Application Support", "frames", "shaders"):
        (out / d).mkdir(parents=True, exist_ok=True)
    (out / "input.txt").write_text(input_script(args.seconds))
    dumps = ",".join(str(n) for n in range(DUMP_STEP, DUMP_STEP * (DUMP_MAX + 1), DUMP_STEP)
                     [: min(DUMP_MAX, args.seconds * 200 // DUMP_STEP)])
    cmd, extra = resolve_exe(args.exe)
    env = {**base_env(extra), "HOME": str(out / "home"),
           "HPRECOMP_FRAME_TRACE_OUT": str(out / "ft.txt"), "HPRECOMP_DUMP_FRAMES": dumps,
           "HPRECOMP_DUMP_DIR": str(out / "frames"), "HPRECOMP_SHADER_DUMP": str(out / "shaders"),
           "HPRECOMP_INPUT_SCRIPT": str(out / "input.txt")}
    before = crash_reports()
    meta: dict[str, object] = {"exe": str(args.exe), "seconds": args.seconds, "samples": {}, "rss": [],
                               "cover": None, "ft_offset_s": None}
    caff = subprocess.Popen(["caffeinate", "-u", "-d", "-t", str(args.seconds + 60)])
    with (out / "game.log").open("wb") as log:
        proc = subprocess.Popen(cmd, env=env, stdout=log, stderr=subprocess.STDOUT)
        t0 = time.monotonic()
        meta["pid"] = proc.pid
        due = {"early": EARLY_AT, "late": args.seconds - 60}
        next_rss = 60
        offset = math.inf  # run time - frame-trace time; the smallest reading has the least flush lag
        try:
            while (now := time.monotonic() - t0) < args.seconds and proc.poll() is None:
                pid = game_pid(proc.pid)
                last = ft_last(out / "ft.txt")
                if last is not None:
                    offset = min(offset, time.monotonic() - t0 - last[1] / 1000)
                    meta["ft_offset_s"] = offset
                if meta["cover"] is None and now >= COVER_AT:
                    meta["cover"] = cover_game(t0) or []
                if now >= next_rss:
                    rss = subprocess.run(["ps", "-o", "rss=", "-p", str(pid)], capture_output=True,
                                         text=True).stdout.strip()
                    meta["rss"].append([round(now), int(rss) if rss else None])  # type: ignore[union-attr]
                    next_rss += 60
                for label, at in list(due.items()):
                    if now >= at:
                        meta["samples"][label] = sample(pid, out, label, t0)  # type: ignore[index]
                        del due[label]
                time.sleep(1)
        finally:
            code = proc.poll()
            meta["exited_early"] = code is not None
            meta["exit_code"] = code
            meta["ran_s"] = round(time.monotonic() - t0, 1)
            if code is None:
                boot_check._stop(proc)
            caff.terminate()
    if meta["exited_early"]:
        time.sleep(15)  # ReportCrash writes the report after the process is gone
    meta["new_crash_reports"] = sorted(crash_reports() - before)
    (out / "run.json").write_text(json.dumps(meta, indent=1))


# ---- judge ----------------------------------------------------------------------------------
def frames_of(ft: Path) -> list[tuple[int, float, float]]:
    rows: list[tuple[int, float, float]] = []
    if ft.is_file():
        for line in ft.open(errors="replace"):
            p = line.split()
            if len(p) == 9 and p[0] == "F":
                rows.append((int(p[1]), float(p[2]), float(p[3])))
    return rows


def ppm(path: Path) -> tuple[int, int, bytes]:
    """(width, height, RGB bytes) of a binary P6 frame dump."""
    raw = path.read_bytes()
    w, h = map(int, raw.split(b"\n", 3)[1].split())
    return w, h, raw[len(raw) - w * h * 3:]


def box_count(img: tuple[int, int, bytes], box: tuple[int, int, int, int],
              test: Callable[[int, int, int], bool]) -> int:
    """Pixels in box (REF_W x REF_H coordinates) of a ppm() image where test(r, g, b) holds."""
    w, h, px = img
    sx, sy = w / REF_W, h / REF_H
    x0, y0, x1, y1 = int(box[0] * sx), int(box[1] * sy), int(box[2] * sx), int(box[3] * sy)
    n = 0
    for y in range(y0, y1):
        row = px[(y * w + x0) * 3:(y * w + x1) * 3]
        for i in range(0, len(row), 3):
            n += test(row[i], row[i + 1], row[i + 2])
    return n


def ref_area(img: tuple[int, int, bytes]) -> float:
    return img[0] / REF_W * img[1] / REF_H


def hud_score(path: Path) -> tuple[int, int, bool]:
    img = ppm(path)
    red = box_count(img, HUD_RED_BOX, lambda r, g, b: r > 150 and g < 70 and b < 70)
    blue = box_count(img, HUD_BLUE_BOX, lambda r, g, b: b > 120 and b > r + 50 and b > g + 20)
    return red, blue, red >= HUD_RED_MIN * ref_area(img) and blue >= HUD_BLUE_MIN * ref_area(img)


def menu_score(path: Path) -> tuple[int, int, bool]:
    """(gold label pixels, light-blue column pixels, pause menu shown)."""
    img = ppm(path)
    label = box_count(img, MENU_LABEL_BOX, lambda r, g, b: r > 150 and 90 < g < r and b < 70)
    col = box_count(img, MENU_COL_BOX, lambda r, g, b: b > 200 and g > 150 and r < 130)
    return label, col, label >= MENU_LABEL_MIN * ref_area(img) and col >= MENU_COL_MIN * ref_area(img)


def size_mb(text: str, key: str) -> float | None:
    m = re.search(rf"^{re.escape(key)}:\s+([\d.]+)([KMG])", text, re.M)
    if not m:
        return None
    return float(m.group(1)) * {"K": 1 / 1024, "M": 1, "G": 1024}[m.group(2)]


def heap_classes(text: str) -> dict[str, int]:
    found: dict[str, int] = {}
    for line in text.splitlines():
        p = line.split()
        if len(p) >= 4 and p[0].isdigit() and p[1].isdigit():
            found[" ".join(p[3:])] = found.get(" ".join(p[3:]), 0) + int(p[0])
    return found


def in_samples(t_ms: float, dt_ms: float, windows: list[list[float]]) -> bool:
    lo, hi = (t_ms - dt_ms) / 1000, t_ms / 1000
    return any(lo < b + SAMPLE_PAD_S and hi > a - SAMPLE_PAD_S for a, b in windows)


INPUT_RE = re.compile(r"INPUT t=(\d+) (down|up) (\S+)")
PRESENT_RE = re.compile(r"D3D9 Present #(\d+) ")
WINDOW_RE = re.compile(r"D3D9 window (hidden|visible) at Present #(\d+)")
HZ_RE = re.compile(r"^hp: (\d+) Hz display", re.M)
Frame = tuple[int, float, float]
Span = tuple[float, float]


def game_log(out: Path) -> str:
    """A bare binary logs to game.log; the .app launcher to run-*.log in the private HOME."""
    runs = sorted((out / "home" / "Library" / "Logs" / "HarryPotterRecomp").glob("run-*.log"))
    return "\n".join(p.read_text(errors="replace") for p in [out / "game.log", *runs] if p.is_file())


def key_events(log: str) -> list[tuple[int, str, str, int]]:
    """(script ms, down|up, key, last Present number logged before the event)."""
    n, ev = 0, []
    for ln in log.splitlines():
        if m := PRESENT_RE.match(ln):
            n = int(m[1])
        elif m := INPUT_RE.match(ln):
            ev.append((int(m[1]), m[2], m[3], n))
    return ev


def pause_spans(events: list[tuple[int, str, str, int]], t_of: dict[int, float]) -> list[Span]:
    """(open, close) frame-trace ms of each scheduled pause: open and close key downs from
    PAUSE_AT, in pairs."""
    downs = [t_of.get(n, 0.0) for ms, act, key, n in events
             if key in (PAUSE_OPEN_KEY, PAUSE_CLOSE_KEY) and act == "down" and ms >= PAUSE_AT[0] * 1000]
    return list(zip(downs[0::2], downs[1::2]))


def open_ms(frames: list[Frame], times: list[float], t_key: float, dumps: set[int]) -> float | None:
    """Key-down to the end of the last stall in the PAUSE_WINDOW_S after it (one frame if none).
    The key is polled after Present t_key, so this is at most one frame long. Dump Presents
    are not stalls of the game."""
    i = bisect.bisect_right(times, t_key)
    after = frames[i:bisect.bisect_right(times, t_key + PAUSE_WINDOW_S * 1000)]
    if not after:
        return None
    end = max((t for n, t, dt in after if dt > STALL_MS and n not in dumps), default=after[0][1])
    return end - t_key


def pause_opened(dumps: list[tuple[int, Path]], t_of: dict[int, float], a: float, b: float) -> bool | None:
    """True when a dump from MENU_DELAY_S after the key (a) to the close (b) shows the menu
    signature (menu_score); None when no dump falls in that time."""
    inside = [p for n, p in dumps if n in t_of and a + MENU_DELAY_S * 1000 <= t_of[n] < b]
    if not inside:
        return None
    return any(menu_score(p)[2] for p in inside)


def pause_result(frames: list[Frame], times: list[float], dumps: list[tuple[int, Path]],
                 t_of: dict[int, float], a: float, b: float) -> float | str:
    """The open time in ms when a dump shows the menu, else why there is none."""
    shown = pause_opened(dumps, t_of, a, b)
    if not shown:
        return "no dump in hold" if shown is None else "pause not opened"
    o = open_ms(frames, times, a, {n for n, _ in dumps})
    return "no frames" if o is None else o


def load_stalls(frames: list[Frame], until_ms: float, windows: list[list[float]], dumps: set[int]) -> list[Span]:
    """Runs of Presents over STALL_MS (merged within LOAD_MERGE_MS) before until_ms: from the
    last steady frame before to the first frame after, longest first. Dump Presents are skipped."""
    spans: list[list[float]] = []
    for n, t, dt in frames:
        if t > until_ms:
            break
        if dt > STALL_MS and n not in dumps and not in_samples(t, dt, windows):
            if spans and t - dt - spans[-1][1] < LOAD_MERGE_MS:
                spans[-1][1] = t
            else:
                spans.append([t - dt, t])
    return sorted(((a, b) for a, b in spans), key=lambda s: s[0] - s[1])


def histogram(dts: list[float], hz: float) -> list[int]:
    """Present intervals per vblank count 1, 2, 3, 4+ at the display rate."""
    h = [0] * HIST_MAX
    for dt in dts:
        h[min(HIST_MAX, max(1, round(dt * hz / 1000))) - 1] += 1
    return h


def judder_pct(h: list[int]) -> float | None:
    """Share of frames off the most common interval (a 60-on-120 judder mixes 1 and 3)."""
    return 100.0 * (1 - max(h) / sum(h)) if sum(h) else None


def judge(out: Path, seconds: int, tests_rc: int | None, boot_limit: int
          ) -> tuple[list[tuple[CheckResult, str, str]], dict[str, object]]:
    meta = json.loads((out / "run.json").read_text())
    log = game_log(out)
    frames = frames_of(out / "ft.txt")
    times = [t for _, t, _ in frames]
    windows = list(meta["samples"].values())
    res: list[tuple[CheckResult, str, str]] = []
    m: dict[str, object] = {}

    def add(r: CheckResult, limit: str, src: str) -> None:
        res.append((r, limit, src))

    if tests_rc is not None:
        add(CheckResult("unit tests", tests_rc == 0, f"tests/run_all.sh exit {tests_rc}"), "exit 0",
            "HP tests/run_all.sh")
    alive = boot_check.judge(log, meta["exited_early"], seconds, exit_code=meta["exit_code"])
    named = {r.name: r for r in alive}
    add(named["alive"], f"running at {seconds} s", "engine boot_check.py:100-103")
    crashes = meta["new_crash_reports"]
    add(CheckResult("crash reports", not crashes, f"{len(crashes)} new", [f"  {c}" for c in crashes]),
        "0 new", "brief; DiagnosticReports")

    # Boot to gameplay: HUD signature on the frame dumps, timed by the frame trace.
    t_of = {n: t for n, t, _ in frames}
    dumps = sorted(((int(p.stem.split("_")[1]), p) for p in out.glob("frames/frame_*.ppm")), key=lambda d: d[0])
    dump_n = {n for n, _ in dumps}
    scored = [(n, *hud_score(p)) for n, p in dumps]
    first = next((n for n, _, _, ok in scored if ok), None)
    boot_t = t_of.get(first, 0) / 1000 if first is not None else None
    m["boot_s"] = boot_t
    last_ok = bool(scored) and scored[-1][3]
    ok = boot_t is not None and boot_t <= boot_limit and last_ok
    detail = (f"gameplay HUD at Present #{first} t={boot_t:.0f} s; last dump #{scored[-1][0]} "
              f"{'in gameplay' if last_ok else 'NOT in gameplay'}" if boot_t is not None
              else f"no gameplay HUD in {len(scored)} dumps")
    add(CheckResult("boot to gameplay", ok, detail), f"<= {boot_limit} s, still in game at end",
        "HP relck/final (60 fps cap); dumps every 600")

    judged = [(n, t, dt) for n, t, dt in frames if not in_samples(t, dt, windows)]
    clean = [f for f in judged if f[0] not in dump_n]  # interval rows: no sample, no dump Present
    worst = max(clean, key=lambda f: f[2], default=None)
    m["gap_ms"] = worst[2] if worst else None
    add(CheckResult("Present gap", bool(clean) and worst is not None and worst[2] <= GAP_MS,
                    f"max {worst[2]:.0f} ms at t={worst[1] / 1000:.1f} s ({len(frames)} frames)"
                    if worst else "no frame trace"),
        f"<= {GAP_MS:.0f} ms", "brief; HP boot_log run 24 (1.5 s)")

    # Whole seconds only: the first second after the settle and the last one are partial.
    per_s: dict[int, int] = {}
    start = math.ceil((boot_t or 0) + SETTLE_S)
    for n, t, dt in judged:
        if t / 1000 >= start:
            per_s[int(t // 1000)] = per_s.get(int(t // 1000), 0) + 1
    bad_s = {int(s) for a, b in windows for s in range(int(a - SAMPLE_PAD_S), int(b + SAMPLE_PAD_S) + 2)}
    fps = [c for s, c in sorted(per_s.items())[:-1] if s not in bad_s]
    med = statistics.median(fps) if fps else 0
    low = min(fps) if fps else 0
    m["fps_median"], m["fps_min"] = (med, low) if boot_t is not None else (None, None)
    add(CheckResult("fps median", boot_t is not None and med >= FPS_MEDIAN_MIN,
                    f"{med:.0f} fps over {len(fps)} s of gameplay"), f">= {FPS_MEDIAN_MIN}", "owner 60 fps target")
    add(CheckResult("fps min", boot_t is not None and low >= FPS_MIN, f"{low} fps (worst second)"),
        f">= {FPS_MIN}", "engine plot_frames.py:16 SLOW_MS 33.3")

    # Scene loads: the two longest stall runs before gameplay settles.
    loads = load_stalls(frames, ((boot_t or seconds) + SETTLE_S) * 1000, windows, dump_n)[:2]
    m["load_ms"] = max((b - a for a, b in loads), default=None)
    add(CheckResult("scene loads", bool(loads) and (m["load_ms"] or 0) <= GAP_MS,
                    "; ".join(f"{b - a:.0f} ms at t={a / 1000:.1f} s" for a, b in loads) or "no stall"),
        f"<= {GAP_MS:.0f} ms each", "brief; frame-trace stalls > 50 ms")

    # Pause menu: three opens, each seen in a dump; key-down to the end of the stall the open causes.
    events = key_events(log)
    pauses = pause_spans(events, t_of)
    rows = [pause_result(frames, times, dumps, t_of, a, b) for a, b in pauses]
    got = [o for o in rows if isinstance(o, float)]
    all_open = len(got) == len(PAUSE_AT)
    m["pause_open_ms"] = max(got) if all_open else None
    m["pauses_opened"] = len(got)
    add(CheckResult("pause open", all_open and max(got) <= GAP_MS,
                    f"{len(got)}/{len(PAUSE_AT)} opened: " + ", ".join(
                        o if isinstance(o, str) else f"{o:.0f} ms" for o in rows)),
        f"{len(PAUSE_AT)} opens, each <= {GAP_MS:.0f} ms", "brief; menu in a dump, stall after the open key")

    # Cover: the run times of cover.py's lines, moved to frame-trace time with the offset.
    hz = float(h[1]) if (h := HZ_RE.search(log)) else DISPLAY_HZ
    states = [(t_of.get(int(n), 0.0), st) for st, n in WINDOW_RE.findall(log)]
    cover = meta.get("cover") or []
    off = meta.get("ft_offset_s")
    c0 = c1 = None
    if len(cover) == 2 and off is not None:
        c0, c1 = ((x - off) * 1000 for x in cover)
        after = [dt for n, t, dt in frames if c1 < t <= c1 + AFTER_COVER_S * 1000 and n not in dump_n]
        m["uncover_stall_ms"] = max(after, default=None)
        vis = [t for t, st in states if st == "visible" and t >= c0]
        hidden_seen = any(st == "hidden" and c0 <= t <= c1 + AFTER_COVER_S * 1000 for t, st in states)
        m["cover_resume_ms"] = max(0.0, vis[0] - c1) if hidden_seen and vis else None
        end_state = [st for t, st in states if t <= c1 + AFTER_COVER_S * 1000]
        m["hidden_stuck"] = bool(end_state) and end_state[-1] == "hidden"
        detail = (f"hidden signal {'seen' if hidden_seen else 'not logged'}; resume "
                  f"{m['cover_resume_ms'] if m['cover_resume_ms'] is None else round(m['cover_resume_ms'])} ms; "
                  f"worst frame after {m['uncover_stall_ms'] or 0:.0f} ms; "
                  f"{'STILL HIDDEN' if m['hidden_stuck'] else 'visible'} at uncover+{AFTER_COVER_S} s")
        ok = not m["hidden_stuck"] and (m["uncover_stall_ms"] or 0) <= GAP_MS and (
            m["cover_resume_ms"] is None or m["cover_resume_ms"] <= GAP_MS)
    else:
        m["uncover_stall_ms"] = m["cover_resume_ms"] = m["hidden_stuck"] = None
        detail, ok = "cover did not run", False
    add(CheckResult("cover", ok, detail), f"resume <= {GAP_MS:.0f} ms, not hidden", "brief; cover.py")

    # Present-interval histogram per phase.
    def phase(t: float) -> str:
        if boot_t is None or t < boot_t * 1000:
            return "boot"
        if c0 is not None and c1 is not None and c0 <= t <= c1 + AFTER_COVER_S * 1000:
            return "cover"
        if any(a <= t <= b + PAUSE_WINDOW_S * 1000 for a, b in pauses):
            return "pause"
        return "play"
    dts: dict[str, list[float]] = {"boot": [], "play": [], "pause": [], "cover": []}
    for _, t, dt in clean:
        dts[phase(t)].append(dt)
    hists = {k: histogram(v, hz) for k, v in dts.items()}
    for k, h in hists.items():
        m[f"judder_{k}"] = judder_pct(h)
        m[f"hist_{k}"] = h
    add(CheckResult("present intervals", True, " | ".join(
        f"{k} {'/'.join(map(str, h))}" for k, h in hists.items()), [
        f"  {k}: judder {m[f'judder_{k}'] or 0:.1f} % (frames off the usual interval)" for k in hists]),
        f"info: 1/2/3/4+ vblanks at {hz:.0f} Hz", "brief; frame-trace dt")

    vm = {k: (out / f"vmmap_{k}.txt").read_text(errors="replace") if (out / f"vmmap_{k}.txt").is_file() else ""
          for k in ("early", "late")}
    fe, fl = size_mb(vm["early"], "Physical footprint"), size_mb(vm["late"], "Physical footprint")
    rss = [r for _, r in meta["rss"] if r]
    rss_txt = f"; RSS {min(rss) // 1024}-{max(rss) // 1024} MB" if rss else ""
    early_s = meta["samples"].get("early", [0])[0]
    in_game = boot_t is not None and early_s >= boot_t
    m["footprint_x"] = fl / fe if fe and fl and in_game else None
    add(CheckResult("memory growth", in_game and fe is not None and fl is not None and fl <= fe * FOOTPRINT_GROWTH,
                    (f"footprint {fe:.0f} -> {fl:.0f} MB{rss_txt}" if fe and fl else "vmmap sample missing")
                    + ("" if in_game else f"; early sample {early_s:.0f} s is before gameplay")),
        f"x{FOOTPRINT_GROWTH} from {EARLY_AT} s to {seconds - 60} s", "HP boot_log run 24")

    he = heap_classes((out / "heap_early.txt").read_text(errors="replace")) if (out / "heap_early.txt").is_file() else {}
    hl = heap_classes((out / "heap_late.txt").read_text(errors="replace")) if (out / "heap_late.txt").is_file() else {}
    grown = sorted(((hl[k] - he.get(k, 0), k) for k in hl
                    if hl[k] - he.get(k, 0) >= OBJ_GROWTH_COUNT and hl[k] >= OBJ_GROWTH_RATIO * he.get(k, 0)),
                   reverse=True)
    add(CheckResult("live objects", bool(he) and bool(hl) and not grown,
                    f"{len(grown)} classes grew" if he and hl else "heap sample missing",
                    [f"  {k}: {he.get(k, 0)} -> {hl[k]}" for _, k in grown[:boot_check.MAX_LISTED]]),
        f"no class +{OBJ_GROWTH_COUNT} and x{OBJ_GROWTH_RATIO:g}", "brief (CAMetalDrawable leak)")

    known = [ln.strip() for ln in KNOWN_UNSUPPORTED.read_text().splitlines() if ln.strip() and not ln.startswith("#")]
    unsup = [ln for ln in log.splitlines() if boot_check.UNSUPPORTED_RE.match(ln) and not ln.startswith(tuple(known))]
    add(CheckResult("unsupported calls", not unsup, f"{len(unsup)} new UNSUPPORTED lines", [f"  {x}" for x in unsup[:10]]),
        "0 new", "engine boot_check.py:93-97,114")
    for name, src in (("translation gaps", "boot_check.py:92,112"), ("heap", "boot_check.py:115"),
                      ("assertions", "boot_check.py:99,116")):
        add(named[name], "0 lines", "engine " + src)
    d3d = [ln for ln in log.splitlines() if re.match(r"D3D9 (Present|EndScene|Reset) #\d+ hr=", ln)
           and not ln.endswith("hr=00000000")]
    add(CheckResult("D3D9 errors", not d3d, f"{len(d3d)} Present/EndScene/Reset hr != 0", [f"  {x}" for x in d3d[:10]]),
        "0", "engine boot_check.py:110 (GL errors)")
    shaders = [json.loads(p.read_text()) for p in out.glob("shaders/*.json")]
    bad = [s for s in shaders if s.get("hr", 0) != 0]
    add(CheckResult("shader compiles", bool(shaders) and not bad, f"{len(shaders)} compiled, {len(bad)} failed"),
        "0 failed", "HP boot_log runs 23-26")
    return res, m


def print_table(res: list[tuple[CheckResult, str, str]], out: Path) -> int:
    print(f"{'Check':<20} {'Result':<6} {'Value':<58} {'Limit':<34} Source")
    for r, limit, src in res:
        print(f"{r.name:<20} {'PASS' if r.ok else 'FAIL':<6} {r.detail:<58} {limit:<34} {src}")
        for line in r.lines:
            print(line)
    failed = sum(not r.ok for r, _, _ in res)
    print(f"run dir: {out}")
    print("reliability: PASS" if failed == 0 else f"reliability: FAIL ({failed} checks)")
    return 1 if failed else 0


# ---- compare --------------------------------------------------------------------------------
MS_ROWS = ("gap_ms", "load_ms", "pause_open_ms", "cover_resume_ms", "uncover_stall_ms")
FPS_ROWS = ("fps_median", "fps_min")
JUDDER_ROWS = ("judder_boot", "judder_play", "judder_pause", "judder_cover")


def worse(key: str, base: float | None, cand: float | None) -> tuple[bool | None, str]:
    """(failed, margin text); failed is None for an info row or when both values are missing."""
    if key == "boot_s":
        return None, "info (first cutscene runs per frame)"
    if key == "hidden_stuck":
        return bool(cand), "candidate must be False"
    if cand is None:
        return (None, "both missing") if base is None else (True, "candidate missing")
    if key in MS_ROWS:
        lim = GAP_MS if base is None else base * MS_RATIO + MS_ABS
        return cand > lim, f"<= {lim:.0f}" + (" (no baseline)" if base is None else "")
    if base is None:
        return None, "no baseline"
    if key in FPS_ROWS:
        lim = min(base, FPS_TARGET) - FPS_DROP
        return min(cand, FPS_TARGET) < lim, f">= {lim:.0f} (capped at {FPS_TARGET})"
    if key in JUDDER_ROWS:
        return cand > base + JUDDER_PTS, f"<= {base + JUDDER_PTS:.1f}"
    return cand > base + FOOTPRINT_PTS, f"<= {base + FOOTPRINT_PTS:.2f}"


def compare(base_dir: Path, cand_dir: Path) -> int:
    b, c = (json.loads((d / "metrics.json").read_text()) for d in (base_dir, cand_dir))
    keys = ("boot_s", *MS_ROWS, *FPS_ROWS, "footprint_x", "hidden_stuck", *JUDDER_ROWS)

    def fmt(v: object) -> str:
        return "n/a" if v is None else f"{v:.2f}" if isinstance(v, float) else str(v)
    print(f"{'Metric':<18} {'Baseline':>10} {'Candidate':>10}  {'Result':<6} Rule")
    failed = 0
    for k in keys:
        bad, rule = worse(k, b.get(k), c.get(k))
        failed += bad is True
        print(f"{k:<18} {fmt(b.get(k)):>10} {fmt(c.get(k)):>10}  "
              f"{'-' if bad is None else 'FAIL' if bad else 'PASS':<6} {rule}")
    for k in ("boot", "play", "pause", "cover"):
        print(f"hist_{k:<13} {'/'.join(map(str, b.get(f'hist_{k}', []))):>10} "
              f"{'/'.join(map(str, c.get(f'hist_{k}', []))):>10}  -      info: 1/2/3/4+ vblanks")
    print(f"baseline {base_dir}\ncandidate {cand_dir}")
    print("compare: PASS" if not failed else f"compare: FAIL ({failed} rows)")
    return 1 if failed else 0


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=(__doc__ or "").split("\n")[0])
    ap.add_argument("mode", choices=("run", "judge", "compare"))
    ap.add_argument("--exe", type=Path)
    ap.add_argument("--seconds", type=int)
    ap.add_argument("--out", type=Path)
    ap.add_argument("--tests-rc", type=int)
    ap.add_argument("--boot-limit", type=int, default=BOOT_LIMIT_S)
    ap.add_argument("--baseline", type=Path)
    ap.add_argument("--candidate", type=Path)
    ap.add_argument("--warm", action="store_true",
                    help="first an unjudged run of the same length into OUT/warm (fills the shader caches)")
    a = ap.parse_args(argv)
    if a.mode == "compare":
        if a.baseline is None or a.candidate is None:
            ap.error("compare needs --baseline DIR and --candidate DIR")
        return compare(a.baseline, a.candidate)
    if a.seconds is None or a.out is None:
        ap.error(f"{a.mode} needs --seconds and --out")
    if a.seconds < MIN_SECONDS:
        ap.error(f"--seconds must be at least {MIN_SECONDS}")
    if a.mode == "run":
        if a.exe is None:
            ap.error("run needs --exe")
        if a.warm:
            # The Metal shader cache is per bundle id, outside the private HOME, so a build's
            # first run compiles pipelines its predecessor had cached: 1-2 s Presents at every
            # scene change (notes/reliability.md, "Shader cache"). Judge the second run.
            run(argparse.Namespace(**{**vars(a), "out": a.out / "warm"}))
        run(a)
    res, metrics = judge(a.out, a.seconds, a.tests_rc, a.boot_limit)
    (a.out / "metrics.json").write_text(json.dumps(metrics, indent=1))
    return print_table(res, a.out)


if __name__ == "__main__":
    sys.exit(main())

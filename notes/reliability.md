# Reliability check (2026-10-24)

One command gives a PASS/FAIL verdict for an HP build:

```sh
tools/check/reliability.sh [--short] [--skip-tests] [--boot-limit S] [--out DIR] BUILD [SECONDS]
tools/check/reliability.sh [--skip-tests] [--out DIR] --baseline APP_A --candidate APP_B [SECONDS]
```

`BUILD` is a game binary (`lhp_boot`) or `Harry Potter Recompiled.app`. `SECONDS` defaults to
1200 (the run-24 soak); `--short` and compare runs are 600, the minimum.

Every new build is compared with a baseline build before the owner gets it: the compare form
runs A, then B (same input script, each with a new empty private HOME), judges each into
`OUT/baseline` and `OUT/candidate`, then `reliability.py compare` prints one row per metric and
fails a row when the candidate is worse than the baseline by more than the margin below. Exit 0 only when every check passes; 1 on a failed
check; 2 on a usage error. The run folder (default `$TMPDIR/hp_reliability/<stamp>`; with no `$TMPDIR` and no `--out` the script stops) keeps
`game.log`, `ft.txt` (frame trace), `frames/`, `shaders/`, `vmmap_*.txt`, `heap_*.txt`,
`run.json`, `tests.log` and `verdict.txt`. `reliability.py judge --out DIR --seconds N` judges a
kept folder again without a new run.

## What a run does

- Waits, before the tests and again before the game, while `pgrep -x` finds `lhp_boot`, `lhp`,
  `hobbit` or `marvel` (engine boot_check refuses instead; HP has several lanes sharing one GPU
  and display). Process names only: a `pgrep -f` pattern also matches other lanes' waiting
  shells and deadlocked three lanes.
- Runs `tests/run_all.sh` into the run folder.
- Starts the game with a private `HOME` (empty saves, cold shader caches: the new-game path of
  run 24), `caffeinate -u -d`, `RECOMP_ASSUME_FOCUS=1`, `RECOMP_NO_ALERT_UI=1`,
  `SDL_AUDIO_DRIVER=dummy`, `HP_TRACE_D3D9=1`, `HPRECOMP_FRAME_TRACE=1`, a frame dump every
  300 Presents (5 s at 60 fps), `HPRECOMP_SHADER_DUMP`, and a generated keyboard input script
  in wall-clock ms (input_script.c: CLOCK_MONOTONIC): RETURN every 3 s to 60 s; a W/D/S/A, J,
  K walk block at 405 s; pause open/close (ESCAPE, then RETURN on Resume 6 s later) at 440, 452
  and 464 s (RETURN never pauses: game code 0x96E2D0 maps RETURN, NUMPADENTER and SPACE to
  confirm and ESC to pause);
  walk blocks from 495 s. At 480 s `tools/check/cover.py` (run with `/usr/local/bin/python3.11`,
  which has PyObjC) puts a black window over every screen for 5 s without taking focus.
  The schedule assumes gameplay by 390 s: with the 60 fps cap the first story cutscene plays
  at real speed and gameplay starts at about 345 s (job relck/final: between Present #19500 at
  332 s and #21000 at 358 s); uncapped at about 115 fps it was 282 s (relck/val3). The old
  300 s limit and 240 s play start were set on the uncapped runs. A bare binary gets the
  `tools/boot/run_boot.sh` Vulkan/DXVK environment; the `.app` launcher sets its own.
  These are defaults only: a variable the caller sets (for example `VK_DRIVER_FILES`) wins.
- Samples RSS every 60 s, and `vmmap --summary` and `heap` at 395 s (after the level load; the
  old 120 s sample came before it, so the growth check measured the load) and at SECONDS-60 s.
- Each second it reads the last flushed frame-trace line; the smallest (run time - trace time)
  is `ft_offset_s` in run.json, used to put the cover times on the trace clock.
- Reads the game log from `game.log` (bare binary) and from the private HOME's
  `Library/Logs/HarryPotterRecomp/run-*.log` (the .app launcher's log; before 2026-10-24 the
  log checks read the empty game.log of .app runs).
  Both tools suspend the game while they read it; frames within 5 s of a sample are left out
  of the gap and fps checks.
- Stops its own game PID at SECONDS (SIGTERM, 5 s, SIGKILL: engine `boot_check._stop`).

## Checks

| Check | Limit | Source |
|---|---|---|
| unit tests | `tests/run_all.sh` exit 0 | HP tests |
| alive | still running at SECONDS | engine `tools/checks/boot_check.py:100-103` (imported) |
| crash reports | no new `lhp*`/`HarryPotter*` file in `~/Library/Logs/DiagnosticReports` | brief |
| boot to gameplay | gameplay HUD by 300 s, and the last dump still in gameplay | runs 23/26 reached gameplay at 235-245 s |
| Present gap | no frame-trace `dt` over 1000 ms | brief; run 24 judged 1.5 s between `D3D9 SEC` lines |
| fps median | per-second fps median >= 55 in gameplay (after a 10 s settle, whole seconds only) | owner 60 fps target |
| fps min | worst whole gameplay second >= 30 fps (the partial first second gave "10 fps" in relck/final) | engine `tools/perf/plot_frames.py:16` `SLOW_MS = 33.3` |
| scene loads | the two longest stall runs (Presents over 50 ms, merged within 0.5 s) before gameplay settles, each <= 1 s | brief |
| pause open | 3 opens, each seen in a dump: a dump from key + 0.5 s to the close shows the pause menu (`menu_score`, below), else "pause not opened" or "no dump in hold"; then key-down (Present before the logged `INPUT` line) to the end of the last stall in the next 4 s, each <= 1 s | brief |
| cover | not `D3D9 window hidden` 3 s after the uncover; `hidden` to `visible` resume <= 1 s; worst frame in those 3 s <= 1 s | brief |
| present intervals | info: per phase (boot, play, pause, cover) the count of intervals of 1/2/3/4+ vblanks at the display rate (`hp: N Hz display`, else 120), and judder = share off the most common interval | brief (60-on-120 judder) |
| memory growth | physical footprint at SECONDS-60 <= 1.25 x footprint at 395 s; FAIL if that sample came before gameplay | run 24: +5 % peak in 20 min |
| live objects | no `heap` class gains >= 1000 objects and doubles between the samples | brief (KosmicKrisp CAMetalDrawable leak) |
| unsupported calls | no `UNSUPPORTED` line outside `tools/check/known_unsupported.txt` | `boot_check.py:28-29,93-97,114` (regex imported) |
| translation gaps | 0 `RECOMP:` lines (unhandled instruction, fatal paths) | `boot_check.py:92,112` (imported) |
| heap | 0 `guest_heap:` lines | `boot_check.py:115` (imported) |
| assertions | 0 `Assertion` lines | `boot_check.py:99,116` (imported) |
| D3D9 errors | 0 `D3D9 Present/EndScene/Reset` with hr != 0 | stands in for `boot_check.py:110` GL errors |
| shader compiles | `HPRECOMP_SHADER_DUMP` json: 0 with hr != 0 | runs 23-26 ("all S_OK", "0 failed") |

Gameplay HUD test (`hud_score`): red heart pixels in (140,80)-(250,115) and blue portrait pixels
in (90,40)-(150,110) of a 960x600 dump, scaled for other sizes. On runs 23, 24 and 26 every
gameplay frame scored red >= 1012 and blue >= 581; title, menus, cutscenes and loads scored
red <= 299 and blue <= 18. The limits are 500 and 300. A dump's time comes from the frame-trace
`F` line with the same Present number.

Pause menu test (`menu_score`): gold "Paused" label pixels in (60,155)-(145,190) and light-blue
Options/Extras/Quit Game column pixels in (400,260)-(560,370) of a 960x600 dump, scaled for other
sizes. On 800 dumps of runs pause/esc1, pause/probe1, gpu1/run1, fifo/rel (baseline and
candidate) and relck/val3, the 84 menu dumps scored label 239-289 and column 1112-1908; the
other 716 scored label <= 44 and column up to 8205 (title, boot) or 888 (a blue spell in
gameplay). The limits are label 120 (inside the 44-239 gap) and column 500; the column alone
does not separate menus, so it only backs up the label. Evidence:
`<job tmp>/pause2/` (cal2.txt per dump, verify.txt per run, scripts/).

## Compare rows

| Row | Candidate fails when |
|---|---|
| boot_s | info only: the first cutscene runs per frame, so an uncapped build boots sooner |
| gap_ms, load_ms, pause_open_ms, uncover_stall_ms, cover_resume_ms | over baseline x1.25 + 100 ms (no baseline value: over 1000 ms) |
| fps_median, fps_min | below min(baseline, 60) - 5; fps above the 60 target counts as 60 |
| footprint_x | over baseline + 0.10 |
| judder_boot/play/pause/cover | over baseline + 5 points |
| hidden_stuck | True |

A value missing on the candidate but present on the baseline fails. Unit tests:
`pytest tools/check` (Hobbit venv).

Frame dumps cost ~85 ms inside the Present that writes them (`d3d9g_dump_frame`, runtime/src/gen_d3d9_guest.c:631). Those Presents (known by number) are left out of the gap, scene-load, pause-open, uncover and interval rows; before this, every candidate "pause open" (1.7-3.9 s) was a dump Present and RETURN had opened no menu. Dumps are by Present number and the schedule is by time, so they cannot be placed outside the windows.

Limits: the pause open is timed by Present stalls; no per-frame menu signal exists (`D3D9 SEC`
draws are per second), so a menu that opens late without a stall is not seen. Scene loads are
the boot's stall runs; a scene-to-scene load in gameplay needs the nav script, which the
d300cda baseline does not have (no `HPRECOMP_NAV_SCRIPT`, no `D3D9 window` line).

## Shader cache

The long Present stalls (0.6-6.6 s) are first-time shader compiles. During each one the
`dxvk-cs` thread is in `DxvkGraphicsPipeline` -> `kk_compile_graphics_pipeline` ->
`newLibraryWithSource` (about 80%) or `newRenderPipelineState` (about 15%) -> `MTLCompiler`.
The main thread waits in `D3D9SwapChainEx::Present` until the pipeline is ready.

macOS keeps the compiled Metal shaders in a cache per bundle id. That cache is outside the
private HOME, so a new empty HOME does not clear it. Measured on 2026-10-24:

| Run | Bundle id | Shader cache | Presents over 330 ms |
|---|---|---|---|
| cand1 (fifo app) | `local.harrypotterrecomp` | warm | 0 in 286 s |
| cold1 (`HP stallprobe.app`) | `local.harrypotterrecomp.stallprobe` | empty | 15, of 0.6-6.6 s |

So the first run of a build can stall at every scene change, and a later run of the same
bundle id does not. A compare that judges one cold run against one warm run measures the
cache, not the build. For this reason the compare form runs each build once unjudged first
(`reliability.py run --warm`, output in `OUT/<side>/warm`) and judges the second run.

A player sees these stalls only the first time a scene's shaders compile. After that, the
cache holds them.

## Engine and Hobbit checks not ported

- `recomp-check` (`tools/checks/recomp_check.py`): it reads a `game.toml` profile and the Mach-O
  translation layout. HP has no profile; its lift goes through the PE front end. The runtime
  side is covered by the `RECOMP:` line check.
- boot-check `stats:` checks (`rendering` draws per swap > 50, `textures`, `audio`, `stats
  fresh`; `boot_check.py:77-84,107-109`): the HP D3D9 runtime prints no `RECOMP_STATS` line.
  Boot to gameplay, Present gap and fps cover rendering and liveness; audio runs on the dummy
  driver.
- `GUARD=1` heap guard (`RECOMP_HEAP_GUARD=2`): the HP binary reads the variable, but a guard
  run is a slower diagnostic run, not a verdict run.
- Marvel adds no checks of its own; it uses the same engine boot-check (Marvel README).

## Hooks other lanes could add (not done here)

- runtime/: a log line at each level load (for example `HP LEVEL <name>` from the CreateFileA
  of the level file) would replace the pixel HUD test with an exact scene name.
- runtime/: a `stats:` line in the engine format (`t`, `swaps`, `draws_per_swap`,
  `tex_uploads`, `audio_renders`) would let `boot_check.judge` run unchanged on HP.
- The live-object count needs `heap`, which suspends the game. A count from KosmicKrisp itself
  (live drawables) would avoid the pause; that is outside this repo.
- tests/run_all.sh: the Python path is fixed to the Hobbit venv; this check uses the same path.

## Validation runs

2026-10-24, `--short` (480 s), `<work>/hp-pkg-build/lhp_boot`, private HOME. Unit tests
17 passed, 0 failed (two runs). The first two game launches exited at 1 s: the private HOME had
no `Library/Application Support`, and `runtime/src/hp_boot.c:252-253` creates only the last
folder. `reliability.py run` now creates it. Third run (job folder `relck/val3`):

| Check | Result | Value |
|---|---|---|
| alive, crash reports | PASS | ran 480 s, 0 new reports |
| boot to gameplay | PASS | HUD at Present #30000, t=282 s; last dump #52500 in gameplay |
| Present gap | FAIL | 1082 ms at t=203 s (cutscene-to-level load), 53,271 frames |
| fps median / min | PASS | 120 / 89 fps over 172 s of gameplay |
| memory growth | PASS | footprint 2662 -> 3174 MB (x1.19); RSS 862-1215 MB |
| live objects | FAIL | MTLRenderPassColorAttachmentDescriptorArrayInternal 271,077 -> 1,217,438 |
| log checks, D3D9 errors | PASS | 0 new UNSUPPORTED, 0 RECOMP:, 0 guest_heap, 0 Assertion, 0 hr != 0 |
| shader compiles | PASS | 520 compiled, 0 failed |

Verdict FAIL (2 checks), exit 1, as expected for a build that still has the KosmicKrisp leak.

Known weakness: `CAMetalDrawable` itself grew 12,652 -> 21,374 (x1.69). That is under the x2 rule,
so the check caught the leak through the Metal descriptor class only. A growth-only rule would
also flag `non-object` and `NSMutableArray`, because the 120 s sample comes before the level load
(282 s). A better fix is to take the early sample after gameplay is reached; that is not done.

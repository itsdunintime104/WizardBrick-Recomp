# Slow frames after the fill-screen fix (2026-10-24)

Read-only analysis of existing frame traces. No game runs, no builds.
Main trace: job tmp `display/vsI.ft` (commit 4533590, 2056x1290, FIFO 120 Hz,
window visible). Phases: cutscene 45-235 s, gameplay 235-336 s.
[V] = measured from a trace or read in code. [I] = inferred.

Method. An F line is written at each Present entry. `present_ms` is the
Present call that starts the interval, `host - present` is other wrapped
calls, and `dt - host` is guest code. Calls of 2 ms or more come from S lines
on the Present thread (tid 12178084). Analysis ran as inline `python3 -I`
scripts over the F and S lines.

## Answer: the game's own 60 fps limiter makes most slow frames

`sub_00966E70` (lhp_0078.c) is the game's frame limiter [V: code]:

- It runs only while `[0x1042BEC] != 0` or `[[0x101B5E0]+0x2C] != [[0x101B5E0]+0x28]`.
- It reads timer `sub_00929F90`, subtracts the time stored in 0x10D66F8, and
  calls `Sleep(trunc((0.016666668 - elapsed_s) * 1000.0))` with a floor of 1 ms.
  The constants are 0xC5A400 = 0.01666666753590107, 0xC61708 = 1000.0 and
  0xC444C0 = 1.0, read from `orig.nosync/lhp_win32.exe` [V].
  The Sleep call is at 0x966F36, with return address 0x966F3C [V].
- `sub_009A7D10` (lhp_0081.c:27878-27966) is a background job thread loop. It
  stores the current job in 0x1042BEC while the job runs and clears it after
  (lhp_0081.c:27908, 27963) [V]. 0x101B5E0 is the queue that the loop takes
  jobs from (`sub_009734A0`, ecx = [0x101B5E0]). +0x28/+0x2C are probably its
  head/tail pair [I].
- Result: while a background load is queued or running, the game caps itself
  at 60 fps. At other times it runs at 120 Hz. Every switch into the cap
  turns a run of frames into ~17 ms frames.

The cap period is 17.1 ms, not 16.67 [V: median dt in the runs 16.85-17.19].
The guest's measured elapsed time is 0.5-1.5 ms lower than the real one
(Sleep 13 goes with 4.16 ms of other work, but truncation allows at most
3.67) [V]. Why: the timer at 0x929F90 probably has coarse resolution or
truncates [I, not read]. So the capped frames land just above 16.7 ms and are
all counted.

## Q1. Split of each frame > 16.7 ms (vsI)

Per-phase, by largest component [V]:

| Phase | Slow frames | Limiter Sleep largest | Present wait largest | Guest CPU largest | Other call |
|---|---|---|---|---|---|
| cutscene | 235 | 211 (3,648 ms dt) | 18 (965 ms) | 5 (139 ms) | 1 PeekMessageA 16 ms (24 ms dt) |
| gameplay | 414 | 407 (7,014 ms) | 4 (180 ms) | 3 (115 ms) | 0 |

Summed components over the slow frames [V]:

| Phase | Present wait ms | Other host calls ms (the Sleep) | Guest code ms |
|---|---|---|---|
| cutscene | 939 | 3,086 | 750 |
| gameplay | 160 | 5,456 | 1,692 |

Main-thread calls of 2 ms or more inside slow frames [V]: KERNEL32 Sleep
212 + 409 = 621 frames, each 11-17 ms (2,939 + 5,234 ms). USER32
PeekMessageA once (128.853 s, 16.1 ms). No COM call other than Present took
2 ms or more on the game thread after 45 s. No CreateTexture, Lock or shader
create appears.

Ranked by time lost (dt - 8.33 ms), both phases, 649 frames, 6,676 ms [V]:

| Rank | Cause | Frames | Excess ms | Share |
|---|---|---|---|---|
| 1 | Game limiter Sleep (sub_00966E70) during background loads | 621 | 5,539 | 83% |
| 2 | Present wait at the start of a load (within 1.2 s of a run start) | 14 | 817 | 12% |
| 3 | Guest CPU frame at the start of a load (17-72 ms of guest code, cpu ≈ wall) | 7 | 178 | 3% |
| 4 | Isolated Present wait (48.4, 56.5, 69.5, 86.1, 121.2, 327.6 s) | 6 | 126 | 2% |
| 5 | PeekMessageA (SDL pump) | 1 | 16 | <1% |

Frames over 33.3 ms (13) [V]: 11 are Present waits. 9 of these are at load
starts (50.00 s 264.6 ms, 234.51 s 126.3 ms, 50.10 s 91.6 ms, 119.24 s 85.5 ms,
259.02 s 73.2 ms, ...) and 2 are isolated (86.14 s 64.8 ms, 327.60 s 36.3 ms).
The other 2 are guest CPU (49.74 s 33.6 ms, 258.92 s 72.4 ms). The limiter
causes none of them.

## Q2. Clusters and matching events

The slow frames are clustered. Each run of limiter frames (frames gap at most 3) [V]:

| Run (s) | Frames | > 16.7 | Median dt | Median Sleep | Loader threads active (s) | Before the run |
|---|---|---|---|---|---|---|
| 50.19-51.39 | 71 | 56 | 17.10 | 15 | from 49.25 | guest 23 ms, Present 258.6 + 89.4 ms |
| 119.28-119.59 | 19 | 11 | 16.85 | 13 | from 118.36 | guest 19.8 ms, Present 82.0 ms |
| 188.86-190.15 | 76 | 58 | 17.19 | 14 | from 187.89 | guest 16.1 ms, Present 39.0 ms |
| 205.67-207.17 | 89 | 67 | 17.01 | 13 | from 204.76 | guest 17.7 ms, Present 47.5 ms |
| 234.54-235.57 | 61 | 46 | 17.17 | 11.6 | from 233.55 | guest 17.6 ms, Present 120.9 ms |
| 259.09-267.02 | 465 | 383 | 17.10 | 13 | 258.17-268.16 | guest 61.6 ms, Present 66 + 46 ms |

Events [V]: loader and worker threads (tids ...152, ...153, ...172, ...513)
log their slow WaitForSingleObject, ReadFile (152) and EnterCriticalSection
(513) calls only inside these windows. Between 45 and 336 s, outside the
windows, 0 calls from 152/153/172/513 reach the log. Only the audio-like
threads (522 Sleep 105 ms, 445/446 WaitForMultipleObjects) do. Every cluster
is a background load: cutscene shot changes and the cutscene-to-gameplay
transition, then a level stream at 259-267 s. The trace has no shader-compile
or texture-create events. No such COM call took 2 ms or more on any thread
[V], so a compile or upload burst would show only as Present time [I].

Each load has the same sequence [V]. First one heavy guest frame (16-62 ms).
Then one or two long Present waits (39-259 ms). Then 0.3-8 s of limiter frames.
Why Present stalls at a load start is not in this trace. It may be nextDrawable
(stutter.md samp2) or DXVK work for new resources [I].

## Q3. Vsync quantisation

| Bucket (ms) | cutscene | gameplay | all |
|---|---|---|---|
| 16.7-17.5 (two 120 Hz intervals) | 156 | 342 | 498 (77%) |
| 17.5-24.2 | 66 | 67 | 133 |
| 24.2-25.8 (three intervals) | 1 | 1 | 2 |
| 25.8-33.3 | 3 | 0 | 3 |
| > 33.3 | 9 | 4 | 13 |

[V] 77% fall in the two-interval band, but these are not missed vsyncs:
- Normal frames are tight around 8.33 ms (mode 8.3). The slow ones spread
  evenly over 16.8-17.4 with a centre of 17.1. That is Sleep pacing, not
  vblank locking [V].
- In limiter frames, Present takes about 0.05 ms (gameplay) and 0.3 ms
  (cutscene) on average. Normal frames take 3.7-5.9 ms [V: component sums
  minus the Present-dominated frames]. At 58 fps the queue is empty, so the
  game thread never waits on vblank [V].
- The frames after a slow frame are slow too (next dt at least 9.2 ms in
  633 of 649) [V]. These are runs, not single misses.

A deeper queue does nothing for these frames, because the queue is already
empty [I]. A 60 fps pacing choice (`d3d9.presentInterval = 2` or
`dxvk.maxFrameRate = 60`) would hide the 120-to-58 switches by running at 60
all the time. The limiter would then sleep 1 ms or less [I]. 58.5 fps into
120 Hz FIFO should hold one frame for 3 vblanks about every 39 frames [I].

The claim in template_reuse.md:93-94 (">16.7 counts are mostly single missed
vblanks") does not hold for vsI [V].

## Q4. Why gameplay has more (414 vs 235)

[V] 383 of the 414 gameplay slow frames come from one load, 259.09-267.02 s
(loader active 258.17-268.16 s, about 10 s). Each cutscene load lasts
0.3-1.5 s. Across runs, the share of slow frames is similar (58-82%). So
gameplay has more slow frames because one load runs longer, not because each
gameplay frame is heavier. A small extra effect: gameplay work per frame is
higher (median 4.26 ms besides the Sleep, against 2.2-3.7 ms in the cutscene
runs). With the 17.1 ms period, that does not change the count much [I].

## Other traces (frames at 45 s or later, files not changed for 3 min)

| Trace | > 16.7 | With limiter Sleep >= 2 ms |
|---|---|---|
| display/vsI | 649 | 621 (96%) |
| display/vsD | 674 | 633 (94%) |
| kk/fd1 | 575 | 484 (84%) |
| stall/ls1b | 484 | 371 (77%) |
| display/vsF | 543 | 191 (35%) |
| stall/vs0, fl1, fl2, fl3, pi1 (4112x2658, GPU-bound) | 125-5370 | 0 |

[V] In GPU-bound runs the frame already takes about 15 ms. The limiter then
sleeps under 2 ms, below the trace's log threshold. So those runs show 0 here,
and their slow frames are the Present/nextDrawable stall that present_stall.md
describes [I]. speed/*.ft (v120a, cun, c120a, c60) have 390-528 frames over 33 ms
and almost no limiter Sleep. Their settings are not known to this lane, so they
were not analysed.

## Next fix

Stop the main-thread throttle during background loads. The game expects a
60 Hz console-era target. The host has spare cores for the loader.

- Hook point [V]: the runtime has no guest-function override (grep of
  runtime/src for `sub_00`). Sleep is `imp_Sleep` in runtime/src/win32_sync.c:876.
  Options:
  (a) In imp_Sleep, return at once when the guest return address is
      0x00966F3C, the limiter call site only. How to read the return address
      in this ABI (W32_ARG reads [esp+4..]) was not checked [I].
  (b) Change the target constant at 0xC5A400 to 1/refresh. Not recommended:
      at 120 Hz the 0.5-1.5 ms timer underestimate gives about 8.8 ms frames [I].
  (c) Configuration only: a 60 fps cap. This hides the switch and halves the
      frame rate everywhere.
- Proof check: rerun the vsI setup (2056x1290, FIFO 120 Hz, visible, 335 s).
  Expected: 0 KERNEL32 Sleep S lines of 2 ms or more on the Present thread
  after 45 s (vsI: 621). Frames over 16.7 ms drop from 649 to about 30 (the
  Present and guest frames in rank 2-5). Median stays 8.33. The loader window
  near 258-268 s stays about 10 s long, so the throttle removal does not slow
  loads.
- Second target: the Present waits at load starts (817 ms, 11 of the 13 frames
  over 33 ms). The `d3d9.maxFrameLatency = 2` run at 2056x1290 (another lane)
  is the test for these.

## Fix chosen (2026-10-24)

Option (a), as a clamp instead of a skip: `imp_Sleep` (runtime/src/win32_sync.c)
sleeps at most 2 ms when the guest return address (`MEM32(g_esp)` at the
import, the address the call pushed) is 0x00966F3C, the limiter's call site.
Every other Sleep keeps its time. Always on; no frame-rate condition. The
table is runtime/src/hp_patches.c; tests/test_hp_patches.c checks the
decision. Prior art: the sister game's fix
(github.com/illusion-pasture-program/lhp2-performance-fix,
src/lhp2_d3d9_fix.c:33-34, 203-217) clamps its limiter's Sleep to 2 ms the
same way, by call site. Not yet measured with the proof check above.

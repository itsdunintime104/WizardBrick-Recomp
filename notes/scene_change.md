# Scene changes: ~11 s frozen or black (fixed 2026-10-25)

## Cause
- sub_00A3B5A0 (lhp_0083.c, 0x00A3B6DD..0x00A3B7C8) loads edi = [0xED88CC], the
  multi-GPU (AFR) count. If it is > 1 and a viewport is active, it renders and
  Presents edi extra frames at each scene change. The exe's .data default is 1.
- Writer: sub_00A37D90 at 0x00A38540. DXVK reports AMD (0x1002) for Apple, so the
  AMD branch calls sub_00A41A30 (CrossFire count). Its count slot is the
  caller's ecx, uninitialised: the back buffer height from [dev+0x140].
- sub_00A41A30 calls sub_00A417E0(&count). sub_00A417E0 asks GetProcAddress(gdi32)
  for D3DKMTEscape and D3DKMTOpenAdapterFromHdc. Neither was exported, so it took
  loc_00A419E4 ("Could Not Find ... in gdi32.dll") and returned 0x80004005. The
  caller takes non-zero as success and returns the unwritten count: 1290 at
  2056x1290. So each scene change rendered ~1290 extra frames.

## Fix
runtime/src/win32_ui.c exports D3DKMTOpenAdapterFromHdc and D3DKMTEscape (stdcall,
1 argument). Both return STATUS_NOT_SUPPORTED (0xC00000BB) and write nothing.
sub_00A41630 then gets a negative NTSTATUS and returns 0x80004005.
sub_00A417E0 prints "Could Not Get Primary Adapter Handle" (loc_00A418C0) and
returns 0. sub_00A41A30 then returns 1.

## Measured (probe1 input script, empty HOME, fifo-build runtime + this fix)
Scene-change detector: runs of >= 100 Presents with main-thread CPU < 2 ms.
Gaps of up to 3 Presents (frame dumps) do not end a run.
| # | before (probe1) start, length, Presents | after: long Presents (start-end) |
|---|---|---|
| 1 | 32.2 s, 13.6 s, 1553 | 40.3-43.8 s (3.6 s) |
| 2 | 61.3 s, 11.0 s, 1288 | 49.1-52.4 s (3.3 s) |
| 3 | 132.4 s, 11.1 s, 1289 | 63.5-68.4 s (4.9 s) |
| 4 | 202.5 s, 11.1 s, 1289 | 126.7-128.2 s (1.5 s) |
| 5 | 218.6 s, 12.4 s, 1417 | 193.3-194.6 s (1.3 s) |
| 6 | 249.1 s, 11.2 s, 1289 | 212.6-214.2 s (1.7 s) |
| 7 | 274.2 s, 11.1 s, 1289 | 227.9-230.4 s (2.5 s) |
After the fix, no gameplay span reaches 100 low-CPU Presents. A scene change is now
one or two 1-3 s Presents (the load itself). The "after" rows are matched to the
"before" rows by order only, not checked frame by frame. At 60 s and 100 s the
fixed run is further into the game than probe1. Gameplay and walking were checked
in frame dumps 43200 and 44400 (404 s and 415 s).
Evidence: <job tmp>/gpu1/run1 (ft.txt, frames, game.log).

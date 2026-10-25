# Game speed against frame rate

Date: 2026-10-24. Binary: ~/Developer/hp-pkg-build/lhp_boot (commit 4533590, -O2, default desktop size 2056x1290, forced vsync at 120 Hz).

## Verdict

Game speed does not depend on frame rate. The pre-title intro scene ends 32.1-32.4 s after launch at 58, 100 and 148 fps. The Present count at that point is 1750, 2800 and 4200. If the game counted frames, the 58 fps run would end the scene about 2.5 times later than the uncapped run. It ends at the same time (spread 0.3 s, which is one dump interval).

The title screen segment gives the same result with more noise: about 18-20 s at 57 fps (1150 Presents) and at 99-100 fps (1750-2000 Presents). Inside the 60 fps run, the second title segment ran after the cap stopped holding (about 100 fps) and lasted 18.9 s, against 19.2 s for the first title segment at 57 fps.

The runtime timer thunks read the host monotonic clock: GetTickCount at runtime/src/win32_sync.c:1191 and QueryPerformanceCounter at runtime/src/win32_sync.c:1197. Which timer the game reads each frame was not traced.

## Runs

No input script (the game plays the legal screen, the intro scene, a fade to black, then the title and attract scenes). Each run is 300 s with a fresh pcconfig.txt and the same save.

| Setting | Measured fps (launch to intro end) | Intro end (s after launch) | Presents at intro end | Title segment fps | Title segment (s) | Title segment Presents | Intro end to title (s) |
|---|---|---|---|---|---|---|---|
| dxvk.maxFrameRate = 60 | 58 | 32.1 | 1750 | 57 | 20.1 | 1150 | 34.8 |
| default (vsync 120 Hz), run 1 | 100 | 32.4 | 2800 | 100 | 20.0 | 2000 | 24.8 |
| default (vsync 120 Hz), run 2 | 101 | 32.1 | 2800 | 99 | 18.1 | 1800 | 24.7 |
| HPRECOMP_VSYNC=0 (uncapped) | 148 | 32.1 | 4200 | 99 | 17.7 | 1750 | 20.7 |

Title segment lengths of 18.1 and 17.7 s are cut short by one misclassified dump in the middle (the segment continues after it). Resolution is one dump interval: 0.35-0.9 s depending on fps.

## Method

- HPRECOMP_DUMP_FRAMES dumps the back buffer every 50 Presents (500 to 51200). A watcher script records each dump's file time and the mean brightness of three bands (top 1-6 %, middle 40-60 %, bottom 94-99 %), then deletes the dump.
- Classes: black (all bands under 3), title (bands near 12/32/45), other. The intro end is the first black dump after 20 s. The title segment is the first run of title dumps.
- fps comes from the dump indices and times, cross-checked against HPRECOMP_FRAME_TRACE per 30 s windows.
- Scripts and data: <job tmp>/speed/ (run.sh, chain.sh, tools/watch.py, tools/seg.py, *.lb, *.ft).

## Limits and side observations

- Each dump stalls about 100 ms, so dump runs reach about 100 fps instead of 120. The comparison still spans 58 to 148 fps.
- The uncapped run drops to about 100 fps after 60 s for the same reason. Its distinct fps applies to the intro marker only.
- In the 60 fps run the dxvk.maxFrameRate cap held at 57-58 fps until about 180 s, then fps rose to about 98. Not explained. Both markers fall before 180 s.
- The black load between the intro and the title gets shorter at higher fps (34.8 s at 58 fps, 20.7 s at about 130-148 fps). This is load time, not game speed. A per-frame loading step would explain it; this was not confirmed.
- A run with the load_kb.input RETURN taps and dumps stayed on the title screen. The likely cause is that 100 ms dump stalls let 100 ms taps fall between two input polls. Not confirmed. The intro cutscene after Load Game 1 was therefore not used as the marker.
